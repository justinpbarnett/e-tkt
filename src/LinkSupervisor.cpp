#include "LinkSupervisor.h"

#include "Configuration.h"

namespace {

// The reason the radio gives for a network it did not hear.
const uint8_t NOT_HEARD = 201;

// The disconnect reasons this core reports, by the number the event
// carries. Anything else is printed as that number. The ones the core
// itself never retries are 3, 4, 8, 15 and 202, which is most of what a
// weak link produces.
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
    case 8:
      return "ASSOC_LEAVE";
    case 15:
      return "4WAY_HANDSHAKE_TIMEOUT";
    case 16:
      return "GROUP_KEY_UPDATE_TIMEOUT";
    case 200:
      return "BEACON_TIMEOUT";
    case 201:
      return "NO_AP_FOUND";
    case 202:
      return "AUTH_FAIL";
    case 203:
      return "ASSOC_FAIL";
    case 204:
      return "HANDSHAKE_TIMEOUT";
    default:
      return NULL;
  }
}

// What the end of a try says about the network. 15 and 204 are a handshake
// the network did not finish and 202 is the network saying no. A wrong
// password does that, and so does a signal too weak to carry the handshake.
JoinFailure failureOf(uint8_t reason) {
  switch (reason) {
    case NOT_HEARD:
      return JoinFailure::NOT_FOUND;
    case 15:
    case 202:
    case 204:
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
  } else if (!this->followSettings(nowMs)) {
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
    this->changePending = true;
    this->changeSeenMs = nowMs;
  }
  if (!this->changePending || nowMs - this->changeSeenMs < WIFI_SETTLE_MS) {
    return false;
  }
  this->changePending = false;

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
    if (this->wasOnline || this->tryOpen || this->radio->station().associated) {
      this->radio->leave();
    }
    this->wasOnline = false;
    this->joinedSsid = "";
    this->joinedAddress = "";
    this->leftOnPurpose = false;
    this->tryOpen = false;
    this->awaitingRetry = false;
    this->waitingForAddress = false;
    this->addressGivenUp = false;
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
  if (this->tryOpen || this->radio->station().associated) {
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
    this->awaitingRetry = true;
    this->retryFromMs = nowMs;
  }

  const bool online = state.associated && state.addressed;
  if (online && !this->wasOnline) {
    // The radio says it has an address a moment before it can say which.
    // Until it can, this is a machine still waiting for one.
    const String address = this->radio->address();
    if (address != "0.0.0.0") {
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
    if (address != "0.0.0.0") {
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
    if (!this->waitingForAddress) {
      this->waitingForAddress = true;
      this->addressWaitFromMs = nowMs;
      this->addressGivenUp = false;
      this->logger->log("associated, waiting for an address");
    } else if (!this->addressGivenUp &&
               nowMs - this->addressWaitFromMs >= WIFI_DHCP_MS) {
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
  this->waitingForAddress = false;
  this->addressGivenUp = false;

  if (this->tryOpen && dropped) {
    this->tryOpen = false;
    if (gaveUp) {
      // Left for want of an address, which is already noted as why.
      this->moveOn();
    } else {
      this->noteFailure(failureOf(state.lastReason), state.lastReason);
      // A network that is there and turned the machine away is worth another
      // try before the next one: on a weak link that is how most tries end.
      // One that was not heard is not.
      if (state.lastReason == NOT_HEARD ||
          this->triesHere >= WIFI_TRIES_PER_NETWORK) {
        this->moveOn();
      }
    }
  }
  if (this->tryOpen) {
    if (nowMs - this->tryStartedMs >= WIFI_TRY_MS) {
      // No disconnect arrived. The stack is stuck in this try.
      this->radio->leave();
      this->tryOpen = false;
      this->awaitingRetry = true;
      this->retryFromMs = nowMs;
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
  if (this->awaitingRetry &&
      nowMs - this->retryFromMs < this->retryWait(nowMs)) {
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
  this->tryOpen = false;
  this->awaitingRetry = false;
  this->waitingForAddress = false;
  this->addressGivenUp = false;
  this->wasOnline = true;
  // Its own network, if it is open, closes WIFI_OWN_LINGER_MS from here.
  this->lingerFromMs = nowMs;
  this->clearFailure();
}

void LinkSupervisor::noteLost(const String& why, uint32_t nowMs) {
  if (this->leftOnPurpose) {
    this->logger->log(String("left ") + this->joinedSsid);
  } else {
    this->logger->log(String("lost ") + this->joinedSsid + " (" + why +
                      "), joining it again");
  }
  this->leftOnPurpose = false;
  this->wasOnline = false;
  this->tryOpen = false;
  // The network it was on is the first it tries.
  this->nextNetwork = 0;
  for (size_t i = 0; i < this->networks.size(); i++) {
    if (this->networks[i].ssid == this->joinedSsid) {
      this->nextNetwork = i;
    }
  }
  this->triesHere = 0;
  this->joinedSsid = "";
  this->joinedAddress = "";
  this->beginOutage(nowMs);
}

void LinkSupervisor::noteOffline(uint32_t nowMs) {
  if (nowMs - this->lastReportMs < WIFI_REPORT_MS) {
    return;
  }
  this->lastReportMs = nowMs;
  String last = "none";
  if (this->failure == JoinFailure::NO_ADDRESS) {
    last = "no address";
  } else if (this->failure != JoinFailure::NONE) {
    // A try that never ended has no reason to give.
    last = this->failureReason == 0 ? String("no answer")
                                    : reasonText(this->failureReason);
  }
  this->logger->log(String("still joining ") + namesOf(this->networks) + ", " +
                    String(this->tries) +
                    (this->tries == 1 ? " try in " : " tries in ") +
                    String((nowMs - this->outageBeganMs) / 1000) +
                    " s, last failure " + last);
}

void LinkSupervisor::noteFailure(JoinFailure failure, uint8_t reason) {
  this->failedNetwork = this->tryingSsid;
  this->failure = failure;
  this->failureReason = reason;
}

void LinkSupervisor::clearFailure() {
  this->failedNetwork = "";
  this->failure = JoinFailure::NONE;
  this->failureReason = 0;
}

void LinkSupervisor::beginOutage(uint32_t nowMs) {
  this->outageBeganMs = nowMs;
  this->lastReportMs = nowMs;
  this->tries = 0;
}

void LinkSupervisor::startOver(uint32_t nowMs) {
  // From the first network, with the tries close together again, as after a
  // start. A wait for the radio to finish a try that has just ended is left
  // as it is.
  this->tryOpen = false;
  this->nextNetwork = 0;
  this->triesHere = 0;
  this->waitingForAddress = false;
  this->addressGivenUp = false;
  this->clearFailure();
  this->beginOutage(nowMs);
}

void LinkSupervisor::moveOn() {
  this->nextNetwork++;
  this->triesHere = 0;
}

uint32_t LinkSupervisor::retryWait(uint32_t nowMs) const {
  if (!this->ownOpen || nowMs - this->outageBeganMs < WIFI_OWN_AFTER_MS) {
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
  this->tryOpen = true;
  this->awaitingRetry = false;
  this->tryStartedMs = nowMs;
  // The station is on from here, and its own network goes where the station
  // goes, from channel to channel.
  this->ownAlone = false;
  this->radio->join(network.ssid, network.password);
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
  if (this->ownRefused && nowMs - this->ownRefusedMs < WIFI_OWN_RETRY_MS) {
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
    if (!this->ownRefused) {
      this->logger->warn(String("its own network ") + this->ownName +
                         " did not open under its password, trying again");
    }
    this->ownRefused = true;
    this->ownRefusedMs = doneMs;
    return;
  }
  if (alone) {
    this->radio->stopStation();
  }
  this->ownRefused = false;
  this->ownOpen = true;
  this->ownAlone = alone;
  this->clients = 0;
  this->addressShown = false;
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
  this->addressShown = false;
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
    this->addressShown = true;
    this->addressShownMs = nowMs;
  }
  this->clients = now;
  if (now > 0) {
    this->lingerFromMs = nowMs;
  } else {
    this->addressShown = false;
  }
  if (this->addressShown &&
      nowMs - this->addressShownMs >= WIFI_ADDRESS_SHOWN_MS) {
    this->addressShown = false;
  }
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
        this->tryOpen || this->waitingForAddress
            ? this->tryingSsid
            : this->networks[this->nextNetwork % this->networks.size()].ssid;
  }
  status.ownName = this->ownName;
  status.ownOpen = this->ownOpen;
  status.clients = this->clients;
  status.failedNetwork = this->failedNetwork;
  status.failure = this->failure;
  status.failureReason = this->failureReason;

  this->lock.lock();
  this->published = status;
  this->lock.unlock();

  // The screen: where the panel is, or failing that, how to get to it.
  ConnectionInfo info;
  if (status.station == StationLink::JOINED) {
    info.name = status.network;
    info.detail = status.address;
    info.qr = String("http://") + status.address;
  } else if (this->ownOpen && this->addressShown) {
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
  // The three channels that do not overlap each other, looked at from the
  // preferred one on, so that it is the one taken when none is quieter.
  const int channels[] = {1, 6, 11};
  int first = 0;
  for (int i = 0; i < 3; i++) {
    if (channels[i] == preferred) {
      first = i;
    }
  }
  int best = channels[first];
  long bestLoad = -1;
  for (int i = 0; i < 3; i++) {
    const int channel = channels[(first + i) % 3];
    long load = 0;
    for (const HeardNetwork& network : heard) {
      // A network is in the way of every channel fewer than five from its
      // own, less so the further off, and more so the louder it is here.
      const int apart = network.channel > channel ? network.channel - channel
                                                  : channel - network.channel;
      if (apart >= 5 || network.rssi <= -100) {
        continue;
      }
      load += (long)(network.rssi + 100) * (5 - apart);
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
  return 1 + 5 * (int)(sum % 3);
}
