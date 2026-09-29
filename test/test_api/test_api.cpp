// Host-side tests for the Api, through the one call the webserver makes on
// it: handle(), which turns a Request into a Reply.
//
// Every answer the device gives the panel is decided there: which fields a
// command needs, what the device says when one is wrong, and what a status
// carries. Until these tests none of that could be checked without a board
// on wifi. The machine behind the Api is the real one, built by HostMachine
// with fakes where it meets the hardware, so a command the Api accepts is one
// the job runner really took.
//
// Run with:  pio test -e native
#include <unity.h>

#include <string>

#include "Api.h"
#include "ArduinoJson.h"
#include "HostMachine.h"

static HostMachine* machine;
static Api* api;

void setUp(void) {
  stubReset();
  machine = new HostMachine();
  api = new Api(&machine->etkt, &machine->logger);
}

void tearDown(void) {
  delete api;
  delete machine;
}

static Reply get(const char* path) {
  Request request;
  request.method = Method::GET;
  request.path = path;
  return api->handle(request);
}

// A post the way the panel sends one: a JSON body, and a Content-Type that
// says so.
static Reply post(const char* path, const char* body) {
  Request request;
  request.method = Method::POST;
  request.path = path;
  request.contentType = "application/json";
  request.body = body;
  return api->handle(request);
}

// A post whose body is sent as some other type, or as none.
static Reply postAs(const char* contentType, const char* path,
                    const char* body) {
  Request request;
  request.method = Method::POST;
  request.path = path;
  request.contentType = contentType;
  request.body = body;
  return api->handle(request);
}

// A stop the way the panel sends one: a bare post, with no body and no type,
// and what to stop after in the query string, or nothing to stop now.
static Reply postStop(const char* after) {
  Request request;
  request.method = Method::POST;
  request.path = "/api/stop";
  if (after != NULL) {
    request.query["after"] = after;
  }
  return api->handle(request);
}

// What the job runner has in hand, which the Api's word alone does not prove.
static Command running(void) {
  return machine->etkt.createStatus().currentCommand;
}

// The reply's body, parsed. Fails the test unless the reply is JSON. What it
// returns is good until the next call.
static JsonObject json(const Reply& reply) {
  static DynamicJsonDocument doc(8192);
  TEST_ASSERT_EQUAL_STRING("application/json", reply.contentType);
  const DeserializationError error = deserializeJson(doc, reply.body.c_str());
  TEST_ASSERT_EQUAL_STRING("Ok", error.c_str());
  return doc.as<JsonObject>();
}

// What a refusal says, which is what the panel shows the operator.
static const char* errorOf(const Reply& reply) {
  return json(reply)["error"].as<const char*>();
}

static bool endsWith(const std::string& text, const std::string& tail) {
  return text.size() >= tail.size() &&
         text.compare(text.size() - tail.size(), tail.size(), tail) == 0;
}

// --- routes -----------------------------------------------------------------

// A mistyped address gets an answer that says so, in JSON like every other
// answer under /api/, rather than the webserver's page.
void test_a_path_nothing_answers_is_not_found(void) {
  const Reply reply = get("/api/nothing");

  TEST_ASSERT_EQUAL_INT(404, reply.code);
  TEST_ASSERT_EQUAL_STRING("Not found", errorOf(reply));
}

// Idle is a name the status reports, not a job, so there is nothing at
// /api/idle to post to.
void test_idle_is_not_a_command_anyone_can_post(void) {
  const Reply reply = post("/api/idle", "{}");

  TEST_ASSERT_EQUAL_INT(404, reply.code);
  TEST_ASSERT_EQUAL_STRING("Not found", errorOf(reply));
}

// A command asked for the wrong way is told the right one, in the Allow
// header HTTP asks for and in words, rather than that it does not exist.
void test_a_command_asked_for_with_get_is_told_to_post(void) {
  const Reply reply = get("/api/cut");

  TEST_ASSERT_EQUAL_INT(405, reply.code);
  TEST_ASSERT_EQUAL_STRING("POST", reply.allow);
  TEST_ASSERT_EQUAL_STRING("Please use POST for /api/cut", errorOf(reply));
  TEST_ASSERT_EQUAL_INT((int)Command::IDLE, (int)running());
}

// A method no route answers to, such as a PUT, is told which one the path
// does answer to, and is not taken for a post.
void test_a_method_nothing_answers_to_is_told_the_one_that_is(void) {
  Request request;
  request.method = Method::OTHER;
  request.path = "/api/cut";
  request.contentType = "application/json";
  request.body = "{}";

  const Reply reply = api->handle(request);

  TEST_ASSERT_EQUAL_INT(405, reply.code);
  TEST_ASSERT_EQUAL_STRING("POST", reply.allow);
  TEST_ASSERT_EQUAL_INT((int)Command::IDLE, (int)running());
}

// --- commands ---------------------------------------------------------------

// A command the machine can take is handed to the job runner, which has it by
// the time the reply says so.
void test_a_command_is_handed_to_the_job_runner(void) {
  const Reply reply = post("/api/cut", "{}");

  TEST_ASSERT_EQUAL_INT(200, reply.code);
  TEST_ASSERT_EQUAL_STRING("success", json(reply)["result"].as<const char*>());
  TEST_ASSERT_EQUAL_INT((int)Command::CUT, (int)running());
}

