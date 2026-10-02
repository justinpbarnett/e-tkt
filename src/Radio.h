#pragma once

#include <Arduino.h>

#include <vector>

/**
 * @brief Where the station half of the radio has got to with the network it
 * was last told to join.
 */
struct StationState {
  // On that network, with an address or still waiting for one.
  bool associated = false;
  // Given an address by it.
  bool addressed = false;
  // Why the last try, or the last link, ended: the number the disconnect
  // carried, which is a RADIO_REASON or one the link reads nothing into. See
  // LinkSupervisor::reasonText().
  uint8_t lastReason = 0;
  // How many tries and links have ended since the radio came up. A try that
  // fails is one more, and so is a link that is lost or left.
  uint32_t drops = 0;
};

// The numbers the radio ends a try or a link with that the link reads
// something into. They are the chip's own, and Esp32Radio.cpp holds them to
// that.
//
// The machine itself left.
constexpr uint8_t RADIO_REASON_ASSOC_LEAVE = 8;
// The network did not finish the handshake.
constexpr uint8_t RADIO_REASON_4WAY_HANDSHAKE_TIMEOUT = 15;
// The network went quiet, which is what walking out of reach looks like.
constexpr uint8_t RADIO_REASON_BEACON_TIMEOUT = 200;
// No network of that name was heard.
constexpr uint8_t RADIO_REASON_NO_AP_FOUND = 201;
// The network said no.
constexpr uint8_t RADIO_REASON_AUTH_FAIL = 202;
// The network did not finish the handshake, as the chip also says it.
constexpr uint8_t RADIO_REASON_HANDSHAKE_TIMEOUT = 204;

/**
 * @brief A network the radio heard when it listened on every channel.
 */
struct HeardNetwork {
  String ssid = "";
  int channel = 0;
  // In dBm: -40 is beside the machine, and -90 is barely there.
  int rssi = 0;
  // Whether it asks for a password.
  bool secured = false;
};

/**
 * @brief The Wi-Fi radio as the link supervisor works it: a station, which
 * joins somebody else's network, and an access point, which is the machine's
 * own.
 *
 * An interface, for the reason Display is one: what decides when to try a
 * network, when to give one up and when to open the machine's own is
 * LinkSupervisor, and it can be built and tested off the board. Two adapters
 * sit behind this: Esp32Radio, which is the chip's, and FakeRadio in
 * test/fakes, which the tests and the simulator script.
 *
 * Nothing here decides anything, and nothing waits but survey(). A try is
 * started and left to run. How it went is read from station() later.
 */
class Radio {
 public:
  virtual ~Radio() {}

  // What a network's name and password may be: what the radio takes for a
  // name, and what WPA2 takes for a password.
  static const int MAX_NAME_BYTES = 32;
  static const int MIN_PASSWORD_LENGTH = 8;
  static const int MAX_PASSWORD_LENGTH = 63;

  /**
   * @brief Starts one try at joining a network, and turns the station on if
   * it was off. It does not try again by itself: a try that fails is one
   * more drop in station().
   *
   * @param password empty for an open network.
   */
  virtual void join(const String& ssid, const String& password) = 0;

  /**
   * @brief Ends the try that is under way, or leaves the network the machine
   * is on. Either is one more drop in station(). The station stays on.
   */
  virtual void leave() = 0;

  /**
   * @brief Turns the station off, which leaves the radio to the machine's
   * own network.
   */
  virtual void stopStation() = 0;

  virtual StationState station() = 0;

  /**
   * @brief The address the joined network gave the machine. Empty when it
   * has none.
   */
  virtual String address() = 0;

  /** @brief The channel of the network the machine is on. */
  virtual int channel() = 0;

  /** @brief How strongly the machine hears the network it is on, in dBm. */
  virtual int rssi() = 0;

  /**
   * @brief Opens the machine's own network, with the panel at
   * WIFI_OWN_ADDRESS.
   *
   * @param channel where it sits while the station is off. Beside a station
   *        it sits on the station's channel, whatever is asked for here.
   * @param offerRouter whether it tells a phone that it is the way to the
   *        internet. See NetworkSettings::routerOffered().
   * @return false when it is not open under that password. It is never left
   *         open without one.
   */
  virtual bool openAccessPoint(const String& name, const String& password,
                               int channel, bool offerRouter) = 0;

  virtual void closeAccessPoint() = 0;

  /** @brief How many phones are on the machine's own network. */
  virtual int clients() = 0;

  /**
   * @brief Listens on every channel for the networks in reach, which takes a
   * few seconds, and turns the station on to do it. Not while a try is under
   * way.
   *
   * @return false when the radio could not listen. `heard` is then empty.
   */
  virtual bool survey(std::vector<HeardNetwork>* heard) = 0;

  /**
   * @brief The network the firmware before this one had saved in the radio
   * itself, which is where it kept the only network it knew.
   *
   * @return false when there is none.
   */
  virtual bool inherited(String* ssid, String* password) = 0;
};
