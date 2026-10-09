// Host-side tests for the log buffer and its levels.
//
// The point of the buffer is that the machine sits on a bench with no screen
// worth reading and nothing but a USB cable to say what it just did. Keeping
// the last lines in RAM lets the status page serve them, so these tests care
// mostly about what survives in the buffer and in what order.
//
// Run with:  pio test -e native
#include <unity.h>

#include <cstring>
#include <string>

#include "Arduino.h"
#include "Logger.h"

static Logger* logger;

void setUp(void) {
  stubReset();
  logger = new Logger();
}

void tearDown(void) { delete logger; }

static std::string recent(void) { return logger->recent().str(); }

static bool contains(const std::string& haystack, const char* needle) {
  return haystack.find(needle) != std::string::npos;
}

static int lineCount(const std::string& text) {
  if (text.empty()) {
    return 0;
  }
  int lines = 1;
  for (size_t i = 0; i < text.size(); i++) {
    if (text[i] == '\n') lines++;
  }
  return lines;
}

// --- what reaches the serial port ----------------------------------------
// The bench reads these live over USB, so the plain log() line has to stay
// byte for byte what it always was. Only the two new levels add anything.

void test_a_plain_log_line_reaches_serial_unchanged(void) {
  logger->log("print HELLO");
  TEST_ASSERT_EQUAL_INT(1, (int)stubSerialLines().size());
  TEST_ASSERT_EQUAL_STRING("print HELLO", stubSerialLines()[0].c_str());
}

void test_a_warning_is_marked_on_serial(void) {
  logger->warn("tape may be out");
  TEST_ASSERT_TRUE(contains(stubSerialLines()[0], "WARN"));
  TEST_ASSERT_TRUE(contains(stubSerialLines()[0], "tape may be out"));
}

void test_an_error_is_marked_on_serial(void) {
  logger->error("no such character");
  TEST_ASSERT_TRUE(contains(stubSerialLines()[0], "ERROR"));
  TEST_ASSERT_TRUE(contains(stubSerialLines()[0], "no such character"));
}

// --- the buffer ----------------------------------------------------------

void test_nothing_logged_means_nothing_to_serve(void) {
  TEST_ASSERT_EQUAL_INT(0, lineCount(recent()));
}

void test_a_logged_line_comes_back(void) {
  logger->log("homing");
  TEST_ASSERT_TRUE(contains(recent(), "homing"));
}

void test_lines_come_back_oldest_first(void) {
  logger->log("first");
  logger->log("second");
  const std::string text = recent();
  TEST_ASSERT_TRUE(text.find("first") < text.find("second"));
}

void test_every_level_lands_in_the_buffer(void) {
  logger->log("info line");
  logger->warn("warn line");
  logger->error("error line");
  const std::string text = recent();
  TEST_ASSERT_TRUE(contains(text, "info line"));
  TEST_ASSERT_TRUE(contains(text, "warn line"));
  TEST_ASSERT_TRUE(contains(text, "error line"));
  TEST_ASSERT_EQUAL_INT(3, lineCount(text));
}

void test_the_level_is_visible_in_the_buffer(void) {
  logger->error("no such character");
  TEST_ASSERT_TRUE(contains(recent(), "ERROR"));
}

void test_the_buffer_says_when(void) {
  // Without a timestamp the dump cannot answer the only question worth
  // asking of it: what happened just before the machine stopped.
  delay(4200);
  logger->log("homing");
  TEST_ASSERT_TRUE_MESSAGE(contains(recent(), "4.2"),
                           "each line must carry the time it was logged");
}

void test_the_buffer_drops_the_oldest_line_when_full(void) {
  for (int i = 0; i < LOG_HISTORY_LINES + 10; i++) {
    logger->log(String("line ") + String(i));
  }
  const std::string text = recent();
  TEST_ASSERT_EQUAL_INT(LOG_HISTORY_LINES, lineCount(text));
  TEST_ASSERT_FALSE_MESSAGE(contains(text, "line 0\n"),
                            "the oldest line must have been dropped");
  TEST_ASSERT_TRUE(contains(
      text, (String("line ") + String(LOG_HISTORY_LINES + 9)).c_str()));
}

void test_the_buffer_stays_in_order_after_it_wraps(void) {
  for (int i = 0; i < LOG_HISTORY_LINES * 2; i++) {
    logger->log(String("line ") + String(i));
  }
  const std::string text = recent();
  const std::string oldest =
      std::string("line ") + std::to_string(LOG_HISTORY_LINES);
  const std::string newest =
      std::string("line ") + std::to_string(LOG_HISTORY_LINES * 2 - 1);
  TEST_ASSERT_TRUE(text.find(oldest) < text.find(newest));
}

