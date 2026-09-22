#include "Network.h"

#include <Arduino.h>
#include <AsyncElegantOTA.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ESPAsyncWiFiManager.h>
#include <ESPmDNS.h>
#include <WiFi.h>

#include "ArduinoJson.h"
#include "AsyncJson.h"
#include "CharacterSet.h"
#include "Configuration.h"
#include "Display.h"
#include "ETKT.h"
#include "Logger.h"
#include "PressGeometry.h"
#include "SPIFFS.h"
#include "esp_heap_caps.h"
#include "esp_wifi.h"

Network *Network::instance = NULL;

Network::Network(Logger *logger, Display *display, ETKT *etkt,
                 uint8_t resetPin) {
  Network::instance = this;
  this->logger = logger;
  this->display = display;
  this->etkt = etkt;
  this->server = new AsyncWebServer(80);
  this->dns = new DNSServer();
  this->resetPin = resetPin;
}

Network::~Network() {
  delete server;
  delete dns;
}

void Network::softAPCallbackStatic(AsyncWiFiManager *manager) {
  if (instance) {
    instance->softAPCallback(manager);
  }
}

void Network::softAPCallback(AsyncWiFiManager *manager) {
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
    this->logger->log("failed to connect and hit timeout");
    // reset and try again, or maybe put it to deep sleep
    ESP.restart();
  }

  if (!MDNS.begin("e-tkt")) {
    this->logger->log("Error starting mDNS");
  } else {
    // Advertise the webserver over mdns-sd, and add some custom props
    // to identify it as an e-tkt in case future integrations want to
    // find it.
    MDNS.addService("http", "tcp", 80);
    MDNS.addServiceTxt("http", "tcp", "e-tkt", "true");
  }

  // if you get here you have connected to the WiFi
  display->setConnectionInfo(WiFi.localIP().toString(), WiFi.SSID());

  // Initialize SPIFFS
  if (!SPIFFS.begin()) {
    this->logger->log("An Error has occurred while mounting SPIFFS");
    return;
  }

  // One route per command, straight off the table in ETKT.cpp. A command
  // added there gets its endpoint here for free, and cannot get one whose
  // name disagrees with the name /api/status reports for it.
  for (size_t i = 0; i < ETKT::COMMAND_COUNT; i++) {
    const CommandSpec *spec = &ETKT::COMMANDS[i];
    if (spec->run == NULL) {
      // Nothing to run means nothing to post to.
      continue;
    }
    this->server->addHandler(new AsyncCallbackJsonWebHandler(
        String("/api/") + spec->name,
        [this, spec](AsyncWebServerRequest *request, JsonVariant &json) {
          this->commandPostHandler(spec, request, json);
        }));
  }

  // Check printing status
  this->server->on(
      "/api/status", HTTP_GET,
      std::bind(&Network::statusGetHandler, this, std::placeholders::_1));

  // What a label is allowed to say. Fetched once at page load so the webapp
  // does not have to keep its own copy of the wheel's character set.
  this->server->on(
      "/api/characters", HTTP_GET,
      std::bind(&Network::charactersGetHandler, this, std::placeholders::_1));

  // Serve static assets from the SPIFFS root directory.
  this->server->serveStatic("/", SPIFFS, "/").setDefaultFile("index.html");

  // Handle 404s
  this->server->onNotFound(
      std::bind(&Network::notFoundHandler, this, std::placeholders::_1));

  if (ENABLE_OTA) {
    // Endpoint to accept OTA updates to hardware and firmware.
    AsyncElegantOTA.begin(server);
  }

  // Start server
  this->server->begin();
}

void Network::notFoundHandler(AsyncWebServerRequest *request) {
  request->send(404, "text/plain", "Not found");
}

// Reads one 1-9 calibration field out of the request body. On success it
// writes the value through and returns true; otherwise it fills the response
// in with a 400 and returns false, so a handler can chain the fields it needs
// and let the first failure stand.
//
// Refusing out-of-range values here rather than clamping them is the point:
// Settings and pressPeakAngle() both clamp as a backstop, but a clamp is
// silent -- the panel would report success while the machine used a different
// number than the one on screen.
static bool readCalibrationField(const JsonObject &request_data,
                                 const char *field, const char *missingMessage,
                                 AsyncJsonResponse *response_data,
                                 int *value) {
  const auto response_root = response_data->getRoot();
  if (!request_data.containsKey(field)) {
    response_root["error"] = missingMessage;
    response_data->setCode(400);
    return false;
  }
  const int parsed = request_data[field].as<int>();
  if (!isValidCalibrationValue(parsed)) {
    response_root["error"] = String("Please provide a ") + field +
                             " value between " + CALIBRATION_VALUE_MIN +
                             " and " + CALIBRATION_VALUE_MAX + ", got " +
                             parsed;
    response_data->setCode(400);
    return false;
  }
  *value = parsed;
  return true;
}

