#include "LinkSupervisor.h"

#include <algorithm>
#include <cstring>

#include "Configuration.h"

namespace {

// The three channels that do not overlap each other.
const int CHANNELS[] = {1, 6, 11};
const int CHANNEL_COUNT = sizeof(CHANNELS) / sizeof(CHANNELS[0]);
// A network is in the way of every channel fewer than this many from its
// own.
const int CHANNEL_REACH = 5;
// A network no louder than this here, in dBm, is in the way of none.
const int FAINTEST_RSSI = -100;

// The disconnect reasons this core reports, by the number the event
// carries: the ones Radio.h names, and the ones the link reads nothing into,
// which have their number here alone. Anything else is printed as that
// number. The ones the core itself never retries are 3, 4, 8, 15 and 202,
// which is most of what a weak link produces.
const char* reasonName(uint8_t reason) {
  switch (reason) {
    case 0:
      return "NONE";
    case 1:
      return "UNSPECIFIED";
    case 2:
      return "AUTH_EXPIRE";
    case 3:
      return "AUTH_LEAVE";
    case 4:
      return "ASSOC_EXPIRE";
    case 5:
      return "ASSOC_TOOMANY";
    case 6:
      return "NOT_AUTHED";
    case 7:
      return "NOT_ASSOCED";
    case RADIO_REASON_ASSOC_LEAVE:
      return "ASSOC_LEAVE";
    case RADIO_REASON_4WAY_HANDSHAKE_TIMEOUT:
      return "4WAY_HANDSHAKE_TIMEOUT";
    case 16:
      return "GROUP_KEY_UPDATE_TIMEOUT";
    case RADIO_REASON_BEACON_TIMEOUT:
      return "BEACON_TIMEOUT";
    case RADIO_REASON_NO_AP_FOUND:
      return "NO_AP_FOUND";
    case RADIO_REASON_AUTH_FAIL:
      return "AUTH_FAIL";
    case 203:
      return "ASSOC_FAIL";
    case RADIO_REASON_HANDSHAKE_TIMEOUT:
      return "HANDSHAKE_TIMEOUT";
    default:
      return NULL;
  }
}

// What the end of a try says about the network. A handshake the network did
// not finish and the network saying no are one thing here: a wrong password
// does either, and so does a signal too weak to carry the handshake.
JoinFailure failureOf(uint8_t reason) {
  switch (reason) {
    case RADIO_REASON_NO_AP_FOUND:
      return JoinFailure::NOT_FOUND;
    case RADIO_REASON_4WAY_HANDSHAKE_TIMEOUT:
    case RADIO_REASON_AUTH_FAIL:
    case RADIO_REASON_HANDSHAKE_TIMEOUT:
      return JoinFailure::REFUSED;
    default:
      return JoinFailure::OTHER;
  }
}

// "Church", "Church or Basement", "Church, Basement or Garage".
String namesOf(const std::vector<RememberedNetwork>& networks) {
  String names = "";
  for (size_t i = 0; i < networks.size(); i++) {
    if (i > 0) {
      names += i + 1 == networks.size() ? " or " : ", ";
    }
    names += networks[i].ssid;
  }
  return names;
}

// What the panel lists of the networks the radio heard: each name once, as
// loud as its loudest access point, the loudest first, and no more than the
// reply that lists them has room for. A network that hides its name is left
// out, and its name can still be typed in. So is one whose name is not text,
// which the machine would not remember.
std::vector<HeardNetwork> toPickFrom(const std::vector<HeardNetwork>& heard) {
  std::vector<HeardNetwork> networks;
  for (const HeardNetwork& one : heard) {
    if (one.ssid.length() == 0 || !NetworkSettings::nameIsText(one.ssid)) {
      continue;
    }
    bool listed = false;
    for (HeardNetwork& network : networks) {
      if (network.ssid == one.ssid) {
        listed = true;
        if (one.rssi > network.rssi) {
          network = one;
        }
      }
    }
    if (!listed) {
      networks.push_back(one);
    }
  }
  std::sort(networks.begin(), networks.end(),
            [](const HeardNetwork& a, const HeardNetwork& b) {
              if (a.rssi != b.rssi) {
                return a.rssi > b.rssi;
              }
              return strcmp(a.ssid.c_str(), b.ssid.c_str()) < 0;
            });
  if (networks.size() > LinkSupervisor::MAX_NEARBY) {
    networks.resize(LinkSupervisor::MAX_NEARBY);
  }
  return networks;
}

bool sameNetworks(const std::vector<RememberedNetwork>& a,
                  const std::vector<RememberedNetwork>& b) {
  if (a.size() != b.size()) {
    return false;
  }
  for (size_t i = 0; i < a.size(); i++) {
    if (a[i].ssid != b[i].ssid || a[i].password != b[i].password) {
      return false;
    }
  }
  return true;
}

}  // namespace

