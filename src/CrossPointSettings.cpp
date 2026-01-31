#include "CrossPointSettings.h"

#include <HardwareSerial.h>
#include <SDCardManager.h>
#include <Serialization.h>

#include <cstring>

#include "fontIds.h"

// Initialize the static instance
CrossPointSettings CrossPointSettings::instance;

void readAndValidate(FsFile& file, uint8_t& member, const uint8_t maxValue) {
  uint8_t tempValue;
  serialization::readPod(file, tempValue);
  if (tempValue < maxValue) {
    member = tempValue;
  }
}

namespace {
constexpr uint8_t SETTINGS_FILE_VERSION = 6;  // Incremented for paragraph indent support
// Increment this when adding new persisted settings fields
constexpr uint8_t SETTINGS_COUNT = 24;  // +1 for hyphenationEnabled (Korean), includes all upstream fields
constexpr char SETTINGS_FILE[] = "/.crosspoint/settings.bin";
}  // namespace

bool CrossPointSettings::saveToFile() const {
  // Make sure the directory exists
  SdMan.mkdir("/.crosspoint");

  FsFile outputFile;
  if (!SdMan.openFileForWrite("CPS", SETTINGS_FILE, outputFile)) {
    return false;
  }

  serialization::writePod(outputFile, SETTINGS_FILE_VERSION);
  serialization::writePod(outputFile, SETTINGS_COUNT);
  serialization::writePod(outputFile, sleepScreen);
  serialization::writePod(outputFile, extraParagraphSpacing);
  serialization::writePod(outputFile, shortPwrBtn);
  serialization::writePod(outputFile, statusBar);
  serialization::writePod(outputFile, orientation);
  serialization::writePod(outputFile, frontButtonLayout);
  serialization::writePod(outputFile, sideButtonLayout);
  serialization::writePod(outputFile, lineSpacing);
  serialization::writePod(outputFile, paragraphAlignment);
  serialization::writePod(outputFile, sleepTimeout);
  serialization::writePod(outputFile, refreshFrequency);
  serialization::writePod(outputFile, screenMargin);
  serialization::writePod(outputFile, sleepScreenCoverMode);
  serialization::writeString(outputFile, std::string(opdsServerUrl));
  serialization::writePod(outputFile, textAntiAliasing);
  serialization::writePod(outputFile, hideBatteryPercentage);
  serialization::writePod(outputFile, longPressChapterSkip);
  serialization::writeString(outputFile, std::string(customFontPath));
  serialization::writePod(outputFile, characterWrap);
  serialization::writePod(outputFile, paragraphIndent);
  serialization::writePod(outputFile, hyphenationEnabled);
  serialization::writeString(outputFile, std::string(opdsUsername));
  serialization::writeString(outputFile, std::string(opdsPassword));
  serialization::writePod(outputFile, sleepScreenCoverFilter);
  // New fields added at end for backward compatibility
  outputFile.close();

  Serial.printf("[%lu] [CPS] Settings saved to file\n", millis());
  return true;
}

