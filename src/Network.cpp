#include "Network.h"

#include <Arduino.h>
#include <AsyncElegantOTA.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ESPAsyncWiFiManager.h>
#include <ESPmDNS.h>
#include <WiFi.h>

#include <algorithm>
#include <cstring>

#include "Api.h"
#include "Configuration.h"
#include "Display.h"
#include "Logger.h"
#include "SPIFFS.h"
#include "esp_heap_caps.h"
#include "esp_wifi.h"

Network* Network::instance = NULL;

// --- ApiHandler ---

// The webserver's side of the Api. Every request under /api/, whatever its
// method or type, goes to the Api as a Request, and its Reply comes back as it
// is. Whether a path exists, which method it takes and what its body must
// hold are the Api's to say, and are tested there, so this answers for all of
// /api/ and has no rules of its own.
class ApiHandler : public AsyncWebHandler {
 private:
  Api* api;

 public:
  explicit ApiHandler(Api* api) : api(api) {}

  bool canHandle(AsyncWebServerRequest* request) override {
    return request->url().startsWith("/api/");
  }

  // The body as it arrives, a chunk at a time, into a buffer the request
  // frees once it is done. Past Api::MAX_BODY_BYTES + 1 bytes the rest is
  // dropped: a body that long is refused whatever the rest says, and the one
  // byte over is what tells the Api it is too long.
  void handleBody(AsyncWebServerRequest* request, uint8_t* data, size_t len,
                  size_t index, size_t total) override {
    const size_t kept = std::min(total, Api::MAX_BODY_BYTES + 1);
    if (index == 0) {
      // Zeroed, so the body is a string that ends where the last chunk kept
      // does. The request frees it with free().
      request->_tempObject = calloc(kept + 1, 1);
    }
    if (request->_tempObject != NULL && index < kept) {
      memcpy(static_cast<char*>(request->_tempObject) + index, data,
             std::min(len, kept - index));
    }
  }

  void handleRequest(AsyncWebServerRequest* request) override {
    Request asked;
    switch (request->method()) {
      case HTTP_GET:
        asked.method = Method::GET;
        break;
      case HTTP_POST:
        asked.method = Method::POST;
        break;
      default:
        asked.method = Method::OTHER;
        break;
    }
    asked.path = request->url();
    // The webserver has already split and decoded the query string. It keeps
    // a form post's fields in the same list, marked as posted, and those are
    // a body the Api does not read, so they are left out.
    for (size_t i = 0; i < request->params(); i++) {
      const AsyncWebParameter* param = request->getParam(i);
      if (!param->isPost() && !param->isFile()) {
        asked.query[param->name()] = param->value();
      }
    }
    asked.contentType = request->contentType();
    if (request->_tempObject != NULL) {
      asked.body = static_cast<const char*>(request->_tempObject);
    }

    const Reply reply = this->api->handle(asked);
    AsyncWebServerResponse* response =
        request->beginResponse(reply.code, reply.contentType, reply.body);
    if (reply.allow != NULL) {
      response->addHeader("Allow", reply.allow);
    }
    // True only at that moment: a stored copy would be the device as it
    // was, and the page would keep showing that.
    response->addHeader("Cache-Control", "no-store");
    request->send(response);
  }
};

// --- Network ---

