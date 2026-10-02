#include "NetworkSettings.h"

#include <Arduino.h>
#include <Preferences.h>

#include <mutex>
#include <vector>

#include "ArduinoJson.h"
#include "JsonText.h"
#include "Logger.h"

// Where it all lives in EEPROM. The networks are one entry, written together
// as JSON: an entry each would be a write each, and a power cut between them
// would leave a name with the password of the network that had its place.
static const char* NETWORK_NAMESPACE = "network";
static const char* MODE_KEY = "mode";
static const char* NETWORKS_KEY = "networks";
static const char* PASSWORD_KEY = "password";
static const char* ROUTER_KEY = "router";
static const char* IMPORTED_KEY = "imported";

// The mode as it is stored.
static const uint32_t STORED_JOIN = 0;
static const uint32_t STORED_OWN = 1;

// What the password of the machine's own network is made of, and how much of
// it. It is read off a small screen and typed on a phone, so there are no
// capitals, and none of the letters and digits that pass for one another:
// no i, l or o, and no 0 or 1. Ten of these 31 are as hard to guess as 49
// coin flips, which is more than a network that reaches across a room, and
// prints labels, has to withstand.
static const char OWN_PASSWORD_ALPHABET[] = "abcdefghjkmnpqrstuvwxyz23456789";
static const int OWN_PASSWORD_LENGTH = 10;

// Room for the list of networks once parsed, without the text of it.
static const size_t NETWORKS_JSON_BYTES =
    JSON_ARRAY_SIZE(NetworkSettings::MAX_REMEMBERED) +
    NetworkSettings::MAX_REMEMBERED * JSON_OBJECT_SIZE(2);

// Whether the machine can keep a network under this name and password, and
// what is wrong with them if it cannot.
static RememberRefusal judged(const String& ssid, const String& password) {
  if (ssid.length() == 0) {
    return RememberRefusal::NAME_MISSING;
  }
  if (ssid.length() > (unsigned int)Radio::MAX_NAME_BYTES) {
    return RememberRefusal::NAME_TOO_LONG;
  }
  if (!NetworkSettings::nameIsText(ssid)) {
    return RememberRefusal::NAME_NOT_TEXT;
  }
  // No password at all is an open network.
  if (password.length() > 0 &&
      password.length() < (unsigned int)Radio::MIN_PASSWORD_LENGTH) {
    return RememberRefusal::PASSWORD_TOO_SHORT;
  }
  if (password.length() > (unsigned int)Radio::MAX_PASSWORD_LENGTH) {
    return RememberRefusal::PASSWORD_TOO_LONG;
  }
  return RememberRefusal::NONE;
}

// The networks as they are stored.
static String stored(const std::vector<RememberedNetwork>& networks) {
  DynamicJsonDocument doc(NETWORKS_JSON_BYTES);
  doc.to<JsonArray>();
  for (const RememberedNetwork& network : networks) {
    JsonObject entry = doc.createNestedObject();
    entry["ssid"] = network.ssid.c_str();
    entry["password"] = network.password.c_str();
  }
  return jsonText(doc);
}

// Reads stored networks into `networks`. Returns false, with none read, for
// anything that is not a list as stored() writes one. A network in the list
// that remember() would refuse is left out: this firmware keeps none, so it
// is some other firmware's, or flash gone bad.
static bool readStored(const String& text,
                       std::vector<RememberedNetwork>* networks) {
  // A text that is not JSON reads as a document with nothing in it, which
  // is no list.
  const DynamicJsonDocument doc = parsedJson(text, NETWORKS_JSON_BYTES);
  if (!doc.is<JsonArrayConst>()) {
    return false;
  }
  for (JsonVariantConst entry : doc.as<JsonArrayConst>()) {
    if (!entry["ssid"].is<const char*>() ||
        !entry["password"].is<const char*>()) {
      continue;
    }
    RememberedNetwork network;
    network.ssid = String(entry["ssid"].as<const char*>());
    network.password = String(entry["password"].as<const char*>());
    if (judged(network.ssid, network.password) == RememberRefusal::NONE &&
        networks->size() < (size_t)NetworkSettings::MAX_REMEMBERED) {
      networks->push_back(network);
    }
  }
  return true;
}

// A password for the machine's own network.
static String madePassword() {
  String password = "";
  for (int i = 0; i < OWN_PASSWORD_LENGTH; i++) {
    // random() draws on the chip's hardware generator, and evenly: no
    // character comes up more often than another.
    password += OWN_PASSWORD_ALPHABET[random(
        0, (long)sizeof(OWN_PASSWORD_ALPHABET) - 1)];
  }
  return password;
}

NetworkSettings::NetworkSettings(Logger* logger) { this->logger = logger; }

String NetworkSettings::namesOf(
    const std::vector<RememberedNetwork>& networks) {
  String names = "";
  for (size_t i = 0; i < networks.size(); i++) {
    if (i > 0) {
      names += i + 1 == networks.size() ? " or " : ", ";
    }
    names += networks[i].ssid;
  }
  return names;
}