// A second command while the first is still running is refused with a 409:
// nothing is wrong with what was sent, and the same body will work once the
// machine is free.
void test_a_command_while_another_runs_is_refused_as_a_conflict(void) {
  post("/api/cut", "{}");

  const Reply reply = post("/api/feed", "{}");

  TEST_ASSERT_EQUAL_INT(409, reply.code);
  TEST_ASSERT_EQUAL_STRING("The printer is already busy executing a command.",
                           errorOf(reply));
  TEST_ASSERT_EQUAL_INT((int)Command::CUT, (int)running());
}

// A command that trials an align needs one in the body. Refused, and nothing
// reaches the job runner.
void test_a_command_that_trials_an_align_needs_one(void) {
  const Reply reply = post("/api/testalign", "{}");

  TEST_ASSERT_EQUAL_INT(400, reply.code);
  TEST_ASSERT_EQUAL_STRING("Please provide an align value", errorOf(reply));
  TEST_ASSERT_EQUAL_INT((int)Command::IDLE, (int)running());
}

// An align outside the range the panel offers is refused rather than
// clamped. A clamp is silent: the panel would report the test pressed at the
// align on screen while the machine used another.
void test_an_align_out_of_range_is_refused_not_clamped(void) {
  const Reply reply = post("/api/testalign", "{\"align\":12}");

  TEST_ASSERT_EQUAL_INT(400, reply.code);
  TEST_ASSERT_EQUAL_STRING(
      "Please provide an align value between 1 and 9, got 12", errorOf(reply));
  TEST_ASSERT_EQUAL_INT((int)Command::IDLE, (int)running());
}

// A command that trials a force as well needs both, and the align alone is
// not enough.
void test_a_command_that_trials_a_force_needs_one(void) {
  const Reply reply = post("/api/testfull", "{\"align\":5}");

  TEST_ASSERT_EQUAL_INT(400, reply.code);
  TEST_ASSERT_EQUAL_STRING("Please provide a force value", errorOf(reply));
  TEST_ASSERT_EQUAL_INT((int)Command::IDLE, (int)running());
}

// The align and force in the body are the ones the job gets. Saved, they are
// what the device then runs at.
void test_a_calibration_in_the_body_is_the_one_the_job_gets(void) {
  TEST_ASSERT_EQUAL_INT(200,
                        post("/api/save", "{\"align\":7,\"force\":3}").code);

  machine->etkt.loop();

  const StatusUpdate status = machine->etkt.createStatus();
  TEST_ASSERT_EQUAL_INT(7, status.align);
  TEST_ASSERT_EQUAL_INT(3, status.force);
}

// A label is refused without its text, named by the field the panel missed.
void test_a_label_without_its_text_is_refused(void) {
  const Reply reply = post("/api/tag", "{}");

  TEST_ASSERT_EQUAL_INT(400, reply.code);
  TEST_ASSERT_EQUAL_STRING("Please provide a tag value", errorOf(reply));
  TEST_ASSERT_EQUAL_INT((int)Command::IDLE, (int)running());
}

// The text in the body is the label the job runner prints.
void test_a_label_is_submitted_with_its_text(void) {
  TEST_ASSERT_EQUAL_INT(200, post("/api/tag", "{\"tag\":\"HELLO\"}").code);

  const StatusUpdate status = machine->etkt.createStatus();
  TEST_ASSERT_EQUAL_INT((int)Command::TAG, (int)status.currentCommand);
  TEST_ASSERT_EQUAL_STRING("HELLO", status.currentLabel.c_str());
}

// A label that is not text is refused, not printed as whatever it turns into.
// Read loosely, a null would come out as the four letters NULL.
void test_a_label_that_is_not_text_is_refused(void) {
  const Reply reply = post("/api/tag", "{\"tag\":null}");

  TEST_ASSERT_EQUAL_INT(400, reply.code);
  TEST_ASSERT_EQUAL_STRING("Please provide a tag value", errorOf(reply));
  TEST_ASSERT_EQUAL_INT((int)Command::IDLE, (int)running());
}

// A label longer than the device prints is refused before any tape is fed,
// rather than cut short partway down the roll.
void test_a_label_too_long_to_print_is_refused(void) {
  const std::string body = "{\"tag\":\"" + std::string(250, 'A') + "\"}";

  const Reply reply = post("/api/tag", body.c_str());

  TEST_ASSERT_EQUAL_INT(400, reply.code);
  TEST_ASSERT_EQUAL_STRING("A tag may be at most 249 characters, got 250",
                           errorOf(reply));
  TEST_ASSERT_EQUAL_INT((int)Command::IDLE, (int)running());
}

// The limit counts characters, not bytes. The longest label of symbols, three
// bytes each, is as long as the longest of letters.
void test_the_longest_label_of_symbols_is_accepted(void) {
  std::string label;
  for (int i = 0; i < 249; i++) {
    label += "\u2661";
  }
  const std::string body = "{\"tag\":\"" + label + "\"}";

  const Reply reply = post("/api/tag", body.c_str());

  TEST_ASSERT_EQUAL_INT(200, reply.code);
  TEST_ASSERT_EQUAL_STRING(label.c_str(),
                           machine->etkt.createStatus().currentLabel.c_str());
}

// A label with a character the wheel does not carry is refused, and the
// refusal quotes the character, so the operator knows which one to change.
void test_a_label_the_wheel_cannot_print_is_refused(void) {
  const Reply reply = post("/api/tag", "{\"tag\":\"NO#1\"}");

  TEST_ASSERT_EQUAL_INT(400, reply.code);
  TEST_ASSERT_EQUAL_STRING("The daisy wheel cannot print '#'", errorOf(reply));
  TEST_ASSERT_EQUAL_INT((int)Command::IDLE, (int)running());
}

