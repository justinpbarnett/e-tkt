#include "Network.h"

#include <Arduino.h>
#include <AsyncElegantOTA.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ESPAsyncWiFiManager.h>
#include <ESPmDNS.h>
#include <WiFi.h>

#include <algorithm>

#include "Api.h"
#include "Configuration.h"
#include "Display.h"
#include "Logger.h"
#include "SPIFFS.h"
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
    request->send(response);
  }
};

// --- Network ---

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

void Network::softAPCallbackStatic(AsyncWiFiManager* manager) {
  if (instance) {
    instance->softAPCallback(manager);
  }
}

void Network::softAPCallback(AsyncWiFiManager* manager) {
  // captive portal to configure SSID and password
  this->display->render(Screen::WIFI_SETUP);
  this->logger->log(String("SoftAP SSID: ") + manager->getConfigPortalSSID());
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

void Network::initialize() {
  // devkit build 2026-09: the PCB pulls this pin high externally. On a bare
  // ESP32 devkit an unwired GPIO13 floats low, which reads as "reset button
  // held", so every boot wipes the WiFi credentials and calls esp_restart().
  // The internal pull-up makes the button a plain short-to-GND.
  pinMode(resetPin, INPUT_PULLUP);

  // local intialization. once its business is done, there is no need to keep it
  // around
  AsyncWiFiManager wifiManager(this->server, this->dns);

  // reset wifi settings if button is pressed
  if (!digitalRead(this->resetPin)) {
    this->clearWiFiCredentials();
  }
  // Set this->softAPCallback as wifiManager's APCallback.
  // This function will be called once the WiFiManager enters AP mode.
  wifiManager.setAPCallback(&Network::softAPCallbackStatic);
  wifiManager.setDebugOutput(DEBUG_WIFI);

  // fetches ssid and pass and tries to connect
  // if it does not connect it starts an access point with the specified name
  // here  "AutoConnectAP"
  // and goes into a blocking loop awaiting configuration
  if (!wifiManager.autoConnect("E-TKT")) {
    this->logger->error("failed to connect and hit timeout");
    // reset and try again, or maybe put it to deep sleep
    ESP.restart();
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

  // if you get here you have connected to the WiFi
  display->setConnectionInfo(WiFi.localIP().toString(), WiFi.SSID());

  // Every request under /api/ goes to the Api. It comes ahead of the files,
  // so that none of them is taken for a file, and it does not wait on them
  // mounting: without the files there is no panel, but the device's status,
  // its log and a stop still answer. Before this, a filesystem that failed to
  // mount returned ahead of every route, and of starting the server at all.
  this->server->addHandler(new ApiHandler(this->api));

  // Serve static assets from the SPIFFS root directory.
  if (SPIFFS.begin()) {
    this->server->serveStatic("/", SPIFFS, "/").setDefaultFile("index.html");
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

  // Start server
  this->server->begin();
}
