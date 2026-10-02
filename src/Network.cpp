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
#include "PanelFiles.h"

// The panel, as scripts/embed_panel.py made it from data/ for this build.
#include "EmbeddedPanel.h"

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
  if (this->radio->forgetInherited()) {
    this->logger->log("Wiped the radio's own storage");
  } else {
    this->logger->warn("Could not wipe the radio's own storage");
  }
  this->display->render(Screen::WIFI_RESET);
  delay(1500);
  esp_restart();
}

namespace {

// The webserver's side of the panel. Which file an address is, whether the
// browser's own copy is still the one and how long it may keep what it gets
// are PanelFiles.h's to say, and are tested there, so this has no rules of
// its own. It sends the bytes from where the firmware holds them, which is
// the mapped flash: a send takes no memory but the window it goes through.
class PanelHandler : public AsyncWebHandler {
 public:
  bool canHandle(AsyncWebServerRequest* request) override {
    if (request->method() != HTTP_GET ||
        panelFile(EMBEDDED_PANEL, request->url().c_str()) == NULL) {
      return false;
    }
    request->addInterestingHeader("If-None-Match");
    return true;
  }

  void handleRequest(AsyncWebServerRequest* request) override {
    const String version =
        request->hasParam("v") ? request->getParam("v")->value() : String();
    const PanelReply reply =
        panelReply(EMBEDDED_PANEL, request->url().c_str(), version.c_str(),
                   request->header("If-None-Match").c_str());
    if (reply.file == NULL) {
      request->send(404, "text/plain", "Not found");
      return;
    }

    AsyncWebServerResponse* response;
    if (reply.unchanged) {
      response = request->beginResponse(304);
    } else {
      response = request->beginResponse_P(
          200, reply.file->type, reply.file->bytes, reply.file->length);
      if (reply.file->gzip) {
        response->addHeader("Content-Encoding", "gzip");
      }
    }
    response->addHeader("Cache-Control", reply.cacheControl);
    response->addHeader("ETag", reply.file->etag);
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

  // Every request under /api/ goes to the Api. It comes ahead of the panel,
  // so that none of them is taken for a file.
  this->server->addHandler(new ApiHandler(this->api));

  // The panel is part of the firmware, so there is nothing to mount and
  // nothing to copy. It was copied into RAM at each start before, out of a
  // SPIFFS partition, and the copy left a connection little to be made of.
  this->server->addHandler(new PanelHandler());
  this->logger->log(String("panel ") + EMBEDDED_PANEL.version + ": " +
                    EMBEDDED_PANEL.count + " files, " +
                    panelBytes(EMBEDDED_PANEL) + " bytes");

  // Everything else.
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