// A move's field names a slot on the wheel rather than carrying a label, so
// the label rules are not its rules: the cut mark no label may contain is a
// slot the wheel can move to.
void test_a_move_may_name_the_cut_mark(void) {
  const Reply reply = post("/api/move", "{\"character\":\"*\"}");

  TEST_ASSERT_EQUAL_INT(200, reply.code);
  const StatusUpdate status = machine->etkt.createStatus();
  TEST_ASSERT_EQUAL_INT((int)Command::MOVE, (int)status.currentCommand);
  TEST_ASSERT_EQUAL_STRING("*", status.currentLabel.c_str());
}

// A run of labels is 1 to MAX_COPIES long. None at all is refused, not taken
// as one.
void test_a_run_of_no_labels_is_refused(void) {
  const Reply reply = post("/api/tag", "{\"tag\":\"HI\",\"copies\":0}");

  TEST_ASSERT_EQUAL_INT(400, reply.code);
  TEST_ASSERT_EQUAL_STRING(
      "Please provide a copies value between 1 and 500, "
      "got 0",
      errorOf(reply));
  TEST_ASSERT_EQUAL_INT((int)Command::IDLE, (int)running());
}

// A run's length in the body is the one the job runner counts to.
void test_a_run_of_labels_is_submitted_with_its_length(void) {
  TEST_ASSERT_EQUAL_INT(200,
                        post("/api/tag", "{\"tag\":\"HI\",\"copies\":3}").code);

  TEST_ASSERT_EQUAL_INT(3, machine->etkt.createStatus().copies);
}

// Copies mean nothing to a command that prints no run of labels, so they are
// ignored there rather than checked: a stale page that sends them anyway
// still gets its cut.
void test_copies_are_ignored_by_a_command_that_prints_no_labels(void) {
  const Reply reply = post("/api/cut", "{\"copies\":0}");

  TEST_ASSERT_EQUAL_INT(200, reply.code);
  TEST_ASSERT_EQUAL_INT((int)Command::CUT, (int)running());
}

// A new roll is declared at a length the device can count down from, or not
// at all. Too short to be a roll is refused.
void test_a_roll_too_short_to_be_one_is_refused(void) {
  const Reply reply = post("/api/reel", "{\"length_mm\":100}");

  TEST_ASSERT_EQUAL_INT(400, reply.code);
  TEST_ASSERT_EQUAL_STRING(
      "Please provide a length_mm value between 500 and "
      "10000, got 100",
      errorOf(reply));
  TEST_ASSERT_EQUAL_INT((int)Command::IDLE, (int)running());
}

// A new roll's length in the body is the one the device counts down from.
void test_a_new_roll_is_declared_at_its_length(void) {
  TEST_ASSERT_EQUAL_INT(200, post("/api/reel", "{\"length_mm\":4000}").code);

  machine->etkt.loop();

  TEST_ASSERT_EQUAL_UINT32(4000, machine->etkt.createStatus().roll.lengthMm);
}

// --- bodies -----------------------------------------------------------------

// A body not sent as JSON is refused as the wrong type rather than guessed
// at, and the refusal says what to send instead.
void test_a_body_not_sent_as_json_is_refused(void) {
  const Reply reply =
      postAs("application/x-www-form-urlencoded", "/api/cut", "{}");

  TEST_ASSERT_EQUAL_INT(415, reply.code);
  TEST_ASSERT_EQUAL_STRING("Please send the body as application/json",
                           errorOf(reply));
  TEST_ASSERT_EQUAL_INT((int)Command::IDLE, (int)running());
}

// JSON is JSON however the type is written: in any case, and with the
// parameters a client may add after it.
void test_json_is_json_in_any_case_and_with_parameters(void) {
  const Reply reply =
      postAs("Application/JSON;charset=UTF-8", "/api/cut", "{}");

  TEST_ASSERT_EQUAL_INT(200, reply.code);
  TEST_ASSERT_EQUAL_INT((int)Command::CUT, (int)running());
}

// A body that is not a JSON object is refused rather than read as an empty
// one. Read as one, a body cut off on its way here would still cut the tape.
void test_a_body_that_is_not_a_json_object_is_refused(void) {
  const Reply reply = post("/api/cut", "{\"copies\":");

  TEST_ASSERT_EQUAL_INT(400, reply.code);
  TEST_ASSERT_EQUAL_STRING("The body must be a JSON object", errorOf(reply));
  TEST_ASSERT_EQUAL_INT((int)Command::IDLE, (int)running());
}

// JSON that is not an object has no fields to read, and is refused the same
// way as a body that is not JSON at all.
void test_json_that_is_not_an_object_is_refused(void) {
  const Reply reply = post("/api/cut", "[]");

  TEST_ASSERT_EQUAL_INT(400, reply.code);
  TEST_ASSERT_EQUAL_STRING("The body must be a JSON object", errorOf(reply));
  TEST_ASSERT_EQUAL_INT((int)Command::IDLE, (int)running());
}

// A body longer than any command needs is refused unread. The longest label
// there is fits in well under half the limit.
void test_a_body_over_the_limit_is_refused_unread(void) {
  const std::string head = "{\"tag\":\"";
  const std::string tail = "\"}";
  const std::string body =
      head +
      std::string(Api::MAX_BODY_BYTES + 1 - head.size() - tail.size(), 'A') +
      tail;
  TEST_ASSERT_EQUAL_INT(Api::MAX_BODY_BYTES + 1, body.size());

  const Reply reply = post("/api/tag", body.c_str());

  TEST_ASSERT_EQUAL_INT(413, reply.code);
  TEST_ASSERT_EQUAL_STRING("The body may be at most 2048 bytes",
                           errorOf(reply));
  TEST_ASSERT_EQUAL_INT((int)Command::IDLE, (int)running());
}

