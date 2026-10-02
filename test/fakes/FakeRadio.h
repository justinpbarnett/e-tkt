#pragma once

// The second adapter for the Radio interface in src/Radio.h.
//
// Esp32Radio is the first. This one has no radio. It has an air: the
// networks in reach, and how each one answers a machine that tries it. A try
// is played out against the virtual clock from test/stubs/Arduino.h, so a
// test can say that a network takes two seconds to let the machine in, or
// turns it away five times first, or never gives it an address, and then
// read what the link supervisor made of that.
//
// Everything the supervisor asked for is recorded, with the time it asked.

#include <deque>
#include <vector>

#include "Arduino.h"
#include "Radio.h"

/**
 * @brief A network in the air, and how it answers.
 */
struct FakeNetwork {
  String ssid = "";
  // Empty for an open network.
  String password = "";
  int channel = 6;
  int rssi = -60;
  // From the start of a try to the machine being on the network, or to the
  // network turning it away.
  uint32_t joinMs = 2000;
  // From there to the address.
  uint32_t addressMs = 1000;
  // False for a network that lets the machine on and never gives it one.
  bool givesAddress = true;
  String address = "192.168.1.50";
  // How many tries it turns away before it lets one in, as a weak link does,
  // and the reason it gives.
  int refusals = 0;
  uint8_t refusal = 15;
  // False for a network that leaves a try hanging: no answer either way.
  bool answers = true;
};

/** @brief A try the supervisor started. */
struct FakeJoin {
  String ssid = "";
  String password = "";
  unsigned long atMs = 0;
};

/** @brief A time the supervisor asked for the machine's own network. */
struct FakeOpening {
  String name = "";
  String password = "";
  int channel = 0;
  bool offerRouter = true;
  // Whether it opened.
  bool opened = false;
  unsigned long atMs = 0;
};

class FakeRadio : public Radio {
 private:
  // What is on its way: the machine getting onto the network, its address,
  // or the end of the try.
  struct Due {
    enum Kind { ASSOCIATE, ADDRESS, DROP };
    Kind kind;
    unsigned long atMs;
    uint8_t reason;
  };

  std::vector<Due> due;
  StationState state;
  // The network being tried or joined, as a place in the air. -1 for none.
  int current = -1;
  bool trying = false;
  unsigned long addressedAtMs = 0;

  void schedule(Due::Kind kind, unsigned long atMs, uint8_t reason) {
    Due next = {kind, atMs, reason};
    this->due.push_back(next);
  }

  void drop(uint8_t reason) {
    this->due.clear();
    this->trying = false;
    this->state.associated = false;
    this->state.addressed = false;
    this->state.lastReason = reason;
    this->state.drops++;
  }

  // Everything that has come due by now, in the order it happened.
  void settle() {
    while (!this->due.empty() && millis() >= this->due.front().atMs) {
      const Due next = this->due.front();
      this->due.erase(this->due.begin());
      switch (next.kind) {
        case Due::ASSOCIATE:
          this->trying = false;
          this->state.associated = true;
          break;
        case Due::ADDRESS:
          this->state.addressed = true;
          this->addressedAtMs = next.atMs;
          break;
        case Due::DROP:
          this->drop(next.reason);
          break;
      }
    }
  }

 public:
  // --- what a test scripts -------------------------------------------------

  /**
   * @brief The networks in reach. A deque, so that what add() and find()
   * hand out stays good as more are added.
   */
  std::deque<FakeNetwork> air;

  // How long the radio looks for a network that is not there before it says
  // so.
  uint32_t searchMs = 2500;

  // How long the adapter takes to publish an address after the event that
  // says it has one.
  uint32_t addressLagMs = 0;

  // How many phones are on the machine's own network, while it is open.
  int phones = 0;

  // True for a radio that will not open the machine's own network under a
  // password.
  bool accessPointFails = false;

  // How long listening on every channel takes, and whether it works.
  uint32_t surveyMs = 2500;
  bool surveyFails = false;

  // The network the firmware before this one left in the radio.
  String inheritedSsid = "";
  String inheritedPassword = "";

  // --- what it records -----------------------------------------------------

  std::vector<FakeJoin> joins;
  std::vector<FakeOpening> openings;
  int leaves = 0;
  int stops = 0;
  int closings = 0;
  int surveys = 0;
  bool stationOn = false;
  bool accessPointOpen = false;
  // A survey asked for while a try was under way, which the radio cannot do.
  bool surveyedDuringATry = false;

