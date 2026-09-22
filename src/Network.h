#pragma once

#include <Arduino.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ESPAsyncWiFiManager.h>
#include <ESPmDNS.h>
#include <WiFi.h>

#include "ArduinoJson.h"
#include "AsyncJson.h"
#include "Configuration.h"
#include "Display.h"
#include "ETKT.h"
#include "Logger.h"
#include "SPIFFS.h"
#include "esp_wifi.h"

/**
 * @brief Manages connection to the network. 
 * 
 * @details Handles:
 *  - Setting up WiFi credentials, including creating a soft AP for
 *    configuration
 *  - Serving the web interface and API endpoints
 *  - Advertising the device over MDNS.
 */
class Network {
 private:
  // A hacky static workaround to make the softAPCallbackStatic method work.
  // Needed because of how the ESPAsyncWiFiManager library works.
  static Network *instance;

  Logger *logger;
  AsyncWebServer *server;
  DNSServer *dns;
  Display *display;
  ETKT *etkt;

  // WiFi reset button pin
  uint8_t resetPin;

  /**
   * @brief A callback for when the captive portal is started.
   */
  void softAPCallback(AsyncWiFiManager *wifi);
  static void softAPCallbackStatic(AsyncWiFiManager *myAsyncWiFiManager);

  /*
   * @brief Clears stored wifi credentials in response to a button press.
   */
  void clearWiFiCredentials();

  /**
   * Handles a POST to /api/<name> for any command in ETKT::COMMANDS. spec is
   * the row that route was registered from, and says which body fields to
   * read; it points into static storage and outlives the request.
   */
  void commandPostHandler(const CommandSpec *spec,
                          AsyncWebServerRequest *request, JsonVariant &json);
  void statusGetHandler(AsyncWebServerRequest *request);
  void charactersGetHandler(AsyncWebServerRequest *request);
  void notFoundHandler(AsyncWebServerRequest *request);

 public:
  Network(Logger *logger, Display *display, ETKT *etkt, uint8_t resetPin);
  ~Network();

  /**
   * Connects to the network, starts the captive portal if necessary, and starst
   * the webapp's server.
   */
  void initialize();
};