// A body inside the limit can still hold more fields than there is room to
// read. Refused as too large too, rather than read with fields missing.
void test_a_body_with_more_fields_than_room_is_refused(void) {
  std::string body = "{";
  for (int i = 0; i < 250; i++) {
    body += (i > 0 ? ",\"" : "\"") + std::to_string(i) + "\":0";
  }
  body += "}";
  TEST_ASSERT_TRUE(body.size() <= Api::MAX_BODY_BYTES);

  const Reply reply = post("/api/cut", body.c_str());

  TEST_ASSERT_EQUAL_INT(413, reply.code);
  TEST_ASSERT_EQUAL_STRING("The body has more fields than the device can read",
                           errorOf(reply));
  TEST_ASSERT_EQUAL_INT((int)Command::IDLE, (int)running());
}

// --- status -----------------------------------------------------------------

// What the panel polls once a second. An idle machine says it is idle, by the
// name the table gives it, with nothing running to describe and no stop to
// explain.
void test_an_idle_machine_says_it_is_idle(void) {
  const Reply reply = get("/api/status");

  TEST_ASSERT_EQUAL_INT(200, reply.code);
  const JsonObject status = json(reply);
  TEST_ASSERT_TRUE(status["busy"].is<bool>());
  TEST_ASSERT_FALSE(status["busy"].as<bool>());
  TEST_ASSERT_EQUAL_STRING("idle", status["command"].as<const char*>());
  TEST_ASSERT_TRUE(status["progress"].is<int>());
  TEST_ASSERT_EQUAL_INT(0, status["progress"].as<int>());
  TEST_ASSERT_FALSE(status.containsKey("stop"));
  TEST_ASSERT_FALSE(status.containsKey("stopped"));
}

// The calibration the device presses at, whether or not it is pressing, so
// the panel's two sliders open where the device has them.
void test_the_status_carries_the_saved_calibration(void) {
  machine->settings.save(7, 3);

  const JsonObject status = json(get("/api/status"));

  TEST_ASSERT_EQUAL_INT(7, status["align"].as<int>());
  TEST_ASSERT_EQUAL_INT(3, status["force"].as<int>());
}

// While a run prints, the status says what the label says, which label of
// the run is being pressed, and how far into it the press is. The panel
// polls from outside the job, so the status is read here partway through
// the second label, the way a poll would land.
void test_a_run_in_progress_says_which_label_it_is_on(void) {
  post("/api/tag", "{\"tag\":\"AB\",\"copies\":3}");
  static Reply during;
  during = Reply();
  stubAfterDelay() = [] {
    const StatusUpdate now = machine->etkt.createStatus();
    if (during.code == 0 && now.copy == 2 && now.progress == 50) {
      during = get("/api/status");
    }
  };

  machine->etkt.loop();

  const JsonObject status = json(during);
  TEST_ASSERT_TRUE(status["busy"].as<bool>());
  TEST_ASSERT_EQUAL_STRING("tag", status["command"].as<const char*>());
  TEST_ASSERT_EQUAL_STRING("AB", status["current_label"].as<const char*>());
  TEST_ASSERT_EQUAL_INT(2, status["copy"].as<int>());
  TEST_ASSERT_EQUAL_INT(3, status["copies"].as<int>());
  TEST_ASSERT_EQUAL_INT(50, status["progress"].as<int>());
}

// A command that prints no run of labels has no label or run to describe,
// and says nothing about either rather than an empty label of 0 of 0.
void test_a_command_that_prints_no_labels_says_nothing_of_them(void) {
  post("/api/move", "{\"character\":\"A\"}");

  const JsonObject status = json(get("/api/status"));

  TEST_ASSERT_EQUAL_STRING("move", status["command"].as<const char*>());
  TEST_ASSERT_FALSE(status.containsKey("current_label"));
  TEST_ASSERT_FALSE(status.containsKey("copy"));
  TEST_ASSERT_FALSE(status.containsKey("copies"));
}

// A stop asked for and not yet obeyed is reported, and which stop, so a
// panel opened partway through one says so too rather than offering it
// again. A stop now overtakes a stop after the label.
void test_a_stop_asked_for_is_reported_until_it_is_obeyed(void) {
  post("/api/tag", "{\"tag\":\"AB\",\"copies\":3}");
  TEST_ASSERT_FALSE(json(get("/api/status")).containsKey("stop"));

  machine->etkt.stopAfterLabel();
  TEST_ASSERT_EQUAL_STRING("after_label",
                           json(get("/api/status"))["stop"].as<const char*>());

  machine->etkt.stop();
  TEST_ASSERT_EQUAL_STRING("now",
                           json(get("/api/status"))["stop"].as<const char*>());
}

// What the last stop cut short, kept after the job has ended, so a panel
// that was not watching when a run was stopped can still say it was. This
// stop lands partway into the first label of three, once the tape has moved,
// and leaves that label on the tape for the operator to cut off.
void test_a_stop_reports_what_it_cut_short(void) {
  post("/api/tag", "{\"tag\":\"AB\",\"copies\":3}");
  machine->charStepper.afterStep = [] {
    if (machine->feeder.feeds() > 0) {
      machine->etkt.stop();
    }
  };

  machine->etkt.loop();

  const JsonObject status = json(get("/api/status"));
  TEST_ASSERT_FALSE(status["busy"].as<bool>());
  const JsonObject stopped = status["stopped"];
  // The stub's random() starts the count at the bottom of its range, so the
  // first stop since boot is 1.
  TEST_ASSERT_EQUAL_UINT32(1, stopped["id"].as<uint32_t>());
  TEST_ASSERT_EQUAL_STRING("tag", stopped["command"].as<const char*>());
  TEST_ASSERT_EQUAL_STRING("operator", stopped["cause"].as<const char*>());
  TEST_ASSERT_EQUAL_INT(0, stopped["printed"].as<int>());
  TEST_ASSERT_EQUAL_INT(3, stopped["copies"].as<int>());
  TEST_ASSERT_TRUE(stopped["unfinished"].as<bool>());
}

