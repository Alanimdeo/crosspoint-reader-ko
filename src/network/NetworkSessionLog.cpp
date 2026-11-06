#include "NetworkSessionLog.h"

#ifdef SIMULATOR

namespace netlog {
void open() {}
void line(const char*) {}
void tap(bool) {}
void tick() {}
void close() {}
}  // namespace netlog

#else

#include <Arduino.h>
#include <BatteryMonitor.h>
#include <BoardConfig.h>
#include <HalMemory.h>
#include <HalStorage.h>
#include <WiFi.h>
#include <esp_netif_net_stack.h>
#include <esp_private/wifi.h>
#include <esp_wifi.h>
#include <lwip/netif.h>
#include <lwip/pbuf.h>
#include <ping/ping_sock.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>

namespace {

constexpr char LOG_PATH[] = "/wifi-debug.log";
constexpr size_t LOG_MAX_BYTES = 16384;
constexpr unsigned long TICK_MS = 10000;
constexpr uint16_t MAX_TICKS = 60;

// Held open for the session and written from stack buffers, so logging keeps working on the nearly empty heap it may
// have to show. writeDebugLog() cannot: it reads the whole log into a std::string, which aborts when that fails.
HalFile logFile;
uint16_t ticks = 0;
unsigned long lastTickMs = 0;
bool eventsRegistered = false;

// Written by the Arduino event task. A lease proves frames went both ways: DISCOVER in, OFFER out, REQUEST in, ACK out.
std::atomic<uint16_t> joins{0};   // stations that associated with our AP
std::atomic<uint16_t> leaves{0};  // stations that left our AP
std::atomic<uint16_t> leases{0};  // leases our AP gave out, or addresses we got as a station
std::atomic<uint16_t> drops{0};   // disconnects from the network we joined
std::atomic<uint8_t> dropReason{0};
std::atomic<uint32_t> clientAddress{0};  // network order; the station our AP last gave a lease to

// Frames crossing the boundary between the Wi-Fi driver and lwIP, read one byte at a time because the buffers are not
// aligned for wider loads. Each field has one writer: the Wi-Fi task for rx and air, lwIP for tx, the ping task for
// ping.
struct FrameCounts {
  uint32_t rxUnicast, rxBroadcast, rxMulticast;  // every frame received, by destination
  uint32_t rxArp;                                // ARP requests for our address
  uint32_t rxEcho;                               // ICMP echo requests
  uint32_t rxSyn;                                // TCP SYN to port 80
  uint32_t rxTcp;                                // other TCP to port 80
  uint32_t rxDns;                                // DNS queries
  uint32_t txArp;                                // ARP replies
  uint32_t txEcho;                               // ICMP echo replies
  uint32_t txSynAck;                             // SYN-ACK from port 80
  uint32_t txTcp;                                // other TCP from port 80
  uint32_t txRst;                                // RST from port 80
  uint32_t txDns;                                // DNS answers
  uint32_t txError;                              // frames the Wi-Fi driver refused
  uint32_t airOk, airFailed;                     // transmissions the peer acknowledged over the air, or did not
  uint32_t pingOk, pingLost;                     // our own echo requests to the peer
  uint8_t peers[3][4];                           // addresses that tried to reach us
  uint8_t peerCount;
};
FrameCounts frames{};

constexpr uint16_t ETHERTYPE_IPV4 = 0x0800;
constexpr uint16_t ETHERTYPE_ARP = 0x0806;
constexpr uint8_t IP_ICMP = 1;
constexpr uint8_t IP_TCP = 6;
constexpr uint8_t IP_UDP = 17;
constexpr uint8_t TCP_SYN_FLAG = 0x02;
constexpr uint8_t TCP_RST_FLAG = 0x04;
constexpr uint8_t TCP_ACK_FLAG = 0x10;
constexpr uint16_t HTTP_PORT = 80;
constexpr uint16_t DNS_PORT = 53;

uint16_t readBe16(const uint8_t* bytes) { return static_cast<uint16_t>(bytes[0] << 8 | bytes[1]); }

void notePeer(const uint8_t* address) {
  for (uint8_t i = 0; i < frames.peerCount; ++i) {
    if (memcmp(frames.peers[i], address, 4) == 0) return;
  }
  if (frames.peerCount == sizeof(frames.peers) / sizeof(frames.peers[0])) return;
  memcpy(frames.peers[frames.peerCount], address, 4);
  ++frames.peerCount;
}

void countRx(const uint8_t* frame, const size_t length, const uint8_t* ownAddress) {
  static constexpr uint8_t BROADCAST[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  if (memcmp(frame, BROADCAST, sizeof(BROADCAST)) == 0) {
    ++frames.rxBroadcast;
  } else if (frame[0] & 1) {
    ++frames.rxMulticast;
  } else {
    ++frames.rxUnicast;
  }

  const uint16_t etherType = readBe16(frame + 12);
  if (etherType == ETHERTYPE_ARP) {
    // Operation at 20, sender address at 28, target address at 38.
    if (length >= 42 && readBe16(frame + 20) == 1 && memcmp(frame + 38, ownAddress, 4) == 0) {
      ++frames.rxArp;
      notePeer(frame + 28);
    }
    return;
  }
  if (etherType != ETHERTYPE_IPV4 || length < 34) return;
  const size_t l4 = 14 + (frame[14] & 0x0F) * 4u;
  const uint8_t protocol = frame[23];
  if (protocol == IP_ICMP && length > l4) {
    if (frame[l4] == 8) {
      ++frames.rxEcho;
      notePeer(frame + 26);
    }
  } else if (protocol == IP_TCP && length >= l4 + 14 && readBe16(frame + l4 + 2) == HTTP_PORT) {
    if ((frame[l4 + 13] & (TCP_SYN_FLAG | TCP_ACK_FLAG)) == TCP_SYN_FLAG) {
      ++frames.rxSyn;
      notePeer(frame + 26);
    } else {
      ++frames.rxTcp;
    }
  } else if (protocol == IP_UDP && length >= l4 + 4 && readBe16(frame + l4 + 2) == DNS_PORT) {
    ++frames.rxDns;
  }
}

void countTx(const uint8_t* frame, const size_t length) {
  const uint16_t etherType = readBe16(frame + 12);
  if (etherType == ETHERTYPE_ARP) {
    if (length >= 22 && readBe16(frame + 20) == 2) ++frames.txArp;
    return;
  }
  if (etherType != ETHERTYPE_IPV4 || length < 34) return;
  const size_t l4 = 14 + (frame[14] & 0x0F) * 4u;
  const uint8_t protocol = frame[23];
  if (protocol == IP_ICMP && length > l4) {
    if (frame[l4] == 0) ++frames.txEcho;
  } else if (protocol == IP_TCP && length >= l4 + 14 && readBe16(frame + l4) == HTTP_PORT) {
    const uint8_t flags = frame[l4 + 13];
    if (flags & TCP_RST_FLAG) {
      ++frames.txRst;
    } else if ((flags & (TCP_SYN_FLAG | TCP_ACK_FLAG)) == (TCP_SYN_FLAG | TCP_ACK_FLAG)) {
      ++frames.txSynAck;
    } else {
      ++frames.txTcp;
    }
  } else if (protocol == IP_UDP && length >= l4 + 2 && readBe16(frame + l4) == DNS_PORT) {
    ++frames.txDns;
  }
}

netif_input_fn originalInput = nullptr;
netif_linkoutput_fn originalOutput = nullptr;
esp_ping_handle_t ping = nullptr;

err_t tapInput(struct pbuf* packet, struct netif* interface) {
  if (packet->len >= 14) {
    countRx(static_cast<const uint8_t*>(packet->payload), packet->len,
            reinterpret_cast<const uint8_t*>(&netif_ip4_addr(interface)->addr));
  }
  return originalInput(packet, interface);
}

err_t tapOutput(struct netif* interface, struct pbuf* packet) {
  if (packet->len >= 14) countTx(static_cast<const uint8_t*>(packet->payload), packet->len);
  const err_t result = originalOutput(interface, packet);
  if (result != ERR_OK) ++frames.txError;
  return result;
}

void IRAM_ATTR onTxDone(uint8_t, uint8_t*, uint16_t*, const bool acknowledged) {
  if (acknowledged) {
    ++frames.airOk;
  } else {
    ++frames.airFailed;
  }
}

void onPingOk(esp_ping_handle_t, void*) { ++frames.pingOk; }
void onPingLost(esp_ping_handle_t, void*) { ++frames.pingLost; }

// Our own echo requests prove the path to the peer without its help, one every 3 s.
void startPing(const uint32_t address) {
  esp_ping_config_t config = ESP_PING_DEFAULT_CONFIG();
  config.target_addr.type = IPADDR_TYPE_V4;
  config.target_addr.u_addr.ip4.addr = address;
  config.count = ESP_PING_COUNT_INFINITE;
  config.interval_ms = 3000;
  esp_ping_callbacks_t callbacks{};
  callbacks.on_ping_success = onPingOk;
  callbacks.on_ping_timeout = onPingLost;
  if (esp_ping_new_session(&config, &callbacks, &ping) != ESP_OK) {
    ping = nullptr;
    return;
  }
  esp_ping_start(ping);
}

void onNetworkEvent(arduino_event_t* event) {
  switch (event->event_id) {
    case ARDUINO_EVENT_WIFI_AP_STACONNECTED:
      joins.fetch_add(1, std::memory_order_relaxed);
      break;
    case ARDUINO_EVENT_WIFI_AP_STADISCONNECTED:
      leaves.fetch_add(1, std::memory_order_relaxed);
      break;
    case ARDUINO_EVENT_WIFI_AP_STAIPASSIGNED:
      clientAddress.store(event->event_info.wifi_ap_staipassigned.ip.addr, std::memory_order_relaxed);
      leases.fetch_add(1, std::memory_order_relaxed);
      break;
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      leases.fetch_add(1, std::memory_order_relaxed);
      break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      drops.fetch_add(1, std::memory_order_relaxed);
      dropReason.store(event->event_info.wifi_sta_disconnected.reason, std::memory_order_relaxed);
      break;
    default:
      break;
  }
}

void writeText(const char* text) {
  logFile.write(text, strlen(text));
  logFile.flush();
}

void writeFrameCounts() {
  if (!logFile) return;
  char text[240];
  const int written =
      snprintf(text, sizeof(text),
               "net rx u=%lu b=%lu m=%lu arp=%lu echo=%lu syn=%lu tcp=%lu dns=%lu tx arp=%lu echo=%lu synack=%lu "
               "tcp=%lu rst=%lu "
               "dns=%lu err=%lu air=%lu/%lu ping=%lu/%lu",
               static_cast<unsigned long>(frames.rxUnicast), static_cast<unsigned long>(frames.rxBroadcast),
               static_cast<unsigned long>(frames.rxMulticast), static_cast<unsigned long>(frames.rxArp),
               static_cast<unsigned long>(frames.rxEcho), static_cast<unsigned long>(frames.rxSyn),
               static_cast<unsigned long>(frames.rxTcp), static_cast<unsigned long>(frames.rxDns),
               static_cast<unsigned long>(frames.txArp), static_cast<unsigned long>(frames.txEcho),
               static_cast<unsigned long>(frames.txSynAck), static_cast<unsigned long>(frames.txTcp),
               static_cast<unsigned long>(frames.txRst), static_cast<unsigned long>(frames.txDns),
               static_cast<unsigned long>(frames.txError), static_cast<unsigned long>(frames.airOk),
               static_cast<unsigned long>(frames.airFailed), static_cast<unsigned long>(frames.pingOk),
               static_cast<unsigned long>(frames.pingLost));
  size_t length = written > 0 ? std::min(static_cast<size_t>(written), sizeof(text) - 2) : 0;
  for (uint8_t i = 0; i < frames.peerCount && length + 22 < sizeof(text); ++i) {
    length += snprintf(text + length, sizeof(text) - length, " peer=%u.%u.%u.%u", frames.peers[i][0],
                       frames.peers[i][1], frames.peers[i][2], frames.peers[i][3]);
  }
  text[length] = '\n';
  text[length + 1] = '\0';
  writeText(text);
}

}  // namespace

namespace netlog {

void open() {
  constexpr auto flags = O_WRITE | O_CREAT | O_APPEND;
  logFile = Storage.open(LOG_PATH, flags);
  if (logFile && logFile.size() > LOG_MAX_BYTES) {
    logFile.close();
    Storage.remove(LOG_PATH);
    logFile = Storage.open(LOG_PATH, flags);
  }
  if (!logFile) return;

  if (!eventsRegistered) {
    WiFi.onEvent(onNetworkEvent);
    eventsRegistered = true;
  }
  joins = 0;
  leaves = 0;
  leases = 0;
  drops = 0;
  dropReason = 0;
  clientAddress = 0;
  frames = {};
  ticks = 0;
  lastTickMs = millis();

  static const BatteryMonitor battery;
  char text[96];
  snprintf(text, sizeof(text), "--- %s %s bat=%umV\n", CROSSPOINT_VERSION, BoardConfig::ACTIVE.name,
           static_cast<unsigned>(battery.readMillivolts()));
  writeText(text);
}

void line(const char* stage) {
  if (!logFile) return;
  const auto heap = HalMemory::getInternalHeap();
  const wifi_mode_t mode = WiFi.getMode();
  const int stations = (mode & WIFI_MODE_AP) ? WiFi.softAPgetStationNum() : -1;
  int rssi = 0;
  if (mode & WIFI_MODE_STA) {
    rssi = WiFi.RSSI();
  } else if (mode & WIFI_MODE_AP) {
    static wifi_sta_list_t list;
    if (esp_wifi_ap_get_sta_list(&list) == ESP_OK && list.num > 0) rssi = list.sta[0].rssi;
  }
  const int channel = mode != WIFI_MODE_NULL ? WiFi.channel() : 0;

  char text[160];
  snprintf(
      text, sizeof(text), "%lu %s free=%u largest=%u min=%u sta=%d join=%u leave=%u ip=%u drop=%u/%u rssi=%d ch=%d\n",
      millis(), stage, static_cast<unsigned>(heap.freeBytes), static_cast<unsigned>(heap.largestBlockBytes),
      static_cast<unsigned>(heap.minFreeBytes), stations, static_cast<unsigned>(joins.load(std::memory_order_relaxed)),
      static_cast<unsigned>(leaves.load(std::memory_order_relaxed)),
      static_cast<unsigned>(leases.load(std::memory_order_relaxed)),
      static_cast<unsigned>(drops.load(std::memory_order_relaxed)),
      static_cast<unsigned>(dropReason.load(std::memory_order_relaxed)), rssi, channel);
  writeText(text);
}

void tap(const bool accessPoint) {
  if (originalInput) return;
  auto* interface =
      static_cast<struct netif*>(esp_netif_get_netif_impl(accessPoint ? WiFi.AP.netif() : WiFi.STA.netif()));
  if (!interface) return;
  originalInput = interface->input;
  originalOutput = interface->linkoutput;
  // The Wi-Fi task can call through the taps as soon as they are in place.
  std::atomic_thread_fence(std::memory_order_seq_cst);
  interface->linkoutput = tapOutput;
  interface->input = tapInput;
  esp_wifi_set_tx_done_cb(onTxDone);

  esp_netif_ip_info_t info{};
  if (!accessPoint && esp_netif_get_ip_info(WiFi.STA.netif(), &info) == ESP_OK) startPing(info.gw.addr);
}

void tick() {
  const uint32_t client = clientAddress.load(std::memory_order_relaxed);
  if (!ping && client != 0) startPing(client);

  const unsigned long now = millis();
  if (ticks >= MAX_TICKS || now - lastTickMs < TICK_MS) return;
  lastTickMs = now;
  ++ticks;
  line("run");
  writeFrameCounts();
}

void close() {
  writeFrameCounts();
  logFile.close();
  if (ping) {
    esp_ping_stop(ping);
    esp_ping_delete_session(ping);
    ping = nullptr;
  }
}

}  // namespace netlog

#endif  // SIMULATOR