LinkSupervisor::LinkSupervisor(Logger* logger, Radio* radio,
                               NetworkSettings* settings, Display* display) {
  this->logger = logger;
  this->radio = radio;
  this->settings = settings;
  this->display = display;
}

void LinkSupervisor::step() {
  const uint32_t nowMs = millis();
  if (!this->started) {
    this->started = true;
    this->start(nowMs);
  } else if (!this->followSettings(nowMs) && !this->listenIfAsked(nowMs)) {
    if (this->mode == NetworkMode::OWN) {
      this->keepOwn(nowMs);
    } else {
      this->keepJoining(nowMs);
    }
  }
  this->publish();
}

LinkStatus LinkSupervisor::status() {
  this->lock.lock();
  const LinkStatus status = this->published;
  this->lock.unlock();
  return status;
}

uint32_t LinkSupervisor::listen() {
  this->lock.lock();
  this->heard.listening = true;
  const uint32_t listens = this->heard.listens;
  this->lock.unlock();
  return listens;
}

NearbyNetworks LinkSupervisor::nearby() {
  this->lock.lock();
  const NearbyNetworks nearby = this->heard;
  this->lock.unlock();
  return nearby;
}

void LinkSupervisor::start(uint32_t nowMs) {
  // The firmware before this one kept its one network in the radio itself.
  // Looked for once: see NetworkSettings::imported().
  if (!this->settings->imported()) {
    String ssid = "";
    String password = "";
    if (this->radio->inherited(&ssid, &password)) {
      this->logger->log(String("found ") + ssid +
                        ", saved by the firmware before this one");
      if (this->settings->remember(ssid, password) != Remembered::KEPT) {
        this->logger->warn(String("could not keep ") + ssid +
                           ", which has to be typed in again");
      }
    }
    this->settings->markImported();
  }

  this->seenRevision = this->settings->revision();
  this->mode = this->settings->mode();
  this->routerOffered = this->settings->routerOffered();
  this->networks = this->settings->networks();
  this->ownName = this->settings->ownName();
  this->seenDrops = this->radio->station().drops;
  this->beginOutage(nowMs);
}

// --- following the settings ---

bool LinkSupervisor::followSettings(uint32_t nowMs) {
  const uint32_t revision = this->settings->revision();
  if (revision != this->seenRevision) {
    // The reply to the request that made the change has to leave first, over
    // a link the change may take away. Another change restarts the wait.
    this->seenRevision = revision;
    this->changePending.set(nowMs);
  }
  if (!this->changePending.forAtLeast(nowMs, WIFI_SETTLE_MS)) {
    return false;
  }
  this->changePending.clear();

  const bool offered = this->settings->routerOffered();
  const bool routerChanged = offered != this->routerOffered;
  this->routerOffered = offered;
  if (routerChanged && this->ownOpen) {
    // A phone is told whether the network is a way to the internet as it
    // joins. Closing the network puts every phone off it, and they join
    // again as it opens.
    const bool alone = this->ownAlone;
    this->closeOwn();
    this->openOwn(nowMs, alone);
  }

  const std::vector<RememberedNetwork> latest = this->settings->networks();
  const bool networksKept = sameNetworks(latest, this->networks);
  this->networks = latest;

  const NetworkMode mode = this->settings->mode();
  if (mode != this->mode) {
    this->switchMode(mode, nowMs);
  } else if (mode == NetworkMode::JOIN && !(routerChanged && networksKept)) {
    // A network sent again exactly as it is kept changes nothing in the
    // list, and is still followed: that is the panel asking for a try now.
    // A change to the router alone is not.
    this->followNetworks(nowMs);
  }
  // What the change asks of the radio is given a step to happen before
  // anything else is asked of it.
  return true;
}

