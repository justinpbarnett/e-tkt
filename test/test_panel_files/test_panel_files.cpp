// Host-side tests for what a browser is answered when it asks for one of the
// panel's files: which file, whether the browser's own copy is still the one,
// and how long it may keep what it gets.
//
// A wrong answer to the last of these is the costly one. A file sent as one
// to keep for good under an address that never changes is a file the browser
// does not ask for again, so a firmware put on the machine afterwards shows
// the panel of the firmware before it.
//
// The panel here is a small one made up for the tests. The real one is made
// from data/ at each build, by scripts/embed_panel.py.
//
// Run with:  pio test -e native
#include <unity.h>

#include "PanelFiles.h"

static const uint8_t PAGE[] = {'p', 'a', 'g', 'e'};
static const uint8_t SCRIPT[] = {'s', 'c', 'r', 'i', 'p', 't'};
static const uint8_t ICON[] = {'i', 'c', 'o'};

static const PanelFile FILES[] = {
    {"/favicon.ico", "image/x-icon", "\"1c0\"", ICON, sizeof(ICON), false},
    {"/index.html", "text/html", "\"4a6e\"", PAGE, sizeof(PAGE), true},
    {"/script.js", "application/javascript", "\"5c21\"", SCRIPT, sizeof(SCRIPT),
     true},
};
static const PanelFiles PANEL = {"055d246b", FILES, 3};

static const char KEPT_FOR_GOOD[] = "public, max-age=31536000, immutable";
static const char ASKED_EACH_TIME[] = "no-cache";

// A request for `path`, with the version in its address and the tag of the
// copy the browser holds. Either is empty for a request without one.
static PanelReply asked(const char* path, const char* version = "",
                        const char* held = "") {
  return panelReply(PANEL, path, version, held);
}

void setUp(void) {}
void tearDown(void) {}

// --- which file ------------------------------------------------------------

void test_the_address_of_the_machine_is_the_page(void) {
  TEST_ASSERT_EQUAL_PTR(&FILES[1], asked("/").file);
  TEST_ASSERT_EQUAL_PTR(&FILES[1], asked("/index.html").file);
}

void test_a_file_is_found_by_its_whole_address(void) {
  TEST_ASSERT_EQUAL_PTR(&FILES[2], asked("/script.js").file);
  TEST_ASSERT_EQUAL_PTR(&FILES[0], asked("/favicon.ico").file);
}

void test_an_address_the_panel_has_no_file_for_finds_none(void) {
  TEST_ASSERT_NULL(asked("/script.js.map").file);
  TEST_ASSERT_NULL(asked("/script").file);
  TEST_ASSERT_NULL(asked("script.js").file);
  TEST_ASSERT_NULL(asked("/SCRIPT.JS").file);
  TEST_ASSERT_NULL(asked("").file);
  // Whatever it is asked under, and whatever the browser holds.
  TEST_ASSERT_NULL(asked("/nothing.css", "055d246b", "\"5c21\"").file);
}

// --- how long the browser may keep it --------------------------------------

void test_a_file_asked_for_under_this_panels_version_is_kept_for_good(void) {
  // The page names every file with the version of the panel it belongs to,
  // so a file that changes is asked for under a new address.
  TEST_ASSERT_EQUAL_STRING(KEPT_FOR_GOOD,
                           asked("/script.js", "055d246b").cacheControl);
}

void test_a_file_asked_for_under_no_version_is_asked_for_each_time(void) {
  // A browser asks for /favicon.ico by itself, under that address for ever.
  // Kept for good, it would outlive every firmware that changes it.
  TEST_ASSERT_EQUAL_STRING(ASKED_EACH_TIME, asked("/favicon.ico").cacheControl);
  TEST_ASSERT_EQUAL_STRING(ASKED_EACH_TIME, asked("/script.js").cacheControl);
}

