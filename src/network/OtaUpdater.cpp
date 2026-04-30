#include "OtaUpdater.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <Logging.h>
#include <esp_flash.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <spi_flash_mmap.h>

#include <algorithm>
#include <memory>

#include "OtaBootSwitch.h"
#include "esp_http_client.h"
#include "esp_wifi.h"

namespace {
// Korean fork release URL
constexpr char latestReleaseUrl[] =
    "https://api.github.com/repos/crosspoint-reader-ko/crosspoint-reader-ko/releases/latest";

/* This is buffer and size holder to keep upcoming data from latestReleaseUrl */
char* local_buf;
int output_len;
int buf_cap;

/*
 * When esp_crt_bundle.h included, it is pointing wrong header file
 * which is something under WifiClientSecure because of our framework based on arduno platform.
 * To manage this obstacle, don't include anything, just extern and it will point correct one.
 */
extern "C" {
extern esp_err_t esp_crt_bundle_attach(void* conf);
}

esp_err_t http_client_set_header_cb(esp_http_client_handle_t http_client) {
  return esp_http_client_set_header(http_client, "User-Agent", "CrossPoint-ESP32-" CROSSPOINT_VERSION);
}

/*
 * Initial buffer size used for chunked responses (no Content-Length header).
 * Grows geometrically via realloc if the response exceeds this.
 */
constexpr int kChunkedInitialBuf = 16384;

esp_err_t event_handler(esp_http_client_event_t* event) {
  /* We do interested in only HTTP_EVENT_ON_DATA event only */
  if (event->event_id != HTTP_EVENT_ON_DATA) return ESP_OK;

  const bool chunked = esp_http_client_is_chunked_response(event->client);
  int content_len = esp_http_client_get_content_length(event->client);

  /* First data event: allocate the backing buffer. */
  if (local_buf == NULL) {
    const int initial = (chunked || content_len <= 0) ? kChunkedInitialBuf : (content_len + 1);
    local_buf = static_cast<char*>(calloc(initial, sizeof(char)));
    output_len = 0;
    buf_cap = initial;
    if (local_buf == NULL) {
      LOG_ERR("OTA", "HTTP Client Out of Memory Failed, Allocation %d", initial);
      return ESP_ERR_NO_MEM;
    }
  }

  /* Grow buffer for chunked/unknown-length responses. */
  if (output_len + event->data_len + 1 > buf_cap) {
    int new_cap = buf_cap * 2;
    while (new_cap < output_len + event->data_len + 1) new_cap *= 2;
    char* new_buf = static_cast<char*>(realloc(local_buf, new_cap));
    if (new_buf == NULL) {
      LOG_ERR("OTA", "HTTP Client realloc Failed, target %d", new_cap);
      return ESP_ERR_NO_MEM;
    }
    local_buf = new_buf;
    memset(local_buf + buf_cap, 0, new_cap - buf_cap);
    buf_cap = new_cap;
  }

  int copy_len = event->data_len;
  if (!chunked && content_len > 0) {
    copy_len = min(copy_len, content_len - output_len);
  }
  if (copy_len > 0) {
    memcpy(local_buf + output_len, event->data, copy_len);
    output_len += copy_len;
  }
  return ESP_OK;
} /* event_handler */
} /* namespace */

