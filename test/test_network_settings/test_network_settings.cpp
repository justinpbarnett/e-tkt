// Host-side tests for what the machine keeps about its place on a network:
// whether it joins a network or runs its own, the networks it remembers, the
// password of its own, and the names it goes by.
//
// All of it is kept in EEPROM, and all of it decides whether the machine can
// be reached at all, so these tests care about what survives a restart, and
// about what is refused before it is kept: a network kept with a password
// WPA2 cannot use is a network the machine tries for ever and never joins.
// A restart here is a new NetworkSettings over the same flash.
//
// Run with:  pio test -e native
#include <unity.h>

#include <string>
#include <vector>

#include "Arduino.h"
#include "Logger.h"
#include "NetworkSettings.h"

static Logger* logger;
static NetworkSettings* settings;

// The settings as a machine reads them at boot. The id is what the board
// makes of its MAC address.
static void start(const char* machineId = "9C4F") {
  settings = new NetworkSettings(logger);
  settings->initialize(machineId);
}

static void restart(void) {
  delete settings;
  start();
}

void setUp(void) {
  stubReset();
  // A generator in place of the board's hardware one, so that two passwords
  // made in one test differ, and every test still gets the same ones.
  static unsigned long state;
  state = 12345;
  stubRandom() = [](long howsmall, long howbig) {
    state = state * 1103515245UL + 12345UL;
    return howsmall +
           (long)((state >> 16) % (unsigned long)(howbig - howsmall));
  };
  logger = new Logger();
  start();
}

void tearDown(void) {
  delete settings;
  delete logger;
}

// The names of the networks the machine remembers, in the order it would try
// them, separated by commas.
static std::string remembered(void) {
  std::string names;
  for (const RememberedNetwork& network : settings->networks()) {
    names += (names.empty() ? "" : ",") + network.ssid.str();
  }
  return names;
}

// The password the machine keeps for a network, or "(none)" for a network it
// does not remember.
static std::string passwordOf(const char* ssid) {
  for (const RememberedNetwork& network : settings->networks()) {
    if (network.ssid == ssid) {
      return network.password.str();
    }
  }
  return "(none)";
}

static bool takes(const char* ssid, const char* password) {
  return settings->remember(ssid, password) == Remembered::KEPT;
}

// --- a machine as it is first switched on ----------------------------------

void test_a_machine_that_was_never_set_up_joins_a_network_and_remembers_none(
    void) {
  TEST_ASSERT_TRUE(NetworkMode::JOIN == settings->mode());
  TEST_ASSERT_EQUAL_STRING("", remembered().c_str());
  TEST_ASSERT_TRUE(settings->routerOffered());
}

// --- the networks it remembers ---------------------------------------------

void test_a_network_is_remembered_with_its_password_across_a_restart(void) {
  TEST_ASSERT_TRUE(takes("Basement", "correct horse"));

  restart();

  TEST_ASSERT_EQUAL_STRING("Basement", remembered().c_str());
  TEST_ASSERT_EQUAL_STRING("correct horse", passwordOf("Basement").c_str());
}

void test_the_network_remembered_last_is_the_first_to_be_tried(void) {
  // Somebody has just typed it in, standing at the machine, so it is the one
  // they want the machine on.
  takes("Basement", "correct horse");
  takes("Church", "battery staple");

  TEST_ASSERT_EQUAL_STRING("Church,Basement", remembered().c_str());
  restart();
  TEST_ASSERT_EQUAL_STRING("Church,Basement", remembered().c_str());
}

void test_a_network_remembered_again_is_kept_once_with_its_new_password(void) {
  // How a password typed wrong is put right.
  takes("Basement", "correct horse");
  takes("Church", "battery staple");

  TEST_ASSERT_TRUE(takes("Basement", "correct horses"));

  TEST_ASSERT_EQUAL_STRING("Basement,Church", remembered().c_str());
  TEST_ASSERT_EQUAL_STRING("correct horses", passwordOf("Basement").c_str());
}