void test_a_file_asked_for_under_another_version_is_asked_for_each_time(void) {
  // A page from the firmware before this one, still open, asks under the
  // version it was made with. What it gets is this panel's file, which is
  // not the one that address named.
  TEST_ASSERT_EQUAL_STRING(ASKED_EACH_TIME,
                           asked("/script.js", "0b5e11aa").cacheControl);
  TEST_ASSERT_EQUAL_STRING(ASKED_EACH_TIME,
                           asked("/script.js", "055d246").cacheControl);
  TEST_ASSERT_EQUAL_STRING(ASKED_EACH_TIME,
                           asked("/script.js", "055d246b0").cacheControl);
}

void test_the_page_is_asked_for_each_time_under_any_version(void) {
  // The page is what names the other files. Kept, it would go on naming the
  // ones of the firmware it came with.
  TEST_ASSERT_EQUAL_STRING(ASKED_EACH_TIME, asked("/").cacheControl);
  TEST_ASSERT_EQUAL_STRING(ASKED_EACH_TIME,
                           asked("/", "055d246b").cacheControl);
  TEST_ASSERT_EQUAL_STRING(ASKED_EACH_TIME,
                           asked("/index.html", "055d246b").cacheControl);
}

// --- whether the browser's copy is still the one ---------------------------

void test_a_browser_that_holds_the_file_is_not_sent_it_again(void) {
  TEST_ASSERT_TRUE(asked("/", "", "\"4a6e\"").unchanged);
  TEST_ASSERT_TRUE(asked("/script.js", "055d246b", "\"5c21\"").unchanged);
  // As a browser may send it, with space around.
  TEST_ASSERT_TRUE(asked("/", "", " \"4a6e\"\t").unchanged);
}

void test_a_browser_that_holds_another_copy_or_none_is_sent_the_file(void) {
  TEST_ASSERT_FALSE(asked("/").unchanged);
  TEST_ASSERT_FALSE(asked("/", "", "\"5c21\"").unchanged);
  TEST_ASSERT_FALSE(asked("/", "", "\"4a6e").unchanged);
  TEST_ASSERT_FALSE(asked("/", "", "\"4a6e\"0").unchanged);
  TEST_ASSERT_FALSE(asked("/", "", "4a6e").unchanged);
}

void test_a_copy_that_is_still_the_one_is_kept_as_long_as_a_new_one(void) {
  // The answer that sends no file says again how long the copy may be kept.
  TEST_ASSERT_EQUAL_STRING(
      KEPT_FOR_GOOD, asked("/script.js", "055d246b", "\"5c21\"").cacheControl);
  TEST_ASSERT_EQUAL_STRING(ASKED_EACH_TIME,
                           asked("/", "", "\"4a6e\"").cacheControl);
}

// --- what the panel weighs -------------------------------------------------

void test_the_panel_weighs_what_its_files_do(void) {
  TEST_ASSERT_EQUAL_UINT32(sizeof(PAGE) + sizeof(SCRIPT) + sizeof(ICON),
                           panelBytes(PANEL));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_the_address_of_the_machine_is_the_page);
  RUN_TEST(test_a_file_is_found_by_its_whole_address);
  RUN_TEST(test_an_address_the_panel_has_no_file_for_finds_none);
  RUN_TEST(test_a_file_asked_for_under_this_panels_version_is_kept_for_good);
  RUN_TEST(test_a_file_asked_for_under_no_version_is_asked_for_each_time);
  RUN_TEST(test_a_file_asked_for_under_another_version_is_asked_for_each_time);
  RUN_TEST(test_the_page_is_asked_for_each_time_under_any_version);
  RUN_TEST(test_a_browser_that_holds_the_file_is_not_sent_it_again);
  RUN_TEST(test_a_browser_that_holds_another_copy_or_none_is_sent_the_file);
  RUN_TEST(test_a_copy_that_is_still_the_one_is_kept_as_long_as_a_new_one);
  RUN_TEST(test_the_panel_weighs_what_its_files_do);
  return UNITY_END();
}