OtaUpdater::OtaUpdaterError OtaUpdater::checkForUpdate() {
  JsonDocument filter;
  esp_err_t esp_err;
  JsonDocument doc;

  esp_http_client_config_t client_config = {
      .url = latestReleaseUrl,
      /* 15s covers WiFi-warmup TLS handshake jitter right after WifiSelection. */
      .timeout_ms = 15000,
      .event_handler = event_handler,
      /* Default HTTP client buffer size 512 byte only */
      .buffer_size = 8192,
      .buffer_size_tx = 8192,
      .skip_cert_common_name_check = true,
      .crt_bundle_attach = esp_crt_bundle_attach,
      .keep_alive_enable = true,
  };

  /* To track life time of local_buf, dtor will be called on exit from that function */
  struct localBufCleaner {
    char** bufPtr;
    ~localBufCleaner() {
      if (*bufPtr) {
        free(*bufPtr);
        *bufPtr = NULL;
      }
    }
  } localBufCleaner = {&local_buf};

  /* Reset per-call state: prior aborted attempt may have left stale values. */
  local_buf = NULL;
  output_len = 0;
  buf_cap = 0;

  esp_err = ESP_FAIL;
  for (int attempt = 0; attempt < 2; ++attempt) {
    esp_http_client_handle_t client_handle = esp_http_client_init(&client_config);
    if (!client_handle) {
      LOG_ERR("OTA", "HTTP Client Handle Failed");
      return INTERNAL_UPDATE_ERROR;
    }

    esp_err = esp_http_client_set_header(client_handle, "User-Agent", "CrossPoint-ESP32-" CROSSPOINT_VERSION);
    if (esp_err != ESP_OK) {
      LOG_ERR("OTA", "esp_http_client_set_header Failed : %s", esp_err_to_name(esp_err));
      esp_http_client_cleanup(client_handle);
      return INTERNAL_UPDATE_ERROR;
    }

    esp_err = esp_http_client_perform(client_handle);
    esp_http_client_cleanup(client_handle);
    if (esp_err == ESP_OK && output_len > 0) break;

    LOG_ERR("OTA", "perform attempt %d failed: %s (len=%d)", attempt, esp_err_to_name(esp_err), output_len);
    /* Drop any partial buffer before retrying. */
    if (local_buf) {
      free(local_buf);
      local_buf = NULL;
    }
    output_len = 0;
    buf_cap = 0;
    delay(500);
  }
  if (esp_err != ESP_OK || output_len == 0) {
    return HTTP_ERROR;
  }

  filter["tag_name"] = true;
  filter["assets"][0]["name"] = true;
  filter["assets"][0]["browser_download_url"] = true;
  filter["assets"][0]["size"] = true;
  const DeserializationError error = deserializeJson(doc, local_buf, DeserializationOption::Filter(filter));
  if (error) {
    LOG_ERR("OTA", "JSON parse failed: %s", error.c_str());
    return JSON_PARSE_ERROR;
  }

  if (!doc["tag_name"].is<std::string>()) {
    LOG_ERR("OTA", "No tag_name found");
    return JSON_PARSE_ERROR;
  }

  if (!doc["assets"].is<JsonArray>()) {
    LOG_ERR("OTA", "No assets found");
    return JSON_PARSE_ERROR;
  }

  latestVersion = doc["tag_name"].as<std::string>();

  for (int i = 0; i < doc["assets"].size(); i++) {
    if (doc["assets"][i]["name"] == "firmware.bin") {
      otaUrl = doc["assets"][i]["browser_download_url"].as<std::string>();
      otaSize = doc["assets"][i]["size"].as<size_t>();
      totalSize = otaSize;
      updateAvailable = true;
      break;
    }
  }

  if (!updateAvailable) {
    LOG_ERR("OTA", "No firmware.bin asset found");
    return NO_UPDATE;
  }

  LOG_DBG("OTA", "Found update: %s", latestVersion.c_str());
  return OK;
}

bool OtaUpdater::isUpdateNewer() const {
  if (!updateAvailable || latestVersion.empty() || latestVersion == CROSSPOINT_VERSION) {
    return false;
  }

  int currentMajor, currentMinor, currentPatch;
  int latestMajor, latestMinor, latestPatch;

  const auto currentVersion = CROSSPOINT_VERSION;

  // semantic version check (only match on 3 segments)
  sscanf(latestVersion.c_str(), "%d.%d.%d", &latestMajor, &latestMinor, &latestPatch);
  sscanf(currentVersion, "%d.%d.%d", &currentMajor, &currentMinor, &currentPatch);

  /*
   * Compare major versions.
   * If they differ, return true if latest major version greater than current major version
   * otherwise return false.
   */
  if (latestMajor != currentMajor) return latestMajor > currentMajor;

  /*
   * Compare minor versions.
   * If they differ, return true if latest minor version greater than current minor version
   * otherwise return false.
   */
  if (latestMinor != currentMinor) return latestMinor > currentMinor;

  /*
   * Check patch versions.
   */
  if (latestPatch != currentPatch) return latestPatch > currentPatch;

  // If we reach here, it means all segments are equal.
  // One final check, if we're on an RC build (contains "-rc"), we should consider the latest version as newer even if
  // the segments are equal, since RC builds are pre-release versions.
  if (strstr(currentVersion, "-rc") != nullptr) {
    return true;
  }

  return false;
}

