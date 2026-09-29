#pragma once

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <ESPAsyncWiFiManager.h>

#include "Api.h"
#include "Display.h"
#include "Logger.h"

/**
 * @brief Manages connection to the network.
 *
 * @details Handles:
 *  - Setting up WiFi credentials, including creating a soft AP for
 *    configuration
 *  - Serving the web interface, and handing every request under /api/ to the
 *    Api, which answers it
 *  - Advertising the device over MDNS.
 */
class Network {
 private:
  // A hacky static workaround to make the softAPCallbackStatic method work.
  // Needed because of how the ESPAsyncWiFiManager library works.
  static Network* instance;

  Logger* logger;
  AsyncWebServer* server;
  DNSServer* dns;
  Display* display;
  Api* api;

  // WiFi reset button pin
  uint8_t resetPin;

  /**
   * @brief A callback for when the captive portal is started.
   */
  void softAPCallback(AsyncWiFiManager* wifi);
  static void softAPCallbackStatic(AsyncWiFiManager* myAsyncWiFiManager);

  /*
   * @brief Clears stored wifi credentials in response to a button press.
   */
  void clearWiFiCredentials();

 public:
  Network(Logger* logger, Display* display, Api* api, uint8_t resetPin);
  ~Network();

  /**
   * Connects to the network, starts the captive portal if necessary, and starts
   * the webapp's server.
   */
  void initialize();
};
