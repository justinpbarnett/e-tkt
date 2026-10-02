// Host-side tests for the link supervisor: what the machine does to be
// reachable, and what it does when it cannot join a network.
//
// This is the part of the firmware a weak network works hardest: tries that
// fail on the handshake, tries that never end, a network that takes the
// machine on and gives it no address. None of it could be checked without a
// machine and a network that misbehaves on demand. The radio here is a
// FakeRadio: an air of networks, each scripted to let the machine in, turn it
// away or leave it hanging, played against the virtual clock. The supervisor
// is stepped as the board's task steps it, every WIFI_STEP_MS.
//
// Run with:  pio test -e native
#include <unity.h>

#include <string>
#include <vector>

#include "Arduino.h"
#include "Configuration.h"
#include "FakeDisplay.h"
#include "FakeRadio.h"
#include "LinkSupervisor.h"
#include "Logger.h"
#include "NetworkSettings.h"

static Logger* logger;
static FakeRadio* radio;
static FakeDisplay* display;
static NetworkSettings* settings;
static LinkSupervisor* supervisor;

static const char* CHURCH_KEY = "battery staple";
static const char* BASEMENT_KEY = "correct horse";

// What the screen says about the machine's own network before a phone is on
// it. No generator stands in for the chip's here, so the password is ten of
// the first character it can be made of.
static const char* OWN_NAME = "E-TKT-9C4F";
static const char* OWN_KEY = "aaaaaaaaaa";
static const char* OWN_JOIN_CODE = "WIFI:T:WPA;S:E-TKT-9C4F;P:aaaaaaaaaa;;";

// What the machine keeps about its network, as it reads it at boot.
static void readSettings(void) {
  settings = new NetworkSettings(logger);
  settings->initialize("9C4F");
}

void setUp(void) {
  stubReset();
  logger = new Logger();
  radio = new FakeRadio();
  display = new FakeDisplay();
  readSettings();
  supervisor = NULL;
}

void tearDown(void) {
  delete supervisor;
  delete settings;
  delete display;
  delete radio;
  delete logger;
}

// The supervisor as the board starts it: built, and stepped once before
// anything else runs.
static void start(void) {
  supervisor = new LinkSupervisor(logger, radio, settings, display);
  supervisor->step();
}

// Lets time pass, with the supervisor stepped as its task steps it.
static void run(unsigned long ms) {
  for (unsigned long elapsed = 0; elapsed < ms; elapsed += WIFI_STEP_MS) {
    delay(WIFI_STEP_MS);
    supervisor->step();
  }
}

// The machine switched off and on again: a radio that knows nothing, and the
// settings read back from flash.
static void powerCycle(void) {
  delete supervisor;
  delete settings;
  delete radio;
  radio = new FakeRadio();
  readSettings();
}

// A machine on the church's network, which is the one network it remembers.
static void joinChurch(void) {
  settings->remember("Church", CHURCH_KEY);
  radio->add("Church", CHURCH_KEY);
  start();
  run(4000);
  TEST_ASSERT_TRUE(StationLink::JOINED == supervisor->status().station);
}

// How many lines of the log hold this text.
static int count(const char* text) {
  int n = 0;
  for (const std::string& line : stubSerialLines()) {
    if (line.find(text) != std::string::npos) {
      n++;
    }
  }
  return n;
}

static bool logged(const char* text) { return count(text) > 0; }

// What the idle screen was last told about how the machine is reached.
static void expectScreen(const char* name, const char* detail, const char* qr) {
  const DisplayCall* call = display->last(DisplayCall::CONNECTION_INFO);
  TEST_ASSERT_NOT_NULL(call);
  TEST_ASSERT_EQUAL_STRING(name, call->info.name.c_str());
  TEST_ASSERT_EQUAL_STRING(detail, call->info.detail.c_str());
  TEST_ASSERT_EQUAL_STRING(qr, call->info.qr.c_str());
}

// How long after the try before it the supervisor started this one.
static unsigned long gapBefore(size_t i) {
  return radio->joins[i].atMs - radio->joins[i - 1].atMs;
}

// --- joining the network it remembers --------------------------------------

void test_it_joins_the_network_it_remembers(void) {
  settings->remember("Church", CHURCH_KEY);
  radio->add("Church", CHURCH_KEY);

  start();
  run(4000);

  const LinkStatus status = supervisor->status();
  TEST_ASSERT_TRUE(StationLink::JOINED == status.station);
  TEST_ASSERT_EQUAL_STRING("Church", status.network.c_str());
  TEST_ASSERT_EQUAL_STRING("192.168.1.50", status.address.c_str());
  TEST_ASSERT_EQUAL_INT(1, (int)radio->joins.size());
  TEST_ASSERT_EQUAL_STRING(CHURCH_KEY, radio->joins[0].password.c_str());
  TEST_ASSERT_TRUE(
      logged("joined Church at 192.168.1.50 (channel 6, -60 dBm)"));
  // A join at the first try is not worth counting the tries of.
  TEST_ASSERT_FALSE(logged("tries in"));
}

void test_the_screen_says_where_the_panel_is(void) {
  joinChurch();

  // The network, the address to type, and the address for a camera.
  expectScreen("Church", "192.168.1.50", "http://192.168.1.50");
}

void test_starting_waits_for_nothing(void) {
  // The machine starts, and prints, with no network at all. Whatever starts
  // it takes the first step itself, so that step asks nothing of the radio
  // that takes time. It says what the machine is about to do.
  settings->remember("Church", CHURCH_KEY);

  start();

  TEST_ASSERT_EQUAL_UINT32(0, millis());
  TEST_ASSERT_EQUAL_INT(0, (int)radio->joins.size());
  TEST_ASSERT_EQUAL_INT(0, radio->surveys);
  const LinkStatus status = supervisor->status();
  TEST_ASSERT_TRUE(StationLink::JOINING == status.station);
  TEST_ASSERT_EQUAL_STRING("Church", status.network.c_str());
  expectScreen("Church", "joining", "");
}

void test_starting_with_no_network_to_join_waits_for_nothing_either(void) {
  start();

  TEST_ASSERT_EQUAL_UINT32(0, millis());
  TEST_ASSERT_EQUAL_INT(0, radio->surveys);
  TEST_ASSERT_EQUAL_INT(0, (int)radio->openings.size());
  TEST_ASSERT_TRUE(StationLink::OFF == supervisor->status().station);
  expectScreen(OWN_NAME, "starting", "");
}

void test_a_machine_that_has_joined_stays_joined(void) {
  // The firmware before this one took a machine that was on its network for
  // one still waiting for an address, and left the network two minutes
  // after every join.
  joinChurch();
  const size_t lines = stubSerialLines().size();
  const int told = display->countOf(DisplayCall::CONNECTION_INFO);

  run(10UL * 60 * 1000);

  TEST_ASSERT_EQUAL_INT(0, radio->leaves);
  TEST_ASSERT_EQUAL_INT(1, (int)radio->joins.size());
  TEST_ASSERT_TRUE(StationLink::JOINED == supervisor->status().station);
  // Nothing happened, so nothing is said: not in the log, and not to the
  // screen, which would draw itself again for it.
  TEST_ASSERT_EQUAL_INT((int)lines, (int)stubSerialLines().size());
  TEST_ASSERT_EQUAL_INT(told, display->countOf(DisplayCall::CONNECTION_INFO));
}