namespace {

// The disconnect reasons this core reports, by the number the event
// carries. Anything else is printed as that number. The ones the core
// itself never retries are 3, 4, 8, 15 and 202, which is most of what a
// weak link produces.
const char* reasonName(uint8_t reason) {
  switch (reason) {
    case 0:
      return "NONE";
    case 1:
      return "UNSPECIFIED";
    case 2:
      return "AUTH_EXPIRE";
    case 3:
      return "AUTH_LEAVE";
    case 4:
      return "ASSOC_EXPIRE";
    case 5:
      return "ASSOC_TOOMANY";
    case 6:
      return "NOT_AUTHED";
    case 7:
      return "NOT_ASSOCED";
    case 8:
      return "ASSOC_LEAVE";
    case 15:
      return "4WAY_HANDSHAKE_TIMEOUT";
    case 16:
      return "GROUP_KEY_UPDATE_TIMEOUT";
    case 200:
      return "BEACON_TIMEOUT";
    case 201:
      return "NO_AP_FOUND";
    case 202:
      return "AUTH_FAIL";
    case 203:
      return "ASSOC_FAIL";
    case 204:
      return "HANDSHAKE_TIMEOUT";
    default:
      return NULL;
  }
}

String reasonText(uint8_t reason) {
  const char* name = reasonName(reason);
  if (name != NULL) {
    return String(name);
  }
  return String(reason);
}

// Awake, and as loud as this radio goes. Sleep stretched a round trip on
// the weak spot from about 80 ms to about 250 ms. The pages leave the
// device, so a quieter transmit makes them slower.
void wakeRadio() {
  WiFi.setSleep(false);
  WiFi.setTxPower(WIFI_POWER_19_5dBm);
}

}  // namespace

Network::Network(Logger* logger, Display* display, Api* api, uint8_t resetPin) {
  Network::instance = this;
  this->logger = logger;
  this->display = display;
  this->api = api;
  this->server = new AsyncWebServer(80);
  this->dns = new DNSServer();
  this->resetPin = resetPin;
}

Network::~Network() {
  delete server;
  delete dns;
}

void Network::onWiFiEventStatic(system_event_id_t event,
                                system_event_info_t info) {
  if (instance != NULL) {
    instance->onWiFiEvent(event, info);
  }
}

void Network::onWiFiEvent(system_event_id_t event, system_event_info_t info) {
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
      this->lastDropMs.store(millis());
      this->drops.fetch_add(1);
      break;
    default:
      break;
  }
}

bool Network::online() const {
  return this->associated.load() && this->addressed.load();
}

String Network::savedNetwork() {
  // The connected SSID (WiFi.SSID()) is empty until a join succeeds, and
  // the stored one is 32 bytes that are not always NUL-terminated.
  wifi_config_t conf;
  memset(&conf, 0, sizeof(conf));
  if (esp_wifi_get_config(WIFI_IF_STA, &conf) != ESP_OK) {
    return "";
  }
  char ssid[33];
  memcpy(ssid, conf.sta.ssid, 32);
  ssid[32] = '\0';
  return String(ssid);
}

void Network::preferStrongest() {
  // arduino-esp32 2.x connects to the strongest AP of a name and accepts
  // any signal. This core's begin(ssid, pass) asks for a full scan but
  // leaves the signal floor at 0, which drops every real AP, and a
  // no-argument begin() keeps whatever was stored. Written once, when
  // the stored choice differs, because the write goes to NVS.
  wifi_config_t conf;
  if (esp_wifi_get_config(WIFI_IF_STA, &conf) != ESP_OK) {
    return;
  }
  if (conf.sta.scan_method == WIFI_ALL_CHANNEL_SCAN &&
      conf.sta.sort_method == WIFI_CONNECT_AP_BY_SIGNAL &&
      conf.sta.threshold.rssi == -127) {
    return;
  }
  conf.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
  conf.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
  conf.sta.threshold.rssi = -127;
  esp_wifi_set_config(WIFI_IF_STA, &conf);
}

void Network::startTry() {
  const String ssid = this->savedNetwork();
  if (ssid.length() == 0) {
    return;
  }
  this->preferStrongest();
  this->tries++;
  this->tryOpen = true;
  this->awaitingRetry = false;
  this->tryStartedMs = millis();
  this->dropsAtTry = this->drops.load();
  WiFi.begin();
}