bool NetworkSettings::nameIsText(const String& name) {
  const unsigned int length = name.length();
  unsigned int i = 0;
  while (i < length) {
    const uint8_t lead = (uint8_t)name.charAt(i);
    // How many bytes follow the first, and what the second may be: the
    // bounds are narrower after four of the first bytes, which is what
    // keeps out the long forms of short characters and the halves of pairs.
    unsigned int following = 0;
    uint8_t lowest = 0x80;
    uint8_t highest = 0xBF;
    if (lead < 0x80) {
      if (lead < 0x20 || lead == 0x7F) {
        return false;
      }
    } else if (lead >= 0xC2 && lead <= 0xDF) {
      following = 1;
    } else if (lead >= 0xE0 && lead <= 0xEF) {
      following = 2;
      lowest = lead == 0xE0 ? 0xA0 : 0x80;
      highest = lead == 0xED ? 0x9F : 0xBF;
    } else if (lead >= 0xF0 && lead <= 0xF4) {
      following = 3;
      lowest = lead == 0xF0 ? 0x90 : 0x80;
      highest = lead == 0xF4 ? 0x8F : 0xBF;
    } else {
      return false;
    }
    if (i + following >= length) {
      return false;
    }
    for (unsigned int k = 1; k <= following; k++) {
      const uint8_t next = (uint8_t)name.charAt(i + k);
      if (next < (k == 1 ? lowest : 0x80) || next > (k == 1 ? highest : 0xBF)) {
        return false;
      }
    }
    i += 1 + following;
  }
  return true;
}

void NetworkSettings::initialize(const String& machineId) {
  this->lock.lock();
  this->machineId = machineId;
  this->machineId.toUpperCase();

  this->preferences.begin(NETWORK_NAMESPACE, false);
  // A missing key is a machine that was never set up -- asked rather than
  // inferred from an empty text, as Settings does.
  const bool found = this->preferences.isKey(NETWORKS_KEY);
  const String text =
      found ? this->preferences.getString(NETWORKS_KEY, "") : String("");
  this->current = this->preferences.getUInt(MODE_KEY, STORED_JOIN) == STORED_OWN
                      ? NetworkMode::OWN
                      : NetworkMode::JOIN;
  this->offersRouter = this->preferences.getBool(ROUTER_KEY, true);
  this->ownNetworkPassword = this->preferences.getString(PASSWORD_KEY, "");
  this->importDone = this->preferences.getBool(IMPORTED_KEY, false);
  this->preferences.end();

  this->remembered.clear();
  const bool read = found && readStored(text, &this->remembered);
  // One that WPA2 could not use is not a password this code made. The next
  // one asked for is made new.
  if (this->ownNetworkPassword.length() <
          (unsigned int)Radio::MIN_PASSWORD_LENGTH ||
      this->ownNetworkPassword.length() >
          (unsigned int)Radio::MAX_PASSWORD_LENGTH) {
    this->ownNetworkPassword = "";
  }
  const NetworkMode mode = this->current;
  const String names = namesOf(this->remembered);
  const String ownName = String("E-TKT-") + this->machineId;
  this->lock.unlock();

  if (found && !read) {
    this->logger->warn("Ignored a stored list of networks that cannot be read");
  }
  if (mode == NetworkMode::OWN) {
    this->logger->log("Network: its own, " + ownName);
  } else if (names.length() > 0) {
    this->logger->log("Network: joins " + names);
  } else {
    this->logger->log("Network: joins one, and remembers none");
  }
}

NetworkMode NetworkSettings::mode() {
  this->lock.lock();
  const NetworkMode mode = this->current;
  this->lock.unlock();
  return mode;
}

void NetworkSettings::setMode(NetworkMode mode) {
  this->lock.lock();
  const bool changed = this->current != mode;
  if (changed) {
    this->current = mode;
    this->changeCount++;
    this->preferences.begin(NETWORK_NAMESPACE, false);
    this->preferences.putUInt(
        MODE_KEY, mode == NetworkMode::OWN ? STORED_OWN : STORED_JOIN);
    this->preferences.end();
  }
  this->lock.unlock();

  if (changed) {
    this->logger->log(mode == NetworkMode::OWN
                          ? "Network mode: its own network"
                          : "Network mode: joins a network");
  }
}

RememberRefusal NetworkSettings::remember(const String& ssid,
                                          const String& password) {
  const RememberRefusal refusal = judged(ssid, password);
  if (refusal != RememberRefusal::NONE) {
    return refusal;
  }

  RememberedNetwork added;
  added.ssid = ssid;
  added.password = password;

  this->lock.lock();
  std::vector<RememberedNetwork> next;
  next.push_back(added);
  for (const RememberedNetwork& network : this->remembered) {
    if (network.ssid != ssid) {
      next.push_back(network);
    }
  }
  if (next.size() > (size_t)MAX_REMEMBERED) {
    this->lock.unlock();
    return RememberRefusal::FULL;
  }
  // A network typed in again as it is kept already, and first, is the panel
  // sending it again over a slow link, or somebody asking for another try.
  // Nothing is written for it, and the link is still told.
  const bool same = !this->remembered.empty() &&
                    this->remembered[0].ssid == ssid &&
                    this->remembered[0].password == password;
  if (!same) {
    this->remembered = next;
    this->storeNetworks();
  }
  this->changeCount++;
  this->lock.unlock();

  this->logger->log("Remembered the network " + ssid);
  return RememberRefusal::NONE;
}