// A wheel that turned without finding its magnet stops the job the way the
// button does, and the status says it was the wheel, so the operator looks
// at the magnet rather than at who pressed stop. A home prints no run of
// labels, so there is no count of them to report.
void test_a_lost_wheel_is_reported_as_the_cause(void) {
  machine->magnet.present = false;
  post("/api/home", "{}");

  machine->etkt.loop();

  const JsonObject stopped = json(get("/api/status"))["stopped"];
  TEST_ASSERT_EQUAL_STRING("home", stopped["command"].as<const char*>());
  TEST_ASSERT_EQUAL_STRING("lost_wheel", stopped["cause"].as<const char*>());
  TEST_ASSERT_FALSE(stopped.containsKey("printed"));
  TEST_ASSERT_FALSE(stopped.containsKey("copies"));
  TEST_ASSERT_FALSE(stopped["unfinished"].as<bool>());
}

// What is estimated to be left on the roll, busy or not, so the panel can
// say how many labels fit before anything has been printed. A feed is 4 mm
// of tape, so 250 of them take a metre off a 4 m roll.
void test_the_status_says_what_is_left_on_the_roll(void) {
  machine->roll.load(4000);
  machine->roll.use(250);

  const JsonObject roll = json(get("/api/status"))["roll"];

  TEST_ASSERT_EQUAL_INT(4000, roll["length_mm"].as<int>());
  TEST_ASSERT_EQUAL_INT(3000, roll["remaining_mm"].as<int>());
}

// How much memory the device has free, how much of that is in one piece, and
// how long it has been up: what someone chasing a crash on the bench reads
// first. The panel shows none of it.
void test_the_status_says_how_much_memory_is_free_and_the_uptime(void) {
  stubHeap().freeBytes = 123456;
  stubHeap().largestFreeBlockBytes = 65536;
  stubClockMs() = 90210;

  const JsonObject status = json(get("/api/status"));

  TEST_ASSERT_EQUAL_UINT32(123456,
                           status["mem_heap_free_bytes"].as<uint32_t>());
  TEST_ASSERT_EQUAL_UINT32(
      65536, status["mem_largest_free_block_bytes"].as<uint32_t>());
  TEST_ASSERT_EQUAL_UINT32(90210, status["uptime_ms"].as<uint32_t>());
}

// The longest status there is: a run of the longest label of symbols, three
// bytes each, with a stop asked for. Every field still fits, rather than the
// last ones going missing without a word.
void test_the_longest_status_has_every_field(void) {
  std::string label;
  for (int i = 0; i < 249; i++) {
    label += "\u2661";
  }
  const std::string body = "{\"tag\":\"" + label + "\",\"copies\":500}";
  post("/api/tag", body.c_str());
  machine->etkt.stopAfterLabel();

  const Reply reply = get("/api/status");

  TEST_ASSERT_EQUAL_INT(200, reply.code);
  const JsonObject status = json(reply);
  TEST_ASSERT_EQUAL_STRING(label.c_str(),
                           status["current_label"].as<const char*>());
  TEST_ASSERT_EQUAL_STRING("after_label", status["stop"].as<const char*>());
  TEST_ASSERT_TRUE(status["roll"]["remaining_mm"].is<long>());
  TEST_ASSERT_TRUE(status["uptime_ms"].is<unsigned long>());
}

// A reply that ran out of room is not sent with its last fields missing, the
// way ArduinoJson would leave it without a word. It is the device failing,
// so it is a 500. The Api refuses a label this long, but the job runner holds
// whatever it is handed, and this is how a field too many would show up.
void test_a_reply_too_large_for_its_room_is_a_failure_not_a_short_reply(void) {
  CommandOptions options;
  options.command = Command::TAG;
  options.label = String(std::string(3000, 'A'));
  machine->etkt.submit(options);

  const Reply reply = get("/api/status");

  TEST_ASSERT_EQUAL_INT(500, reply.code);
  TEST_ASSERT_EQUAL_STRING("The reply is larger than the device has room for",
                           errorOf(reply));
}

// A failure is logged as well as answered. The panel reads a poll that
// failed as the device being out of reach, and shows nothing of what the
// reply said, so the log is where somebody at the bench finds out why.
void test_a_failure_is_logged_as_well_as_answered(void) {
  CommandOptions options;
  options.command = Command::TAG;
  options.label = String(std::string(3000, 'A'));
  machine->etkt.submit(options);

  get("/api/status");

  const std::string log = machine->logger.recent().str();
  TEST_ASSERT_TRUE(log.find("ERROR /api/status failed with 500: {\"error\":"
                            "\"The reply is larger than the device has room "
                            "for\"}") != std::string::npos);
}

// A status is read, never written, so a post to it is told to get rather
// than answered as if it were a get.
void test_the_status_posted_to_is_told_to_get(void) {
  const Reply reply = post("/api/status", "{}");

  TEST_ASSERT_EQUAL_INT(405, reply.code);
  TEST_ASSERT_EQUAL_STRING("GET", reply.allow);
  TEST_ASSERT_EQUAL_STRING("Please use GET for /api/status", errorOf(reply));
}