void LinkSupervisor::switchMode(NetworkMode mode, uint32_t nowMs) {
  this->mode = mode;
  if (mode == NetworkMode::OWN) {
    if (this->wasOnline) {
      this->logger->log(String("left ") + this->joinedSsid +
                        " for its own network");
    }
    // The station is stopped once its own network is open: see openOwn().
    if (this->wasOnline || this->tryOpen.isSet() ||
        this->radio->station().associated) {
      this->radio->leave();
    }
    this->wasOnline = false;
    this->joinedSsid = "";
    this->joinedAddress = "";
    this->leftOnPurpose = false;
    this->endTry();
    this->awaitingRetry.clear();
    this->startedOver.clear();
    this->clearFailure();
    return;
  }
  // Back to joining. Its own network stays open, as it does beside any try:
  // whoever asked for this is on it, and has to see how it went.
  this->seenDrops = this->radio->station().drops;
  this->startOver(nowMs);
}

void LinkSupervisor::followNetworks(uint32_t nowMs) {
  if (this->wasOnline) {
    bool kept = false;
    for (const RememberedNetwork& network : this->networks) {
      kept = kept || network.ssid == this->joinedSsid;
    }
    if (!kept) {
      // noteLost() says so, at the next step.
      this->leftOnPurpose = true;
      this->radio->leave();
    }
    // A machine got ready for another place stays on the network it is on.
    return;
  }
  if (this->tryOpen.isSet() || this->radio->station().associated) {
    this->radio->leave();
  }
  this->startOver(nowMs);
}

// --- joining ---