void test_a_weak_network_is_tried_until_it_lets_the_machine_in(void) {
  // The basement: a try fails on the handshake far more often than it
  // passes, and the core's own reconnect gives up on that reason. So every
  // try is the supervisor's, and it goes on for as long as it takes.
  settings->remember("Church", CHURCH_KEY);
  radio->add("Church", CHURCH_KEY)->refusals = 5;

  start();
  run(30000);

  TEST_ASSERT_EQUAL_INT(6, (int)radio->joins.size());
  for (size_t i = 1; i < radio->joins.size(); i++) {
    // A try takes two seconds to fail here, and the next waits
    // WIFI_RETRY_MS for the radio to be done with it.
    TEST_ASSERT_EQUAL_UINT32(2000 + WIFI_RETRY_MS, gapBefore(i));
  }
  TEST_ASSERT_TRUE(StationLink::JOINED == supervisor->status().station);
  TEST_ASSERT_TRUE(
      logged("joined Church at 192.168.1.50 (channel 6, -60 dBm) "
             "after 6 tries in 28 s"));
}

void test_a_try_that_never_ends_is_given_up(void) {
  // The radio has been seen to sit in a try and say nothing, not even that
  // it failed.
  settings->remember("Church", CHURCH_KEY);
  radio->add("Church", CHURCH_KEY)->answers = false;

  start();
  run(WIFI_TRY_MS);
  TEST_ASSERT_EQUAL_INT(0, radio->leaves);

  run(500);
  TEST_ASSERT_EQUAL_INT(1, radio->leaves);
  const LinkStatus status = supervisor->status();
  TEST_ASSERT_TRUE(JoinFailure::OTHER == status.failedTry.cause);
  TEST_ASSERT_EQUAL_UINT8(0, status.failedTry.reason);
  TEST_ASSERT_EQUAL_STRING("Church", status.failedTry.network.c_str());

  run(WIFI_RETRY_MS + 500);
  TEST_ASSERT_EQUAL_INT(2, (int)radio->joins.size());
  TEST_ASSERT_TRUE(gapBefore(1) >= WIFI_TRY_MS + WIFI_RETRY_MS);
}

void test_a_network_that_gives_no_address_is_left_after_two_minutes(void) {
  settings->remember("Church", CHURCH_KEY);
  radio->add("Church", CHURCH_KEY)->givesAddress = false;

  // On the network 2 s after the try started at the first step.
  start();
  run(WIFI_STEP_MS + 2000 + WIFI_DHCP_MS - WIFI_STEP_MS);
  TEST_ASSERT_EQUAL_INT(0, radio->leaves);
  TEST_ASSERT_EQUAL_INT(1, count("associated, waiting for an address"));

  run(2 * WIFI_STEP_MS);
  TEST_ASSERT_EQUAL_INT(1, radio->leaves);
  TEST_ASSERT_TRUE(logged("no address after 120 s, joining again"));
  TEST_ASSERT_TRUE(JoinFailure::NO_ADDRESS ==
                   supervisor->status().failedTry.cause);

  // And it is tried again, like any network that failed.
  run(WIFI_RETRY_BESIDE_OWN_MS + 1000);
  TEST_ASSERT_EQUAL_INT(2, (int)radio->joins.size());
}

void test_a_slow_address_is_waited_for(void) {
  // On the weak spot the address has taken the better part of a minute and
  // then come. Another try in that time would throw it away.
  settings->remember("Church", CHURCH_KEY);
  radio->add("Church", CHURCH_KEY)->addressMs = 51000;

  start();
  run(55000);

  TEST_ASSERT_TRUE(StationLink::JOINED == supervisor->status().station);
  TEST_ASSERT_EQUAL_INT(1, (int)radio->joins.size());
  TEST_ASSERT_EQUAL_INT(0, radio->leaves);
}

void test_an_address_the_radio_has_not_published_yet_is_not_shown(void) {
  // The radio says it has an address a moment before it can say which.
  settings->remember("Church", CHURCH_KEY);
  radio->add("Church", CHURCH_KEY);
  radio->addressLagMs = 600;

  start();
  run(5000);

  const LinkStatus status = supervisor->status();
  TEST_ASSERT_TRUE(StationLink::JOINED == status.station);
  TEST_ASSERT_EQUAL_STRING("192.168.1.50", status.address.c_str());
  // Said once it could be said with the address in it, and the screen was
  // never told of a network with nothing under its name.
  TEST_ASSERT_EQUAL_INT(1, count("joined Church"));
  TEST_ASSERT_TRUE(logged("joined Church at 192.168.1.50"));
  for (const DisplayCall& call : display->calls) {
    TEST_ASSERT_TRUE(call.info.detail.length() > 0);
  }
}

void test_a_lost_network_is_joined_again(void) {
  joinChurch();

  // 200: the network's beacons stopped coming.
  radio->lose(RADIO_REASON_BEACON_TIMEOUT);
  run(WIFI_STEP_MS);

  TEST_ASSERT_TRUE(logged("lost Church (BEACON_TIMEOUT), joining it again"));
  TEST_ASSERT_TRUE(StationLink::JOINING == supervisor->status().station);
  expectScreen("Church", "joining", "");

  run(WIFI_RETRY_MS + 4000);
  TEST_ASSERT_TRUE(StationLink::JOINED == supervisor->status().station);
  TEST_ASSERT_EQUAL_INT(2, (int)radio->joins.size());
  expectScreen("Church", "192.168.1.50", "http://192.168.1.50");
}

void test_the_screen_follows_a_new_address(void) {
  // The network hands the machine another address when its lease runs out.
  // The screen is the only place the address is written.
  joinChurch();

  radio->find("Church")->address = "192.168.1.77";
  run(WIFI_STEP_MS);

  TEST_ASSERT_EQUAL_STRING("192.168.1.77",
                           supervisor->status().address.c_str());
  expectScreen("Church", "192.168.1.77", "http://192.168.1.77");
}

// --- more than one network -------------------------------------------------

void test_the_networks_it_remembers_are_tried_in_turn(void) {
  // A machine carried between two places. It is in the basement today.
  settings->remember("Basement", BASEMENT_KEY);
  settings->remember("Church", CHURCH_KEY);
  radio->add("Basement", BASEMENT_KEY);

  start();
  run(10000);

  TEST_ASSERT_EQUAL_INT(2, (int)radio->joins.size());
  TEST_ASSERT_EQUAL_STRING("Church", radio->joins[0].ssid.c_str());
  TEST_ASSERT_EQUAL_STRING("Basement", radio->joins[1].ssid.c_str());
  TEST_ASSERT_EQUAL_STRING(BASEMENT_KEY, radio->joins[1].password.c_str());
  const LinkStatus status = supervisor->status();
  TEST_ASSERT_TRUE(StationLink::JOINED == status.station);
  TEST_ASSERT_EQUAL_STRING("Basement", status.network.c_str());
}

void test_a_network_that_is_there_is_tried_again_before_the_next_one(void) {
  // On a weak link most tries end with the network turning the machine
  // away, and the other network it remembers is one from another place. A
  // try spent on that one is a try the weak one did not get.
  settings->remember("Basement", BASEMENT_KEY);
  settings->remember("Church", CHURCH_KEY);
  radio->add("Church", CHURCH_KEY)->refusals = 2;

  start();
  run(15000);

  TEST_ASSERT_EQUAL_INT(3, (int)radio->joins.size());
  for (const FakeJoin& join : radio->joins) {
    TEST_ASSERT_EQUAL_STRING("Church", join.ssid.c_str());
  }
  TEST_ASSERT_TRUE(StationLink::JOINED == supervisor->status().station);
}

void test_a_network_that_keeps_turning_the_machine_away_gives_the_next_its_turn(
    void) {
  // Its password was changed, or typed wrong. The other one is fine.
  settings->remember("Basement", BASEMENT_KEY);
  settings->remember("Church", CHURCH_KEY);
  radio->add("Church", "another password");
  radio->add("Basement", BASEMENT_KEY);

  start();
  run(20000);

  TEST_ASSERT_EQUAL_INT((int)WIFI_TRIES_PER_NETWORK + 1,
                        (int)radio->joins.size());
  TEST_ASSERT_EQUAL_STRING("Basement", radio->joins.back().ssid.c_str());
  const LinkStatus status = supervisor->status();
  TEST_ASSERT_TRUE(StationLink::JOINED == status.station);
  TEST_ASSERT_EQUAL_STRING("Basement", status.network.c_str());
}