  /** @brief Puts a network in the air, and returns it to be scripted. */
  FakeNetwork* add(const String& ssid, const String& password) {
    FakeNetwork network;
    network.ssid = ssid;
    network.password = password;
    this->air.push_back(network);
    return &this->air.back();
  }

  /** @brief The network of that name in the air, or NULL. */
  FakeNetwork* find(const String& ssid) {
    for (size_t i = 0; i < this->air.size(); i++) {
      if (this->air[i].ssid == ssid) {
        return &this->air[i];
      }
    }
    return NULL;
  }

  /**
   * @brief The network the machine is on goes away, for this reason: 200 is
   * a beacon timeout, which is what walking out of reach looks like.
   */
  void lose(uint8_t reason) {
    this->settle();
    if (this->state.associated) {
      this->drop(reason);
    }
  }

  // --- Radio ---------------------------------------------------------------

  void join(const String& ssid, const String& password) override {
    this->settle();
    FakeJoin join;
    join.ssid = ssid;
    join.password = password;
    join.atMs = millis();
    this->joins.push_back(join);

    this->stationOn = true;
    this->due.clear();
    this->trying = true;
    this->current = -1;
    for (size_t i = 0; i < this->air.size(); i++) {
      if (this->air[i].ssid == ssid) {
        this->current = (int)i;
      }
    }
    if (this->current < 0) {
      // 201: no network of that name was found.
      this->schedule(Due::DROP, join.atMs + this->searchMs, 201);
      return;
    }
    FakeNetwork& network = this->air[this->current];
    if (!network.answers) {
      return;
    }
    if (network.password != password) {
      // 15: the handshake a wrong password never finishes.
      this->schedule(Due::DROP, join.atMs + network.joinMs, 15);
      return;
    }
    if (network.refusals > 0) {
      network.refusals--;
      this->schedule(Due::DROP, join.atMs + network.joinMs, network.refusal);
      return;
    }
    this->schedule(Due::ASSOCIATE, join.atMs + network.joinMs, 0);
    if (network.givesAddress) {
      this->schedule(Due::ADDRESS,
                     join.atMs + network.joinMs + network.addressMs, 0);
    }
  }

  void leave() override {
    this->settle();
    this->leaves++;
    if (this->state.associated || this->trying) {
      // 8: the machine left.
      this->drop(8);
    }
  }

  void stopStation() override {
    this->settle();
    this->stops++;
    if (this->state.associated || this->trying) {
      this->drop(8);
    }
    this->stationOn = false;
  }

  StationState station() override {
    this->settle();
    return this->state;
  }

  String address() override {
    this->settle();
    if (!this->state.addressed || this->current < 0 ||
        millis() - this->addressedAtMs < this->addressLagMs) {
      return String("0.0.0.0");
    }
    return this->air[this->current].address;
  }

  int channel() override {
    this->settle();
    return this->state.associated ? this->air[this->current].channel : 0;
  }

  int rssi() override {
    this->settle();
    return this->state.associated ? this->air[this->current].rssi : 0;
  }

  bool openAccessPoint(const String& name, const String& password, int channel,
                       bool offerRouter) override {
    FakeOpening opening;
    opening.name = name;
    opening.password = password;
    opening.channel = channel;
    opening.offerRouter = offerRouter;
    // WPA2 takes no password shorter than this, and the chip opens the
    // network without one when it is given one.
    opening.opened = password.length() >= 8 && !this->accessPointFails;
    opening.atMs = millis();
    this->openings.push_back(opening);
    this->accessPointOpen = opening.opened;
    return opening.opened;
  }

  void closeAccessPoint() override {
    this->closings++;
    this->accessPointOpen = false;
  }

  int clients() override { return this->accessPointOpen ? this->phones : 0; }

  bool survey(std::vector<HeardNetwork>* heard) override {
    this->settle();
    this->surveys++;
    if (this->trying) {
      this->surveyedDuringATry = true;
    }
    this->stationOn = true;
    delay(this->surveyMs);
    heard->clear();
    if (this->surveyFails) {
      return false;
    }
    for (size_t i = 0; i < this->air.size(); i++) {
      HeardNetwork network;
      network.ssid = this->air[i].ssid;
      network.channel = this->air[i].channel;
      network.rssi = this->air[i].rssi;
      network.secured = this->air[i].password.length() > 0;
      heard->push_back(network);
    }
    return true;
  }

  bool inherited(String* ssid, String* password) override {
    if (this->inheritedSsid.length() == 0) {
      return false;
    }
    *ssid = this->inheritedSsid;
    *password = this->inheritedPassword;
    return true;
  }
};