void test_a_network_with_no_password_is_remembered(void) {
  // An open network, which a venue's guest network often is.
  TEST_ASSERT_TRUE(takes("Guest", ""));

  restart();

  TEST_ASSERT_EQUAL_STRING("", passwordOf("Guest").c_str());
}

void test_no_more_networks_are_remembered_than_there_is_room_for(void) {
  takes("One", "password1");
  takes("Two", "password2");
  takes("Three", "password3");
  takes("Four", "password4");

  TEST_ASSERT_TRUE(Remembered::FULL == settings->remember("Five", "password5"));

  TEST_ASSERT_EQUAL_INT(NetworkSettings::MAX_REMEMBERED,
                        (int)settings->networks().size());
  TEST_ASSERT_EQUAL_STRING("Four,Three,Two,One", remembered().c_str());
  // One of the four can still be given a new password.
  TEST_ASSERT_TRUE(takes("Two", "password2b"));
  TEST_ASSERT_EQUAL_STRING("Two,Four,Three,One", remembered().c_str());
}

void test_a_network_without_a_name_or_with_too_long_a_one_is_refused(void) {
  // A name is 1 to 32 bytes on the air, and the radio takes nothing else.
  const std::string longest(32, 'n');
  const std::string tooLong(33, 'n');

  TEST_ASSERT_TRUE(Remembered::NAME_MISSING ==
                   settings->remember("", "correct horse"));
  TEST_ASSERT_TRUE(Remembered::NAME_TOO_LONG ==
                   settings->remember(tooLong.c_str(), "correct horse"));
  TEST_ASSERT_EQUAL_STRING("", remembered().c_str());

  TEST_ASSERT_TRUE(takes(longest.c_str(), "correct horse"));
}

void test_a_password_wpa2_cannot_use_is_refused(void) {
  // 8 to 63 characters. One typed short is the commonest way to get it
  // wrong, and the machine would try it for ever and never say why.
  const std::string longest(63, 'p');
  const std::string tooLong(64, 'p');

  TEST_ASSERT_TRUE(Remembered::PASSWORD_TOO_SHORT ==
                   settings->remember("Basement", "seven77"));
  TEST_ASSERT_TRUE(Remembered::PASSWORD_TOO_LONG ==
                   settings->remember("Basement", tooLong.c_str()));
  TEST_ASSERT_EQUAL_STRING("", remembered().c_str());

  TEST_ASSERT_TRUE(takes("Basement", "eight888"));
  TEST_ASSERT_TRUE(takes("Church", longest.c_str()));
}

void test_a_refused_password_leaves_the_one_already_kept(void) {
  takes("Basement", "correct horse");

  settings->remember("Basement", "short");

  TEST_ASSERT_EQUAL_STRING("correct horse", passwordOf("Basement").c_str());
}

void test_a_forgotten_network_stays_forgotten_across_a_restart(void) {
  takes("Basement", "correct horse");
  takes("Church", "battery staple");

  settings->forget("Basement");

  TEST_ASSERT_EQUAL_STRING("Church", remembered().c_str());
  restart();
  TEST_ASSERT_EQUAL_STRING("Church", remembered().c_str());
}

void test_the_network_it_last_joined_is_tried_first_from_then_on(void) {
  // The machine is carried between two places, and at each one it finds
  // that place's network at the first try.
  takes("Basement", "correct horse");
  takes("Church", "battery staple");

  settings->prefer("Basement");

  TEST_ASSERT_EQUAL_STRING("Basement,Church", remembered().c_str());
  restart();
  TEST_ASSERT_EQUAL_STRING("Basement,Church", remembered().c_str());
  TEST_ASSERT_EQUAL_STRING("correct horse", passwordOf("Basement").c_str());
}