void test_the_network_it_joined_is_the_first_it_tries_the_next_time(void) {
  settings->remember("Basement", BASEMENT_KEY);
  settings->remember("Church", CHURCH_KEY);
  radio->add("Basement", BASEMENT_KEY);
  start();
  run(10000);

  powerCycle();
  radio->add("Basement", BASEMENT_KEY);
  start();
  run(4000);

  TEST_ASSERT_EQUAL_INT(1, (int)radio->joins.size());
  TEST_ASSERT_EQUAL_STRING("Basement", radio->joins[0].ssid.c_str());
  TEST_ASSERT_TRUE(StationLink::JOINED == supervisor->status().station);
}

void test_the_network_it_was_on_is_tried_first_after_a_loss(void) {
  joinChurch();
  // Somebody gets the machine ready for another place while it is on this
  // one. That is no reason to leave this one.
  settings->remember("Garage", "garage door");
  run(WIFI_SETTLE_MS + 1000);
  TEST_ASSERT_EQUAL_INT(0, radio->leaves);
  TEST_ASSERT_TRUE(StationLink::JOINED == supervisor->status().station);

  radio->lose(RADIO_REASON_BEACON_TIMEOUT);
  run(WIFI_RETRY_MS + 500);

  TEST_ASSERT_EQUAL_INT(2, (int)radio->joins.size());
  TEST_ASSERT_EQUAL_STRING("Church", radio->joins[1].ssid.c_str());
}

// --- saying what is going on -----------------------------------------------

void test_it_says_now_and_then_that_it_is_still_joining(void) {
  // The log is read over USB by somebody wondering why the machine is not
  // on the network. A line a try would bury what else it says.
  settings->remember("Church", CHURCH_KEY);

  start();
  run(WIFI_REPORT_MS);

  // A try at a network that is not there takes 2.5 s to fail, and the next
  // one starts WIFI_RETRY_MS after: eleven of them in the first minute.
  TEST_ASSERT_TRUE(logged(
      "still joining Church, 11 tries in 60 s, last failure NO_AP_FOUND"));
  TEST_ASSERT_EQUAL_INT(1, count("still joining"));

  run(WIFI_REPORT_MS);
  TEST_ASSERT_EQUAL_INT(2, count("still joining"));
}

void test_it_names_every_network_it_is_trying(void) {
  settings->remember("Basement", BASEMENT_KEY);
  settings->remember("Church", CHURCH_KEY);

  start();
  run(WIFI_REPORT_MS);

  TEST_ASSERT_TRUE(logged("still joining Church or Basement, "));
}

void test_it_says_why_the_last_try_failed(void) {
  // What the panel tells somebody standing at the machine to put right: the
  // name, the password, or where the machine stands.
  settings->remember("Church", CHURCH_KEY);

  start();
  run(3000);

  LinkStatus status = supervisor->status();
  TEST_ASSERT_TRUE(JoinFailure::NOT_FOUND == status.failedTry.cause);
  TEST_ASSERT_EQUAL_UINT8(RADIO_REASON_NO_AP_FOUND, status.failedTry.reason);
  TEST_ASSERT_EQUAL_STRING("Church", status.failedTry.network.c_str());

  // The network comes into reach, under another password than the one the
  // machine remembers.
  radio->add("Church", "another password");
  run(WIFI_RETRY_MS + 2500);

  status = supervisor->status();
  TEST_ASSERT_TRUE(JoinFailure::REFUSED == status.failedTry.cause);
  TEST_ASSERT_EQUAL_UINT8(RADIO_REASON_4WAY_HANDSHAKE_TIMEOUT,
                          status.failedTry.reason);
  TEST_ASSERT_EQUAL_STRING("Church", status.failedTry.network.c_str());
}

void test_a_failure_is_forgotten_once_the_machine_has_joined(void) {
  settings->remember("Church", CHURCH_KEY);
  radio->add("Church", CHURCH_KEY)->refusals = 1;

  start();
  run(3000);
  TEST_ASSERT_TRUE(JoinFailure::REFUSED ==
                   supervisor->status().failedTry.cause);

  run(6000);
  const LinkStatus status = supervisor->status();
  TEST_ASSERT_TRUE(StationLink::JOINED == status.station);
  TEST_ASSERT_TRUE(JoinFailure::NONE == status.failedTry.cause);
  TEST_ASSERT_EQUAL_STRING("", status.failedTry.network.c_str());
}

void test_the_reasons_a_try_fails_have_names(void) {
  TEST_ASSERT_EQUAL_STRING(
      "NO_AP_FOUND",
      LinkSupervisor::reasonText(RADIO_REASON_NO_AP_FOUND).c_str());
  TEST_ASSERT_EQUAL_STRING(
      "4WAY_HANDSHAKE_TIMEOUT",
      LinkSupervisor::reasonText(RADIO_REASON_4WAY_HANDSHAKE_TIMEOUT).c_str());
  TEST_ASSERT_EQUAL_STRING(
      "BEACON_TIMEOUT",
      LinkSupervisor::reasonText(RADIO_REASON_BEACON_TIMEOUT).c_str());
  // One the radio has no name for is still told apart, by its number.
  TEST_ASSERT_EQUAL_STRING("67", LinkSupervisor::reasonText(67).c_str());
}

void test_no_password_is_logged(void) {
  // The log is served to anybody who opens the panel.
  static unsigned long state;
  state = 12345;
  stubRandom() = [](long howsmall, long howbig) {
    state = state * 1103515245UL + 12345UL;
    return howsmall +
           (long)((state >> 16) % (unsigned long)(howbig - howsmall));
  };
  radio->inheritedSsid = "Basement";
  radio->inheritedPassword = BASEMENT_KEY;
  settings->remember("Church", CHURCH_KEY);
  radio->add("Church", "another password");

  start();
  run(WIFI_OWN_AFTER_MS + WIFI_REPORT_MS);

  const std::string own = settings->ownPassword().str();
  TEST_ASSERT_TRUE(supervisor->status().ownOpen);
  TEST_ASSERT_EQUAL_INT(10, (int)own.length());
  TEST_ASSERT_TRUE(logged("Church"));
  TEST_ASSERT_TRUE(logged("Basement"));
  TEST_ASSERT_FALSE(logged(CHURCH_KEY));
  TEST_ASSERT_FALSE(logged(BASEMENT_KEY));
  TEST_ASSERT_FALSE(logged(own.c_str()));
}

// --- its own network -------------------------------------------------------

void test_with_no_network_to_join_its_own_opens_at_once(void) {
  // A machine fresh from the bench, or after a reset at the button. Its own
  // network is the one way to tell it about another.
  start();
  run(WIFI_STEP_MS);

  TEST_ASSERT_EQUAL_INT(1, (int)radio->openings.size());
  TEST_ASSERT_EQUAL_STRING(OWN_NAME, radio->openings[0].name.c_str());
  TEST_ASSERT_EQUAL_STRING(OWN_KEY, radio->openings[0].password.c_str());
  TEST_ASSERT_TRUE(radio->accessPointOpen);
  // The radio is its own network's alone: nothing is tried, so nothing
  // takes it away from a phone.
  TEST_ASSERT_FALSE(radio->stationOn);
  TEST_ASSERT_EQUAL_INT(0, (int)radio->joins.size());
  const LinkStatus status = supervisor->status();
  TEST_ASSERT_TRUE(StationLink::OFF == status.station);
  TEST_ASSERT_TRUE(status.ownOpen);
  TEST_ASSERT_EQUAL_STRING(OWN_NAME, status.ownName.c_str());
  // All it waited for is the radio listening for a quiet channel.
  TEST_ASSERT_EQUAL_UINT32(WIFI_STEP_MS + radio->surveyMs, millis());
}

