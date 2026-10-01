#pragma once

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <ESPAsyncWiFiManager.h>
#include <WiFi.h>

#include <atomic>

#include "Api.h"
#include "Display.h"
#include "Logger.h"

/**
 * @brief Joins the saved network and serves the panel.
 *
 * A weak link here fails most tries in a handshake timeout, and a try
 * that works can take from half a minute to several minutes. The core
 * only retries a few disconnect reasons, and the old setup gave the
 * saved network one ten-second try and then stayed in the portal, whose
 * scan took the station down with it. This keeps starting the join
 * itself until the station has an address, and it leaves modem sleep
 * off: sleep stretched a round trip on this link from about 80 ms to
 * about 250 ms.
 *
 * The setup portal opens beside that join when no network is saved, or
 * when a minute has passed and the saved one still has not answered. A
 * phone on the portal slows the retries, so its pages can load. After
 * the join the portal is closed. It stays closed until a restart, which
 * is how a network is changed.
 *
 * The server hands every request under /api/ to the Api, and the device
 * is advertised over mDNS.
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

  // Written by the Wi-Fi event task, read by the join step. That task
  // has a 4 KB stack, so the handler records the fact and returns. The
  // join step is what logs.
  std::atomic<bool> associated{false};
  std::atomic<bool> addressed{false};
  std::atomic<uint8_t> lastReason{0};
  std::atomic<uint32_t> lastDropMs{0};
  std::atomic<uint32_t> drops{0};

  // The join step only. setup() runs it until the first address, then
  // the supervisor task runs it. Never both.
  bool tryOpen = false;
  bool awaitingRetry = false;
  bool wasOnline = false;
  bool waitingForAddress = false;
  bool dhcpAbandoned = false;
  bool screenAddressSet = false;
  uint32_t tryStartedMs = 0;
  uint32_t dropsAtTry = 0;
  uint32_t tries = 0;
  uint32_t outageBeganMs = 0;
  uint32_t lastReportMs = 0;
  uint32_t dhcpSinceMs = 0;
  String joinedSsid;
  String shownAddress;

  static void onWiFiEventStatic(system_event_id_t event,
                                system_event_info_t info);
  void onWiFiEvent(system_event_id_t event, system_event_info_t info);
  bool online() const;
  String savedNetwork();
  void preferStrongest();
  void startTry();
  void keepJoining();
  void noteOffline(uint32_t nowMs);
  static void supervisorTask(void* arg);

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
   * Joins the saved network, opens the setup portal if that join does
   * not happen, and starts the web server once the station has an
   * address. Returns only once joined. A supervisor task keeps the
   * join up afterwards.
   */
  void initialize();
};