bool CrossPointSettings::loadFromFile() {
  FsFile inputFile;
  if (!SdMan.openFileForRead("CPS", SETTINGS_FILE, inputFile)) {
    return false;
  }

  // Check file size for sanity
  const uint32_t fileSize = inputFile.size();
  if (fileSize < 2 || fileSize > 4096) {
    Serial.printf("[%lu] [CPS] Settings file corrupted (size=%u), deleting\n", millis(), fileSize);
    inputFile.close();
    SdMan.remove(SETTINGS_FILE);
    return false;
  }

  uint8_t version;
  serialization::readPod(inputFile, version);

  // Handle different versions - accept version 1, 2, 3, 4, 5, 6
  if (version < 1 || version > 6) {
    Serial.printf("[%lu] [CPS] Deserialization failed: Unknown version %u, deleting settings file\n", millis(),
                  version);
    inputFile.close();
    SdMan.remove(SETTINGS_FILE);
    return false;
  }

  uint8_t fileSettingsCount = 0;
  serialization::readPod(inputFile, fileSettingsCount);

  // Sanity check settings count
  if (fileSettingsCount > 50) {
    Serial.printf("[%lu] [CPS] Settings count invalid (%u), deleting settings file\n", millis(), fileSettingsCount);
    inputFile.close();
    SdMan.remove(SETTINGS_FILE);
    return false;
  }

  // load settings that exist (support older files with fewer fields)
  uint8_t settingsRead = 0;
  do {
    readAndValidate(inputFile, sleepScreen, SLEEP_SCREEN_MODE_COUNT);
    if (++settingsRead >= fileSettingsCount) break;
    serialization::readPod(inputFile, extraParagraphSpacing);
    if (++settingsRead >= fileSettingsCount) break;
    readAndValidate(inputFile, shortPwrBtn, SHORT_PWRBTN_COUNT);
    if (++settingsRead >= fileSettingsCount) break;
    readAndValidate(inputFile, statusBar, STATUS_BAR_MODE_COUNT);
    if (++settingsRead >= fileSettingsCount) break;
    readAndValidate(inputFile, orientation, ORIENTATION_COUNT);
    if (++settingsRead >= fileSettingsCount) break;
    readAndValidate(inputFile, frontButtonLayout, FRONT_BUTTON_LAYOUT_COUNT);
    if (++settingsRead >= fileSettingsCount) break;
    readAndValidate(inputFile, sideButtonLayout, SIDE_BUTTON_LAYOUT_COUNT);
    if (++settingsRead >= fileSettingsCount) break;

    // Skip fontFamily and fontSize from older versions (Korean version ignores these)
    if (version == 1) {
      uint8_t dummy;
      serialization::readPod(inputFile, dummy);  // fontFamily (ignored)
      if (++settingsRead >= fileSettingsCount) break;
      serialization::readPod(inputFile, dummy);  // fontSize (ignored)
      if (++settingsRead >= fileSettingsCount) break;
    }

    readAndValidate(inputFile, lineSpacing, LINE_COMPRESSION_COUNT);
    if (++settingsRead >= fileSettingsCount) break;

    // Skip systemFontFamily from version 2
    if (version == 2) {
      uint8_t dummy;
      serialization::readPod(inputFile, dummy);  // systemFontFamily (ignored)
      if (++settingsRead >= fileSettingsCount) break;
    }

    readAndValidate(inputFile, paragraphAlignment, PARAGRAPH_ALIGNMENT_COUNT);
    if (++settingsRead >= fileSettingsCount) break;
    readAndValidate(inputFile, sleepTimeout, SLEEP_TIMEOUT_COUNT);
    if (++settingsRead >= fileSettingsCount) break;
    readAndValidate(inputFile, refreshFrequency, REFRESH_FREQUENCY_COUNT);
    if (++settingsRead >= fileSettingsCount) break;
    serialization::readPod(inputFile, screenMargin);
    if (++settingsRead >= fileSettingsCount) break;
    readAndValidate(inputFile, sleepScreenCoverMode, SLEEP_SCREEN_COVER_MODE_COUNT);
    if (++settingsRead >= fileSettingsCount) break;
    {
      std::string urlStr;
      serialization::readString(inputFile, urlStr);
      strncpy(opdsServerUrl, urlStr.c_str(), sizeof(opdsServerUrl) - 1);
      opdsServerUrl[sizeof(opdsServerUrl) - 1] = '\0';
    }
    if (++settingsRead >= fileSettingsCount) break;
    serialization::readPod(inputFile, textAntiAliasing);
    if (++settingsRead >= fileSettingsCount) break;
    readAndValidate(inputFile, hideBatteryPercentage, HIDE_BATTERY_PERCENTAGE_COUNT);
    if (++settingsRead >= fileSettingsCount) break;
    serialization::readPod(inputFile, longPressChapterSkip);
    if (++settingsRead >= fileSettingsCount) break;
    // Version 4+: Custom font path
    if (version >= 4) {
      std::string fontPathStr;
      serialization::readString(inputFile, fontPathStr);
      strncpy(customFontPath, fontPathStr.c_str(), sizeof(customFontPath) - 1);
      customFontPath[sizeof(customFontPath) - 1] = '\0';
      if (++settingsRead >= fileSettingsCount) break;
    }
    // Version 5+: Character wrap
    if (version >= 5) {
      serialization::readPod(inputFile, characterWrap);
      if (++settingsRead >= fileSettingsCount) break;
    }
    // Version 6+: Paragraph indent
    if (version >= 6) {
      serialization::readPod(inputFile, paragraphIndent);
      if (++settingsRead >= fileSettingsCount) break;
    }
    serialization::readPod(inputFile, hyphenationEnabled);
    if (++settingsRead >= fileSettingsCount) break;
    {
      std::string usernameStr;
      serialization::readString(inputFile, usernameStr);
      strncpy(opdsUsername, usernameStr.c_str(), sizeof(opdsUsername) - 1);
      opdsUsername[sizeof(opdsUsername) - 1] = '\0';
    }
    if (++settingsRead >= fileSettingsCount) break;
    {
      std::string passwordStr;
      serialization::readString(inputFile, passwordStr);
      strncpy(opdsPassword, passwordStr.c_str(), sizeof(opdsPassword) - 1);
      opdsPassword[sizeof(opdsPassword) - 1] = '\0';
    }
    if (++settingsRead >= fileSettingsCount) break;
    readAndValidate(inputFile, sleepScreenCoverFilter, SLEEP_SCREEN_COVER_FILTER_COUNT);
    if (++settingsRead >= fileSettingsCount) break;
    // New fields added at end for backward compatibility
  } while (false);

  inputFile.close();
  Serial.printf("[%lu] [CPS] Settings loaded from file\n", millis());
  return true;
}