void test_its_own_network_opens_after_a_minute_without_the_one_it_remembers(
    void) {
  // The way in when the venue's network is down, or the machine is somewhere
  // new: nobody has to reset it to reach it.
  settings->remember("Church", CHURCH_KEY);

  start();
  run(WIFI_OWN_AFTER_MS - WIFI_STEP_MS);
  TEST_ASSERT_EQUAL_INT(0, (int)radio->openings.size());

  run(WIFI_STEP_MS);
  TEST_ASSERT_TRUE(radio->accessPointOpen);
  TEST_ASSERT_TRUE(supervisor->status().ownOpen);
  expectScreen(OWN_NAME, OWN_KEY, OWN_JOIN_CODE);
  // Beside the tries, which go on. The radio is not asked to listen for a
  // quiet channel: beside a station the network sits on the station's.
  TEST_ASSERT_TRUE(radio->stationOn);
  TEST_ASSERT_EQUAL_INT(0, radio->surveys);
  const size_t tries = radio->joins.size();
  run(2 * WIFI_RETRY_BESIDE_OWN_MS + 6000);
  TEST_ASSERT_TRUE(radio->joins.size() >= tries + 2);
  TEST_ASSERT_TRUE(StationLink::JOINING == supervisor->status().station);
}

void test_its_own_network_is_never_open_without_its_password(void) {
  // The portal this replaces opened its network with no password at all
  // when the radio turned the password down. This one stays shut, and is
  // asked for again.
  radio->accessPointFails = true;

  start();
  run(2 * WIFI_OWN_RETRY_MS + 3000);

  TEST_ASSERT_FALSE(radio->accessPointOpen);
  TEST_ASSERT_FALSE(supervisor->status().ownOpen);
  TEST_ASSERT_EQUAL_INT(3, (int)radio->openings.size());
  TEST_ASSERT_EQUAL_UINT32(WIFI_OWN_RETRY_MS,
                           radio->openings[1].atMs - radio->openings[0].atMs);
  TEST_ASSERT_EQUAL_INT(1, count("did not open under its password"));
  expectScreen(OWN_NAME, "starting", "");

  radio->accessPointFails = false;
  run(WIFI_OWN_RETRY_MS);

  TEST_ASSERT_TRUE(radio->accessPointOpen);
  TEST_ASSERT_TRUE(supervisor->status().ownOpen);
  expectScreen(OWN_NAME, OWN_KEY, OWN_JOIN_CODE);
}

void test_tries_are_spaced_out_while_its_own_network_is_open(void) {
  // Every try takes the radio off the channel its own network is on, and
  // whoever is on that network waits. A try at a network that is not there
  // takes 2.5 s.
  settings->remember("Church", CHURCH_KEY);

  start();
  run(WIFI_OWN_AFTER_MS);
  const size_t first = radio->joins.size();
  TEST_ASSERT_EQUAL_UINT32(2500 + WIFI_RETRY_MS, gapBefore(first - 1));

  // Open, with nobody on it.
  run(2 * (2500 + WIFI_RETRY_BESIDE_OWN_MS) + 1000);
  TEST_ASSERT_EQUAL_INT((int)first + 2, (int)radio->joins.size());
  TEST_ASSERT_EQUAL_UINT32(2500 + WIFI_RETRY_BESIDE_OWN_MS,
                           gapBefore(first + 1));

  // A phone joins it.
  radio->phones = 1;
  run(2500 + WIFI_RETRY_WHILE_CLIENT_MS);
  TEST_ASSERT_EQUAL_INT((int)first + 3, (int)radio->joins.size());
  TEST_ASSERT_EQUAL_UINT32(2500 + WIFI_RETRY_WHILE_CLIENT_MS,
                           gapBefore(first + 2));
}

void test_its_own_network_stays_open_for_as_long_as_no_network_is_joined(void) {
  settings->remember("Church", CHURCH_KEY);

  start();
  run(WIFI_OWN_AFTER_MS);
  run(10UL * 60 * 1000);

  TEST_ASSERT_EQUAL_INT(0, radio->closings);
  TEST_ASSERT_TRUE(supervisor->status().ownOpen);
}

void test_its_own_network_closes_a_minute_after_the_last_phone_has_left(void) {
  settings->remember("Church", CHURCH_KEY);
  start();
  run(WIFI_OWN_AFTER_MS);
  radio->phones = 1;
  // The venue's network comes back, and the machine joins it at its next
  // try.
  radio->add("Church", CHURCH_KEY);
  run(WIFI_RETRY_WHILE_CLIENT_MS + 10000);
  TEST_ASSERT_TRUE(StationLink::JOINED == supervisor->status().station);

  // The phone is still on the machine's own network, with the panel open.
  run(5UL * 60 * 1000);
  TEST_ASSERT_EQUAL_INT(0, radio->closings);
  TEST_ASSERT_TRUE(supervisor->status().ownOpen);

  radio->phones = 0;
  run(WIFI_OWN_LINGER_MS - 500);
  TEST_ASSERT_EQUAL_INT(0, radio->closings);

  run(1000);
  TEST_ASSERT_EQUAL_INT(1, radio->closings);
  TEST_ASSERT_FALSE(radio->accessPointOpen);
  const LinkStatus status = supervisor->status();
  TEST_ASSERT_FALSE(status.ownOpen);
  TEST_ASSERT_TRUE(StationLink::JOINED == status.station);
  expectScreen("Church", "192.168.1.50", "http://192.168.1.50");
}

void test_a_lost_network_is_not_tried_every_few_seconds_under_a_phone(void) {
  // The tries are close together after a change made on the panel, because
  // somebody is waiting to see whether it worked. Nobody is waiting after a
  // network was lost, and a phone on the machine's own network would lose
  // the radio every few seconds for it.
  settings->remember("Church", CHURCH_KEY);
  start();
  run(WIFI_OWN_AFTER_MS);
  radio->phones = 1;
  radio->add("Church", CHURCH_KEY);
  run(WIFI_RETRY_WHILE_CLIENT_MS + 10000);
  TEST_ASSERT_TRUE(StationLink::JOINED == supervisor->status().station);
  TEST_ASSERT_TRUE(supervisor->status().ownOpen);
  const size_t joined = radio->joins.size();

  // The venue's network goes, and stays away.
  radio->lose(RADIO_REASON_BEACON_TIMEOUT);
  radio->air.clear();
  run(WIFI_OWN_AFTER_MS);
  TEST_ASSERT_EQUAL_INT((int)joined, (int)radio->joins.size());

  run(2 * (2500 + WIFI_RETRY_WHILE_CLIENT_MS));
  TEST_ASSERT_EQUAL_INT((int)joined + 2, (int)radio->joins.size());
  TEST_ASSERT_EQUAL_UINT32(2500 + WIFI_RETRY_WHILE_CLIENT_MS,
                           gapBefore(joined + 1));
}

void test_the_screen_shows_how_to_join_its_own_network(void) {
  start();
  run(WIFI_STEP_MS);

  // The name and the password to read, and both for a camera: a phone joins
  // a network from a code of this form.
  expectScreen(OWN_NAME, OWN_KEY, OWN_JOIN_CODE);
}

