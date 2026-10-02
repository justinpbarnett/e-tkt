#pragma once

#include <Arduino.h>

#include <mutex>
#include <vector>

#include "Display.h"
#include "Logger.h"
#include "NetworkSettings.h"
#include "Radio.h"

/**
 * @brief Where the machine has got to with the networks it remembers.
 */
enum class StationLink {
  // Not trying any: it runs its own network, or it remembers none.
  OFF,
  JOINING,
  JOINED
};

/**
 * @brief Why a try at a network failed, as far as the radio can tell.
 */
enum class JoinFailure {
  NONE,
  // No network of that name was heard.
  NOT_FOUND,
  // The network was there and the join did not go through: the password is
  // wrong, or the signal is too weak to finish the handshake. The two look
  // the same from here.
  REFUSED,
  // The network took the machine on and gave it no address.
  NO_ADDRESS,
  OTHER
};

/**
 * @brief How the machine is reached, for the panel.
 */
struct LinkStatus {
  StationLink station = StationLink::OFF;
  // The network the machine is on, or the one it is trying.
  String network = "";
  // Its address on the network it is on. Empty unless it is on one.
  String address = "";
  // The machine's own network, and how many phones are on it.
  String ownName = "";
  bool ownOpen = false;
  int clients = 0;
  // The last try that failed since the machine was last on a network, or
  // since the networks it remembers were changed.
  String failedNetwork = "";
  JoinFailure failure = JoinFailure::NONE;
  // The number the radio gave for it. 0 when it gave none.
  uint8_t failureReason = 0;
};

/**
 * @brief The networks in reach, as the radio heard them the last time the
 * panel asked it to listen.
 */
struct NearbyNetworks {
  // A listen has been asked for and has not ended yet.
  bool listening = false;
  // How many listens have ended since the machine started. One the radio
  // could not do is counted too, and leaves the list empty.
  uint32_t listens = 0;
  // The loudest first, and each name once.
  std::vector<HeardNetwork> networks;
};

/**
 * @brief Keeps the machine reachable: joins a network it remembers, keeps
 * trying for as long as that takes, and opens the machine's own network when
 * there is no other way in.
 *
 * There are two modes, which NetworkSettings keeps. Joining, the machine
 * tries the networks it remembers in turn, for ever. A weak link fails a try
 * far more often than it passes one, and this core's own reconnect gives up
 * on most of the reasons a weak link produces, so every try is started from
 * here. A network that is there and turns the machine away is tried
 * WIFI_TRIES_PER_NETWORK times before the next one gets its turn, and one
 * that is not there once.
 *
 * When WIFI_OWN_AFTER_MS has gone by with no network joined, the machine's
 * own network opens beside the tries, and the tries are spaced out, more so
 * with a phone on it: every try takes the radio away from that phone. With
 * no network remembered, and in own mode, the machine's own network is all
 * there is: the station is off, and the network sits on the quietest
 * channel.
 *
 * Nothing waits for a network. step() looks at where things stand and does
 * the next thing, every WIFI_STEP_MS, from a task of its own on the board.
 * The one thing in it that takes time is listening on every channel, which
 * is a few seconds: once for a quiet channel to put its own network on, and
 * whenever the panel asks which networks are in reach.
 *
 * The idle screen is told how the machine is reached whenever that changes,
 * and status() says it to the panel.
 */
class LinkSupervisor {
 private:
  Logger* logger;
  Radio* radio;
  NetworkSettings* settings;
  Display* display;

  // Covers `published` and `heard`, which the webserver's task reads, and
  // asks a listen of. Everything else here is step()'s alone.
  std::mutex lock;
  LinkStatus published;
  NearbyNetworks heard;

  bool started = false;

  // --- the settings, as last followed ---
  uint32_t seenRevision = 0;
  // A change waits WIFI_SETTLE_MS before it is followed.
  bool changePending = false;
  uint32_t changeSeenMs = 0;
  NetworkMode mode = NetworkMode::JOIN;
  bool routerOffered = true;
  // In the order they are tried.
  std::vector<RememberedNetwork> networks;