// Reads the body fields this command declares it needs. Returns false with
// the response already filled in as a 400 if one is missing or out of range,
// so the caller can stop at the first failure.
static bool readCommandOptions(const CommandSpec *spec,
                               const JsonObject &request_data,
                               AsyncJsonResponse *response_data,
                               CommandOptions *options) {
  if (spec->usesAlign &&
      !readCalibrationField(request_data, "align",
                            "Please provide an align value", response_data,
                            &options->align)) {
    return false;
  }
  if (spec->usesForce &&
      !readCalibrationField(request_data, "force",
                            "Please provide a force value", response_data,
                            &options->force)) {
    return false;
  }
  if (spec->labelField != NULL) {
    if (!request_data.containsKey(spec->labelField)) {
      response_data->getRoot()["error"] =
          String("Please provide a ") + spec->labelField + " value";
      response_data->setCode(400);
      return false;
    }
    options->label = request_data[spec->labelField].as<String>();
  }
  return true;
}

// Every command endpoint. There used to be nine of these, alike down to the
// catch block, and the differences that mattered -- which fields the body
// must carry -- were buried in the sameness. The table in ETKT.cpp holds
// those differences now and this reads them.
void Network::commandPostHandler(const CommandSpec *spec,
                                 AsyncWebServerRequest *request,
                                 JsonVariant &json) {
  const auto request_data = json.as<JsonObject>();
  auto response_data = new AsyncJsonResponse();
  const auto response_root = response_data->getRoot();

  CommandOptions options;
  options.command = spec->command;

  if (readCommandOptions(spec, request_data, response_data, &options)) {
    try {
      this->etkt->submit(options);
      response_root["result"] = "success";
    } catch (const PrinterBusyException &e) {
      // 409, not 400. The request was fine; the machine was not. A caller
      // that gets a 400 has something to fix in what it sent, and retrying
      // the same body would be pointless -- here it is the only sensible
      // thing to do.
      response_root["error"] = e.what();
      response_data->setCode(409);
    } catch (const std::exception &e) {
      // Nothing else escapes submit() today. If something does it is the
      // device failing, not the caller.
      response_root["error"] = e.what();
      response_data->setCode(500);
    }
  }

  response_data->setLength();
  request->send(response_data);
}

void Network::statusGetHandler(AsyncWebServerRequest *request) {
  AsyncJsonResponse *response = new AsyncJsonResponse();
  const JsonObject &root = response->getRoot();

  auto status = this->etkt->createStatus();
  root["progress"] = status->progress;
  root["busy"] = status->currentCommand != Command::IDLE;
  root["command"] = status->currentCommandString;
  root["align"] = status->align;
  root["force"] = status->force;

  // Return the current label, if relevant.
  if (status->currentCommand == Command::TAG) {
    root["current_label"] = status->currentLabel;
  }
  delete status;
  root["mem_heap_free_bytes"] = heap_caps_get_free_size(MALLOC_CAP_8BIT);
  root["mem_largest_free_block_bytes"] = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
  root["uptime_ms"] = millis();
  response->setLength();
  request->send(response);
}

// Serves the set of characters a label may contain, and what the two the
// wheel does not carry come out as instead. The webapp validates against
// this rather than a regex of its own: the same list used to be written out
// in a comment, a regex, a second copy of both, and a hint line in
// index.html, and all four disagreed with the wheel and with each other.
void Network::charactersGetHandler(AsyncWebServerRequest *request) {
  AsyncJsonResponse *response = new AsyncJsonResponse();
  const JsonObject &root = response->getRoot();

  root["printable"] = printableCharacters();

  const JsonObject aliases = root.createNestedObject("aliases");
  for (std::map<String, String>::const_iterator it = CHARACTER_ALIASES.begin();
       it != CHARACTER_ALIASES.end(); ++it) {
    aliases[it->first.c_str()] = it->second.c_str();
  }

  response->setLength();
  request->send(response);
}
