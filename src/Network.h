#pragma once

#include <Arduino.h>
#include <ESPAsyncWebServer.h>

#include "Api.h"
#include "Display.h"
#include "Esp32Radio.h"
#include "LinkSupervisor.h"
#include "Logger.h"
#include "NetworkSettings.h"

/**
 * @brief The board's side of being reached: the webserver, the name the
 * machine answers to, and the task the link is kept from.
 *
 * It decides nothing about the link. Which network is tried, when the
 * machine opens its own and what the screen says about it are the
 * LinkSupervisor's, which is tested off the board. This starts the radio
 * that supervisor works, and steps it.
 *
 * Nothing here waits for a network. The firmware before this one did not
 * return from initialize() until it had an address, so a machine out of
 * reach of its network served nothing and took no press of its button. Now
 * the server listens from the start, on whatever the machine comes to be
 * reached over: a network it joins, or its own.
 *
 * The server hands every request under /api/ to the Api and serves the panel
 * from RAM. The machine is advertised over mDNS under a name of its own, so
 * that two of them on one network do not answer to the same one.
 */
class Network {
 private:
  Logger* logger;
  AsyncWebServer* server;
  Display* display;
  Api* api;
  Esp32Radio* radio;
  NetworkSettings* settings;
  LinkSupervisor* link;

  static void supervisorTask(void* arg);

 public:
  Network(Logger* logger, Display* display, Api* api, Esp32Radio* radio,
          NetworkSettings* settings, LinkSupervisor* link);
  ~Network();

  /**
   * @brief Makes the machine forget every network it remembers, and the one
   * the firmware before this one left in the radio, says so on the screen,
   * and restarts. For the button held through the start. It does not return,
   * and it comes before initialize().
   */
  void forgetNetworks();

  /**
   * @brief Starts the radio, the server and the link, and returns. The link
   * is kept from a task of its own from then on.
   */
  void initialize();
};