float CrossPointSettings::getReaderLineCompression() const {
  // Fixed to KoPub Batang 14pt font
  switch (lineSpacing) {
    case TIGHT:
      return 1.00f;
    case NORMAL:
    default:
      return 1.20f;
    case WIDE:
      return 1.40f;
  }
}

unsigned long CrossPointSettings::getSleepTimeoutMs() const {
  switch (sleepTimeout) {
    case SLEEP_1_MIN:
      return 1UL * 60 * 1000;
    case SLEEP_5_MIN:
      return 5UL * 60 * 1000;
    case SLEEP_10_MIN:
    default:
      return 10UL * 60 * 1000;
    case SLEEP_15_MIN:
      return 15UL * 60 * 1000;
    case SLEEP_30_MIN:
      return 30UL * 60 * 1000;
  }
}

int CrossPointSettings::getRefreshFrequency() const {
  switch (refreshFrequency) {
    case REFRESH_1:
      return 1;
    case REFRESH_5:
      return 5;
    case REFRESH_10:
      return 10;
    case REFRESH_15:
    default:
      return 15;
    case REFRESH_30:
      return 30;
  }
}

int CrossPointSettings::getReaderFontId() const {
  // Return custom font ID if a custom font is configured
  // The actual font loading and availability check happens in main.cpp
  if (hasCustomFont()) {
    // Generate unique negative ID based on font path hash
    // This ensures different custom fonts have different IDs for cache invalidation
    uint32_t hash = 5381;
    for (const char* p = customFontPath; *p; p++) {
      hash = ((hash << 5) + hash) + static_cast<uint8_t>(*p);  // djb2 hash
    }
    // Return negative value to avoid collision with built-in font IDs
    // Mask to ensure it stays in valid range
    return -static_cast<int>((hash & 0x7FFFFFFF) | 1);
  }
  // Default to KoPub Batang 14pt for Korean reader
  return KOPUB_14_FONT_ID;
}

int CrossPointSettings::getUiFontId() const {
  // Fixed to Pretendard 10pt
  return UI_FONT_ID;
}

const char* CrossPointSettings::getCustomFontName() const {
  if (!hasCustomFont()) {
    return "KoPub 바탕 (기본)";
  }
  // Extract filename from path (e.g., "/.crosspoint/fonts/MyFont.bin" -> "MyFont")
  const char* lastSlash = strrchr(customFontPath, '/');
  const char* filename = lastSlash ? lastSlash + 1 : customFontPath;
  // Remove .bin extension for display
  static char nameBuffer[32];
  strncpy(nameBuffer, filename, sizeof(nameBuffer) - 1);
  nameBuffer[sizeof(nameBuffer) - 1] = '\0';
  char* dot = strrchr(nameBuffer, '.');
  if (dot) {
    *dot = '\0';
  }
  return nameBuffer;
}