void test_the_screen_shows_the_address_to_a_phone_that_has_just_joined(void) {
  // Joining the network is the first scan, and opening the panel is the
  // second. No page opens by itself.
  start();
  run(WIFI_STEP_MS);

  radio->phones = 1;
  run(WIFI_STEP_MS);
  expectScreen(OWN_NAME, "192.168.4.1", "http://192.168.4.1");
  TEST_ASSERT_EQUAL_INT(1, supervisor->status().clients);

  // Then the way in again, for the next phone.
  run(WIFI_ADDRESS_SHOWN_MS);
  expectScreen(OWN_NAME, OWN_KEY, OWN_JOIN_CODE);

  radio->phones = 2;
  run(WIFI_STEP_MS);
  expectScreen(OWN_NAME, "192.168.4.1", "http://192.168.4.1");

  // Nobody is left to show the address to.
  radio->phones = 0;
  run(WIFI_STEP_MS);
  expectScreen(OWN_NAME, OWN_KEY, OWN_JOIN_CODE);
  TEST_ASSERT_EQUAL_INT(0, supervisor->status().clients);
}

// --- own mode --------------------------------------------------------------

void test_in_own_mode_it_runs_its_own_network_and_joins_none(void) {
  // For a place whose network is no use to it: a thousand phones on it, or
  // a signal that comes and goes. The network it remembers is in reach, and
  // is left alone.
  settings->remember("Church", CHURCH_KEY);
  radio->add("Church", CHURCH_KEY);
  settings->setMode(NetworkMode::OWN);

  start();
  run(10000);

  TEST_ASSERT_EQUAL_INT(0, (int)radio->joins.size());
  TEST_ASSERT_TRUE(radio->accessPointOpen);
  TEST_ASSERT_FALSE(radio->stationOn);
  const LinkStatus status = supervisor->status();
  TEST_ASSERT_TRUE(StationLink::OFF == status.station);
  TEST_ASSERT_TRUE(status.ownOpen);
  expectScreen(OWN_NAME, OWN_KEY, OWN_JOIN_CODE);
}

void test_its_own_network_goes_on_the_quietest_channel(void) {
  settings->setMode(NetworkMode::OWN);
  FakeNetwork* near = radio->add("Near", "password1");
  near->channel = 1;
  near->rssi = -45;
  FakeNetwork* next = radio->add("Next", "password2");
  next->channel = 6;
  next->rssi = -50;

  start();
  run(WIFI_STEP_MS);

  TEST_ASSERT_EQUAL_INT(11, radio->openings[0].channel);
  TEST_ASSERT_EQUAL_INT(1, radio->surveys);
}

static HeardNetwork heardOn(int channel, int rssi) {
  HeardNetwork network;
  network.ssid = "heard";
  network.channel = channel;
  network.rssi = rssi;
  return network;
}

void test_the_quietest_channel_has_the_least_on_it_and_around_it(void) {
  std::vector<HeardNetwork> air;
  // Nothing heard: the machine's own choice.
  TEST_ASSERT_EQUAL_INT(6, LinkSupervisor::quietestChannel(air, 6));

  air = {heardOn(1, -45), heardOn(6, -50)};
  TEST_ASSERT_EQUAL_INT(11, LinkSupervisor::quietestChannel(air, 1));

  // A network between two channels is in the way of both.
  air = {heardOn(3, -50)};
  TEST_ASSERT_EQUAL_INT(11, LinkSupervisor::quietestChannel(air, 1));
  TEST_ASSERT_EQUAL_INT(11, LinkSupervisor::quietestChannel(air, 6));

  // A loud network counts for more than a faint one.
  air = {heardOn(1, -40), heardOn(6, -85), heardOn(11, -60)};
  TEST_ASSERT_EQUAL_INT(6, LinkSupervisor::quietestChannel(air, 1));

  // As quiet as each other: the machine's own choice again.
  air = {heardOn(1, -60), heardOn(6, -60), heardOn(11, -60)};
  TEST_ASSERT_EQUAL_INT(1, LinkSupervisor::quietestChannel(air, 1));
  TEST_ASSERT_EQUAL_INT(6, LinkSupervisor::quietestChannel(air, 6));
  TEST_ASSERT_EQUAL_INT(11, LinkSupervisor::quietestChannel(air, 11));
}

void test_with_nothing_heard_machines_still_spread_over_the_channels(void) {
  // Three machines in one room, each with a network of its own.
  const int one = LinkSupervisor::preferredChannel("E-TKT-0001");
  const int two = LinkSupervisor::preferredChannel("E-TKT-0002");
  const int three = LinkSupervisor::preferredChannel("E-TKT-0003");
  TEST_ASSERT_TRUE(one != two && two != three && one != three);
  TEST_ASSERT_TRUE(one == 1 || one == 6 || one == 11);
  TEST_ASSERT_TRUE(two == 1 || two == 6 || two == 11);
  TEST_ASSERT_TRUE(three == 1 || three == 6 || three == 11);

  // A radio that could not listen leaves the machine with that choice.
  settings->setMode(NetworkMode::OWN);
  radio->surveyFails = true;
  start();
  run(WIFI_STEP_MS);

  TEST_ASSERT_TRUE(radio->accessPointOpen);
  TEST_ASSERT_EQUAL_INT(LinkSupervisor::preferredChannel(OWN_NAME),
                        radio->openings[0].channel);
}

// --- following what the panel changes --------------------------------------

void test_a_change_of_mode_is_followed_once_it_has_settled(void) {
  // The panel's request travels on the link the change cuts. It is answered
  // first, and a second change of mind inside the wait costs nothing.
  joinChurch();

  settings->setMode(NetworkMode::OWN);
  run(WIFI_SETTLE_MS);
  TEST_ASSERT_EQUAL_INT(0, radio->leaves);
  TEST_ASSERT_TRUE(StationLink::JOINED == supervisor->status().station);

  run(1000);
  TEST_ASSERT_EQUAL_INT(1, radio->leaves);
  TEST_ASSERT_TRUE(logged("left Church"));
  TEST_ASSERT_FALSE(logged("lost Church"));
  TEST_ASSERT_TRUE(radio->accessPointOpen);
  TEST_ASSERT_FALSE(radio->stationOn);
  const LinkStatus status = supervisor->status();
  TEST_ASSERT_TRUE(StationLink::OFF == status.station);
  TEST_ASSERT_TRUE(status.ownOpen);
  TEST_ASSERT_EQUAL_STRING("", status.address.c_str());
}

void test_a_change_to_own_mode_ends_the_try_under_way(void) {
  settings->remember("Church", CHURCH_KEY);
  radio->add("Church", CHURCH_KEY)->answers = false;
  start();
  run(1000);
  TEST_ASSERT_EQUAL_INT(1, (int)radio->joins.size());

  settings->setMode(NetworkMode::OWN);
  run(WIFI_SETTLE_MS + 1000);

  TEST_ASSERT_EQUAL_INT(1, radio->leaves);
  // The radio cannot listen for a quiet channel with a try under way.
  TEST_ASSERT_FALSE(radio->surveyedDuringATry);
  TEST_ASSERT_TRUE(radio->accessPointOpen);
  TEST_ASSERT_FALSE(radio->stationOn);

  run(2UL * 60 * 1000);
  TEST_ASSERT_EQUAL_INT(1, (int)radio->joins.size());
}

void test_going_back_to_joining_keeps_its_own_network_open_until_it_has_joined(
    void) {
  // Whoever asks for the change is on the machine's own network, and has to
  // see how the change went.
  settings->remember("Church", CHURCH_KEY);
  radio->add("Church", CHURCH_KEY);
  settings->setMode(NetworkMode::OWN);
  start();
  run(1000);
  radio->phones = 1;
  run(1000);

  settings->setMode(NetworkMode::JOIN);
  run(WIFI_SETTLE_MS + 5000);

  LinkStatus status = supervisor->status();
  TEST_ASSERT_TRUE(StationLink::JOINED == status.station);
  TEST_ASSERT_EQUAL_STRING("Church", status.network.c_str());
  TEST_ASSERT_TRUE(status.ownOpen);
  TEST_ASSERT_EQUAL_INT(0, radio->closings);

  // They move over to the church's network.
  radio->phones = 0;
  run(WIFI_OWN_LINGER_MS + 1000);

  status = supervisor->status();
  TEST_ASSERT_FALSE(status.ownOpen);
  TEST_ASSERT_TRUE(StationLink::JOINED == status.station);
}