// --- capabilities -----------------------------------------------------------

// What the wheel can print, which the panel fetches once as it loads and
// checks every label against, rather than against a copy of its own. The cut
// mark has a slot but is no character a label may hold, and 0 and 1 are
// printed as the O and I the wheel carries in their place.
void test_the_capabilities_say_what_the_wheel_can_print(void) {
  const Reply reply = get("/api/capabilities");

  TEST_ASSERT_EQUAL_INT(200, reply.code);
  const JsonObject capabilities = json(reply);
  TEST_ASSERT_EQUAL_STRING(
      " $-.0123456789@ABCDEFGHIJKLMNOPQRSTUVWXYZ\u20ac\u2606\u2661\u266a",
      capabilities["printable"].as<const char*>());
  const JsonObject aliases = capabilities["aliases"];
  TEST_ASSERT_EQUAL_INT(2, (int)aliases.size());
  TEST_ASSERT_EQUAL_STRING("O", aliases["0"].as<const char*>());
  TEST_ASSERT_EQUAL_STRING("I", aliases["1"].as<const char*>());
}

// The ranges the device refuses a request outside of, so the panel offers
// only what the device will take: the calibration both sliders share, how
// short and long a label may be, how long a run, and how long a roll, with
// the length a new one usually is.
void test_the_capabilities_give_the_ranges_the_device_checks(void) {
  const JsonObject capabilities = json(get("/api/capabilities"));

  TEST_ASSERT_EQUAL_INT(1, capabilities["calibration"]["min"].as<int>());
  TEST_ASSERT_EQUAL_INT(9, capabilities["calibration"]["max"].as<int>());
  TEST_ASSERT_EQUAL_INT(6, capabilities["label"]["minimum"].as<int>());
  TEST_ASSERT_EQUAL_INT(249, capabilities["label"]["maximum"].as<int>());
  TEST_ASSERT_EQUAL_INT(1, capabilities["copies"]["minimum"].as<int>());
  TEST_ASSERT_EQUAL_INT(500, capabilities["copies"]["maximum"].as<int>());
  TEST_ASSERT_EQUAL_INT(500, capabilities["roll"]["minimum_mm"].as<int>());
  TEST_ASSERT_EQUAL_INT(10000, capabilities["roll"]["maximum_mm"].as<int>());
  TEST_ASSERT_EQUAL_INT(3000, capabilities["roll"]["default_mm"].as<int>());
}

// How far one feed moves the tape, and how many blank feeds lead every
// label, which is what the panel needs to say how much tape a label takes
// and how many fit on what is left of the roll.
void test_the_capabilities_say_how_much_tape_a_feed_takes(void) {
  const JsonObject feed = json(get("/api/capabilities"))["feed"];

  TEST_ASSERT_EQUAL_INT(4000, feed["length_um"].as<int>());
  TEST_ASSERT_EQUAL_INT(1, feed["lead"].as<int>());
}

// Every command the device can be asked for, by the name it answers to, and
// no other. Idle is a status, not a command, so it has no row.
void test_the_capabilities_list_every_command_that_can_be_asked_for(void) {
  const JsonObject commands = json(get("/api/capabilities"))["commands"];

  const char* const names[] = {"cut",  "feed", "reel", "testalign", "testfull",
                               "save", "tag",  "home", "move"};
  const int count = sizeof(names) / sizeof(names[0]);
  TEST_ASSERT_EQUAL_INT(count, (int)commands.size());
  for (int i = 0; i < count; i++) {
    TEST_ASSERT_TRUE_MESSAGE(commands.containsKey(names[i]), names[i]);
  }
}

// Each row says what its command does, which is what the panel offers a
// stop and counts a run by. The expected rows are written out by hand from
// what each command is for, not read off the table they are checking.
void test_each_command_says_what_it_does(void) {
  struct Row {
    const char* name;
    bool usesAlign;
    bool usesForce;
    const char* labelField;
    bool fieldIsLabel;
    bool printsRun;
    bool usesRollLength;
    bool stoppable;
    bool pressesLabel;
  };
  const Row rows[] = {
      // name        align  force  field        label  run    roll   stop
      // press
      {"cut", false, false, NULL, false, false, false, true, false},
      {"feed", false, false, NULL, false, false, false, true, false},
      {"reel", false, false, NULL, false, false, true, true, false},
      {"testalign", true, false, NULL, false, false, false, true, false},
      {"testfull", true, true, NULL, false, false, false, true, true},
      {"save", true, true, NULL, false, false, false, false, false},
      {"tag", false, false, "tag", true, true, false, true, true},
      {"home", false, false, NULL, false, false, false, true, false},
      {"move", false, false, "character", false, false, false, true, false},
  };
  const JsonObject commands = json(get("/api/capabilities"))["commands"];

  for (size_t i = 0; i < sizeof(rows) / sizeof(rows[0]); i++) {
    const Row& expected = rows[i];
    const JsonObject row = commands[expected.name];
    TEST_ASSERT_EQUAL_MESSAGE(expected.usesAlign, row["uses_align"].as<bool>(),
                              expected.name);
    TEST_ASSERT_EQUAL_MESSAGE(expected.usesForce, row["uses_force"].as<bool>(),
                              expected.name);
    // A command that takes no text still has the field, as a null, so every
    // row has every field.
    TEST_ASSERT_TRUE_MESSAGE(row.containsKey("label_field"), expected.name);
    if (expected.labelField == NULL) {
      TEST_ASSERT_TRUE_MESSAGE(row["label_field"].isNull(), expected.name);
    } else {
      TEST_ASSERT_EQUAL_STRING_MESSAGE(expected.labelField,
                                       row["label_field"].as<const char*>(),
                                       expected.name);
    }
    TEST_ASSERT_EQUAL_MESSAGE(expected.fieldIsLabel,
                              row["field_is_label"].as<bool>(), expected.name);
    TEST_ASSERT_EQUAL_MESSAGE(expected.printsRun, row["prints_run"].as<bool>(),
                              expected.name);
    TEST_ASSERT_EQUAL_MESSAGE(expected.usesRollLength,
                              row["uses_roll_length"].as<bool>(),
                              expected.name);
    TEST_ASSERT_EQUAL_MESSAGE(expected.stoppable, row["stoppable"].as<bool>(),
                              expected.name);
    TEST_ASSERT_EQUAL_MESSAGE(expected.pressesLabel,
                              row["presses_label"].as<bool>(), expected.name);
  }
}