void Network::noteOffline(uint32_t nowMs) {
  if (this->outageBeganMs == 0) {
    this->outageBeganMs = nowMs;
    this->lastReportMs = nowMs;
    return;
  }
  if (nowMs - this->lastReportMs < WIFI_REPORT_MS) {
    return;
  }
  this->lastReportMs = nowMs;
  const String ssid = this->savedNetwork();
  const uint32_t secs = (nowMs - this->outageBeganMs) / 1000;
  String line = "still joining ";
  line += ssid.length() > 0 ? ssid : String("(none)");
  line += ", ";
  line += String(this->tries);
  line += " tries in ";
  line += String(secs);
  line += " s, last failure ";
  line += reasonText(this->lastReason.load());
  this->logger->log(line);
}

void Network::keepJoining() {
  const uint32_t nowMs = millis();
  const bool now = this->online();

  if (now && !this->wasOnline) {
    const String ip = WiFi.localIP().toString();
    if (ip == "0.0.0.0") {
      // GOT_IP was recorded before the adapter published the address.
      return;
    }
    this->joinedSsid = WiFi.SSID();
    String line = "joined " + this->joinedSsid + " at " + ip + " (channel " +
                  String(static_cast<int>(WiFi.channel())) + ", " +
                  String(static_cast<int>(WiFi.RSSI())) + " dBm)";
    if (this->tries > 1) {
      const uint32_t secs = this->outageBeganMs == 0
                                ? 0
                                : (nowMs - this->outageBeganMs) / 1000;
      line += " after " + String(this->tries) + " tries in " + String(secs) +
              " s";
    }
    this->logger->log(line);
    if (this->screenAddressSet && ip != this->shownAddress) {
      this->logger->warn(
          "address changed from " + this->shownAddress + " to " + ip +
          "; the screen keeps the first until a restart");
    }
    this->tries = 0;
    this->tryOpen = false;
    this->awaitingRetry = false;
    this->wasOnline = true;
    this->waitingForAddress = false;
    this->dhcpAbandoned = false;
    this->dhcpSinceMs = 0;
    return;
  }

  if (!now && this->wasOnline) {
    const String ssid =
        this->joinedSsid.length() > 0 ? this->joinedSsid : this->savedNetwork();
    this->logger->log("lost " + ssid + " (" +
                      reasonText(this->lastReason.load()) +
                      "), joining it again");
    this->wasOnline = false;
    this->tries = 0;
    this->tryOpen = false;
    this->awaitingRetry = true;
    this->waitingForAddress = false;
    this->dhcpAbandoned = false;
    this->dhcpSinceMs = 0;
    this->outageBeganMs = nowMs;
    this->lastReportMs = nowMs;
  }

  if (this->associated.load()) {
    // DHCP on this link has taken the better part of a minute and then
    // worked. Starting another join here would throw that away. A join
    // that never gets an address is dropped once, and the disconnect
    // event is what opens the next try.
    if (this->dhcpSinceMs == 0) {
      this->dhcpSinceMs = nowMs;
    }
    if (!this->waitingForAddress) {
      this->waitingForAddress = true;
      this->logger->log("associated, waiting for an address");
    }
    if (!this->dhcpAbandoned &&
        nowMs - this->dhcpSinceMs >= WIFI_DHCP_MS) {
      this->dhcpAbandoned = true;
      this->logger->warn("no address after " + String(WIFI_DHCP_MS / 1000) +
                         " s, joining again");
      WiFi.disconnect();
    }
    return;
  }
  this->dhcpSinceMs = 0;
  this->waitingForAddress = false;
  this->dhcpAbandoned = false;

  if (this->tryOpen && this->drops.load() != this->dropsAtTry) {
    this->tryOpen = false;
    this->awaitingRetry = true;
  }
  if (this->tryOpen) {
    if (nowMs - this->tryStartedMs < WIFI_TRY_MS) {
      this->noteOffline(nowMs);
      return;
    }
    // No disconnect arrived. The stack is stuck in this try.
    WiFi.disconnect();
    this->startTry();
    return;
  }

  const uint32_t retryWait = WiFi.softAPgetStationNum() > 0
                                 ? WIFI_RETRY_WHILE_SETUP_MS
                                 : WIFI_RETRY_MS;
  if (this->awaitingRetry && nowMs - this->lastDropMs.load() < retryWait) {
    this->noteOffline(nowMs);
    return;
  }
  this->awaitingRetry = false;

  // The portal's scan shares the radio. scanComplete() gives up after
  // its own 10 s, so this wait cannot last forever.
  if (WiFi.scanComplete() == WIFI_SCAN_RUNNING) {
    this->noteOffline(nowMs);
    return;
  }
  if (this->savedNetwork().length() == 0) {
    this->noteOffline(nowMs);
    return;
  }
  if (this->outageBeganMs == 0) {
    this->outageBeganMs = nowMs;
    this->lastReportMs = nowMs;
  }
  this->startTry();
}