void NetworkSettings::forget(const String& ssid) {
  this->lock.lock();
  std::vector<RememberedNetwork> next;
  for (const RememberedNetwork& network : this->remembered) {
    if (network.ssid != ssid) {
      next.push_back(network);
    }
  }
  const bool changed = next.size() != this->remembered.size();
  if (changed) {
    this->remembered = next;
    this->changeCount++;
    this->storeNetworks();
  }
  this->lock.unlock();

  if (changed) {
    this->logger->log("Forgot the network " + ssid);
  }
}

std::vector<RememberedNetwork> NetworkSettings::networks() {
  this->lock.lock();
  const std::vector<RememberedNetwork> networks = this->remembered;
  this->lock.unlock();
  return networks;
}

void NetworkSettings::prefer(const String& ssid) {
  this->lock.lock();
  // Written only when the order changes, so a machine that joins the same
  // network every day writes nothing.
  for (size_t i = 1; i < this->remembered.size(); i++) {
    if (this->remembered[i].ssid == ssid) {
      const RememberedNetwork preferred = this->remembered[i];
      this->remembered.erase(this->remembered.begin() + i);
      this->remembered.insert(this->remembered.begin(), preferred);
      this->storeNetworks();
      break;
    }
  }
  this->lock.unlock();
}

String NetworkSettings::ownName() {
  this->lock.lock();
  const String name = String("E-TKT-") + this->machineId;
  this->lock.unlock();
  return name;
}

String NetworkSettings::hostName() {
  this->lock.lock();
  String name = String("e-tkt-") + this->machineId;
  this->lock.unlock();
  name.toLowerCase();
  return name;
}

String NetworkSettings::ownPassword() {
  this->lock.lock();
  if (this->ownNetworkPassword.length() == 0) {
    this->ownNetworkPassword = madePassword();
    this->preferences.begin(NETWORK_NAMESPACE, false);
    this->preferences.putString(PASSWORD_KEY, this->ownNetworkPassword.c_str());
    this->preferences.end();
  }
  const String password = this->ownNetworkPassword;
  this->lock.unlock();
  return password;
}

bool NetworkSettings::routerOffered() {
  this->lock.lock();
  const bool offered = this->offersRouter;
  this->lock.unlock();
  return offered;
}

void NetworkSettings::setRouterOffered(bool offered) {
  this->lock.lock();
  const bool changed = this->offersRouter != offered;
  if (changed) {
    this->offersRouter = offered;
    this->changeCount++;
    this->preferences.begin(NETWORK_NAMESPACE, false);
    this->preferences.putBool(ROUTER_KEY, offered);
    this->preferences.end();
  }
  this->lock.unlock();

  if (changed) {
    this->logger->log(offered ? "Its own network offers a router"
                              : "Its own network offers no router");
  }
}

bool NetworkSettings::imported() {
  this->lock.lock();
  const bool imported = this->importDone;
  this->lock.unlock();
  return imported;
}

void NetworkSettings::markImported() {
  this->lock.lock();
  if (!this->importDone) {
    this->importDone = true;
    this->preferences.begin(NETWORK_NAMESPACE, false);
    this->preferences.putBool(IMPORTED_KEY, true);
    this->preferences.end();
  }
  this->lock.unlock();
}

uint32_t NetworkSettings::revision() {
  this->lock.lock();
  const uint32_t revision = this->changeCount;
  this->lock.unlock();
  return revision;
}

void NetworkSettings::reset() {
  this->lock.lock();
  this->remembered.clear();
  this->current = NetworkMode::JOIN;
  this->offersRouter = true;
  this->ownNetworkPassword = "";
  this->importDone = true;
  this->changeCount++;
  this->storeNetworks();
  this->preferences.begin(NETWORK_NAMESPACE, false);
  this->preferences.putUInt(MODE_KEY, STORED_JOIN);
  this->preferences.putBool(ROUTER_KEY, true);
  // Empty is none: ownPassword() makes the next.
  this->preferences.putString(PASSWORD_KEY, "");
  // The radio's own copy of a network is wiped along with this. Should that
  // fail, what is left in it still does not come back.
  this->preferences.putBool(IMPORTED_KEY, true);
  this->preferences.end();
  this->lock.unlock();

  this->logger->log("Forgot every network");
}

void NetworkSettings::storeNetworks() {
  this->preferences.begin(NETWORK_NAMESPACE, false);
  this->preferences.putString(NETWORKS_KEY, stored(this->remembered).c_str());
  this->preferences.end();
}