void LinkSupervisor::keepJoining(uint32_t nowMs) {
  const StationState state = this->radio->station();
  const bool dropped = state.drops != this->seenDrops;
  this->seenDrops = state.drops;
  if (dropped) {
    // Whatever ended, the radio gets WIFI_RETRY_MS to be done with it.
    this->awaitingRetry.set(nowMs);
  }
  if (this->startedOver.forAtLeast(nowMs, WIFI_OWN_AFTER_MS)) {
    this->startedOver.clear();
  }

  const bool online = state.associated && state.addressed;
  if (online && !this->wasOnline) {
    // The radio says it has an address a moment before it can say which.
    // Until it can, this is a machine still waiting for one.
    const String address = this->radio->address();
    if (address.length() > 0) {
      this->noteJoined(address, nowMs);
      return;
    }
  }
  if (!online && this->wasOnline) {
    this->noteLost(
        dropped ? reasonText(state.lastReason) : String("no address"), nowMs);
  }
  if (this->wasOnline) {
    // The network hands out another address when a lease runs out, and the
    // screen is the only place the address is written.
    const String address = this->radio->address();
    if (address.length() > 0) {
      this->joinedAddress = address;
    }
    this->keepFallback(nowMs);
    return;
  }

  if (state.associated) {
    // DHCP on this link has taken the better part of a minute and then
    // worked. Starting another join here would throw that away. A join
    // that never gets an address is dropped once, and the disconnect event
    // is what opens the next try.
    if (!this->waitingForAddress.isSet()) {
      this->waitingForAddress.set(nowMs);
      this->addressGivenUp = false;
      this->logger->log("associated, waiting for an address");
    } else if (!this->addressGivenUp &&
               this->waitingForAddress.forAtLeast(nowMs, WIFI_DHCP_MS)) {
      this->addressGivenUp = true;
      this->logger->warn(String("no address after ") +
                         String(WIFI_DHCP_MS / 1000) + " s, joining again");
      this->noteFailure(JoinFailure::NO_ADDRESS, 0);
      this->radio->leave();
    }
    this->noteOffline(nowMs);
    this->keepFallback(nowMs);
    return;
  }
  const bool gaveUp = this->addressGivenUp;
  this->waitingForAddress.clear();
  this->addressGivenUp = false;

  if (this->tryOpen.isSet() && dropped) {
    this->tryOpen.clear();
    if (gaveUp) {
      // Left for want of an address, which is already noted as why.
      this->moveOn();
    } else {
      this->noteFailure(failureOf(state.lastReason), state.lastReason);
      // A network that is there and turned the machine away is worth another
      // try before the next one: on a weak link that is how most tries end.
      // One that was not heard is not.
      if (state.lastReason == RADIO_REASON_NO_AP_FOUND ||
          this->triesHere >= WIFI_TRIES_PER_NETWORK) {
        this->moveOn();
      }
    }
  }
  if (this->tryOpen.isSet()) {
    if (this->tryOpen.forAtLeast(nowMs, WIFI_TRY_MS)) {
      // No disconnect arrived. The stack is stuck in this try.
      this->radio->leave();
      this->tryOpen.clear();
      this->awaitingRetry.set(nowMs);
      this->noteFailure(JoinFailure::OTHER, 0);
      this->moveOn();
    }
    this->noteOffline(nowMs);
    this->keepFallback(nowMs);
    return;
  }

  if (this->networks.empty()) {
    // Nothing to join, so its own network is the one way in, and the radio
    // is that network's alone.
    this->keepOwn(nowMs);
    return;
  }
  this->keepFallback(nowMs);
  if (this->awaitingRetry.forLessThan(nowMs, this->retryWait())) {
    this->noteOffline(nowMs);
    return;
  }
  this->startTry(nowMs);
}

void LinkSupervisor::noteJoined(const String& address, uint32_t nowMs) {
  this->joinedSsid = this->tryingSsid;
  this->joinedAddress = address;
  String line = String("joined ") + this->joinedSsid + " at " + address +
                " (channel " + String(this->radio->channel()) + ", " +
                String(this->radio->rssi()) + " dBm)";
  if (this->tries > 1) {
    line += String(" after ") + String(this->tries) + " tries in " +
            String((nowMs - this->outageBeganMs) / 1000) + " s";
  }
  this->logger->log(line);

  // A machine that has moved finds its network at the first try the next
  // time. The list is read back in its new order.
  this->settings->prefer(this->joinedSsid);
  this->networks = this->settings->networks();
  this->nextNetwork = 0;
  this->triesHere = 0;
  this->tries = 0;
  this->endTry();
  this->awaitingRetry.clear();
  // Whoever made the change has seen it work.
  this->startedOver.clear();
  this->wasOnline = true;
  // Its own network, if it is open, closes WIFI_OWN_LINGER_MS from here.
  this->lingerFromMs = nowMs;
  this->clearFailure();
}

void LinkSupervisor::noteLost(const String& why, uint32_t nowMs) {
  const String network = this->joinedSsid;
  this->wasOnline = false;
  this->joinedSsid = "";
  this->joinedAddress = "";
  if (this->leftOnPurpose) {
    // Forgotten on the panel, and what is left is tried as after any change
    // made there.
    this->leftOnPurpose = false;
    this->logger->log(String("left ") + network);
    this->startOver(nowMs);
    return;
  }
  this->logger->log(String("lost ") + network + " (" + why +
                    "), joining it again");
  this->endTry();
  // The network it was on is the first it tries. The tries are as far apart
  // as its own network asks for, if that is open: nobody is waiting for this
  // one.
  this->nextNetwork = 0;
  for (size_t i = 0; i < this->networks.size(); i++) {
    if (this->networks[i].ssid == network) {
      this->nextNetwork = i;
    }
  }
  this->triesHere = 0;
  this->beginOutage(nowMs);
}