void Network::supervisorTask(void* arg) {
  Network* network = static_cast<Network*>(arg);
  for (;;) {
    network->keepJoining();
    vTaskDelay(pdMS_TO_TICKS(250));
  }
}

void Network::softAPCallbackStatic(AsyncWiFiManager* manager) {
  if (instance) {
    instance->softAPCallback(manager);
  }
}

void Network::softAPCallback(AsyncWiFiManager* manager) {
  this->display->render(Screen::WIFI_SETUP);
  this->logger->log(String("setup portal \"") + manager->getConfigPortalSSID() +
                    "\" is open");
}

void Network::clearWiFiCredentials() {
  // load the flash-saved configs
  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  esp_wifi_init(&cfg);  // initiate and allocate wifi resources
  delay(2000);          // wait a bit

  // clear credentials if button is pressed
  if (esp_wifi_restore() != ESP_OK) {
    this->logger->log("WiFi is not initialized by esp_wifi_init ");
  } else {
    this->logger->log("WiFi Configurations Cleared!");
  }
  this->display->render(Screen::WIFI_RESET);
  delay(1500);
  esp_restart();
}

namespace {

// What the panel is, in the order it should be copied. The page, the
// script and the stylesheet go first: they are the first paint. The tape
// font is last. It is half the bytes, and the page does not ask for it
// until it is already up. Copying the font first took the one contiguous
// block, and the script and the stylesheet were the files left on flash.
// The modules are folded into script.js when the image is built, so they
// are not separate files here.
const char* PANEL_PATHS[] = {
    "/index.html", "/script.js", "/style.css", "/manifest.json",
    "/icon.png",   "/favicon.ico", "/fontwhite.ttf",
};
const int PANEL_PATH_COUNT = sizeof(PANEL_PATHS) / sizeof(PANEL_PATHS[0]);

// Left free for the radio and the server, which allocate after this copy.
// Taking the last block is how a later connection fails.
const size_t PANEL_HEAP_RESERVE = 48 * 1024;

struct CachedFile {
  char path[32];
  const char* type;
  char etag[16];
  uint8_t* bytes;
  size_t length;
  bool gzip;
};

CachedFile panelFiles[PANEL_PATH_COUNT];
int panelCount = 0;

bool endsWith(const char* text, const char* suffix) {
  const size_t textLength = strlen(text);
  const size_t suffixLength = strlen(suffix);
  return textLength >= suffixLength &&
         strcmp(text + textLength - suffixLength, suffix) == 0;
}

const char* panelType(const char* path) {
  if (endsWith(path, ".html")) {
    return "text/html";
  }
  if (endsWith(path, ".css")) {
    return "text/css";
  }
  if (endsWith(path, ".js")) {
    return "application/javascript";
  }
  if (endsWith(path, ".json")) {
    return "application/json";
  }
  if (endsWith(path, ".png")) {
    return "image/png";
  }
  if (endsWith(path, ".ico")) {
    return "image/x-icon";
  }
  if (endsWith(path, ".svg")) {
    return "image/svg+xml";
  }
  if (endsWith(path, ".ttf")) {
    return "font/ttf";
  }
  return "application/octet-stream";
}

const char* panelCacheControl(const CachedFile* file) {
  // The document is what names the other files, with a hash that changes
  // when they do. It has to be asked every time, or a new image would
  // never be seen. The others can stay: their address changes with them.
  if (strcmp(file->path, "/index.html") == 0) {
    return "no-cache";
  }
  return "public, max-age=31536000, immutable";
}

const CachedFile* findPanel(const String& url) {
  const char* path = url.c_str();
  if (url == "/") {
    path = "/index.html";
  }
  for (int i = 0; i < panelCount; i++) {
    if (strcmp(panelFiles[i].path, path) == 0) {
      return &panelFiles[i];
    }
  }
  return NULL;
}

File openPanelFile(const char* urlPath, bool* gzip) {
  char gzPath[40];
  snprintf(gzPath, sizeof(gzPath), "%s.gz", urlPath);
  File zipped = SPIFFS.open(gzPath, "r");
  if (zipped) {
    *gzip = true;
    return zipped;
  }
  *gzip = false;
  return SPIFFS.open(urlPath, "r");
}

struct Candidate {
  char path[32];
  size_t length;
  bool gzip;
};

bool measurePanel(const char* urlPath, Candidate* out) {
  bool gzip = false;
  File file = openPanelFile(urlPath, &gzip);
  if (!file || file.isDirectory()) {
    if (file) {
      file.close();
    }
    return false;
  }
  const size_t length = file.size();
  file.close();
  if (length == 0) {
    return false;
  }
  snprintf(out->path, sizeof(out->path), "%s", urlPath);
  out->length = length;
  out->gzip = gzip;
  return true;
}

bool readPanel(Logger* logger, const Candidate* candidate) {
  if (panelCount >= PANEL_PATH_COUNT) {
    logger->warn(String("panel left ") + candidate->path + " on flash");
    return false;
  }
  const size_t room = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
  if (room < candidate->length + PANEL_HEAP_RESERVE) {
    logger->warn(String("panel left ") + candidate->path + " on flash");
    return false;
  }
  char stored[40];
  if (candidate->gzip) {
    snprintf(stored, sizeof(stored), "%s.gz", candidate->path);
  } else {
    snprintf(stored, sizeof(stored), "%s", candidate->path);
  }
  File file = SPIFFS.open(stored, "r");
  if (!file) {
    logger->warn(String("panel could not read ") + candidate->path);
    return false;
  }
  uint8_t* bytes = static_cast<uint8_t*>(
      heap_caps_malloc(candidate->length, MALLOC_CAP_8BIT));
  if (bytes == NULL) {
    file.close();
    logger->warn(String("panel left ") + candidate->path + " on flash");
    return false;
  }
  const size_t got = file.read(bytes, candidate->length);
  file.close();
  if (got != candidate->length) {
    heap_caps_free(bytes);
    logger->warn(String("panel could not read ") + candidate->path);
    return false;
  }

  CachedFile* slot = &panelFiles[panelCount];
  snprintf(slot->path, sizeof(slot->path), "%s", candidate->path);
  slot->type = panelType(candidate->path);
  slot->bytes = bytes;
  slot->length = candidate->length;
  slot->gzip = candidate->gzip;
  uint32_t hash = 2166136261u;
  for (size_t i = 0; i < candidate->length; i++) {
    hash ^= bytes[i];
    hash *= 16777619u;
  }
  snprintf(slot->etag, sizeof(slot->etag), "\"%08lx\"",
           static_cast<unsigned long>(hash));
  panelCount++;
  return true;
}

// Serving a file used to read it from flash on the task that also sends
// the packets, a window at a time. Flash and the radio share a bus, so
// each of those reads held the send, and a page moved at a few kilobytes
// a second. This copies the panel once, at boot, and the send then copies
// memory. A file that does not fit is left on flash and served from there.
bool cachePanel(Logger* logger) {
  Candidate pending[PANEL_PATH_COUNT];
  int count = 0;
  for (int i = 0; i < PANEL_PATH_COUNT; i++) {
    if (measurePanel(PANEL_PATHS[i], &pending[count])) {
      count++;
    }
  }
  size_t total = 0;
  for (int i = 0; i < count; i++) {
    if (readPanel(logger, &pending[i])) {
      total += pending[i].length;
    }
  }
  logger->log(String("panel in ram: ") + total + " bytes, " + panelCount +
              " files, heap " + ESP.getFreeHeap());
  return panelCount > 0;
}

class PanelHandler : public AsyncWebHandler {
 public:
  bool canHandle(AsyncWebServerRequest* request) override {
    if (request->method() != HTTP_GET) {
      return false;
    }
    if (findPanel(request->url()) == NULL) {
      return false;
    }
    request->addInterestingHeader("If-None-Match");
    return true;
  }