const std::string& OtaUpdater::getLatestVersion() const { return latestVersion; }

bool OtaUpdater::isUpdateNewerKO() const {
  if (!updateAvailable || latestVersion.empty() || latestVersion == CROSSPOINT_VERSION) {
    return false;
  }

  // Parse version: major.minor.patch-ko.koVersion
  auto parseVersion = [](const std::string& version, int& major, int& minor, int& patch, int& ko) {
    major = minor = patch = ko = 0;

    // Find -ko. suffix
    size_t koPos = version.find("-ko.");
    std::string baseVersion = (koPos != std::string::npos) ? version.substr(0, koPos) : version;

    // Parse ko version if present
    if (koPos != std::string::npos) {
      ko = stoi(version.substr(koPos + 4));
    }

    // Parse major.minor.patch
    size_t firstDot = baseVersion.find('.');
    size_t lastDot = baseVersion.find_last_of('.');

    if (firstDot != std::string::npos) {
      major = stoi(baseVersion.substr(0, firstDot));
      if (lastDot != firstDot) {
        minor = stoi(baseVersion.substr(firstDot + 1, lastDot - firstDot - 1));
        patch = stoi(baseVersion.substr(lastDot + 1));
      } else {
        minor = stoi(baseVersion.substr(firstDot + 1));
      }
    }
  };

  int updateMajor, updateMinor, updatePatch, updateKo;
  int currentMajor, currentMinor, currentPatch, currentKo;

  parseVersion(latestVersion, updateMajor, updateMinor, updatePatch, updateKo);
  parseVersion(CROSSPOINT_VERSION, currentMajor, currentMinor, currentPatch, currentKo);

  if (updateMajor != currentMajor) return updateMajor > currentMajor;
  if (updateMinor != currentMinor) return updateMinor > currentMinor;
  if (updatePatch != currentPatch) return updatePatch > currentPatch;
  return updateKo > currentKo;
}