// What the device can do is read, never written, so a post to it is told to
// get.
void test_the_capabilities_posted_to_are_told_to_get(void) {
  const Reply reply = post("/api/capabilities", "{}");

  TEST_ASSERT_EQUAL_INT(405, reply.code);
  TEST_ASSERT_EQUAL_STRING("GET", reply.allow);
  TEST_ASSERT_EQUAL_STRING("Please use GET for /api/capabilities",
                           errorOf(reply));
}

// --- stop -------------------------------------------------------------------

// The big red button. A stop is taken at once and obeyed at the next point
// the job can stop, so the reply says it is stopping rather than stopped.
void test_a_stop_stops_what_is_running_now(void) {
  post("/api/tag", "{\"tag\":\"AB\",\"copies\":3}");

  const Reply reply = postStop(NULL);

  TEST_ASSERT_EQUAL_INT(200, reply.code);
  TEST_ASSERT_EQUAL_STRING("stopping", json(reply)["result"].as<const char*>());
  TEST_ASSERT_TRUE(machine->etkt.createStatus().stop == PendingStop::NOW);
}

// A run can instead stop once the label being pressed is cut, so it ends on
// a whole label rather than on one left half pressed on the tape.
void test_a_run_can_stop_once_the_label_being_pressed_is_cut(void) {
  post("/api/tag", "{\"tag\":\"AB\",\"copies\":3}");

  const Reply reply = postStop("label");

  TEST_ASSERT_EQUAL_INT(200, reply.code);
  TEST_ASSERT_EQUAL_STRING("stopping", json(reply)["result"].as<const char*>());
  TEST_ASSERT_TRUE(machine->etkt.createStatus().stop ==
                   PendingStop::AFTER_LABEL);
}

// A label is the only thing a stop can wait for. Asked to wait for anything
// else, the device stops nothing rather than guess which stop was meant.
void test_a_stop_after_anything_but_a_label_is_refused(void) {
  post("/api/tag", "{\"tag\":\"AB\",\"copies\":3}");

  const Reply reply = postStop("copy");

  TEST_ASSERT_EQUAL_INT(400, reply.code);
  TEST_ASSERT_EQUAL_STRING(
      "Please provide after=label to stop once the label being pressed is "
      "cut, or leave it out to stop now",
      errorOf(reply));
  TEST_ASSERT_TRUE(machine->etkt.createStatus().stop == PendingStop::NONE);
}

// A stop with nothing running is not an error. The job most likely finished
// while the tap was on its way, and the panel sees that on its next poll.
void test_a_stop_with_nothing_running_says_the_machine_is_idle(void) {
  const Reply reply = postStop(NULL);

  TEST_ASSERT_EQUAL_INT(200, reply.code);
  TEST_ASSERT_EQUAL_STRING("idle", json(reply)["result"].as<const char*>());
}

// Saving cannot be stopped: a stop partway through could leave half the
// calibration written. Refused as a conflict, since the same stop will work
// on the next command.
void test_a_stop_of_what_cannot_be_stopped_is_refused_as_a_conflict(void) {
  post("/api/save", "{\"align\":5,\"force\":5}");

  const Reply reply = postStop(NULL);

  TEST_ASSERT_EQUAL_INT(409, reply.code);
  TEST_ASSERT_EQUAL_STRING("The command running now cannot be stopped",
                           errorOf(reply));
}

// Only a run of labels has a label to stop after. Anything else asked to is
// refused, rather than stopped now when the operator asked it to wait.
void test_only_a_run_of_labels_can_stop_after_a_label(void) {
  post("/api/cut", "{}");

  const Reply reply = postStop("label");

  TEST_ASSERT_EQUAL_INT(409, reply.code);
  TEST_ASSERT_EQUAL_STRING("Only a run of labels can stop after a label",
                           errorOf(reply));
  TEST_ASSERT_TRUE(machine->etkt.createStatus().stop == PendingStop::NONE);
}

// A stop changes what the machine does, so it is a post. A get, which a
// browser may send again or ahead of time, stops nothing.
void test_a_stop_asked_for_with_get_is_told_to_post(void) {
  post("/api/tag", "{\"tag\":\"AB\",\"copies\":3}");

  const Reply reply = get("/api/stop");

  TEST_ASSERT_EQUAL_INT(405, reply.code);
  TEST_ASSERT_EQUAL_STRING("POST", reply.allow);
  TEST_ASSERT_TRUE(machine->etkt.createStatus().stop == PendingStop::NONE);
}

// --- log --------------------------------------------------------------------