  void handleRequest(AsyncWebServerRequest* request) override {
    const CachedFile* file = findPanel(request->url());
    if (file == NULL) {
      request->send(404, "text/plain", "Not found");
      return;
    }
    const char* policy = panelCacheControl(file);
    if (request->hasHeader("If-None-Match")) {
      String match = request->header("If-None-Match");
      match.trim();
      if (match == file->etag) {
        AsyncWebServerResponse* response = request->beginResponse(304);
        response->addHeader("ETag", file->etag);
        response->addHeader("Cache-Control", policy);
        request->send(response);
        return;
      }
    }

    const uint8_t* bytes = file->bytes;
    const size_t length = file->length;
    AsyncWebServerResponse* response = request->beginResponse(
        file->type, length,
        [bytes, length](uint8_t* buffer, size_t maxLen,
                        size_t index) -> size_t {
          if (index >= length) {
            return 0;
          }
          size_t n = length - index;
          if (n > maxLen) {
            n = maxLen;
          }
          memcpy(buffer, bytes + index, n);
          return n;
        });
    if (file->gzip) {
      response->addHeader("Content-Encoding", "gzip");
    }
    response->addHeader("Cache-Control", policy);
    response->addHeader("ETag", file->etag);
    request->send(response);
  }
};

}  // namespace