void test_a_network_typed_in_is_tried_within_seconds(void) {
  // Somebody stands at the machine with a phone on its own network and
  // types in the network it should be on. They are waiting to see it work,
  // and the tries are five minutes apart by then.
  settings->remember("Church", CHURCH_KEY);
  start();
  run(WIFI_OWN_AFTER_MS + 5000);
  radio->phones = 1;
  run(5000);

  settings->remember("Basement", BASEMENT_KEY);
  radio->add("Basement", BASEMENT_KEY);
  run(WIFI_SETTLE_MS + WIFI_RETRY_MS + 4000);

  const LinkStatus status = supervisor->status();
  TEST_ASSERT_TRUE(StationLink::JOINED == status.station);
  TEST_ASSERT_EQUAL_STRING("Basement", status.network.c_str());
  TEST_ASSERT_EQUAL_STRING("Basement", radio->joins.back().ssid.c_str());
}

void test_a_network_typed_in_again_is_tried_again_at_once(void) {
  // The panel's "try again": the same network, sent as it is kept.
  settings->remember("Church", CHURCH_KEY);
  start();
  run(WIFI_OWN_AFTER_MS + 5000);
  radio->phones = 1;
  run(5000);
  const size_t tries = radio->joins.size();

  radio->add("Church", CHURCH_KEY);
  settings->remember("Church", CHURCH_KEY);
  run(WIFI_SETTLE_MS + WIFI_RETRY_MS + 4000);

  TEST_ASSERT_EQUAL_INT((int)tries + 1, (int)radio->joins.size());
  TEST_ASSERT_TRUE(StationLink::JOINED == supervisor->status().station);
}

void test_forgetting_the_network_it_is_on_leaves_it(void) {
  joinChurch();

  settings->forget("Church");
  run(WIFI_SETTLE_MS + 500);

  TEST_ASSERT_EQUAL_INT(1, radio->leaves);
  TEST_ASSERT_TRUE(logged("left Church"));
  TEST_ASSERT_FALSE(logged("lost Church"));

  // With none left to join, its own network is the way in.
  run(1000);
  const LinkStatus status = supervisor->status();
  TEST_ASSERT_TRUE(StationLink::OFF == status.station);
  TEST_ASSERT_TRUE(status.ownOpen);
  TEST_ASSERT_FALSE(radio->surveyedDuringATry);
}

void test_forgetting_the_network_it_is_on_tries_the_next_within_seconds(void) {
  // Whoever forgot it is on the machine's own network, waiting to see where
  // the machine goes next, and a lost network is five minutes from its next
  // try with a phone on that one.
  settings->remember("Basement", BASEMENT_KEY);
  settings->remember("Church", CHURCH_KEY);
  radio->add("Church", CHURCH_KEY);
  settings->setMode(NetworkMode::OWN);
  start();
  run(1000);
  radio->phones = 1;
  settings->setMode(NetworkMode::JOIN);
  run(WIFI_SETTLE_MS + 15000);
  TEST_ASSERT_TRUE(StationLink::JOINED == supervisor->status().station);
  TEST_ASSERT_EQUAL_STRING("Church", supervisor->status().network.c_str());
  TEST_ASSERT_TRUE(supervisor->status().ownOpen);

  radio->add("Basement", BASEMENT_KEY);
  settings->forget("Church");
  run(WIFI_SETTLE_MS + WIFI_RETRY_MS + 5000);

  const LinkStatus status = supervisor->status();
  TEST_ASSERT_TRUE(StationLink::JOINED == status.station);
  TEST_ASSERT_EQUAL_STRING("Basement", status.network.c_str());
}

void test_forgetting_another_network_does_not_disturb_the_one_it_is_on(void) {
  settings->remember("Basement", BASEMENT_KEY);
  joinChurch();

  settings->forget("Basement");
  run(WIFI_SETTLE_MS + 1000);

  TEST_ASSERT_EQUAL_INT(0, radio->leaves);
  TEST_ASSERT_EQUAL_INT(1, (int)radio->joins.size());
  TEST_ASSERT_TRUE(StationLink::JOINED == supervisor->status().station);
}

void test_a_change_to_the_router_option_opens_its_own_network_again(void) {
  // A phone is told whether the network is a way to the internet as it
  // joins, so the phones on it have to join again to hear of the change.
  start();
  run(WIFI_STEP_MS);
  TEST_ASSERT_TRUE(radio->openings[0].offerRouter);

  settings->setRouterOffered(false);
  run(WIFI_SETTLE_MS + 1000);

  TEST_ASSERT_EQUAL_INT(1, radio->closings);
  TEST_ASSERT_EQUAL_INT(2, (int)radio->openings.size());
  TEST_ASSERT_FALSE(radio->openings[1].offerRouter);
  TEST_ASSERT_TRUE(radio->accessPointOpen);
  // On the channel it was on, without listening again.
  TEST_ASSERT_EQUAL_INT(radio->openings[0].channel, radio->openings[1].channel);
  TEST_ASSERT_EQUAL_INT(1, radio->surveys);
}

// --- the networks in reach -------------------------------------------------

void test_it_says_which_networks_are_in_reach_when_the_panel_asks(void) {
  // The panel's list of networks to pick from. The loudest comes first: the
  // network a machine is to be on is most likely the one it stands nearest.
  joinChurch();
  radio->add("Hall", "password1")->rssi = -45;
  radio->add("Cafe", "")->rssi = -80;

  TEST_ASSERT_EQUAL_UINT32(0, supervisor->listen());
  TEST_ASSERT_TRUE(supervisor->nearby().listening);
  run(WIFI_STEP_MS);

  const NearbyNetworks nearby = supervisor->nearby();
  TEST_ASSERT_FALSE(nearby.listening);
  TEST_ASSERT_EQUAL_UINT32(1, nearby.listens);
  TEST_ASSERT_EQUAL_INT(3, (int)nearby.networks.size());
  TEST_ASSERT_EQUAL_STRING("Hall", nearby.networks[0].ssid.c_str());
  TEST_ASSERT_EQUAL_INT(-45, nearby.networks[0].rssi);
  TEST_ASSERT_TRUE(nearby.networks[0].secured);
  TEST_ASSERT_EQUAL_STRING("Church", nearby.networks[1].ssid.c_str());
  TEST_ASSERT_EQUAL_STRING("Cafe", nearby.networks[2].ssid.c_str());
  TEST_ASSERT_FALSE(nearby.networks[2].secured);
  TEST_ASSERT_TRUE(logged("heard 3 networks in reach"));
  // The machine is still on its network.
  TEST_ASSERT_EQUAL_INT(0, radio->leaves);
  TEST_ASSERT_TRUE(StationLink::JOINED == supervisor->status().station);
}

void test_it_listens_only_when_it_is_asked(void) {
  // Listening takes the radio off its channel for seconds, and whoever is
  // talking to the machine waits that long.
  joinChurch();
  run(10UL * 60 * 1000);
  TEST_ASSERT_EQUAL_INT(0, radio->surveys);
  TEST_ASSERT_FALSE(supervisor->nearby().listening);
  TEST_ASSERT_EQUAL_UINT32(0, supervisor->nearby().listens);

  supervisor->listen();
  run(10UL * 60 * 1000);
  TEST_ASSERT_EQUAL_INT(1, radio->surveys);
  TEST_ASSERT_EQUAL_UINT32(1, supervisor->nearby().listens);
}