void test_a_list_of_networks_that_cannot_be_read_is_ignored(void) {
  // Flash another firmware wrote to, or a write cut short. The machine
  // remembers no network then, and opens its own, in place of trying
  // whatever half of the list it could make out.
  stubNvsText()["network"]["networks"] = "[{\"ssid\":\"Basement\",\"passw";

  restart();

  TEST_ASSERT_EQUAL_STRING("", remembered().c_str());
  TEST_ASSERT_TRUE(takes("Church", "battery staple"));
  TEST_ASSERT_EQUAL_STRING("Church", remembered().c_str());
}

void test_a_stored_network_the_radio_could_not_use_is_left_out(void) {
  stubNvsText()["network"]["networks"] =
      "[{\"ssid\":\"Basement\",\"password\":\"short\"},"
      "{\"ssid\":\"Church\",\"password\":\"battery staple\"}]";

  restart();

  TEST_ASSERT_EQUAL_STRING("Church", remembered().c_str());
}

// --- joining a network, or running its own ---------------------------------

void test_the_mode_is_kept_across_a_restart(void) {
  settings->setMode(NetworkMode::OWN);

  restart();

  TEST_ASSERT_TRUE(NetworkMode::OWN == settings->mode());
  settings->setMode(NetworkMode::JOIN);
  restart();
  TEST_ASSERT_TRUE(NetworkMode::JOIN == settings->mode());
}

void test_whether_its_own_network_offers_a_router_is_kept_across_a_restart(
    void) {
  settings->setRouterOffered(false);

  restart();

  TEST_ASSERT_FALSE(settings->routerOffered());
}

// --- its own network -------------------------------------------------------

void test_the_machine_is_named_after_its_id(void) {
  // Three machines in one room, each with a network and a name of its own.
  TEST_ASSERT_EQUAL_STRING("E-TKT-9C4F", settings->ownName().c_str());
  TEST_ASSERT_EQUAL_STRING("e-tkt-9c4f", settings->hostName().c_str());

  delete settings;
  start("01ab");

  TEST_ASSERT_EQUAL_STRING("E-TKT-01AB", settings->ownName().c_str());
  TEST_ASSERT_EQUAL_STRING("e-tkt-01ab", settings->hostName().c_str());
}

void test_the_password_of_its_own_network_is_one_wpa2_can_use(void) {
  const std::string password = settings->ownPassword().str();

  TEST_ASSERT_EQUAL_INT(10, (int)password.length());
  // Read off a small screen and typed on a phone: no capitals, and none of
  // the letters and digits that pass for one another.
  TEST_ASSERT_EQUAL_INT(
      (int)std::string::npos,
      (int)password.find_first_not_of("abcdefghjkmnpqrstuvwxyz23456789"));
}

void test_the_password_of_its_own_network_is_made_once(void) {
  // It is on a phone by now, saved with the network.
  const std::string password = settings->ownPassword().str();

  TEST_ASSERT_EQUAL_STRING(password.c_str(), settings->ownPassword().c_str());
  restart();
  TEST_ASSERT_EQUAL_STRING(password.c_str(), settings->ownPassword().c_str());
}

void test_no_password_is_logged(void) {
  takes("Basement", "correct horse");
  const std::string own = settings->ownPassword().str();
  restart();
  settings->forget("Basement");

  const std::string lines = logger->recent().str();
  TEST_ASSERT_TRUE(lines.find("Basement") != std::string::npos);
  TEST_ASSERT_TRUE(lines.find("correct horse") == std::string::npos);
  TEST_ASSERT_TRUE(lines.find(own) == std::string::npos);
}

// --- starting again --------------------------------------------------------

void test_a_reset_forgets_every_network_and_goes_back_to_joining_one(void) {
  // What holding the button through a boot asks for.
  takes("Basement", "correct horse");
  takes("Church", "battery staple");
  settings->setMode(NetworkMode::OWN);
  settings->setRouterOffered(false);

  settings->reset();

  TEST_ASSERT_EQUAL_STRING("", remembered().c_str());
  TEST_ASSERT_TRUE(NetworkMode::JOIN == settings->mode());
  TEST_ASSERT_TRUE(settings->routerOffered());
  restart();
  TEST_ASSERT_EQUAL_STRING("", remembered().c_str());
  TEST_ASSERT_TRUE(NetworkMode::JOIN == settings->mode());
  TEST_ASSERT_TRUE(settings->routerOffered());
}