void Network::initialize() {
  // devkit build 2026-09: the PCB pulls this pin high externally. On a bare
  // ESP32 devkit an unwired GPIO13 floats low. The internal pull-up makes
  // the button a plain short-to-GND, but the pin is still low in the moment
  // after the pull-up is enabled, and a read in that moment wipes a saved
  // network on a boot where nobody is holding the button. The pull-up is
  // given time to rise, and the low has to still be there after that, which
  // a finger on the button is and a floating pin is not.
  pinMode(resetPin, INPUT_PULLUP);
  delay(50);
  bool held = digitalRead(this->resetPin) == LOW;
  if (held) {
    delay(100);
    held = digitalRead(this->resetPin) == LOW;
  }
  if (held) {
    this->logger->log("Wi-Fi button held, clearing the saved network");
    this->clearWiFiCredentials();
  }

  // The core's own retry skips the reasons a weak link actually fails
  // with, so it is off and keepJoining() is the only thing that starts
  // a try. Modem sleep is off for the same link: it costs airtime the
  // station does not have.
  WiFi.onEvent(Network::onWiFiEventStatic);
  WiFi.setAutoReconnect(false);
  WiFi.mode(WIFI_STA);
  wakeRadio();

  const String saved = this->savedNetwork();
  this->outageBeganMs = millis();
  this->lastReportMs = this->outageBeganMs;
  if (saved.length() > 0) {
    this->display->render(Screen::WIFI_JOINING);
    this->logger->log("joining " + saved);
  }

  // Lives until this function returns. After the join its portal
  // handlers are cleared off the server, and the manager itself holds
  // no wifi state worth keeping.
  AsyncWiFiManager wifiManager(this->server, this->dns);
  wifiManager.setAPCallback(&Network::softAPCallbackStatic);
  wifiManager.setDebugOutput(DEBUG_WIFI);

  bool portal = false;
  const uint32_t bootMs = millis();
  while (!this->online()) {
    const bool noSaved = this->savedNetwork().length() == 0;
    if (!portal && (noSaved || millis() - bootMs >= WIFI_SETUP_AFTER_MS)) {
      portal = true;
      this->logger->log(noSaved ? "no saved network, opening setup"
                                : "still offline, opening setup");
      // This blocks for up to 10 s inside one connect attempt, then
      // serves the portal. The join beside it carries on from the loop
      // below. connect stays false, so the portal does not start its
      // own tries over ours except when someone saves a network.
      wifiManager.startConfigPortalModeless("E-TKT", NULL);
      wakeRadio();
    }
    if (portal) {
      wifiManager.loop();
    }
    this->keepJoining();
    delay(10);
  }

  if (portal) {
    // Back to the station alone. A phone on E-TKT was slowing the
    // retries, and the portal's handlers are not the panel.
    WiFi.mode(WIFI_STA);
    wakeRadio();
    this->dns->stop();
    this->server->reset();
    if (!this->online()) {
      this->logger->log("setup closed, joining again");
      while (!this->online()) {
        this->keepJoining();
        delay(10);
      }
    }
  }

  if (!MDNS.begin("e-tkt")) {
    // Not fatal: the device is still reachable at its IP, just not by name.
    this->logger->warn("Error starting mDNS");
  } else {
    // Advertise the webserver over mdns-sd, and add some custom props
    // to identify it as an e-tkt in case future integrations want to
    // find it.
    MDNS.addService("http", "tcp", 80);
    MDNS.addServiceTxt("http", "tcp", "e-tkt", "true");
  }

  // The idle screen keeps this address until a restart. A later one is
  // logged and left off the glass: the screen is not drawn from the
  // supervisor task.
  const String ip = WiFi.localIP().toString();
  display->setConnectionInfo(ip, WiFi.SSID());
  this->shownAddress = ip;
  this->screenAddressSet = true;

  // Every request under /api/ goes to the Api. It comes ahead of the files,
  // so that none of them is taken for a file, and it does not wait on them
  // mounting: without the files there is no panel, but the device's status,
  // its log and a stop still answer. Before this, a filesystem that failed to
  // mount returned ahead of every route, and of starting the server at all.
  this->server->addHandler(new ApiHandler(this->api));

  // The files are copied into RAM and served from there. What is left on
  // flash, because it did not fit, is still served below. With the page
  // itself in RAM, a leftover is an asset whose address changes with its
  // contents, so the browser can keep it instead of fetching it again.
  if (SPIFFS.begin()) {
    if (cachePanel(this->logger)) {
      this->server->addHandler(new PanelHandler());
    }
    AsyncStaticWebHandler& files =
        this->server->serveStatic("/", SPIFFS, "/").setDefaultFile("index.html");
    if (findPanel("/") != NULL) {
      files.setCacheControl("public, max-age=31536000, immutable");
    }
  } else {
    this->logger->error(
        "An Error has occurred while mounting SPIFFS. There is no panel to "
        "serve, but the api still answers.");
  }

  // Everything else, including every file when there are none.
  this->server->onNotFound([](AsyncWebServerRequest* request) {
    request->send(404, "text/plain", "Not found");
  });

  if (ENABLE_OTA) {
    // Endpoint to accept OTA updates to hardware and firmware.
    AsyncElegantOTA.begin(server);
  }

  // Already listening when the portal was up. begin() then keeps the
  // socket and serves the handlers just registered.
  this->server->begin();

  // Core 0, under the Wi-Fi tasks. The log lines allocate, so the stack
  // is a step past the 4 KB the event task gets by.
  xTaskCreatePinnedToCore(Network::supervisorTask, "wifi-join", 6144, this, 1,
                          NULL, 0);
}