void LinkSupervisor::noteOffline(uint32_t nowMs) {
  if (nowMs - this->lastReportMs < WIFI_REPORT_MS) {
    return;
  }
  this->lastReportMs = nowMs;
  String last = "none";
  if (this->failedTry.cause == JoinFailure::NO_ADDRESS) {
    last = "no address";
  } else if (this->failedTry.cause != JoinFailure::NONE) {
    // A try that never ended has no reason to give.
    last = this->failedTry.reason == 0 ? String("no answer")
                                       : reasonText(this->failedTry.reason);
  }
  this->logger->log(String("still joining ") + namesOf(this->networks) + ", " +
                    String(this->tries) +
                    (this->tries == 1 ? " try in " : " tries in ") +
                    String((nowMs - this->outageBeganMs) / 1000) +
                    " s, last failure " + last);
}

void LinkSupervisor::noteFailure(JoinFailure cause, uint8_t reason) {
  this->failedTry.network = this->tryingSsid;
  this->failedTry.cause = cause;
  this->failedTry.reason = reason;
}

void LinkSupervisor::clearFailure() { this->failedTry = FailedTry(); }

void LinkSupervisor::beginOutage(uint32_t nowMs) {
  this->outageBeganMs = nowMs;
  this->lastReportMs = nowMs;
  this->tries = 0;
}

void LinkSupervisor::startOver(uint32_t nowMs) {
  // From the first network, with the tries close together again, as after a
  // start: somebody has made a change on the panel, and is waiting to see
  // whether it worked. A wait for the radio to finish a try that has just
  // ended is left as it is.
  this->endTry();
  this->nextNetwork = 0;
  this->triesHere = 0;
  this->startedOver.set(nowMs);
  this->clearFailure();
  this->beginOutage(nowMs);
}

void LinkSupervisor::moveOn() {
  this->nextNetwork++;
  this->triesHere = 0;
}

uint32_t LinkSupervisor::retryWait() const {
  if (!this->ownOpen || this->startedOver.isSet()) {
    return WIFI_RETRY_MS;
  }
  // Every try takes the radio away from whoever is on its own network.
  return this->clients > 0 ? WIFI_RETRY_WHILE_CLIENT_MS
                           : WIFI_RETRY_BESIDE_OWN_MS;
}

void LinkSupervisor::startTry(uint32_t nowMs) {
  this->nextNetwork %= this->networks.size();
  const RememberedNetwork& network = this->networks[this->nextNetwork];
  this->tryingSsid = network.ssid;
  this->tries++;
  this->triesHere++;
  this->tryOpen.set(nowMs);
  this->awaitingRetry.clear();
  // The station is on from here, and its own network goes where the station
  // goes, from channel to channel.
  this->ownAlone = false;
  this->radio->join(network.ssid, network.password);
}

void LinkSupervisor::endTry() {
  // No try is under way, and none is waiting for an address.
  this->tryOpen.clear();
  this->waitingForAddress.clear();
  this->addressGivenUp = false;
}

// --- its own network ---

void LinkSupervisor::keepFallback(uint32_t nowMs) {
  if (!this->ownOpen) {
    if (!this->wasOnline && nowMs - this->outageBeganMs >= WIFI_OWN_AFTER_MS) {
      this->openOwn(nowMs, false);
    }
    return;
  }
  this->countClients(nowMs);
  // Open for as long as no network is joined. Once one is, whoever typed it
  // in is still on this one, reading where to go next.
  if (this->wasOnline && this->clients == 0 &&
      nowMs - this->lingerFromMs >= WIFI_OWN_LINGER_MS) {
    this->closeOwn();
  }
}

void LinkSupervisor::keepOwn(uint32_t nowMs) {
  if (this->ownOpen && !this->ownAlone) {
    // It was open beside the tries, on whatever channel they had it on.
    // Opened again, it is on the one chosen for it.
    this->closeOwn();
  }
  if (!this->ownOpen) {
    this->openOwn(nowMs, true);
    return;
  }
  this->countClients(nowMs);
}

