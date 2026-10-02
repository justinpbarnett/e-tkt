#pragma once

#include <Arduino.h>

#include <atomic>
#include <vector>

#include "Logger.h"
#include "Radio.h"

/**
 * @brief The chip's own radio, as the link supervisor works it.
 *
 * The first of the two adapters behind Radio. FakeRadio in test/fakes is the
 * second, and it is the one every decision about the link is tested on:
 * nothing here decides anything. This is what the calls of the supervisor
 * come to on this core, and why each of them is the call it is.
 *
 * The radio's own storage is read once, for the network the firmware before
 * this one left in it, and is then left alone. What the machine remembers is
 * NetworkSettings' to keep. So a walk through four networks writes no flash,
 * and a machine put back on the firmware before this one finds its network
 * where it left it.
 */
class Esp32Radio : public Radio {
 private:
  Logger* logger;

  // Written by the Wi-Fi event task, read by the supervisor's step. That task
  // has a 4 KB stack, so the handler records the fact and returns. The step
  // is what logs.
  std::atomic<bool> associated{false};
  std::atomic<bool> addressed{false};
  std::atomic<uint8_t> lastReason{0};
  std::atomic<uint32_t> drops{0};

  // What the radio's own storage held as it came up.
  String inheritedSsid = "";
  String inheritedPassword = "";

  // Whether the access point last offered itself as the router. As the chip
  // starts it does, so nothing is said about it until the offer first changes.
  bool routerOffered = true;

  void wake();

 public:
  explicit Esp32Radio(Logger* logger);

  /**
   * @brief What tells this chip from every other: the last four figures of
   * its hardware address, as "9C4F". Needs no radio.
   */
  static String machineId();

  /**
   * @brief Brings the radio up as a station that joins nothing until it is
   * told to. Once, before anything else here.
   */
  void initialize();

  void join(const String& ssid, const String& password) override;
  void leave() override;
  void stopStation() override;
  StationState station() override;
  String address() override;
  int channel() override;
  int rssi() override;
  bool openAccessPoint(const String& name, const String& password, int channel,
                       bool offerRouter) override;
  void closeAccessPoint() override;
  int clients() override;
  bool survey(std::vector<HeardNetwork>* heard) override;
  bool inherited(String* ssid, String* password) override;
};