void test_a_reset_changes_the_password_of_its_own_network(void) {
  // The one way to change it: whoever had the old one is shut out.
  const std::string before = settings->ownPassword().str();

  settings->reset();

  const std::string after = settings->ownPassword().str();
  TEST_ASSERT_EQUAL_INT(10, (int)after.length());
  TEST_ASSERT_TRUE(before != after);
  restart();
  TEST_ASSERT_EQUAL_STRING(after.c_str(), settings->ownPassword().c_str());
}

// --- telling the link that something changed -------------------------------

void test_every_change_the_link_has_to_follow_moves_the_revision(void) {
  uint32_t seen = settings->revision();

  takes("Basement", "correct horse");
  TEST_ASSERT_TRUE(settings->revision() != seen);
  seen = settings->revision();

  settings->setMode(NetworkMode::OWN);
  TEST_ASSERT_TRUE(settings->revision() != seen);
  seen = settings->revision();

  settings->setRouterOffered(false);
  TEST_ASSERT_TRUE(settings->revision() != seen);
  seen = settings->revision();

  settings->forget("Basement");
  TEST_ASSERT_TRUE(settings->revision() != seen);
}

void test_what_changes_nothing_leaves_the_revision_alone(void) {
  // The link drops what it is doing to follow a change, so it is not told
  // of one that did not happen.
  takes("Basement", "correct horse");
  takes("Church", "battery staple");
  const uint32_t seen = settings->revision();

  settings->setMode(NetworkMode::JOIN);
  settings->setRouterOffered(true);
  settings->forget("Nowhere");
  settings->remember("Garage", "short");
  // Nor of the order the networks are tried in, which the link sets itself.
  settings->prefer("Basement");

  TEST_ASSERT_EQUAL_UINT32(seen, settings->revision());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(
      test_a_machine_that_was_never_set_up_joins_a_network_and_remembers_none);
  RUN_TEST(test_a_network_is_remembered_with_its_password_across_a_restart);
  RUN_TEST(test_the_network_remembered_last_is_the_first_to_be_tried);
  RUN_TEST(test_a_network_remembered_again_is_kept_once_with_its_new_password);
  RUN_TEST(test_a_network_with_no_password_is_remembered);
  RUN_TEST(test_no_more_networks_are_remembered_than_there_is_room_for);
  RUN_TEST(test_a_network_without_a_name_or_with_too_long_a_one_is_refused);
  RUN_TEST(test_a_password_wpa2_cannot_use_is_refused);
  RUN_TEST(test_a_refused_password_leaves_the_one_already_kept);
  RUN_TEST(test_a_forgotten_network_stays_forgotten_across_a_restart);
  RUN_TEST(test_the_network_it_last_joined_is_tried_first_from_then_on);
  RUN_TEST(test_a_list_of_networks_that_cannot_be_read_is_ignored);
  RUN_TEST(test_a_stored_network_the_radio_could_not_use_is_left_out);
  RUN_TEST(test_the_mode_is_kept_across_a_restart);
  RUN_TEST(
      test_whether_its_own_network_offers_a_router_is_kept_across_a_restart);
  RUN_TEST(test_the_machine_is_named_after_its_id);
  RUN_TEST(test_the_password_of_its_own_network_is_one_wpa2_can_use);
  RUN_TEST(test_the_password_of_its_own_network_is_made_once);
  RUN_TEST(test_no_password_is_logged);
  RUN_TEST(test_a_reset_forgets_every_network_and_goes_back_to_joining_one);
  RUN_TEST(test_a_reset_changes_the_password_of_its_own_network);
  RUN_TEST(test_every_change_the_link_has_to_follow_moves_the_revision);
  RUN_TEST(test_what_changes_nothing_leaves_the_revision_alone);
  return UNITY_END();
}