void LinkSupervisor::openOwn(uint32_t nowMs, bool alone) {
  if (this->ownRefused.forLessThan(nowMs, WIFI_OWN_RETRY_MS)) {
    return;
  }
  int channel = preferredChannel(this->ownName);
  if (alone) {
    if (this->ownChannel == 0) {
      // Listened for once. The channel is kept for as long as the machine is
      // on: a network that moved would have every phone look for it again.
      std::vector<HeardNetwork> heard;
      this->ownChannel = this->radio->survey(&heard)
                             ? quietestChannel(heard, channel)
                             : channel;
    }
    channel = this->ownChannel;
  }
  // Asked for here and not at the start: see NetworkSettings::ownPassword().
  this->ownPassword = this->settings->ownPassword();
  const bool opened = this->radio->openAccessPoint(
      this->ownName, this->ownPassword, channel, this->routerOffered);
  // Listening took a while.
  const uint32_t doneMs = millis();
  if (!opened) {
    // It stays shut. A network with no password on it is not a way in worth
    // having.
    if (!this->ownRefused.isSet()) {
      this->logger->warn(String("its own network ") + this->ownName +
                         " did not open under its password, trying again");
    }
    this->ownRefused.set(doneMs);
    return;
  }
  if (alone) {
    this->radio->stopStation();
  }
  this->ownRefused.clear();
  this->ownOpen = true;
  this->ownAlone = alone;
  this->clients = 0;
  this->addressShown.clear();
  this->lingerFromMs = doneMs;
  // Beside a station it is on the station's channel, whichever that is.
  this->logger->log(
      String("its own network ") + this->ownName + " is open" +
      (alone ? String(" on channel ") + String(channel) : String("")));
}

void LinkSupervisor::closeOwn() {
  this->radio->closeAccessPoint();
  this->ownOpen = false;
  this->ownAlone = false;
  this->clients = 0;
  this->addressShown.clear();
  this->logger->log(String("its own network ") + this->ownName + " is closed");
}

void LinkSupervisor::countClients(uint32_t nowMs) {
  const int now = this->radio->clients();
  if (now != this->clients) {
    this->logger->log(now == 0
                          ? String("nobody on its own network")
                          : String(now) + (now == 1 ? " phone" : " phones") +
                                " on its own network");
  }
  if (now > this->clients) {
    // A phone has just joined, and what it needs next is the address.
    this->addressShown.set(nowMs);
  }
  this->clients = now;
  if (now > 0) {
    this->lingerFromMs = nowMs;
  } else {
    this->addressShown.clear();
  }
  if (this->addressShown.forAtLeast(nowMs, WIFI_ADDRESS_SHOWN_MS)) {
    this->addressShown.clear();
  }
}

// --- the networks in reach ---

bool LinkSupervisor::listenIfAsked(uint32_t nowMs) {
  this->lock.lock();
  const bool asked = this->heard.listening;
  this->lock.unlock();
  // The radio cannot listen and try a network at once, and a machine that is
  // waiting for its address has to be there to be given it. Whatever has
  // just ended gets its WIFI_RETRY_MS first, as it does before a try.
  if (!asked || this->tryOpen.isSet() || this->waitingForAddress.isSet() ||
      this->awaitingRetry.forLessThan(nowMs, WIFI_RETRY_MS)) {
    return false;
  }

  // A few seconds, with the lock free: the panel goes on asking how it is
  // going, and may ask for a listen again.
  std::vector<HeardNetwork> heard;
  const bool listened = this->radio->survey(&heard);
  if (this->ownAlone) {
    // Listening turned the station on, and the radio is its own network's
    // alone.
    this->radio->stopStation();
  }
  if (listened) {
    // Not their names: what a stranger calls a network does not belong in
    // the log.
    this->logger->log(String("heard ") + String((int)heard.size()) +
                      (heard.size() == 1 ? " network" : " networks") +
                      " in reach");
  } else {
    this->logger->warn("could not listen for the networks in reach");
  }
  const std::vector<HeardNetwork> networks = toPickFrom(heard);

  this->lock.lock();
  this->heard.networks = networks;
  this->heard.listens++;
  this->heard.listening = false;
  this->lock.unlock();
  // The step is spent: the clock it was given is seconds behind by now.
  return true;
}

