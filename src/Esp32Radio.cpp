#include "Esp32Radio.h"

#include <WiFi.h>

#include <algorithm>
#include <cstring>

#include "Configuration.h"
#include "esp_system.h"
#include "esp_wifi.h"

namespace {

// How long the radio listens on each channel in survey(). Thirteen channels
// at this are under two seconds.
constexpr uint32_t SURVEY_MS_PER_CHANNEL = 120;

// How long a change to the access point's addresses waits for the access
// point to be up. It is up in a few tens of milliseconds.
constexpr uint32_t ACCESS_POINT_UP_MS = 1000;

// The names and passwords the radio keeps are fixed fields that are full
// without a NUL at the end.
String fieldText(const uint8_t* field, size_t size) {
  char text[65];
  const size_t length = std::min(size, sizeof(text) - 1);
  memcpy(text, field, length);
  text[length] = '\0';
  return String(text);
}

void fillField(uint8_t* field, size_t size, const String& text) {
  memcpy(field, text.c_str(),
         std::min(size, static_cast<size_t>(text.length())));
}

// Radio.h names these for a build that has no chip. They are the chip's.
static_assert(RADIO_REASON_ASSOC_LEAVE == WIFI_REASON_ASSOC_LEAVE,
              "RADIO_REASON_ASSOC_LEAVE is not the chip's");
static_assert(RADIO_REASON_4WAY_HANDSHAKE_TIMEOUT ==
                  WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT,
              "RADIO_REASON_4WAY_HANDSHAKE_TIMEOUT is not the chip's");
static_assert(RADIO_REASON_BEACON_TIMEOUT == WIFI_REASON_BEACON_TIMEOUT,
              "RADIO_REASON_BEACON_TIMEOUT is not the chip's");
static_assert(RADIO_REASON_NO_AP_FOUND == WIFI_REASON_NO_AP_FOUND,
              "RADIO_REASON_NO_AP_FOUND is not the chip's");
static_assert(RADIO_REASON_AUTH_FAIL == WIFI_REASON_AUTH_FAIL,
              "RADIO_REASON_AUTH_FAIL is not the chip's");
static_assert(RADIO_REASON_HANDSHAKE_TIMEOUT == WIFI_REASON_HANDSHAKE_TIMEOUT,
              "RADIO_REASON_HANDSHAKE_TIMEOUT is not the chip's");

}  // namespace

Esp32Radio::Esp32Radio(Logger* logger) { this->logger = logger; }

String Esp32Radio::machineId() {
  uint8_t mac[6] = {0};
  esp_efuse_mac_get_default(mac);
  char id[5];
  snprintf(id, sizeof(id), "%02X%02X", mac[4], mac[5]);
  return String(id);
}

// Awake, and as loud as this radio goes. Sleep stretched a round trip on
// the weak spot from about 80 ms to about 250 ms. The pages leave the
// device, so a quieter transmit makes them slower. Said again whenever the
// station or the access point has been turned on.
void Esp32Radio::wake() {
  WiFi.setSleep(false);
  WiFi.setTxPower(WIFI_POWER_19_5dBm);
}

void Esp32Radio::initialize() {
  WiFi.onEvent([this](system_event_id_t event, system_event_info_t info) {
    switch (event) {
      case SYSTEM_EVENT_STA_CONNECTED:
        this->associated.store(true);
        break;
      case SYSTEM_EVENT_STA_GOT_IP:
        this->addressed.store(true);
        break;
      case SYSTEM_EVENT_STA_LOST_IP:
        this->addressed.store(false);
        break;
      case SYSTEM_EVENT_STA_DISCONNECTED:
        this->associated.store(false);
        this->addressed.store(false);
        this->lastReason.store(info.disconnected.reason);
        this->drops.fetch_add(1);
        break;
      default:
        break;
    }
  });

  // The core's own retry skips the reasons a weak link actually fails with,
  // so it is off, and join() is the only thing that starts a try. Nothing in
  // the library's start joins a network either, whatever its auto-connect
  // setting says: the one way into a join there is esp_wifi_connect().
  WiFi.setAutoReconnect(false);
  WiFi.mode(WIFI_STA);

  // What the driver has just loaded from its own storage is the network of
  // the firmware before this one, when there was one.
  wifi_config_t conf;
  memset(&conf, 0, sizeof(conf));
  if (esp_wifi_get_config(WIFI_IF_STA, &conf) == ESP_OK) {
    this->inheritedSsid = fieldText(conf.sta.ssid, sizeof(conf.sta.ssid));
    this->inheritedPassword =
        fieldText(conf.sta.password, sizeof(conf.sta.password));
  }

  // From here on nothing the radio is told is written to its storage: not
  // the network of each try, and not the name and the password of the
  // machine's own. What was in there stays as it was.
  esp_wifi_set_storage(WIFI_STORAGE_RAM);

  this->wake();
}