OtaUpdater::OtaUpdaterError OtaUpdater::installUpdate(ProgressCallback onProgress, void* ctx) {
  if (!isUpdateNewerKO()) {
    return UPDATE_OLDER_ERROR;
  }

  // Mirror the SD-update path: raw HTTP download → raw partition write →
  // direct otadata switch. Skip esp_https_ota_* entirely so the running
  // ESP-IDF's bogus esp_image_verify (efuse-blk-rev rejection) never runs.
  render = false;

  const esp_partition_t* dest = esp_ota_get_next_update_partition(nullptr);
  if (!dest) {
    LOG_ERR("OTA", "esp_ota_get_next_update_partition returned null");
    return INTERNAL_UPDATE_ERROR;
  }
  LOG_INF("OTA", "dest: %s @0x%x size=%u", dest->label, static_cast<unsigned>(dest->address),
          static_cast<unsigned>(dest->size));

  esp_http_client_config_t client_config = {
      .url = otaUrl.c_str(),
      .timeout_ms = 15000,
      .buffer_size = 8192,
      .buffer_size_tx = 8192,
      .skip_cert_common_name_check = true,
      .crt_bundle_attach = esp_crt_bundle_attach,
      .keep_alive_enable = true,
  };

  esp_http_client_handle_t client = esp_http_client_init(&client_config);
  if (!client) {
    LOG_ERR("OTA", "esp_http_client_init failed");
    return INTERNAL_UPDATE_ERROR;
  }
  esp_http_client_set_header(client, "User-Agent", "CrossPoint-ESP32-" CROSSPOINT_VERSION);

  // Disable power saving for stable throughput.
  esp_wifi_set_ps(WIFI_PS_NONE);
  auto restorePs = [&]() { esp_wifi_set_ps(WIFI_PS_MIN_MODEM); };

  esp_err_t err = esp_http_client_open(client, 0);
  if (err != ESP_OK) {
    LOG_ERR("OTA", "http_client_open failed: %s", esp_err_to_name(err));
    esp_http_client_cleanup(client);
    restorePs();
    return HTTP_ERROR;
  }

  // Read response headers (returns body Content-Length, may be -1 for chunked).
  int contentLen = esp_http_client_fetch_headers(client);
  const int status = esp_http_client_get_status_code(client);
  if (status / 100 != 2) {
    LOG_ERR("OTA", "HTTP status %d", status);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    restorePs();
    return HTTP_ERROR;
  }
  if (contentLen <= 0) contentLen = static_cast<int>(otaSize);  // fallback to release-asset size
  if (contentLen <= 0 || static_cast<size_t>(contentLen) > dest->size) {
    LOG_ERR("OTA", "implausible content length: %d (partition=%u)", contentLen, static_cast<unsigned>(dest->size));
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    restorePs();
    return HTTP_ERROR;
  }
  totalSize = static_cast<size_t>(contentLen);
  processedSize = 0;
  render = true;
  if (onProgress) onProgress(ctx);

  constexpr size_t SEC = SPI_FLASH_SEC_SIZE;  // 4 KiB
  constexpr size_t BLK = 64 * 1024;           // 64 KiB block-erase granularity
  constexpr size_t CHUNK = 4096;
  auto buffer = std::unique_ptr<uint8_t[]>(new (std::nothrow) uint8_t[CHUNK]);
  if (!buffer) {
    LOG_ERR("OTA", "OOM allocating chunk buffer");
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    restorePs();
    return OOM_ERROR;
  }

  size_t pos = 0;
  size_t erasedUpto = 0;
  uint32_t lastLogMs = 0;
  while (pos < totalSize) {
    if (pos >= erasedUpto) {
      size_t eraseLen = std::min<size_t>(BLK, dest->size - pos);
      eraseLen = (eraseLen + SEC - 1) & ~(SEC - 1);
      eraseLen = std::min<size_t>(eraseLen, dest->size - pos);
      if (esp_partition_erase_range(dest, pos, eraseLen) != ESP_OK) {
        LOG_ERR("OTA", "erase_range failed @%u (len=%u)", static_cast<unsigned>(pos), static_cast<unsigned>(eraseLen));
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        restorePs();
        return INTERNAL_UPDATE_ERROR;
      }
      erasedUpto = pos + eraseLen;
    }

    const size_t want = std::min<size_t>(CHUNK, totalSize - pos);
    const int got = esp_http_client_read(client, reinterpret_cast<char*>(buffer.get()), want);
    if (got <= 0) {
      LOG_ERR("OTA", "http_client_read failed @%u (got=%d)", static_cast<unsigned>(pos), got);
      esp_http_client_close(client);
      esp_http_client_cleanup(client);
      restorePs();
      return HTTP_ERROR;
    }
    if (esp_partition_write(dest, pos, buffer.get(), static_cast<size_t>(got)) != ESP_OK) {
      LOG_ERR("OTA", "partition_write failed @%u", static_cast<unsigned>(pos));
      esp_http_client_close(client);
      esp_http_client_cleanup(client);
      restorePs();
      return INTERNAL_UPDATE_ERROR;
    }
    pos += static_cast<size_t>(got);
    processedSize = pos;
    render = true;
    if (onProgress) onProgress(ctx);

    const uint32_t now = millis();
    if (now - lastLogMs > 2000) {
      LOG_DBG("OTA", "downloaded=%u / %u", static_cast<unsigned>(pos), static_cast<unsigned>(totalSize));
      lastLogMs = now;
    }
  }

  esp_http_client_close(client);
  esp_http_client_cleanup(client);
  restorePs();

  if (pos != totalSize) {
    LOG_ERR("OTA", "incomplete download: %u / %u", static_cast<unsigned>(pos), static_cast<unsigned>(totalSize));
    return INTERNAL_UPDATE_ERROR;
  }

  if (!ota_boot::switchTo(dest)) {
    LOG_ERR("OTA", "manual otadata switch failed");
    return INTERNAL_UPDATE_ERROR;
  }

  LOG_INF("OTA", "Update completed");
  return OK;
}