void test_the_buffer_fills_whether_or_not_anyone_is_watching_serial(void) {
  // The buffer exists for the case where nothing is: a machine on a bench
  // with no cable in it. It must not be gated on ENABLE_SERIAL.
  logger->log("happened anyway");
  TEST_ASSERT_TRUE(contains(recent(), "happened anyway"));
}

// --- across a restart ----------------------------------------------------
// The lines that say why a machine restarted in the middle of a job are the
// ones it used to lose. The board keeps them in memory a restart leaves
// alone, and the next start hands them on.

void test_the_lines_before_a_restart_are_there_after_it(void) {
  LogTrail* trail = new LogTrail();
  Logger* first = new Logger(trail);
  first->log("print HOLY x 80");
  first->log("lost Church (BEACON_TIMEOUT), joining it again");
  delete first;

  Logger* next = new Logger(trail);
  const std::string before = next->beforeRestart().str();
  TEST_ASSERT_TRUE(contains(before, "print HOLY x 80"));
  TEST_ASSERT_TRUE(before.find("print HOLY") < before.find("lost Church"));
  // The new start's lines are its own.
  TEST_ASSERT_EQUAL_INT(0, lineCount(next->recent().str()));
  next->log("started after crash");
  TEST_ASSERT_FALSE(contains(next->recent().str(), "print HOLY"));
  delete next;
  delete trail;
}

void test_a_restart_after_one_says_only_the_start_between(void) {
  LogTrail* trail = new LogTrail();
  Logger* first = new Logger(trail);
  first->log("from the first start");
  delete first;
  Logger* second = new Logger(trail);
  second->log("from the second start");
  delete second;

  Logger* third = new Logger(trail);
  const std::string before = third->beforeRestart().str();
  TEST_ASSERT_TRUE(contains(before, "from the second start"));
  TEST_ASSERT_FALSE(contains(before, "from the first start"));
  delete third;
  delete trail;
}

void test_memory_the_power_came_on_with_is_not_read_as_lines(void) {
  LogTrail* trail = new LogTrail();
  memset(trail, 0xA5, sizeof(LogTrail));
  Logger* fresh = new Logger(trail);
  TEST_ASSERT_EQUAL_INT(0, lineCount(fresh->beforeRestart().str()));
  fresh->log("homing");
  TEST_ASSERT_EQUAL_INT(1, lineCount(fresh->recent().str()));
  delete fresh;
  delete trail;
}

void test_a_line_too_long_to_keep_is_cut_short_and_ends(void) {
  logger->log(String(std::string(LOG_LINE_BYTES * 2, 'x').c_str()));
  logger->log("next");
  const std::string text = recent();
  TEST_ASSERT_EQUAL_INT(2, lineCount(text));
  TEST_ASSERT_EQUAL_INT(LOG_LINE_BYTES - 1, (int)text.find('\n'));
  TEST_ASSERT_TRUE(contains(text, "next"));
}

void test_why_the_machine_started_is_kept_and_logged(void) {
  TEST_ASSERT_EQUAL_STRING("", logger->startedAfter().c_str());
  logger->startedAfter("task watchdog");
  TEST_ASSERT_EQUAL_STRING("task watchdog", logger->startedAfter().c_str());
  TEST_ASSERT_TRUE(contains(recent(), "started after task watchdog"));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_a_plain_log_line_reaches_serial_unchanged);
  RUN_TEST(test_a_warning_is_marked_on_serial);
  RUN_TEST(test_an_error_is_marked_on_serial);
  RUN_TEST(test_nothing_logged_means_nothing_to_serve);
  RUN_TEST(test_a_logged_line_comes_back);
  RUN_TEST(test_lines_come_back_oldest_first);
  RUN_TEST(test_every_level_lands_in_the_buffer);
  RUN_TEST(test_the_level_is_visible_in_the_buffer);
  RUN_TEST(test_the_buffer_says_when);
  RUN_TEST(test_the_buffer_drops_the_oldest_line_when_full);
  RUN_TEST(test_the_buffer_stays_in_order_after_it_wraps);
  RUN_TEST(test_the_buffer_fills_whether_or_not_anyone_is_watching_serial);
  RUN_TEST(test_the_lines_before_a_restart_are_there_after_it);
  RUN_TEST(test_a_restart_after_one_says_only_the_start_between);
  RUN_TEST(test_memory_the_power_came_on_with_is_not_read_as_lines);
  RUN_TEST(test_a_line_too_long_to_keep_is_cut_short_and_ends);
  RUN_TEST(test_why_the_machine_started_is_kept_and_logged);
  return UNITY_END();
}