void Esp32Radio::join(const String& ssid, const String& password) {
  const bool wasOn = (WiFi.getMode() & WIFI_MODE_STA) != 0;
  if (!WiFi.enableSTA(true)) {
    this->logger->warn("the radio would not turn its station on");
    return;
  }
  if (!wasOn) {
    this->wake();
  }

  // arduino-esp32 2.x connects to the strongest AP of a name and accepts
  // any signal. This core's begin(ssid, pass) asks for a full scan but
  // leaves the signal floor at 0, which drops every real AP. So the network
  // is handed to the driver from here, with the floor as low as it goes, and
  // the no-argument begin() then joins what the driver was handed.
  wifi_config_t conf;
  memset(&conf, 0, sizeof(conf));
  fillField(conf.sta.ssid, sizeof(conf.sta.ssid), ssid);
  fillField(conf.sta.password, sizeof(conf.sta.password), password);
  conf.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
  conf.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
  conf.sta.threshold.rssi = -127;
  if (esp_wifi_set_config(WIFI_IF_STA, &conf) != ESP_OK) {
    // No try was started, so none will end. The supervisor gives up on it
    // as it does on a try that hangs.
    this->logger->warn(String("the radio would not take ") + ssid);
    return;
  }
  WiFi.begin();
}

void Esp32Radio::leave() { WiFi.disconnect(); }

void Esp32Radio::stopStation() { WiFi.disconnect(true); }

StationState Esp32Radio::station() {
  StationState state;
  state.associated = this->associated.load();
  state.addressed = this->addressed.load();
  state.lastReason = this->lastReason.load();
  state.drops = this->drops.load();
  return state;
}

String Esp32Radio::address() {
  // The core says 0.0.0.0 for a station that has no address.
  const IPAddress address = WiFi.localIP();
  return address == IPAddress(0, 0, 0, 0) ? String("") : address.toString();
}

int Esp32Radio::channel() { return WiFi.channel(); }

int Esp32Radio::rssi() { return WiFi.RSSI(); }

bool Esp32Radio::openAccessPoint(const String& name, const String& password,
                                 int channel, bool offerRouter) {
  // This core opens a network with no password when it is handed none, and
  // ESPAsyncWiFiManager opened the old setup network that way when the one it
  // was handed was too short. Neither is asked to here.
  if (password.length() < (unsigned int)Radio::MIN_PASSWORD_LENGTH) {
    return false;
  }
  // softAP() turns the access point on a moment before it hands it the name
  // and the password. On the first opening after a boot the radio holds, for
  // that moment, the network its storage had, which has no password: the
  // setup network of the firmware before this one, or the one the chip is
  // made with. Closing that would take stopping and starting the radio at
  // every boot, which is more to go wrong than a millisecond that no phone
  // can join in.
  const bool wasOpen = (WiFi.getMode() & WIFI_MODE_AP) != 0;
  if (!WiFi.softAP(name.c_str(), password.c_str(), channel, 0,
                   WIFI_OWN_CLIENTS)) {
    this->closeAccessPoint();
    return false;
  }

  // What the driver holds now is what a phone meets, so that is what is
  // checked: the name asked for, under WPA2.
  wifi_config_t conf;
  memset(&conf, 0, sizeof(conf));
  if (esp_wifi_get_config(WIFI_IF_AP, &conf) != ESP_OK ||
      conf.ap.authmode != WIFI_AUTH_WPA2_PSK ||
      fieldText(conf.ap.ssid, sizeof(conf.ap.ssid)) != name) {
    this->closeAccessPoint();
    return false;
  }
  this->wake();

  // As the chip starts, the access point is at WIFI_OWN_ADDRESS and offers
  // that address as the router. That is left alone until the offer is first
  // taken away. The offer goes when the gateway is 0.0.0.0, and it is set
  // again at every opening, in case a close puts the gateway back.
  //
  // The change has to wait for the access point to be up. Made before that,
  // it leaves the address server marked as started when it is not, and no
  // phone gets an address.
  if (offerRouter != this->routerOffered || (!offerRouter && !wasOpen)) {
    WiFiGenericClass::waitStatusBits(AP_STARTED_BIT, ACCESS_POINT_UP_MS);
    IPAddress own;
    own.fromString(WIFI_OWN_ADDRESS);
    if (WiFi.softAPConfig(own, offerRouter ? own : IPAddress(0, 0, 0, 0),
                          IPAddress(255, 255, 255, 0))) {
      this->routerOffered = offerRouter;
    } else {
      this->logger->warn(
          "the radio would not change what its own network offers");
    }
  }
  return true;
}

void Esp32Radio::closeAccessPoint() {
  // Not softAPdisconnect(): that hands the driver a network with no name and
  // no password before it turns the access point off.
  WiFi.enableAP(false);
  this->wake();
}

int Esp32Radio::clients() { return WiFi.softAPgetStationNum(); }

bool Esp32Radio::survey(std::vector<HeardNetwork>* heard) {
  heard->clear();
  const int16_t count =
      WiFi.scanNetworks(false, true, false, SURVEY_MS_PER_CHANNEL);
  if (count < 0) {
    WiFi.scanDelete();
    return false;
  }
  for (int16_t i = 0; i < count; i++) {
    HeardNetwork network;
    network.ssid = WiFi.SSID(i);
    network.channel = WiFi.channel(i);
    network.rssi = WiFi.RSSI(i);
    network.secured = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
    heard->push_back(network);
  }
  WiFi.scanDelete();
  return true;
}

bool Esp32Radio::inherited(String* ssid, String* password) {
  if (this->inheritedSsid.length() == 0) {
    return false;
  }
  *ssid = this->inheritedSsid;
  *password = this->inheritedPassword;
  return true;
}