  // --- the station ---
  uint32_t seenDrops = 0;
  bool wasOnline = false;
  String joinedSsid = "";
  String joinedAddress = "";
  // Set by the step that leaves a network because it was forgotten.
  bool leftOnPurpose = false;
  // A try is under way, since tryStartedMs, at tryingSsid.
  bool tryOpen = false;
  uint32_t tryStartedMs = 0;
  String tryingSsid = "";
  // The next try waits, counted from retryFromMs.
  bool awaitingRetry = false;
  uint32_t retryFromMs = 0;
  // Which of the remembered networks the next try is at, and how many tries
  // in a row it has had.
  size_t nextNetwork = 0;
  uint32_t triesHere = 0;
  // On a network and waiting for an address, since addressWaitFromMs.
  bool waitingForAddress = false;
  uint32_t addressWaitFromMs = 0;
  bool addressGivenUp = false;
  // Since when the machine has been without a network, the tries in that
  // time, and when the log last said so.
  uint32_t outageBeganMs = 0;
  uint32_t tries = 0;
  uint32_t lastReportMs = 0;
  String failedNetwork = "";
  JoinFailure failure = JoinFailure::NONE;
  uint8_t failureReason = 0;

  // --- the machine's own network ---
  String ownName = "";
  String ownPassword = "";
  bool ownOpen = false;
  // Open with the station off, on the channel chosen for it.
  bool ownAlone = false;
  // 0 until one has been chosen.
  int ownChannel = 0;
  // The radio did not open it, at ownRefusedMs.
  bool ownRefused = false;
  uint32_t ownRefusedMs = 0;
  int clients = 0;
  // It closes WIFI_OWN_LINGER_MS after this, once the machine is on a
  // network and nobody is on its own.
  uint32_t lingerFromMs = 0;
  // A phone has just joined it, and the screen shows the phone the address.
  bool addressShown = false;
  uint32_t addressShownMs = 0;

  // --- the screen ---
  bool shown = false;
  ConnectionInfo lastShown;

  void start(uint32_t nowMs);
  bool followSettings(uint32_t nowMs);
  void switchMode(NetworkMode mode, uint32_t nowMs);
  void followNetworks(uint32_t nowMs);

  void keepJoining(uint32_t nowMs);
  void noteJoined(const String& address, uint32_t nowMs);
  void noteLost(const String& why, uint32_t nowMs);
  void noteOffline(uint32_t nowMs);
  void noteFailure(JoinFailure failure, uint8_t reason);
  void clearFailure();
  void beginOutage(uint32_t nowMs);
  void startOver(uint32_t nowMs);
  void moveOn();
  uint32_t retryWait(uint32_t nowMs) const;
  void startTry(uint32_t nowMs);

  void keepFallback(uint32_t nowMs);
  void keepOwn(uint32_t nowMs);
  void openOwn(uint32_t nowMs, bool alone);
  void closeOwn();
  void countClients(uint32_t nowMs);

  bool listenIfAsked(uint32_t nowMs);

  void publish();

 public:
  LinkSupervisor(Logger* logger, Radio* radio, NetworkSettings* settings,
                 Display* display);

  /**
   * @brief Looks at the link and does the next thing.
   *
   * The first call asks nothing of the radio that takes time: it takes over
   * the network of the firmware before this one, if there is one, and says
   * on the screen what the machine is about to do.
   */
  void step();

  /**
   * @brief How the machine is reached, as of the last step. For any task.
   */
  LinkStatus status();

  /**
   * @brief How many networks nearby() names at most: the loudest ones.
   */
  static const size_t MAX_NEARBY = 12;

  /**
   * @brief Asks the radio to listen for the networks in reach. For any task.
   *
   * It listens at a later step, when no try is under way, and that takes a
   * few seconds.
   *
   * @return how many listens have ended so far. What this one hears is in
   *         nearby() once that count has gone up. A listen that is under way
   *         as this is asked is the one that answers it.
   */
  uint32_t listen();

  /**
   * @brief What the radio heard at its last listen. For any task.
   */
  NearbyNetworks nearby();

  /**
   * @brief The name of the reason a try or a link ended, by the number the
   * radio gave: "NO_AP_FOUND" for 201. A number it has no name for is that
   * number.
   */
  static String reasonText(uint8_t reason);

  /**
   * @brief Which of channels 1, 6 and 11 has the least on it and around it,
   * going by how loud each network heard is.
   *
   * @param preferred the one to take when it is as quiet as any.
   */
  static int quietestChannel(const std::vector<HeardNetwork>& heard,
                             int preferred);

  /**
   * @brief The channel a machine of this name takes when nothing was heard:
   * 1, 6 or 11, so that machines in one room are not all on the same one.
   */
  static int preferredChannel(const String& name);
};