// The log is read, never written, so a post to it is told to get.
void test_the_log_posted_to_is_told_to_get(void) {
  const Reply reply = post("/api/log", "{}");

  TEST_ASSERT_EQUAL_INT(405, reply.code);
  TEST_ASSERT_EQUAL_STRING("GET", reply.allow);
  TEST_ASSERT_EQUAL_STRING("Please use GET for /api/log", errorOf(reply));
}

// What the machine has been doing, oldest line first, as plain text: the one
// reader is somebody at the bench opening it in a phone browser to find out
// why the last label came out wrong.
void test_the_log_is_what_the_machine_logged_as_plain_text(void) {
  machine->logger.warn("tape may be out");
  machine->logger.log("print HELLO");

  const Reply reply = get("/api/log");

  TEST_ASSERT_EQUAL_INT(200, reply.code);
  TEST_ASSERT_EQUAL_STRING("text/plain", reply.contentType);
  const std::string body = reply.body.str();
  TEST_ASSERT_TRUE(body.find("WARN  tape may be out\n") != std::string::npos);
  TEST_ASSERT_TRUE(endsWith(body, " INFO  print HELLO"));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_a_path_nothing_answers_is_not_found);
  RUN_TEST(test_idle_is_not_a_command_anyone_can_post);
  RUN_TEST(test_a_command_asked_for_with_get_is_told_to_post);
  RUN_TEST(test_a_method_nothing_answers_to_is_told_the_one_that_is);
  RUN_TEST(test_a_command_is_handed_to_the_job_runner);
  RUN_TEST(test_a_command_while_another_runs_is_refused_as_a_conflict);
  RUN_TEST(test_a_command_that_trials_an_align_needs_one);
  RUN_TEST(test_an_align_out_of_range_is_refused_not_clamped);
  RUN_TEST(test_a_command_that_trials_a_force_needs_one);
  RUN_TEST(test_a_calibration_in_the_body_is_the_one_the_job_gets);
  RUN_TEST(test_a_label_without_its_text_is_refused);
  RUN_TEST(test_a_label_is_submitted_with_its_text);
  RUN_TEST(test_a_label_that_is_not_text_is_refused);
  RUN_TEST(test_a_label_too_long_to_print_is_refused);
  RUN_TEST(test_the_longest_label_of_symbols_is_accepted);
  RUN_TEST(test_a_label_the_wheel_cannot_print_is_refused);
  RUN_TEST(test_a_move_may_name_the_cut_mark);
  RUN_TEST(test_a_run_of_no_labels_is_refused);
  RUN_TEST(test_a_run_of_labels_is_submitted_with_its_length);
  RUN_TEST(test_copies_are_ignored_by_a_command_that_prints_no_labels);
  RUN_TEST(test_a_roll_too_short_to_be_one_is_refused);
  RUN_TEST(test_a_new_roll_is_declared_at_its_length);
  RUN_TEST(test_a_body_not_sent_as_json_is_refused);
  RUN_TEST(test_json_is_json_in_any_case_and_with_parameters);
  RUN_TEST(test_a_body_that_is_not_a_json_object_is_refused);
  RUN_TEST(test_json_that_is_not_an_object_is_refused);
  RUN_TEST(test_a_body_over_the_limit_is_refused_unread);
  RUN_TEST(test_a_body_with_more_fields_than_room_is_refused);
  RUN_TEST(test_an_idle_machine_says_it_is_idle);
  RUN_TEST(test_the_status_carries_the_saved_calibration);
  RUN_TEST(test_a_run_in_progress_says_which_label_it_is_on);
  RUN_TEST(test_a_command_that_prints_no_labels_says_nothing_of_them);
  RUN_TEST(test_a_stop_asked_for_is_reported_until_it_is_obeyed);
  RUN_TEST(test_a_stop_reports_what_it_cut_short);
  RUN_TEST(test_a_lost_wheel_is_reported_as_the_cause);
  RUN_TEST(test_the_status_says_what_is_left_on_the_roll);
  RUN_TEST(test_the_status_says_how_much_memory_is_free_and_the_uptime);
  RUN_TEST(test_the_longest_status_has_every_field);
  RUN_TEST(test_a_reply_too_large_for_its_room_is_a_failure_not_a_short_reply);
  RUN_TEST(test_a_failure_is_logged_as_well_as_answered);
  RUN_TEST(test_the_status_posted_to_is_told_to_get);
  RUN_TEST(test_the_capabilities_say_what_the_wheel_can_print);
  RUN_TEST(test_the_capabilities_give_the_ranges_the_device_checks);
  RUN_TEST(test_the_capabilities_say_how_much_tape_a_feed_takes);
  RUN_TEST(test_the_capabilities_list_every_command_that_can_be_asked_for);
  RUN_TEST(test_each_command_says_what_it_does);
  RUN_TEST(test_the_capabilities_posted_to_are_told_to_get);
  RUN_TEST(test_a_stop_stops_what_is_running_now);
  RUN_TEST(test_a_run_can_stop_once_the_label_being_pressed_is_cut);
  RUN_TEST(test_a_stop_after_anything_but_a_label_is_refused);
  RUN_TEST(test_a_stop_with_nothing_running_says_the_machine_is_idle);
  RUN_TEST(test_a_stop_of_what_cannot_be_stopped_is_refused_as_a_conflict);
  RUN_TEST(test_only_a_run_of_labels_can_stop_after_a_label);
  RUN_TEST(test_a_stop_asked_for_with_get_is_told_to_post);
  RUN_TEST(test_the_log_is_what_the_machine_logged_as_plain_text);
  RUN_TEST(test_the_log_posted_to_is_told_to_get);
  return UNITY_END();
}