void test_the_next_listen_says_what_is_in_reach_by_then(void) {
  // The panel waits for a listen that ended after it asked, and knows it by
  // the count. Until then the list is the one from before.
  joinChurch();
  supervisor->listen();
  run(WIFI_STEP_MS);
  TEST_ASSERT_EQUAL_INT(1, (int)supervisor->nearby().networks.size());

  radio->add("Hall", "password1");
  TEST_ASSERT_EQUAL_UINT32(1, supervisor->listen());
  TEST_ASSERT_EQUAL_UINT32(1, supervisor->nearby().listens);
  TEST_ASSERT_EQUAL_INT(1, (int)supervisor->nearby().networks.size());
  run(WIFI_STEP_MS);

  const NearbyNetworks nearby = supervisor->nearby();
  TEST_ASSERT_EQUAL_UINT32(2, nearby.listens);
  TEST_ASSERT_EQUAL_INT(2, (int)nearby.networks.size());
}

void test_it_does_not_listen_while_a_try_is_under_way(void) {
  // The radio cannot do both, and a try cut short is a try wasted. The list
  // comes when the try has ended, and before the next one starts.
  settings->remember("Church", CHURCH_KEY);
  radio->add("Church", CHURCH_KEY)->answers = false;
  start();
  run(WIFI_STEP_MS);
  TEST_ASSERT_EQUAL_INT(1, (int)radio->joins.size());

  supervisor->listen();
  run(WIFI_TRY_MS);
  TEST_ASSERT_EQUAL_INT(1, radio->leaves);
  TEST_ASSERT_EQUAL_INT(0, radio->surveys);
  TEST_ASSERT_TRUE(supervisor->nearby().listening);

  // Whatever ended, the radio gets WIFI_RETRY_MS to be done with it, from
  // the step that saw it end.
  run(WIFI_RETRY_MS);
  TEST_ASSERT_EQUAL_INT(0, radio->surveys);

  run(WIFI_STEP_MS);
  TEST_ASSERT_EQUAL_INT(1, radio->surveys);
  TEST_ASSERT_FALSE(radio->surveyedDuringATry);
  TEST_ASSERT_EQUAL_INT(1, (int)radio->joins.size());
  TEST_ASSERT_EQUAL_UINT32(1, supervisor->nearby().listens);

  // And the tries go on.
  run(WIFI_STEP_MS);
  TEST_ASSERT_EQUAL_INT(2, (int)radio->joins.size());
}

void test_it_does_not_listen_while_the_machine_waits_for_its_address(void) {
  // On a weak link the address takes the better part of a minute, and a
  // radio that is off listening is not there to be given it.
  settings->remember("Church", CHURCH_KEY);
  radio->add("Church", CHURCH_KEY)->addressMs = 40000;
  start();
  run(5000);

  supervisor->listen();
  run(30000);
  TEST_ASSERT_EQUAL_INT(0, radio->surveys);

  run(10000);
  TEST_ASSERT_TRUE(StationLink::JOINED == supervisor->status().station);
  TEST_ASSERT_EQUAL_INT(1, radio->surveys);
  TEST_ASSERT_EQUAL_INT(0, radio->leaves);
}

void test_listening_leaves_the_radio_to_its_own_network_again(void) {
  // With nothing to join, the radio is its own network's alone. Listening
  // turns the station on, and it is turned off again.
  radio->add("Church", CHURCH_KEY);
  start();
  run(WIFI_STEP_MS);
  radio->phones = 1;
  run(1000);
  TEST_ASSERT_FALSE(radio->stationOn);

  supervisor->listen();
  run(WIFI_STEP_MS);

  TEST_ASSERT_EQUAL_INT(1, (int)supervisor->nearby().networks.size());
  TEST_ASSERT_FALSE(radio->stationOn);
  // Its own network is as it was, with the phone that asked still on it.
  TEST_ASSERT_TRUE(radio->accessPointOpen);
  TEST_ASSERT_EQUAL_INT(1, (int)radio->openings.size());
  TEST_ASSERT_EQUAL_INT(0, radio->closings);
  TEST_ASSERT_EQUAL_INT(1, supervisor->status().clients);
}

void test_listening_between_tries_leaves_the_station_on(void) {
  // Beside the tries the station is on anyway, and the next try needs it.
  settings->remember("Church", CHURCH_KEY);
  start();
  run(WIFI_OWN_AFTER_MS + 10000);
  TEST_ASSERT_TRUE(supervisor->status().ownOpen);
  const int stops = radio->stops;

  supervisor->listen();
  run(WIFI_RETRY_BESIDE_OWN_MS);

  TEST_ASSERT_EQUAL_UINT32(1, supervisor->nearby().listens);
  TEST_ASSERT_EQUAL_INT(stops, radio->stops);
  TEST_ASSERT_TRUE(radio->stationOn);
  TEST_ASSERT_FALSE(radio->surveyedDuringATry);
}

void test_the_list_has_each_name_once_and_only_names_that_can_be_picked(void) {
  joinChurch();
  // A network with several access points is heard once for each of them. It
  // is listed once, as loud as the loudest.
  radio->add("Church", CHURCH_KEY)->rssi = -40;
  radio->add("Church", CHURCH_KEY)->rssi = -75;
  // A network that hides its name can still be typed in.
  radio->add("", "password1")->rssi = -30;
  // A name is 32 bytes of anything. One that is not text cannot be shown, and
  // the panel could not send it back as it is.
  radio->add("Line\nbreak", "")->rssi = -31;
  radio->add("Caf\xE9", "")->rssi = -32;
  radio->add("Cut short \xE2\x82", "")->rssi = -33;
  radio->add("Caf\xC3\xA9 \xE2\x82\xAC \xF0\x9F\x93\xB6", "")->rssi = -50;

  supervisor->listen();
  run(WIFI_STEP_MS);

  const NearbyNetworks nearby = supervisor->nearby();
  TEST_ASSERT_EQUAL_INT(2, (int)nearby.networks.size());
  TEST_ASSERT_EQUAL_STRING("Church", nearby.networks[0].ssid.c_str());
  TEST_ASSERT_EQUAL_INT(-40, nearby.networks[0].rssi);
  TEST_ASSERT_EQUAL_STRING("Caf\xC3\xA9 \xE2\x82\xAC \xF0\x9F\x93\xB6",
                           nearby.networks[1].ssid.c_str());
}

void test_the_list_is_the_loudest_networks_and_no_more(void) {
  // A church has dozens of networks in reach, and the reply that lists them
  // has to fit. The far ones are the least likely to be wanted, and any name
  // can still be typed in.
  joinChurch();
  for (int i = 0; i < 30; i++) {
    radio->add(String("Net ") + String(i), "password1")->rssi = -59 + i;
  }

  supervisor->listen();
  run(WIFI_STEP_MS);

  const NearbyNetworks nearby = supervisor->nearby();
  TEST_ASSERT_EQUAL_INT((int)LinkSupervisor::MAX_NEARBY,
                        (int)nearby.networks.size());
  // The network the machine is on is further off than any of them.
  TEST_ASSERT_EQUAL_STRING("Net 29", nearby.networks[0].ssid.c_str());
  TEST_ASSERT_EQUAL_INT(-30, nearby.networks[0].rssi);
  TEST_ASSERT_EQUAL_STRING("Net 18", nearby.networks.back().ssid.c_str());
  TEST_ASSERT_TRUE(logged("heard 31 networks in reach"));
}

void test_a_radio_that_could_not_listen_still_ends_the_wait(void) {
  // The panel is waiting for the count to go up. It gets an empty list, and
  // the name can be typed in.
  joinChurch();
  supervisor->listen();
  run(WIFI_STEP_MS);
  TEST_ASSERT_EQUAL_INT(1, (int)supervisor->nearby().networks.size());

  radio->surveyFails = true;
  supervisor->listen();
  run(WIFI_STEP_MS);

  const NearbyNetworks nearby = supervisor->nearby();
  TEST_ASSERT_FALSE(nearby.listening);
  TEST_ASSERT_EQUAL_UINT32(2, nearby.listens);
  TEST_ASSERT_EQUAL_INT(0, (int)nearby.networks.size());
  TEST_ASSERT_TRUE(logged("could not listen for the networks in reach"));
}

