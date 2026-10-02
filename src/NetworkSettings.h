#pragma once

#include <Arduino.h>
#include <Preferences.h>

#include <mutex>
#include <vector>

#include "Logger.h"

/**
 * @brief How the machine gets onto a network: it joins one it remembers, or
 * it runs its own and joins none.
 */
enum class NetworkMode { JOIN, OWN };

/**
 * @brief A network the machine may join, as it was typed in.
 */
struct RememberedNetwork {
  String ssid = "";
  // Empty for an open network.
  String password = "";
};

/**
 * @brief What became of a network the machine was asked to remember.
 */
enum class Remembered {
  KEPT,
  NAME_MISSING,
  NAME_TOO_LONG,
  // See NetworkSettings::nameIsText().
  NAME_NOT_TEXT,
  PASSWORD_TOO_SHORT,
  PASSWORD_TOO_LONG,
  FULL
};

/**
 * @brief Keeps, in EEPROM, what the machine needs to be reached over Wi-Fi:
 * whether it joins a network or runs its own, the networks it remembers, and
 * the password of its own.
 *
 * The panel changes these from the webserver's task and the link reads them
 * from a task of its own, so every access is under one lock, which also
 * covers Preferences, as Roll's does. revision() is how the link learns that
 * something it has to follow has changed.
 *
 * A password leaves here only through networks() and ownPassword(), which
 * are for the radio and for the machine's own screen. None is logged.
 */
class NetworkSettings {
 private:
  Logger* logger;
  // By value, for the reason Settings gives.
  Preferences preferences;
  std::mutex lock;

  // What the board makes of its MAC address, in capitals: see initialize().
  String machineId = "";
  NetworkMode current = NetworkMode::JOIN;
  // In the order they are tried.
  std::vector<RememberedNetwork> remembered;
  // Empty until ownPassword() has made one.
  String password = "";
  bool router = true;
  // Whether the network of the firmware before this one has been looked for.
  bool importDone = false;
  uint32_t changes = 0;

  // Writes the list of networks. Under the lock.
  void storeNetworks();

 public:
  // How many networks the machine remembers. A machine is carried between a
  // few places, and each one more is one more to try before it opens its own
  // network.
  static const int MAX_REMEMBERED = 4;

  // What a network's name and password may be: what the radio takes for a
  // name, and what WPA2 takes for a password.
  static const int MAX_NAME_BYTES = 32;
  static const int MIN_PASSWORD_LENGTH = 8;
  static const int MAX_PASSWORD_LENGTH = 63;

  NetworkSettings(Logger* logger);

  /**
   * @brief Whether a network's name is text: UTF-8, with nothing in it that
   * does not print.
   *
   * On the air a name is up to 32 bytes of anything. The panel is what shows
   * a name and sends it back, and it can do neither with one that is not
   * text, so the machine remembers none, and lists none as in reach.
   */
  static bool nameIsText(const String& name);

  /**
   * @brief Reads everything back from EEPROM. A machine that was never set
   * up joins a network, and remembers none.
   *
   * @param machineId what tells this machine from the others: the end of its
   * MAC address, in hex. Its own network and its host name are named after
   * it.
   */
  void initialize(const String& machineId);

  NetworkMode mode();
  void setMode(NetworkMode mode);

  /**
   * @brief Remembers a network, as the first to be tried. One already
   * remembered under that name is replaced, which is how a password is put
   * right.
   *
   * Nothing is kept unless the answer is KEPT.
   *
   * @param password empty for an open network.
   */
  Remembered remember(const String& ssid, const String& password);

  /**
   * @brief Forgets a network. One that is not remembered is forgotten
   * already.
   */
  void forget(const String& ssid);

  /**
   * @brief The networks remembered, in the order they are tried, with their
   * passwords.
   */
  std::vector<RememberedNetwork> networks();

  /**
   * @brief Tries this network first from now on. For the link, as it joins
   * one: a machine that has moved finds its network at the first try the
   * next time.
   */
  void prefer(const String& ssid);

  /**
   * @brief The name of the machine's own network: "E-TKT-9C4F".
   */
  String ownName();

  /**
   * @brief The name the machine answers to on a network, without the
   * ".local": "e-tkt-9c4f".
   */
  String hostName();

  /**
   * @brief The password of the machine's own network. Made the first time it
   * is asked for, which is once the radio is on: the chip's random numbers
   * are only as good as they get from then.
   */
  String ownPassword();

  /**
   * @brief Whether the machine's own network says it is the way to the
   * internet. A phone treats the network as an ordinary one when it does,
   * and keeps its own way to the internet when it does not.
   */
  bool routerOffered();
  void setRouterOffered(bool offered);

  /**
   * @brief Whether the network the firmware before this one had saved has
   * been looked for. That firmware kept its one network in the radio itself,
   * and it stays there. The link takes it over the first time this firmware
   * starts, and says so here: looked for at every start, it would bring back
   * a network that was forgotten on purpose.
   */
  bool imported();
  void markImported();

  /**
   * @brief A number that moves with every change the link has to follow: the
   * mode, the networks, and the router. Not with prefer(), which is the
   * link's own doing.
   */
  uint32_t revision();

  /**
   * @brief Forgets every network, goes back to joining one, and throws away
   * the password of the machine's own network, so that the next one asked
   * for is new. The network of the firmware before this one is not looked
   * for after it. It needs no initialize() first.
   */
  void reset();
};