// --- saying how the machine is reached ---

void LinkSupervisor::publish() {
  LinkStatus status;
  if (this->mode == NetworkMode::JOIN && this->wasOnline) {
    status.station = StationLink::JOINED;
    status.network = this->joinedSsid;
    status.address = this->joinedAddress;
  } else if (this->mode == NetworkMode::JOIN && !this->networks.empty()) {
    status.station = StationLink::JOINING;
    status.network =
        this->tryOpen.isSet() || this->waitingForAddress.isSet()
            ? this->tryingSsid
            : this->networks[this->nextNetwork % this->networks.size()].ssid;
  }
  status.ownName = this->ownName;
  status.ownOpen = this->ownOpen;
  status.clients = this->clients;
  status.failedTry = this->failedTry;

  this->lock.lock();
  this->published = status;
  this->lock.unlock();

  // The screen: where the panel is, or failing that, how to get to it.
  ConnectionInfo info;
  if (status.station == StationLink::JOINED) {
    info.name = status.network;
    info.detail = status.address;
    info.qr = String("http://") + status.address;
  } else if (this->ownOpen && this->addressShown.isSet()) {
    info.name = this->ownName;
    info.detail = WIFI_OWN_ADDRESS;
    info.qr = String("http://") + WIFI_OWN_ADDRESS;
  } else if (this->ownOpen) {
    // The form a phone's camera joins a network from. Neither the name nor
    // the password has a character in it that the form wants escaped.
    info.name = this->ownName;
    info.detail = this->ownPassword;
    info.qr = String("WIFI:T:WPA;S:") + this->ownName +
              ";P:" + this->ownPassword + ";;";
  } else if (status.station == StationLink::JOINING) {
    info.name = status.network;
    info.detail = "joining";
  } else {
    info.name = this->ownName;
    info.detail = "starting";
  }
  // Told only of a change: the screen draws itself again for each one.
  if (!this->shown || info != this->lastShown) {
    this->shown = true;
    this->lastShown = info;
    this->display->setConnectionInfo(info);
  }
}

String LinkSupervisor::reasonText(uint8_t reason) {
  const char* name = reasonName(reason);
  if (name != NULL) {
    return String(name);
  }
  return String((int)reason);
}

int LinkSupervisor::quietestChannel(const std::vector<HeardNetwork>& heard,
                                    int preferred) {
  // Looked at from the preferred one on, so that it is the one taken when
  // none is quieter.
  int first = 0;
  for (int i = 0; i < CHANNEL_COUNT; i++) {
    if (CHANNELS[i] == preferred) {
      first = i;
    }
  }
  int best = CHANNELS[first];
  long bestLoad = -1;
  for (int i = 0; i < CHANNEL_COUNT; i++) {
    const int channel = CHANNELS[(first + i) % CHANNEL_COUNT];
    long load = 0;
    for (const HeardNetwork& network : heard) {
      // A network is less in the way the further off its channel is, and
      // more so the louder it is here.
      const int apart = network.channel > channel ? network.channel - channel
                                                  : channel - network.channel;
      if (apart >= CHANNEL_REACH || network.rssi <= FAINTEST_RSSI) {
        continue;
      }
      load += (long)(network.rssi - FAINTEST_RSSI) * (CHANNEL_REACH - apart);
    }
    if (bestLoad < 0 || load < bestLoad) {
      best = channel;
      bestLoad = load;
    }
  }
  return best;
}

int LinkSupervisor::preferredChannel(const String& name) {
  unsigned int sum = 0;
  for (unsigned int i = 0; i < name.length(); i++) {
    sum += (unsigned char)name.charAt(i);
  }
  return CHANNELS[sum % CHANNEL_COUNT];
}
