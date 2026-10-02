#include "Network.h"

#include <Arduino.h>
#include <AsyncElegantOTA.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ESPmDNS.h>

#include <algorithm>
#include <cstring>

#include "Api.h"
#include "Configuration.h"
#include "Display.h"
#include "Esp32Radio.h"
#include "LinkSupervisor.h"
#include "Logger.h"
#include "NetworkSettings.h"
#include "SPIFFS.h"
#include "esp_heap_caps.h"
#include "esp_wifi.h"

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

Network::Network(Logger* logger, Display* display, Api* api, Esp32Radio* radio,
                 NetworkSettings* settings, LinkSupervisor* link) {
  this->logger = logger;
  this->display = display;
  this->api = api;
  this->radio = radio;
  this->settings = settings;
  this->link = link;
  this->server = new AsyncWebServer(80);
}

Network::~Network() { delete server; }

void Network::supervisorTask(void* arg) {
  Network* network = static_cast<Network*>(arg);
  for (;;) {
    network->link->step();
    vTaskDelay(pdMS_TO_TICKS(WIFI_STEP_MS));
  }
}

void Network::forgetNetworks() {
  this->logger->log("Button held through the start: forgetting every network");
  this->settings->reset();

  // The firmware before this one kept its network in the radio's own
  // storage, and this one leaves it there. That goes as well: a network
  // forgotten here is not to come back with a firmware put back on.
  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  esp_wifi_init(&cfg);  // initiate and allocate wifi resources
  delay(2000);          // wait a bit

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
    "/index.html", "/script.js",   "/style.css",     "/manifest.json",
    "/icon.png",   "/favicon.ico", "/fontwhite.ttf",
};
const int PANEL_PATH_COUNT = sizeof(PANEL_PATHS) / sizeof(PANEL_PATHS[0]);

// Left free for what allocates after this copy: the radio as it joins, the
// machine's own network when that opens, the server and its connections.
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
    AsyncWebServerResponse* response =
        request->beginResponse(file->type, length,
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
  // What the machine remembers, under the name its chip gives it. Read
  // before anything else: the link goes by it from its first step.
  this->settings->initialize(Esp32Radio::machineId());

  // The radio, and with it the stack everything below stands on. It joins
  // nothing yet.
  this->radio->initialize();

  const String host = this->settings->hostName();
  if (!MDNS.begin(host.c_str())) {
    // Not fatal: the device is still reachable at its IP, just not by name.
    this->logger->warn("Error starting mDNS");
  } else {
    // Advertise the webserver over mdns-sd, and add some custom props
    // to identify it as an e-tkt in case future integrations want to
    // find it.
    MDNS.addService("http", "tcp", 80);
    MDNS.addServiceTxt("http", "tcp", "e-tkt", "true");
    this->logger->log(String("answers to ") + host + ".local");
  }

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
    AsyncStaticWebHandler& files = this->server->serveStatic("/", SPIFFS, "/")
                                       .setDefaultFile("index.html");
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

  // Listening before the machine is on any network. It is reached once it
  // has joined one or has opened its own, and the server is there by then.
  this->server->begin();

  // The first step says on the screen what the machine is about to do, so
  // the idle screen that comes after this has it. Every step from here on
  // is the task's.
  this->link->step();

  // Core 0, under the Wi-Fi tasks. A step logs, writes what the machine
  // remembers to flash and listens for a quiet channel, so the stack is the
  // size of the one setup() runs on, twice the 4 KB the event task gets by
  // on.
  xTaskCreatePinnedToCore(Network::supervisorTask, "link", 8192, this, 1, NULL,
                          0);
}