void test_asking_again_while_it_listens_does_not_make_it_listen_twice(void) {
  // A reply lost on a slow link has the panel ask again. The listen under
  // way answers both.
  joinChurch();
  supervisor->listen();
  bool askedAgain = false;
  uint32_t ended = 99;
  stubAfterDelay() = [&]() {
    // Partway through the radio's listen, as the webserver's task would.
    if (radio->surveys == 1 && !askedAgain) {
      askedAgain = true;
      ended = supervisor->listen();
    }
  };
  run(WIFI_STEP_MS);
  stubAfterDelay() = nullptr;

  TEST_ASSERT_TRUE(askedAgain);
  TEST_ASSERT_EQUAL_UINT32(0, ended);
  const NearbyNetworks nearby = supervisor->nearby();
  TEST_ASSERT_FALSE(nearby.listening);
  TEST_ASSERT_EQUAL_UINT32(1, nearby.listens);
  run(10000);
  TEST_ASSERT_EQUAL_INT(1, radio->surveys);
}

// --- the firmware before this one ------------------------------------------

void test_a_machine_updated_from_the_firmware_before_keeps_its_network(void) {
  // That firmware kept its one network in the radio itself. A machine that
  // is updated over the network has to come back on the network.
  radio->inheritedSsid = "Church";
  radio->inheritedPassword = CHURCH_KEY;
  radio->add("Church", CHURCH_KEY);

  start();
  run(4000);

  const LinkStatus status = supervisor->status();
  TEST_ASSERT_TRUE(StationLink::JOINED == status.station);
  TEST_ASSERT_EQUAL_STRING("Church", status.network.c_str());
  TEST_ASSERT_EQUAL_INT(1, (int)settings->networks().size());
  TEST_ASSERT_EQUAL_STRING("Church", settings->networks()[0].ssid.c_str());
}

void test_a_network_forgotten_on_purpose_does_not_come_back(void) {
  // The radio's copy is left where it is, so that the firmware before this
  // one still finds its network if it is ever put back on the machine.
  radio->inheritedSsid = "Church";
  radio->inheritedPassword = CHURCH_KEY;
  start();
  settings->forget("Church");

  powerCycle();
  radio->inheritedSsid = "Church";
  radio->inheritedPassword = CHURCH_KEY;
  radio->add("Church", CHURCH_KEY);
  start();
  run(4000);

  TEST_ASSERT_EQUAL_INT(0, (int)settings->networks().size());
  TEST_ASSERT_EQUAL_INT(0, (int)radio->joins.size());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_it_joins_the_network_it_remembers);
  RUN_TEST(test_the_screen_says_where_the_panel_is);
  RUN_TEST(test_starting_waits_for_nothing);
  RUN_TEST(test_starting_with_no_network_to_join_waits_for_nothing_either);
  RUN_TEST(test_a_machine_that_has_joined_stays_joined);
  RUN_TEST(test_a_weak_network_is_tried_until_it_lets_the_machine_in);
  RUN_TEST(test_a_try_that_never_ends_is_given_up);
  RUN_TEST(test_a_network_that_gives_no_address_is_left_after_two_minutes);
  RUN_TEST(test_a_slow_address_is_waited_for);
  RUN_TEST(test_an_address_the_radio_has_not_published_yet_is_not_shown);
  RUN_TEST(test_a_lost_network_is_joined_again);
  RUN_TEST(test_the_screen_follows_a_new_address);
  RUN_TEST(test_the_networks_it_remembers_are_tried_in_turn);
  RUN_TEST(test_a_network_that_is_there_is_tried_again_before_the_next_one);
  RUN_TEST(
      test_a_network_that_keeps_turning_the_machine_away_gives_the_next_its_turn);
  RUN_TEST(test_the_network_it_joined_is_the_first_it_tries_the_next_time);
  RUN_TEST(test_the_network_it_was_on_is_tried_first_after_a_loss);
  RUN_TEST(test_it_says_now_and_then_that_it_is_still_joining);
  RUN_TEST(test_it_names_every_network_it_is_trying);
  RUN_TEST(test_it_says_why_the_last_try_failed);
  RUN_TEST(test_a_failure_is_forgotten_once_the_machine_has_joined);
  RUN_TEST(test_the_reasons_a_try_fails_have_names);
  RUN_TEST(test_no_password_is_logged);
  RUN_TEST(test_with_no_network_to_join_its_own_opens_at_once);
  RUN_TEST(
      test_its_own_network_opens_after_a_minute_without_the_one_it_remembers);
  RUN_TEST(test_its_own_network_is_never_open_without_its_password);
  RUN_TEST(test_tries_are_spaced_out_while_its_own_network_is_open);
  RUN_TEST(test_its_own_network_stays_open_for_as_long_as_no_network_is_joined);
  RUN_TEST(test_its_own_network_closes_a_minute_after_the_last_phone_has_left);
  RUN_TEST(test_a_lost_network_is_not_tried_every_few_seconds_under_a_phone);
  RUN_TEST(test_the_screen_shows_how_to_join_its_own_network);
  RUN_TEST(test_the_screen_shows_the_address_to_a_phone_that_has_just_joined);
  RUN_TEST(test_in_own_mode_it_runs_its_own_network_and_joins_none);
  RUN_TEST(test_its_own_network_goes_on_the_quietest_channel);
  RUN_TEST(test_the_quietest_channel_has_the_least_on_it_and_around_it);
  RUN_TEST(test_with_nothing_heard_machines_still_spread_over_the_channels);
  RUN_TEST(test_a_change_of_mode_is_followed_once_it_has_settled);
  RUN_TEST(test_a_change_to_own_mode_ends_the_try_under_way);
  RUN_TEST(
      test_going_back_to_joining_keeps_its_own_network_open_until_it_has_joined);
  RUN_TEST(test_a_network_typed_in_is_tried_within_seconds);
  RUN_TEST(test_a_network_typed_in_again_is_tried_again_at_once);
  RUN_TEST(test_forgetting_the_network_it_is_on_leaves_it);
  RUN_TEST(test_forgetting_the_network_it_is_on_tries_the_next_within_seconds);
  RUN_TEST(test_forgetting_another_network_does_not_disturb_the_one_it_is_on);
  RUN_TEST(test_a_change_to_the_router_option_opens_its_own_network_again);
  RUN_TEST(test_it_says_which_networks_are_in_reach_when_the_panel_asks);
  RUN_TEST(test_it_listens_only_when_it_is_asked);
  RUN_TEST(test_the_next_listen_says_what_is_in_reach_by_then);
  RUN_TEST(test_it_does_not_listen_while_a_try_is_under_way);
  RUN_TEST(test_it_does_not_listen_while_the_machine_waits_for_its_address);
  RUN_TEST(test_listening_leaves_the_radio_to_its_own_network_again);
  RUN_TEST(test_listening_between_tries_leaves_the_station_on);
  RUN_TEST(test_the_list_has_each_name_once_and_only_names_that_can_be_picked);
  RUN_TEST(test_the_list_is_the_loudest_networks_and_no_more);
  RUN_TEST(test_a_radio_that_could_not_listen_still_ends_the_wait);
  RUN_TEST(test_asking_again_while_it_listens_does_not_make_it_listen_twice);
  RUN_TEST(test_a_machine_updated_from_the_firmware_before_keeps_its_network);
  RUN_TEST(test_a_network_forgotten_on_purpose_does_not_come_back);
  return UNITY_END();
}
