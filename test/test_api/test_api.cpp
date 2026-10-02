// Host-side tests for the Api, through the one call the webserver makes on
// it: handle(), which turns a Request into a Reply.
//
// Every answer the device gives the panel is decided there: which fields a
// command needs, what the device says when one is wrong, and what a status
// carries. Until these tests none of that could be checked without a board
// on wifi. The machine behind the Api is the real one, built by HostMachine
// with fakes where it meets the hardware, so a command the Api accepts is one
// the job runner really took. Its link is the real one too, on a FakeRadio,
// so what the Api says of the network is what the link came to.
//
// Run with:  pio test -e native
#include <unity.h>

#include <cstring>
#include <string>
#include <vector>

#include "Api.h"
#include "ArduinoJson.h"
#include "Configuration.h"
#include "FakeRadio.h"
#include "HostMachine.h"
#include "LinkSupervisor.h"
#include "NetworkSettings.h"

static HostMachine* machine;
static FakeRadio* radio;
static NetworkSettings* networkSettings;
// Stepped only by the tests that are about the network: see runLink().
static LinkSupervisor* supervisor;
static Api* api;

// Builds the machine, which boots it, and names what the tests work.
void setUp(void) {
  stubReset();
  machine = new HostMachine();
  radio = &machine->radio;
  networkSettings = &machine->networkSettings;
  supervisor = &machine->linkSupervisor;
  api = &machine->api;
}

void tearDown(void) { delete machine; }

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

// A post with no body and no type, for a route that reads neither.
static Reply postBare(const char* path) {
  Request request;
  request.method = Method::POST;
  request.path = path;
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

// A command sent under an id, the way the panel sends one it may have to send
// again: the id rides in the query string, beside the body.
static Reply postUnder(const char* id, const char* path, const char* body) {
  Request request;
  request.method = Method::POST;
  request.path = path;
  request.query["id"] = id;
  request.contentType = "application/json";
  request.body = body;
  return api->handle(request);
}

// A bare post sent under an id.
static Reply postBareUnder(const char* id, const char* path) {
  Request request;
  request.method = Method::POST;
  request.path = path;
  request.query["id"] = id;
  return api->handle(request);
}

// A stop sent under an id.
static Reply postStopUnder(const char* id, const char* after) {
  Request request;
  request.method = Method::POST;
  request.path = "/api/stop";
  request.query["id"] = id;
  if (after != NULL) {
    request.query["after"] = after;
  }
  return api->handle(request);
}

// A stop for one command alone, named by the id that command was sent under:
// what the panel sends when Stop is tapped before the command was answered.
static Reply postStopFor(const char* command, const char* id,
                         const char* after) {
  Request request;
  request.method = Method::POST;
  request.path = "/api/stop";
  request.query["id"] = id;
  request.query["for"] = command;
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

// The cutter never goes all the way through the tape, so a run can leave the
// cut out, and its labels come off with scissors. A body that says nothing
// of it is cut, as every run was before it could leave it out.
void test_a_run_is_cut_unless_the_body_leaves_the_cut_out(void) {
  const char* bodies[] = {"{\"tag\":\"AB\",\"copies\":2}",
                          "{\"tag\":\"AB\",\"copies\":2,\"cut\":true}",
                          "{\"tag\":\"AB\",\"copies\":2,\"cut\":false}"};
  // A and B, twice, and the cut mark after each label unless it is left out.
  const int strokes[] = {6, 6, 4};
  for (int i = 0; i < 3; i++) {
    machine->strokes.strokes.clear();
    TEST_ASSERT_EQUAL_INT_MESSAGE(200, post("/api/tag", bodies[i]).code,
                                  bodies[i]);

    machine->etkt.loop();

    TEST_ASSERT_EQUAL_INT_MESSAGE(
        strokes[i], (int)machine->strokes.strokes.size(), bodies[i]);
  }
}

// Anything but a JSON true or false is refused rather than read as one or
// the other: a run cut, or not, on a guess is a run to print again.
void test_a_cut_that_is_not_true_or_false_is_refused(void) {
  const char* bodies[] = {"{\"tag\":\"AB\",\"cut\":\"no\"}",
                          "{\"tag\":\"AB\",\"cut\":0}",
                          "{\"tag\":\"AB\",\"cut\":null}"};
  for (const char* body : bodies) {
    const Reply reply = post("/api/tag", body);

    TEST_ASSERT_EQUAL_INT_MESSAGE(400, reply.code, body);
    TEST_ASSERT_EQUAL_STRING_MESSAGE("Please provide cut as true or false",
                                     errorOf(reply), body);
    TEST_ASSERT_EQUAL_INT_MESSAGE((int)Command::IDLE, (int)running(), body);
  }
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
  TEST_ASSERT_FALSE(status.containsKey("remaining_ms"));
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

// While a run prints, the status also says how long one of its labels takes
// and how long the run has left, for the panel to count down from: what the
// machine says, at the moment of the poll.
void test_a_run_in_progress_says_how_long_it_has_left(void) {
  post("/api/tag", "{\"tag\":\"AB\",\"copies\":3}");
  static Reply during;
  static StatusUpdate machineSaid;
  during = Reply();
  stubAfterDelay() = [] {
    const StatusUpdate now = machine->etkt.createStatus();
    if (during.code == 0 && now.copy == 2 && now.progress == 50) {
      during = get("/api/status");
      machineSaid = machine->etkt.createStatus();
    }
  };

  machine->etkt.loop();

  const JsonObject status = json(during);
  TEST_ASSERT_TRUE(machineSaid.labelMs > 0);
  TEST_ASSERT_TRUE(machineSaid.remainingMs > 0);
  TEST_ASSERT_EQUAL_UINT32(machineSaid.labelMs,
                           status["label_ms"].as<uint32_t>());
  TEST_ASSERT_EQUAL_UINT32(machineSaid.remainingMs,
                           status["remaining_ms"].as<uint32_t>());
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
  TEST_ASSERT_FALSE(status.containsKey("label_ms"));
  TEST_ASSERT_FALSE(status.containsKey("remaining_ms"));
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
// say how many labels fit before anything has been printed. A feed is 3.7 mm
// of tape, so 250 of them take 925 mm off a 4 m roll.
void test_the_status_says_what_is_left_on_the_roll(void) {
  machine->roll.load(4000);
  machine->roll.use(250);

  const JsonObject roll = json(get("/api/status"))["roll"];

  TEST_ASSERT_EQUAL_INT(4000, roll["length_mm"].as<int>());
  TEST_ASSERT_EQUAL_INT(3075, roll["remaining_mm"].as<int>());
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
// bytes each, sent under the longest id, with a stop asked for. Every field
// still fits, rather than the last ones going missing without a word.
void test_the_longest_status_has_every_field(void) {
  std::string label;
  for (int i = 0; i < 249; i++) {
    label += "\u2661";
  }
  const std::string body = "{\"tag\":\"" + label + "\",\"copies\":500}";
  const char* id = "123e4567-e89b-12d3-a456-426614174000";
  postUnder(id, "/api/tag", body.c_str());
  machine->etkt.stopAfterLabel();

  const Reply reply = get("/api/status");

  TEST_ASSERT_EQUAL_INT(200, reply.code);
  const JsonObject status = json(reply);
  TEST_ASSERT_EQUAL_STRING(label.c_str(),
                           status["current_label"].as<const char*>());
  TEST_ASSERT_EQUAL_STRING(id, status["last_command_id"].as<const char*>());
  TEST_ASSERT_EQUAL_STRING("after_label", status["stop"].as<const char*>());
  TEST_ASSERT_TRUE(status["roll"]["remaining_mm"].is<long>());
  TEST_ASSERT_TRUE(status["uptime_ms"].is<unsigned long>());
}

// A reply that ran out of room is not sent with its last fields missing, the
// way ArduinoJson would leave it without a word. It is the device failing,
// so it is a 500. The Api refuses a label this long, but the job runner holds
// whatever it is handed, and this is how a field too many would show up.
void test_a_reply_too_large_for_its_room_is_a_failure_not_a_short_reply(void) {
  machine->etkt.submit(tagOptions(String(std::string(3000, 'A'))));

  const Reply reply = get("/api/status");

  TEST_ASSERT_EQUAL_INT(500, reply.code);
  TEST_ASSERT_EQUAL_STRING("The reply is larger than the device has room for",
                           errorOf(reply));
}

// A failure is logged as well as answered. The panel reads a poll that
// failed as the device being out of reach, and shows nothing of what the
// reply said, so the log is where somebody at the bench finds out why.
void test_a_failure_is_logged_as_well_as_answered(void) {
  machine->etkt.submit(tagOptions(String(std::string(3000, 'A'))));

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

  TEST_ASSERT_EQUAL_INT(3700, feed["length_um"].as<int>());
  TEST_ASSERT_EQUAL_INT(1, feed["lead"].as<int>());
}

// Every command the device can be asked for, by the name it answers to, and
// no other. Idle is a status, not a command, so it has no row.
void test_the_capabilities_list_every_command_that_can_be_asked_for(void) {
  const JsonObject commands = json(get("/api/capabilities"))["commands"];

  const char* const names[] = {"cut",       "feed",     "reel", "unload",
                               "testalign", "testfull", "save", "tag",
                               "home",      "move"};
  const int count = sizeof(names) / sizeof(names[0]);
  TEST_ASSERT_EQUAL_INT(count, (int)commands.size());
  for (int i = 0; i < count; i++) {
    TEST_ASSERT_TRUE_MESSAGE(commands.containsKey(names[i]), names[i]);
  }
}

// Each row says what its command does, which is what the panel offers a
// stop, counts a run and words a stopped command by. The expected rows are
// written out by hand from what each command is for, not read off the table
// they are checking.
void test_each_command_says_what_it_does(void) {
  struct Row {
    const char* name;
    bool usesAlign;
    bool usesForce;
    const char* textField;
    bool textIsLabel;
    bool printsRun;
    bool usesRollLength;
    bool stoppable;
    bool pressesLabel;
  };
  const Row rows[] = {
      // name        align  force  text         label  run    roll   stop
      // press
      {"cut", false, false, NULL, false, false, false, true, false},
      {"feed", false, false, NULL, false, false, false, true, false},
      {"reel", false, false, NULL, false, false, true, true, false},
      {"unload", false, false, NULL, false, false, false, true, false},
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
    TEST_ASSERT_TRUE_MESSAGE(row.containsKey("text_field"), expected.name);
    if (expected.textField == NULL) {
      TEST_ASSERT_TRUE_MESSAGE(row["text_field"].isNull(), expected.name);
    } else {
      TEST_ASSERT_EQUAL_STRING_MESSAGE(expected.textField,
                                       row["text_field"].as<const char*>(),
                                       expected.name);
    }
    TEST_ASSERT_TRUE_MESSAGE(row.containsKey("text_is_label"), expected.name);
    TEST_ASSERT_EQUAL_MESSAGE(expected.textIsLabel,
                              row["text_is_label"].as<bool>(), expected.name);
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

// A run can instead stop once the label being pressed is finished, so it
// ends on a whole label rather than on one left half pressed on the tape.
void test_a_run_can_stop_once_the_label_being_pressed_is_finished(void) {
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
      "finished, or leave it out to stop now",
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

// --- ids --------------------------------------------------------------------

// On a slow link the reply to a command can be lost after the device took
// the command, and the panel then sends it again. Sent under the id the
// first was accepted under, it is the same press of the button: the device
// says again that it took it, and runs nothing more.
void test_a_command_sent_again_under_its_id_does_not_run_twice(void) {
  TEST_ASSERT_EQUAL_INT(200, postUnder("press-1", "/api/cut", "{}").code);
  machine->etkt.loop();
  TEST_ASSERT_EQUAL_INT((int)Command::IDLE, (int)running());

  const Reply again = postUnder("press-1", "/api/cut", "{}");

  TEST_ASSERT_EQUAL_INT(200, again.code);
  TEST_ASSERT_EQUAL_STRING("success", json(again)["result"].as<const char*>());
  TEST_ASSERT_EQUAL_INT((int)Command::IDLE, (int)running());
}

// A new press of the same button is a new id, and runs again.
void test_a_command_under_a_new_id_runs_again(void) {
  postUnder("press-1", "/api/cut", "{}");
  machine->etkt.loop();

  const Reply reply = postUnder("press-2", "/api/cut", "{}");

  TEST_ASSERT_EQUAL_INT(200, reply.code);
  TEST_ASSERT_EQUAL_INT((int)Command::CUT, (int)running());
}

// A panel from before there were ids sends none, and so does curl. Each
// command sent that way is its own, as it always was.
void test_a_command_under_no_id_runs_each_time_it_is_sent(void) {
  post("/api/cut", "{}");
  machine->etkt.loop();

  const Reply reply = post("/api/cut", "{}");

  TEST_ASSERT_EQUAL_INT(200, reply.code);
  TEST_ASSERT_EQUAL_INT((int)Command::CUT, (int)running());
}

// A command the device refused ran nothing, so its id is not kept: sent
// again once the machine is free, it runs, which is what the press was for.
void test_a_command_that_was_refused_runs_when_it_is_sent_again(void) {
  post("/api/cut", "{}");
  TEST_ASSERT_EQUAL_INT(409, postUnder("press-1", "/api/feed", "{}").code);
  machine->etkt.loop();

  const Reply again = postUnder("press-1", "/api/feed", "{}");

  TEST_ASSERT_EQUAL_INT(200, again.code);
  TEST_ASSERT_EQUAL_INT((int)Command::FEED, (int)running());
}

// A stop is for the command that was running when it was pressed. Sent
// again after that command has ended and another has begun, it is told what
// it was told the first time, and the new command runs on.
void test_a_stop_sent_again_does_not_stop_the_next_command(void) {
  post("/api/tag", "{\"tag\":\"AB\",\"copies\":3}");
  TEST_ASSERT_EQUAL_STRING(
      "stopping",
      json(postStopUnder("stop-1", NULL))["result"].as<const char*>());
  machine->etkt.loop();
  post("/api/tag", "{\"tag\":\"AB\",\"copies\":3}");

  const Reply again = postStopUnder("stop-1", NULL);

  TEST_ASSERT_EQUAL_INT(200, again.code);
  TEST_ASSERT_EQUAL_STRING("stopping", json(again)["result"].as<const char*>());
  TEST_ASSERT_EQUAL_INT((int)Command::TAG, (int)running());
  TEST_ASSERT_TRUE(machine->etkt.createStatus().stop == PendingStop::NONE);
}

// A stop that found nothing running is kept as well, unlike a command that
// was refused: sent again, it must not stop a command that began after the
// press.
void test_a_stop_that_found_nothing_running_is_remembered_too(void) {
  TEST_ASSERT_EQUAL_STRING(
      "idle", json(postStopUnder("stop-1", NULL))["result"].as<const char*>());
  post("/api/tag", "{\"tag\":\"AB\",\"copies\":3}");

  const Reply again = postStopUnder("stop-1", NULL);

  TEST_ASSERT_EQUAL_STRING("idle", json(again)["result"].as<const char*>());
  TEST_ASSERT_TRUE(machine->etkt.createStatus().stop == PendingStop::NONE);
}

// An id is a few characters the panel makes up, and the device keeps eight
// of them. One longer than a UUID is refused rather than kept, and nothing
// runs.
void test_an_id_too_long_to_keep_is_refused(void) {
  const Reply command =
      postUnder("1234567890123456789012345678901234567", "/api/cut", "{}");

  TEST_ASSERT_EQUAL_INT(400, command.code);
  TEST_ASSERT_EQUAL_STRING("An id may be at most 36 bytes", errorOf(command));
  TEST_ASSERT_EQUAL_INT((int)Command::IDLE, (int)running());

  post("/api/tag", "{\"tag\":\"AB\",\"copies\":3}");
  const Reply stop =
      postStopUnder("1234567890123456789012345678901234567", NULL);

  TEST_ASSERT_EQUAL_INT(400, stop.code);
  TEST_ASSERT_EQUAL_STRING("An id may be at most 36 bytes", errorOf(stop));
  TEST_ASSERT_TRUE(machine->etkt.createStatus().stop == PendingStop::NONE);
}

// The longest id the device takes is as long as a UUID.
void test_an_id_as_long_as_a_uuid_is_kept(void) {
  const char* id = "123e4567-e89b-12d3-a456-426614174000";
  TEST_ASSERT_EQUAL_INT(200, postUnder(id, "/api/cut", "{}").code);
  machine->etkt.loop();

  postUnder(id, "/api/cut", "{}");

  TEST_ASSERT_EQUAL_INT((int)Command::IDLE, (int)running());
}

// The device keeps the newest eight ids. The ninth pushes out the first,
// which by then is far older than any panel still sends.
void test_the_newest_eight_ids_are_remembered(void) {
  const char* ids[] = {"1", "2", "3", "4", "5", "6", "7", "8", "9"};
  for (const char* id : ids) {
    TEST_ASSERT_EQUAL_INT_MESSAGE(200, postUnder(id, "/api/cut", "{}").code,
                                  id);
    machine->etkt.loop();
  }

  postUnder("2", "/api/cut", "{}");
  TEST_ASSERT_EQUAL_INT((int)Command::IDLE, (int)running());
  postUnder("9", "/api/cut", "{}");
  TEST_ASSERT_EQUAL_INT((int)Command::IDLE, (int)running());

  postUnder("1", "/api/cut", "{}");
  TEST_ASSERT_EQUAL_INT((int)Command::CUT, (int)running());
}

// On a slow link the reply to a command can be lost while the polls after it
// get through. The status names the id the command the device last took was
// sent under, so the panel that sent it learns from a poll that it arrived,
// and stops saying it has had no answer.
void test_the_status_names_the_id_of_the_command_the_device_took(void) {
  postUnder("press-1", "/api/cut", "{}");

  const JsonObject status = json(get("/api/status"));

  TEST_ASSERT_EQUAL_STRING("cut", status["command"].as<const char*>());
  TEST_ASSERT_EQUAL_STRING("press-1",
                           status["last_command_id"].as<const char*>());
}

// A short command can be over before a poll gets through. The status goes on
// naming it once it has ended, until the device takes another.
void test_the_status_names_a_command_after_it_has_ended(void) {
  postUnder("press-1", "/api/cut", "{}");
  machine->etkt.loop();

  const JsonObject status = json(get("/api/status"));

  TEST_ASSERT_FALSE(status["busy"].as<bool>());
  TEST_ASSERT_EQUAL_STRING("press-1",
                           status["last_command_id"].as<const char*>());
}

// A command the device refused was not taken, and neither was a stop, which
// is no command. The status goes on naming the command that was.
void test_the_status_names_only_a_command_the_device_took(void) {
  postUnder("press-1", "/api/tag", "{\"tag\":\"AB\",\"copies\":3}");
  TEST_ASSERT_EQUAL_INT(409, postUnder("press-2", "/api/feed", "{}").code);
  TEST_ASSERT_EQUAL_INT(200, postStopUnder("stop-1", "label").code);

  const JsonObject status = json(get("/api/status"));

  TEST_ASSERT_EQUAL_STRING("press-1",
                           status["last_command_id"].as<const char*>());
}

// A command sent under no id has none to be named by. The status then names
// no command, rather than the one before it, and names none before the
// device has taken any.
void test_the_status_names_no_id_for_a_command_sent_under_none(void) {
  TEST_ASSERT_FALSE(json(get("/api/status")).containsKey("last_command_id"));
  postUnder("press-1", "/api/cut", "{}");
  machine->etkt.loop();

  post("/api/cut", "{}");

  TEST_ASSERT_FALSE(json(get("/api/status")).containsKey("last_command_id"));
}

// How many ids the device keeps, which tells a panel that it keeps any: a
// panel sends a command again on its own only to a device that says so.
void test_the_capabilities_say_how_many_ids_are_remembered(void) {
  const JsonObject capabilities = json(get("/api/capabilities"));

  TEST_ASSERT_EQUAL_INT(8, capabilities["remembered_ids"].as<int>());
}

// --- a stop for one command -------------------------------------------------

// On a slow link Stop can be tapped before the command it is for has been
// answered, and the stop can then reach the device first. It names the
// command it is for, by the id that command was sent under, and the device
// does not start that command when it does arrive: the operator has already
// said stop.
void test_a_command_stopped_before_it_arrived_is_not_started(void) {
  const Reply stop = postStopFor("press-1", "stop-1", NULL);

  TEST_ASSERT_EQUAL_INT(200, stop.code);
  TEST_ASSERT_EQUAL_STRING("not_started",
                           json(stop)["result"].as<const char*>());

  const Reply command =
      postUnder("press-1", "/api/tag", "{\"tag\":\"AB\",\"copies\":3}");

  TEST_ASSERT_EQUAL_INT(409, command.code);
  TEST_ASSERT_EQUAL_STRING("Stopped before it started", errorOf(command));
  TEST_ASSERT_EQUAL_STRING("not_started",
                           json(command)["result"].as<const char*>());
  TEST_ASSERT_EQUAL_INT((int)Command::IDLE, (int)running());
}

// The usual case: the command arrived, and only the reply to it was lost. The
// stop that names it stops it, as any other stop would.
void test_a_stop_for_the_command_that_is_running_stops_it(void) {
  postUnder("press-1", "/api/tag", "{\"tag\":\"AB\",\"copies\":3}");

  const Reply reply = postStopFor("press-1", "stop-1", NULL);

  TEST_ASSERT_EQUAL_INT(200, reply.code);
  TEST_ASSERT_EQUAL_STRING("stopping", json(reply)["result"].as<const char*>());
  TEST_ASSERT_TRUE(machine->etkt.createStatus().stop == PendingStop::NOW);
}

// A run named by its stop can finish the label being pressed first, like any
// other run.
void test_a_stop_for_one_command_can_wait_for_the_label(void) {
  postUnder("press-1", "/api/tag", "{\"tag\":\"AB\",\"copies\":3}");

  const Reply reply = postStopFor("press-1", "stop-1", "label");

  TEST_ASSERT_EQUAL_INT(200, reply.code);
  TEST_ASSERT_EQUAL_STRING("stopping", json(reply)["result"].as<const char*>());
  TEST_ASSERT_TRUE(machine->etkt.createStatus().stop ==
                   PendingStop::AFTER_LABEL);
}

// A short command can be over before the stop for it arrives. That is the
// answer any stop gets that finds nothing running, and the command is not
// refused after the fact: sent again, it is told again that it was taken.
void test_a_stop_for_a_command_that_has_ended_says_the_machine_is_idle(void) {
  postUnder("press-1", "/api/cut", "{}");
  machine->etkt.loop();

  const Reply reply = postStopFor("press-1", "stop-1", NULL);

  TEST_ASSERT_EQUAL_INT(200, reply.code);
  TEST_ASSERT_EQUAL_STRING("idle", json(reply)["result"].as<const char*>());
  TEST_ASSERT_EQUAL_INT(200, postUnder("press-1", "/api/cut", "{}").code);
}

// The command a stop names has ended, and another phone has started a run
// since. The stop was never for that run, which goes on.
void test_a_stop_for_one_command_leaves_the_one_after_it_running(void) {
  postUnder("press-1", "/api/cut", "{}");
  machine->etkt.loop();
  postUnder("press-2", "/api/tag", "{\"tag\":\"AB\",\"copies\":3}");

  const Reply reply = postStopFor("press-1", "stop-1", NULL);

  TEST_ASSERT_EQUAL_INT(200, reply.code);
  TEST_ASSERT_EQUAL_STRING("idle", json(reply)["result"].as<const char*>());
  TEST_ASSERT_EQUAL_INT((int)Command::TAG, (int)running());
  TEST_ASSERT_TRUE(machine->etkt.createStatus().stop == PendingStop::NONE);
}

// The machine was busy with another phone's run, and refused this command in
// a reply that was lost. The stop for it leaves that run alone, and the
// command does not start once the machine is free.
void test_a_stop_for_a_command_refused_as_busy_keeps_it_from_starting(void) {
  postUnder("press-1", "/api/tag", "{\"tag\":\"AB\",\"copies\":3}");
  TEST_ASSERT_EQUAL_INT(409, postUnder("press-2", "/api/feed", "{}").code);

  const Reply stop = postStopFor("press-2", "stop-1", NULL);

  TEST_ASSERT_EQUAL_STRING("not_started",
                           json(stop)["result"].as<const char*>());
  TEST_ASSERT_TRUE(machine->etkt.createStatus().stop == PendingStop::NONE);
  machine->etkt.loop();
  const Reply again = postUnder("press-2", "/api/feed", "{}");
  TEST_ASSERT_EQUAL_INT(409, again.code);
  TEST_ASSERT_EQUAL_STRING("Stopped before it started", errorOf(again));
  TEST_ASSERT_EQUAL_INT((int)Command::IDLE, (int)running());
}

// Stop tapped twice is two stops for one command, each under its own id. The
// second is told what the first was, and the command stays refused.
void test_a_second_stop_for_a_command_that_never_arrived_says_the_same(void) {
  postStopFor("press-1", "stop-1", "label");

  const Reply second = postStopFor("press-1", "stop-2", NULL);

  TEST_ASSERT_EQUAL_INT(200, second.code);
  TEST_ASSERT_EQUAL_STRING("not_started",
                           json(second)["result"].as<const char*>());
  TEST_ASSERT_EQUAL_INT(409, postUnder("press-1", "/api/cut", "{}").code);
  TEST_ASSERT_EQUAL_INT((int)Command::IDLE, (int)running());
}

// The id a stop names its command by is no longer than any other. A longer
// one is refused, and nothing is stopped.
void test_a_stop_for_an_id_too_long_to_keep_is_refused(void) {
  post("/api/tag", "{\"tag\":\"AB\",\"copies\":3}");

  const Reply reply =
      postStopFor("1234567890123456789012345678901234567", "stop-1", NULL);

  TEST_ASSERT_EQUAL_INT(400, reply.code);
  TEST_ASSERT_EQUAL_STRING("An id may be at most 36 bytes", errorOf(reply));
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

// --- estimates --------------------------------------------------------------

// How long a run would take, asked with the body that would send it: what
// the panel shows for one label and for the whole set before it is sent.
// Worked out by the machine, and nothing started.
void test_a_run_is_estimated_from_the_body_that_would_send_it(void) {
  const Reply reply = post("/api/tag/estimate",
                           "{\"tag\":\" HELLO \",\"copies\":3,\"cut\":false}");

  TEST_ASSERT_EQUAL_INT(200, reply.code);
  const RunEstimate expected =
      machine->etkt.estimate(tagOptions(" HELLO ", 3, false));
  TEST_ASSERT_TRUE(expected.labelMs > 0);
  TEST_ASSERT_EQUAL_UINT32(expected.labelMs,
                           json(reply)["label_ms"].as<uint32_t>());
  TEST_ASSERT_EQUAL_UINT32(expected.runMs,
                           json(reply)["run_ms"].as<uint32_t>());
  TEST_ASSERT_EQUAL_INT((int)Command::IDLE, (int)running());
}

// The panel asks again as the operator types, whatever the machine is doing.
void test_a_run_can_be_estimated_while_another_prints(void) {
  TEST_ASSERT_EQUAL_INT(200, post("/api/tag", "{\"tag\":\"AB\"}").code);
  TEST_ASSERT_EQUAL_INT((int)Command::TAG, (int)running());

  const Reply reply = post("/api/tag/estimate", "{\"tag\":\"AB\"}");

  TEST_ASSERT_EQUAL_INT(200, reply.code);
  TEST_ASSERT_TRUE(json(reply)["run_ms"].as<uint32_t>() > 0);
}

// A body the run would refuse, its estimate refuses in the same words, so
// the operator hears of a mistake as they make it rather than once they send.
void test_an_estimate_refuses_what_the_run_would_refuse(void) {
  const char* bodies[] = {"{\"copies\":2}", "{\"tag\":\"AB\",\"copies\":0}",
                          "{\"tag\":\"AB\",\"cut\":1}", "[]", "{"};
  for (const char* body : bodies) {
    const Reply run = post("/api/tag", body);
    const std::string refusal = errorOf(run);

    const Reply estimate = post("/api/tag/estimate", body);

    TEST_ASSERT_EQUAL_INT_MESSAGE(400, estimate.code, body);
    TEST_ASSERT_EQUAL_STRING_MESSAGE(refusal.c_str(), errorOf(estimate), body);
  }
  const Reply untyped = postAs("", "/api/tag/estimate", "{\"tag\":\"AB\"}");
  TEST_ASSERT_EQUAL_INT(415, untyped.code);
  TEST_ASSERT_EQUAL_INT((int)Command::IDLE, (int)running());
}

void test_an_estimate_is_asked_for_with_a_post(void) {
  const Reply reply = get("/api/tag/estimate");

  TEST_ASSERT_EQUAL_INT(405, reply.code);
  TEST_ASSERT_EQUAL_STRING("POST", reply.allow);
  TEST_ASSERT_EQUAL_STRING("Please use POST for /api/tag/estimate",
                           errorOf(reply));
}

// Only a run of labels takes long enough to be worth estimating, so only a
// command whose row says it prints one has an estimate to ask for.
void test_only_a_run_of_labels_can_be_estimated(void) {
  const char* paths[] = {"/api/cut/estimate", "/api/idle/estimate",
                         "/api/estimate", "/api/tag/estimate/estimate"};
  for (const char* path : paths) {
    const Reply reply = post(path, "{}");

    TEST_ASSERT_EQUAL_INT_MESSAGE(404, reply.code, path);
    TEST_ASSERT_EQUAL_STRING_MESSAGE("Not found", errorOf(reply), path);
  }
}

// --- network ----------------------------------------------------------------

static const char* CHURCH_KEY = "battery staple";
static const char* BASEMENT_KEY = "correct horse";

// Lets time pass for the link, stepped as its task steps it on the board.
static void runLink(unsigned long ms) {
  for (unsigned long elapsed = 0; elapsed < ms; elapsed += WIFI_STEP_MS) {
    delay(WIFI_STEP_MS);
    supervisor->step();
  }
}

// A machine on the church's network, which is the one network it remembers.
static void joinChurch(void) {
  networkSettings->remember("Church", CHURCH_KEY);
  radio->add("Church", CHURCH_KEY);
  runLink(4000);
}

// The names in a list of them, separated by commas.
static std::string namesIn(JsonArrayConst list) {
  std::string names;
  for (JsonVariantConst name : list) {
    names += (names.empty() ? "" : ",") + std::string(name.as<const char*>());
  }
  return names;
}

// Everything the panel says about how the machine is reached is in one
// reply: the network it is on and its address there, the name it answers to,
// its own network, and the networks it remembers.
void test_a_machine_on_a_network_says_how_it_is_reached(void) {
  joinChurch();

  const Reply reply = get("/api/network");

  TEST_ASSERT_EQUAL_INT(200, reply.code);
  const JsonObject network = json(reply);
  TEST_ASSERT_EQUAL_STRING("join", network["mode"].as<const char*>());
  TEST_ASSERT_EQUAL_STRING("joined", network["station"].as<const char*>());
  TEST_ASSERT_EQUAL_STRING("Church", network["network"].as<const char*>());
  TEST_ASSERT_EQUAL_STRING("192.168.1.50",
                           network["address"].as<const char*>());
  TEST_ASSERT_EQUAL_STRING("e-tkt-9c4f.local",
                           network["host"].as<const char*>());
  TEST_ASSERT_EQUAL_STRING("E-TKT-9C4F",
                           network["own"]["name"].as<const char*>());
  TEST_ASSERT_FALSE(network["own"]["open"].as<bool>());
  TEST_ASSERT_EQUAL_INT(0, network["own"]["clients"].as<int>());
  TEST_ASSERT_EQUAL_STRING("192.168.4.1",
                           network["own"]["address"].as<const char*>());
  TEST_ASSERT_EQUAL_STRING("Church", namesIn(network["remembered"]).c_str());
  TEST_ASSERT_EQUAL_INT(4, network["max_remembered"].as<int>());
  TEST_ASSERT_TRUE(network["router_offered"].as<bool>());
  TEST_ASSERT_FALSE(network.containsKey("failure"));
}

// The panel checks a network before it sends it, and says how long the
// machine's own network takes to open and to close. What it checks against
// and what it says are the machine's own numbers, said in the same reply, so
// that the panel keeps no copy of them to fall behind.
void test_the_network_reply_says_the_limits_and_the_times_the_machine_goes_by(
    void) {
  const JsonObject network = json(get("/api/network"));

  TEST_ASSERT_EQUAL_INT(Radio::MAX_NAME_BYTES,
                        network["max_name_bytes"].as<int>());
  TEST_ASSERT_EQUAL_INT(Radio::MIN_PASSWORD_LENGTH,
                        network["min_password_bytes"].as<int>());
  TEST_ASSERT_EQUAL_INT(Radio::MAX_PASSWORD_LENGTH,
                        network["max_password_bytes"].as<int>());
  TEST_ASSERT_EQUAL_INT(WIFI_OWN_AFTER_MS,
                        network["own"]["opens_after_ms"].as<int>());
  TEST_ASSERT_EQUAL_INT(WIFI_OWN_LINGER_MS,
                        network["own"]["closes_after_ms"].as<int>());
}

// A machine that remembers no network has its own open, and says so, with
// how many phones are on it: the panel is then being read over that network.
// There is no network to name, and no address on one.
void test_a_machine_that_remembers_no_network_says_its_own_is_open(void) {
  radio->phones = 2;
  runLink(4000);

  const JsonObject network = json(get("/api/network"));

  TEST_ASSERT_EQUAL_STRING("join", network["mode"].as<const char*>());
  TEST_ASSERT_EQUAL_STRING("off", network["station"].as<const char*>());
  TEST_ASSERT_FALSE(network.containsKey("network"));
  TEST_ASSERT_FALSE(network.containsKey("address"));
  TEST_ASSERT_TRUE(network["own"]["open"].as<bool>());
  TEST_ASSERT_EQUAL_INT(2, network["own"]["clients"].as<int>());
  TEST_ASSERT_EQUAL_STRING("", namesIn(network["remembered"]).c_str());
}

// A machine set to run its own network says so, and tries none of the
// networks it still remembers.
void test_a_machine_running_its_own_network_says_so(void) {
  networkSettings->remember("Church", CHURCH_KEY);
  radio->add("Church", CHURCH_KEY);
  networkSettings->setMode(NetworkMode::OWN);
  runLink(4000);

  const JsonObject network = json(get("/api/network"));

  TEST_ASSERT_EQUAL_STRING("own", network["mode"].as<const char*>());
  TEST_ASSERT_EQUAL_STRING("off", network["station"].as<const char*>());
  TEST_ASSERT_TRUE(network["own"]["open"].as<bool>());
  TEST_ASSERT_EQUAL_STRING("Church", namesIn(network["remembered"]).c_str());
}

// The webserver is up before the link has taken its first step, and a
// request that gets in ahead of it is still answered: with what the machine
// keeps, and nothing joined or open yet.
void test_the_network_is_answered_for_before_the_link_has_started(void) {
  networkSettings->remember("Church", CHURCH_KEY);

  const Reply reply = get("/api/network");

  TEST_ASSERT_EQUAL_INT(200, reply.code);
  const JsonObject network = json(reply);
  TEST_ASSERT_EQUAL_STRING("off", network["station"].as<const char*>());
  TEST_ASSERT_EQUAL_STRING("E-TKT-9C4F",
                           network["own"]["name"].as<const char*>());
  TEST_ASSERT_FALSE(network["own"]["open"].as<bool>());
  TEST_ASSERT_EQUAL_STRING("Church", namesIn(network["remembered"]).c_str());
}

// The networks it remembers are named in the order it tries them, which is
// the one typed in last first.
void test_the_networks_it_remembers_are_named_in_the_order_they_are_tried(
    void) {
  networkSettings->remember("Basement", BASEMENT_KEY);
  networkSettings->remember("Church", CHURCH_KEY);

  const JsonObject network = json(get("/api/network"));

  TEST_ASSERT_EQUAL_STRING("Church,Basement",
                           namesIn(network["remembered"]).c_str());
}

// A machine that cannot get onto its network says which one it is trying,
// and what became of the last try as far as the radio could tell: the cause
// the panel has words for, and the radio's own number and name for it, which
// is what tells a wrong password from a weak signal for whoever knows them.
void test_a_machine_that_cannot_join_says_what_the_last_try_came_to(void) {
  networkSettings->remember("Basement", "wrong horse");
  radio->add("Basement", BASEMENT_KEY);
  runLink(4000);

  const JsonObject network = json(get("/api/network"));

  TEST_ASSERT_EQUAL_STRING("joining", network["station"].as<const char*>());
  TEST_ASSERT_EQUAL_STRING("Basement", network["network"].as<const char*>());
  TEST_ASSERT_FALSE(network.containsKey("address"));
  const JsonObject failure = network["failure"];
  TEST_ASSERT_EQUAL_STRING("Basement", failure["network"].as<const char*>());
  TEST_ASSERT_EQUAL_STRING("refused", failure["cause"].as<const char*>());
  TEST_ASSERT_EQUAL_INT(15, failure["reason"].as<int>());
  TEST_ASSERT_EQUAL_STRING("4WAY_HANDSHAKE_TIMEOUT",
                           failure["reason_name"].as<const char*>());
}

// A network that is not there: the commonest cause there is, for a machine
// that has been carried somewhere else, or a name typed wrong.
void test_a_network_that_is_not_there_is_named_as_the_cause(void) {
  networkSettings->remember("Garage", "password1");
  runLink(4000);

  const JsonObject failure = json(get("/api/network"))["failure"];

  TEST_ASSERT_EQUAL_STRING("Garage", failure["network"].as<const char*>());
  TEST_ASSERT_EQUAL_STRING("not_found", failure["cause"].as<const char*>());
  TEST_ASSERT_EQUAL_INT(201, failure["reason"].as<int>());
  TEST_ASSERT_EQUAL_STRING("NO_AP_FOUND",
                           failure["reason_name"].as<const char*>());
}

// A network that took the machine on and gave it no address. The radio has
// no number for that, so none is given.
void test_a_network_that_gives_no_address_is_named_as_the_cause(void) {
  networkSettings->remember("Garage", "password1");
  radio->add("Garage", "password1")->givesAddress = false;
  runLink(WIFI_DHCP_MS + 4000);

  const JsonObject failure = json(get("/api/network"))["failure"];

  TEST_ASSERT_EQUAL_STRING("no_address", failure["cause"].as<const char*>());
  TEST_ASSERT_FALSE(failure.containsKey("reason"));
  TEST_ASSERT_FALSE(failure.containsKey("reason_name"));
}

// A cause the panel has no words for still comes with what the radio said.
void test_a_cause_with_no_name_of_its_own_still_has_what_the_radio_said(void) {
  networkSettings->remember("Garage", "password1");
  FakeNetwork* garage = radio->add("Garage", "password1");
  garage->refusals = 5;
  garage->refusal = 2;
  runLink(4000);

  const JsonObject failure = json(get("/api/network"))["failure"];

  TEST_ASSERT_EQUAL_STRING("other", failure["cause"].as<const char*>());
  TEST_ASSERT_EQUAL_INT(2, failure["reason"].as<int>());
  TEST_ASSERT_EQUAL_STRING("AUTH_EXPIRE",
                           failure["reason_name"].as<const char*>());
}

// The fullest reply there is: as many networks as the machine remembers,
// with names as long as a name gets, the machine trying one of them after a
// try that failed, and its own network open with phones on it. Every field
// still fits.
void test_the_fullest_network_reply_has_every_field(void) {
  for (int i = 0; i < NetworkSettings::MAX_REMEMBERED; i++) {
    const std::string name = std::string(31, 'n') + std::to_string(i);
    TEST_ASSERT_TRUE(
        RememberRefusal::NONE ==
        networkSettings->remember(name.c_str(), std::string(63, 'p').c_str()));
  }
  radio->phones = WIFI_OWN_CLIENTS;
  runLink(WIFI_OWN_AFTER_MS + 10000);

  const Reply reply = get("/api/network");

  TEST_ASSERT_EQUAL_INT(200, reply.code);
  const JsonObject network = json(reply);
  TEST_ASSERT_EQUAL_STRING("joining", network["station"].as<const char*>());
  TEST_ASSERT_EQUAL_INT(32, strlen(network["network"].as<const char*>()));
  TEST_ASSERT_TRUE(network["own"]["open"].as<bool>());
  TEST_ASSERT_EQUAL_INT(WIFI_OWN_CLIENTS, network["own"]["clients"].as<int>());
  TEST_ASSERT_EQUAL_INT(NetworkSettings::MAX_REMEMBERED,
                        network["remembered"].size());
  for (JsonVariantConst name : network["remembered"].as<JsonArrayConst>()) {
    TEST_ASSERT_EQUAL_INT(32, strlen(name.as<const char*>()));
  }
  TEST_ASSERT_TRUE(network["router_offered"].is<bool>());
  const JsonObject failure = network["failure"];
  TEST_ASSERT_EQUAL_INT(32, strlen(failure["network"].as<const char*>()));
  TEST_ASSERT_EQUAL_STRING("NO_AP_FOUND",
                           failure["reason_name"].as<const char*>());
}

// A password goes into the machine and never comes out of it: not a
// network's, and not the one of its own network, which is read off its
// screen by somebody stood at it. Every reply that could carry one is looked
// through, and the log with them.
void test_no_reply_carries_a_password(void) {
  // A generator in place of the board's hardware one, so that the password
  // of its own network is not ten of one letter.
  static unsigned long state;
  state = 12345;
  stubRandom() = [](long howsmall, long howbig) {
    state = state * 1103515245UL + 12345UL;
    return howsmall +
           (long)((state >> 16) % (unsigned long)(howbig - howsmall));
  };
  radio->add("Church", CHURCH_KEY);
  radio->add("Basement", BASEMENT_KEY);
  std::vector<Reply> replies;

  replies.push_back(post("/api/network/remember",
                         "{\"ssid\":\"Basement\","
                         "\"password\":\"correct horse\"}"));
  replies.push_back(post("/api/network/remember",
                         "{\"ssid\":\"Church\","
                         "\"password\":\"battery staple\"}"));
  runLink(8000);
  replies.push_back(get("/api/network"));
  replies.push_back(postBare("/api/network/listen"));
  runLink(4000);
  replies.push_back(get("/api/network/nearby"));
  replies.push_back(post("/api/network/router", "{\"offered\":false}"));
  replies.push_back(post("/api/network/forget", "{\"ssid\":\"Basement\"}"));
  replies.push_back(post("/api/network/mode", "{\"mode\":\"own\"}"));
  runLink(8000);
  replies.push_back(get("/api/network"));
  replies.push_back(get("/api/status"));
  replies.push_back(get("/api/log"));

  const std::string own = networkSettings->ownPassword().str();
  TEST_ASSERT_EQUAL_INT(10, own.size());
  TEST_ASSERT_TRUE(radio->accessPointOpen);
  for (const Reply& reply : replies) {
    const std::string body = reply.body.str();
    TEST_ASSERT_EQUAL_INT(200, reply.code);
    TEST_ASSERT_TRUE(body.find(CHURCH_KEY) == std::string::npos);
    TEST_ASSERT_TRUE(body.find(BASEMENT_KEY) == std::string::npos);
    TEST_ASSERT_TRUE(body.find(own) == std::string::npos);
  }
}

// How the machine is reached is read, never written, so a post is told to
// get.
void test_what_says_how_the_machine_is_reached_is_asked_for_with_a_get(void) {
  const char* paths[] = {"/api/network", "/api/network/nearby"};
  for (const char* path : paths) {
    const Reply reply = post(path, "{}");

    TEST_ASSERT_EQUAL_INT_MESSAGE(405, reply.code, path);
    TEST_ASSERT_EQUAL_STRING_MESSAGE("GET", reply.allow, path);
  }
}

// The panel asks the machine to listen, and reads what it heard once the
// count of listens has gone past the one it was answered with: a listen
// takes seconds, and waits for a try that is under way. A bare post, as a
// stop is.
void test_a_listen_is_asked_for_and_what_it_heard_is_read_afterwards(void) {
  joinChurch();
  radio->add("Hall", "password1")->rssi = -45;
  radio->add("Cafe", "")->rssi = -80;

  const Reply asked = postBare("/api/network/listen");

  TEST_ASSERT_EQUAL_INT(200, asked.code);
  TEST_ASSERT_EQUAL_STRING("listening",
                           json(asked)["result"].as<const char*>());
  TEST_ASSERT_EQUAL_INT(0, json(asked)["after"].as<int>());
  JsonObject nearby = json(get("/api/network/nearby"));
  TEST_ASSERT_TRUE(nearby["listening"].as<bool>());
  TEST_ASSERT_EQUAL_INT(0, nearby["listens"].as<int>());
  TEST_ASSERT_EQUAL_INT(0, nearby["networks"].size());

  runLink(WIFI_STEP_MS);

  nearby = json(get("/api/network/nearby"));
  TEST_ASSERT_FALSE(nearby["listening"].as<bool>());
  TEST_ASSERT_EQUAL_INT(1, nearby["listens"].as<int>());
  const JsonArray networks = nearby["networks"];
  TEST_ASSERT_EQUAL_INT(3, networks.size());
  TEST_ASSERT_EQUAL_STRING("Hall", networks[0]["ssid"].as<const char*>());
  TEST_ASSERT_EQUAL_INT(-45, networks[0]["rssi"].as<int>());
  TEST_ASSERT_TRUE(networks[0]["secured"].as<bool>());
  TEST_ASSERT_EQUAL_STRING("Church", networks[1]["ssid"].as<const char*>());
  TEST_ASSERT_EQUAL_STRING("Cafe", networks[2]["ssid"].as<const char*>());
  TEST_ASSERT_EQUAL_INT(-80, networks[2]["rssi"].as<int>());
  TEST_ASSERT_FALSE(networks[2]["secured"].as<bool>());
}

// The fullest list there is: as many networks as are listed, each with a
// name as long as a name gets. Every one still has every field.
void test_the_fullest_list_of_networks_in_reach_has_every_field(void) {
  joinChurch();
  for (int i = 0; i < 20; i++) {
    const std::string name =
        std::string(30, 'n') + (i < 10 ? "0" : "") + std::to_string(i);
    radio->add(name.c_str(), "password1")->rssi = -30 - i;
  }
  postBare("/api/network/listen");
  runLink(WIFI_STEP_MS);

  const Reply reply = get("/api/network/nearby");

  TEST_ASSERT_EQUAL_INT(200, reply.code);
  const JsonArray networks = json(reply)["networks"];
  TEST_ASSERT_EQUAL_INT(LinkSupervisor::MAX_NEARBY, networks.size());
  for (JsonObject network : networks) {
    TEST_ASSERT_EQUAL_INT(32, strlen(network["ssid"].as<const char*>()));
    TEST_ASSERT_TRUE(network["rssi"].is<int>());
    TEST_ASSERT_TRUE(network["secured"].is<bool>());
  }
}

// The mode is changed by name. The reply is how the machine is reached as
// /api/network gives it, so the panel shows the change without asking again.
void test_the_mode_is_changed_by_name(void) {
  const Reply reply = post("/api/network/mode", "{\"mode\":\"own\"}");

  TEST_ASSERT_EQUAL_INT(200, reply.code);
  TEST_ASSERT_EQUAL_STRING("own", json(reply)["mode"].as<const char*>());
  TEST_ASSERT_TRUE(NetworkMode::OWN == networkSettings->mode());

  post("/api/network/mode", "{\"mode\":\"join\"}");

  TEST_ASSERT_TRUE(NetworkMode::JOIN == networkSettings->mode());
}

// A mode the machine does not have is refused with the two it has, and the
// mode it is in stays.
void test_a_mode_the_machine_does_not_have_is_refused(void) {
  networkSettings->setMode(NetworkMode::OWN);
  const char* bodies[] = {"{\"mode\":\"both\"}", "{\"mode\":1}", "{}"};
  for (const char* body : bodies) {
    const Reply reply = post("/api/network/mode", body);

    TEST_ASSERT_EQUAL_INT_MESSAGE(400, reply.code, body);
    TEST_ASSERT_EQUAL_STRING_MESSAGE("Please provide mode as join or own",
                                     errorOf(reply), body);
  }
  TEST_ASSERT_TRUE(NetworkMode::OWN == networkSettings->mode());
}

// A network is remembered from its name and password, and is the first one
// tried from then on. Added a second time, it is still remembered once.
void test_a_network_is_remembered_from_its_name_and_password(void) {
  networkSettings->remember("Church", CHURCH_KEY);
  const char* body = "{\"ssid\":\"Basement\",\"password\":\"correct horse\"}";

  const Reply reply = post("/api/network/remember", body);
  const Reply again = post("/api/network/remember", body);

  TEST_ASSERT_EQUAL_INT(200, reply.code);
  TEST_ASSERT_EQUAL_STRING("Basement,Church",
                           namesIn(json(reply)["remembered"]).c_str());
  TEST_ASSERT_EQUAL_INT(200, again.code);
  const std::vector<RememberedNetwork> networks = networkSettings->networks();
  TEST_ASSERT_EQUAL_INT(2, networks.size());
  TEST_ASSERT_EQUAL_STRING("Basement", networks[0].ssid.c_str());
  TEST_ASSERT_EQUAL_STRING(BASEMENT_KEY, networks[0].password.c_str());
}

// A network sent with no password is one that has none, whether the password
// is left out, empty or null.
void test_a_network_sent_without_a_password_is_one_that_has_none(void) {
  const char* bodies[] = {"{\"ssid\":\"Cafe\"}",
                          "{\"ssid\":\"Cafe\",\"password\":\"\"}",
                          "{\"ssid\":\"Cafe\",\"password\":null}"};
  for (const char* body : bodies) {
    networkSettings->forget("Cafe");

    const Reply reply = post("/api/network/remember", body);

    TEST_ASSERT_EQUAL_INT_MESSAGE(200, reply.code, body);
    const std::vector<RememberedNetwork> networks = networkSettings->networks();
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, networks.size(), body);
    TEST_ASSERT_EQUAL_STRING_MESSAGE("", networks[0].password.c_str(), body);
  }
}

// A name comes back as it went in, whatever JSON has to escape in it.
void test_a_name_json_has_to_escape_comes_back_as_it_went_in(void) {
  post("/api/network/remember",
       "{\"ssid\":\"Dad's \\\"fast\\\" \\\\ Caf\xC3\xA9\","
       "\"password\":\"correct horse\"}");

  const JsonObject network = json(get("/api/network"));

  TEST_ASSERT_EQUAL_STRING("Dad's \"fast\" \\ Caf\xC3\xA9",
                           network["remembered"][0].as<const char*>());
}

// A network the machine cannot keep is refused in words the panel shows as
// they are, and nothing is remembered. A password that is not text is not
// read as no password: that would remember an open network nobody asked for.
void test_a_network_the_machine_cannot_keep_is_refused_in_words(void) {
  const std::string longName = "{\"ssid\":\"" + std::string(33, 'n') + "\"}";
  const std::string longPassword =
      "{\"ssid\":\"Church\",\"password\":\"" + std::string(64, 'p') + "\"}";
  const struct {
    const char* body;
    const char* refusal;
  } cases[] = {
      {"{}", "Please provide an ssid value"},
      {"{\"ssid\":\"\"}", "Please provide an ssid value"},
      {"{\"ssid\":7}", "Please provide an ssid value"},
      {longName.c_str(), "A network's name may be at most 32 bytes, got 33"},
      {"{\"ssid\":\"Line\\nbreak\"}", "A network's name must be plain text"},
      {"{\"ssid\":\"Church\",\"password\":\"seven77\"}",
       "A network's password must be at least 8 characters, got 7"},
      {longPassword.c_str(),
       "A network's password may be at most 63 characters, got 64"},
      {"{\"ssid\":\"Church\",\"password\":12345678}",
       "Please provide password as text, or leave it out for a network "
       "without one"},
  };
  for (const auto& one : cases) {
    const Reply reply = post("/api/network/remember", one.body);

    TEST_ASSERT_EQUAL_INT_MESSAGE(400, reply.code, one.body);
    TEST_ASSERT_EQUAL_STRING_MESSAGE(one.refusal, errorOf(reply), one.body);
  }
  TEST_ASSERT_EQUAL_INT(0, networkSettings->networks().size());
}

// The machine remembers only so many, and says so when it is full: a 409,
// since the same network is taken once another is forgotten. A network it
// remembers already can still have its password put right.
void test_one_network_more_than_it_remembers_is_refused_as_a_conflict(void) {
  for (int i = 0; i < NetworkSettings::MAX_REMEMBERED; i++) {
    networkSettings->remember(("Network " + std::to_string(i)).c_str(),
                              "password1");
  }

  const Reply reply = post("/api/network/remember",
                           "{\"ssid\":\"Church\","
                           "\"password\":\"battery staple\"}");

  TEST_ASSERT_EQUAL_INT(409, reply.code);
  TEST_ASSERT_EQUAL_STRING(
      "The device remembers at most 4 networks, so forget one first",
      errorOf(reply));

  const Reply putRight = post("/api/network/remember",
                              "{\"ssid\":\"Network 0\","
                              "\"password\":\"password2\"}");

  TEST_ASSERT_EQUAL_INT(200, putRight.code);
}

// A network is forgotten by its name. One the machine does not remember is
// forgotten already, so a forget sent again does no harm.
void test_a_network_is_forgotten_by_its_name(void) {
  networkSettings->remember("Basement", BASEMENT_KEY);
  networkSettings->remember("Church", CHURCH_KEY);
  const char* body = "{\"ssid\":\"Basement\"}";

  const Reply reply = post("/api/network/forget", body);
  const Reply again = post("/api/network/forget", body);

  TEST_ASSERT_EQUAL_INT(200, reply.code);
  TEST_ASSERT_EQUAL_STRING("Church",
                           namesIn(json(reply)["remembered"]).c_str());
  TEST_ASSERT_EQUAL_INT(200, again.code);
  TEST_ASSERT_EQUAL_INT(1, networkSettings->networks().size());
}

// A forget that names no network forgets none.
void test_a_forget_that_names_no_network_is_refused(void) {
  networkSettings->remember("Church", CHURCH_KEY);
  const char* bodies[] = {"{}", "{\"ssid\":\"\"}", "{\"ssid\":true}"};
  for (const char* body : bodies) {
    const Reply reply = post("/api/network/forget", body);

    TEST_ASSERT_EQUAL_INT_MESSAGE(400, reply.code, body);
    TEST_ASSERT_EQUAL_STRING_MESSAGE("Please provide an ssid value",
                                     errorOf(reply), body);
  }
  TEST_ASSERT_EQUAL_INT(1, networkSettings->networks().size());
}

// Whether its own network offers a router is set with true or false and
// nothing else. ArduinoJson reads any value as a bool, "no" as true.
void test_whether_its_own_network_offers_a_router_is_set_strictly(void) {
  const Reply reply = post("/api/network/router", "{\"offered\":false}");

  TEST_ASSERT_EQUAL_INT(200, reply.code);
  TEST_ASSERT_FALSE(json(reply)["router_offered"].as<bool>());
  TEST_ASSERT_FALSE(networkSettings->routerOffered());

  const char* bodies[] = {"{\"offered\":\"yes\"}", "{\"offered\":1}", "{}"};
  for (const char* body : bodies) {
    const Reply refused = post("/api/network/router", body);

    TEST_ASSERT_EQUAL_INT_MESSAGE(400, refused.code, body);
    TEST_ASSERT_EQUAL_STRING_MESSAGE("Please provide offered as true or false",
                                     errorOf(refused), body);
  }
  TEST_ASSERT_FALSE(networkSettings->routerOffered());
}

// What changes the network reads its body as a command's is read: sent as
// JSON, an object, and no longer than the device reads whole. Nothing changes
// for a body that is refused.
void test_what_changes_the_network_reads_its_body_as_a_command_does(void) {
  const char* paths[] = {"/api/network/mode", "/api/network/remember",
                         "/api/network/forget", "/api/network/router"};
  const std::string tooLong =
      "{\"ssid\":\"" + std::string(Api::MAX_BODY_BYTES, 'n') + "\"}";
  for (const char* path : paths) {
    const Reply typed = postAs("text/plain", path, "{\"mode\":\"own\"}");
    TEST_ASSERT_EQUAL_INT_MESSAGE(415, typed.code, path);
    TEST_ASSERT_EQUAL_STRING_MESSAGE("Please send the body as application/json",
                                     errorOf(typed), path);

    const Reply list = post(path, "[]");
    TEST_ASSERT_EQUAL_INT_MESSAGE(400, list.code, path);
    TEST_ASSERT_EQUAL_STRING_MESSAGE("The body must be a JSON object",
                                     errorOf(list), path);

    const Reply cutShort = post(path, "{\"ssid\":\"Church\",\"offered\":fa");
    TEST_ASSERT_EQUAL_INT_MESSAGE(400, cutShort.code, path);

    const Reply large = post(path, tooLong.c_str());
    TEST_ASSERT_EQUAL_INT_MESSAGE(413, large.code, path);
    TEST_ASSERT_EQUAL_STRING_MESSAGE("The body may be at most 2048 bytes",
                                     errorOf(large), path);
  }
  TEST_ASSERT_TRUE(NetworkMode::JOIN == networkSettings->mode());
  TEST_ASSERT_EQUAL_INT(0, networkSettings->networks().size());
  TEST_ASSERT_TRUE(networkSettings->routerOffered());
}

// Each of them changes something, or sets the radio to work, so each is a
// post, and a get is told so.
void test_what_changes_the_network_is_asked_for_with_a_post(void) {
  const char* paths[] = {"/api/network/listen", "/api/network/mode",
                         "/api/network/remember", "/api/network/forget",
                         "/api/network/router"};
  for (const char* path : paths) {
    const Reply reply = get(path);

    TEST_ASSERT_EQUAL_INT_MESSAGE(405, reply.code, path);
    TEST_ASSERT_EQUAL_STRING_MESSAGE("POST", reply.allow, path);
  }
  TEST_ASSERT_FALSE(supervisor->nearby().listening);
}

// On a slow link the reply to a change can be lost after the device made the
// change, and the panel then sends the change again. A network sent as it is
// kept already is otherwise somebody asking for another try at it, and the
// link drops the try that is under way for that. Sent again under its id it
// is the same tap: the try under way runs on, and the machine joins.
void test_a_network_sent_again_under_its_id_does_not_start_its_try_over(void) {
  radio->add("Church", CHURCH_KEY)->joinMs = 8000;
  const char* body = "{\"ssid\":\"Church\",\"password\":\"battery staple\"}";
  TEST_ASSERT_EQUAL_INT(200,
                        postUnder("tap-1", "/api/network/remember", body).code);
  runLink(4000);
  TEST_ASSERT_EQUAL_STRING(
      "joining", json(get("/api/network"))["station"].as<const char*>());

  const Reply again = postUnder("tap-1", "/api/network/remember", body);
  runLink(6000);

  TEST_ASSERT_EQUAL_INT(200, again.code);
  TEST_ASSERT_EQUAL_STRING("Church",
                           namesIn(json(again)["remembered"]).c_str());
  TEST_ASSERT_EQUAL_INT(1, radio->joins.size());
  TEST_ASSERT_EQUAL_INT(0, radio->leaves);
  TEST_ASSERT_EQUAL_STRING(
      "joined", json(get("/api/network"))["station"].as<const char*>());
}

// A new tap is a new id, and so is a request sent under none. The same
// network sent that way is somebody asking for another try at it, as it was
// before there were ids, and the link is told.
void test_the_same_network_under_a_new_id_asks_for_another_try(void) {
  const char* body = "{\"ssid\":\"Church\",\"password\":\"battery staple\"}";
  postUnder("tap-1", "/api/network/remember", body);
  const uint32_t first = networkSettings->revision();

  postUnder("tap-1", "/api/network/remember", body);
  TEST_ASSERT_EQUAL_UINT32(first, networkSettings->revision());

  postUnder("tap-2", "/api/network/remember", body);
  const uint32_t second = networkSettings->revision();
  TEST_ASSERT_TRUE(second != first);

  post("/api/network/remember", body);
  TEST_ASSERT_TRUE(networkSettings->revision() != second);
  TEST_ASSERT_EQUAL_INT(1, networkSettings->networks().size());
}

// A change that comes again late, after another one was made, is the tap it
// was the first time, and that tap has had its turn: it does not undo what
// came after it. It is answered with the network as it is by then, which is
// what the panel draws.
void test_a_change_sent_again_under_its_id_does_not_undo_the_one_after_it(
    void) {
  const char* own = "{\"mode\":\"own\"}";
  const char* forget = "{\"ssid\":\"Basement\"}";
  const char* noRouter = "{\"offered\":false}";
  networkSettings->remember("Basement", BASEMENT_KEY);
  postUnder("tap-1", "/api/network/mode", own);
  postUnder("tap-2", "/api/network/forget", forget);
  postUnder("tap-3", "/api/network/router", noRouter);
  post("/api/network/mode", "{\"mode\":\"join\"}");
  post("/api/network/remember",
       "{\"ssid\":\"Basement\",\"password\":\"correct horse\"}");
  post("/api/network/router", "{\"offered\":true}");

  const Reply replies[] = {
      postUnder("tap-1", "/api/network/mode", own),
      postUnder("tap-2", "/api/network/forget", forget),
      postUnder("tap-3", "/api/network/router", noRouter),
  };

  for (const Reply& reply : replies) {
    TEST_ASSERT_EQUAL_INT(200, reply.code);
    const JsonObject network = json(reply);
    TEST_ASSERT_EQUAL_STRING("join", network["mode"].as<const char*>());
    TEST_ASSERT_EQUAL_STRING("Basement",
                             namesIn(network["remembered"]).c_str());
    TEST_ASSERT_TRUE(network["router_offered"].as<bool>());
  }
  TEST_ASSERT_TRUE(NetworkMode::JOIN == networkSettings->mode());
  TEST_ASSERT_EQUAL_INT(1, networkSettings->networks().size());
  TEST_ASSERT_TRUE(networkSettings->routerOffered());
}

// A change the device refused changed nothing, so its id is not kept: sent
// again once there is room for the network, it is made, which is what the
// tap was for.
void test_a_change_that_was_refused_is_made_when_it_is_sent_again(void) {
  for (int i = 0; i < NetworkSettings::MAX_REMEMBERED; i++) {
    networkSettings->remember(("Network " + std::to_string(i)).c_str(),
                              "password1");
  }
  const char* body = "{\"ssid\":\"Church\",\"password\":\"battery staple\"}";
  TEST_ASSERT_EQUAL_INT(409,
                        postUnder("tap-1", "/api/network/remember", body).code);
  post("/api/network/forget", "{\"ssid\":\"Network 0\"}");

  const Reply again = postUnder("tap-1", "/api/network/remember", body);

  TEST_ASSERT_EQUAL_INT(200, again.code);
  TEST_ASSERT_EQUAL_STRING("Church",
                           json(again)["remembered"][0].as<const char*>());
}

// The reply to a listen can be lost as well, and the listen may have ended
// by the time the panel asks again. Asked again under its id it is answered
// as it was, with the count its list comes after, and the radio is not taken
// away a second time. A new id is a new listen.
void test_a_listen_sent_again_under_its_id_does_not_listen_again(void) {
  joinChurch();
  const Reply asked = postBareUnder("tap-1", "/api/network/listen");
  runLink(WIFI_STEP_MS);
  TEST_ASSERT_EQUAL_INT(0, json(asked)["after"].as<int>());
  TEST_ASSERT_EQUAL_INT(1,
                        json(get("/api/network/nearby"))["listens"].as<int>());

  const Reply again = postBareUnder("tap-1", "/api/network/listen");

  TEST_ASSERT_EQUAL_INT(200, again.code);
  TEST_ASSERT_EQUAL_STRING("listening",
                           json(again)["result"].as<const char*>());
  TEST_ASSERT_EQUAL_INT(0, json(again)["after"].as<int>());
  TEST_ASSERT_FALSE(supervisor->nearby().listening);

  const Reply next = postBareUnder("tap-2", "/api/network/listen");

  TEST_ASSERT_EQUAL_INT(1, json(next)["after"].as<int>());
  TEST_ASSERT_TRUE(supervisor->nearby().listening);
}

// An id too long to keep is refused here as it is for a command, and nothing
// changes.
void test_a_change_under_an_id_too_long_to_keep_is_refused(void) {
  const char* id = "1234567890123456789012345678901234567";
  const struct {
    const char* path;
    const char* body;
  } changes[] = {
      {"/api/network/mode", "{\"mode\":\"own\"}"},
      {"/api/network/remember", "{\"ssid\":\"Cafe\"}"},
      {"/api/network/forget", "{\"ssid\":\"Church\"}"},
      {"/api/network/router", "{\"offered\":false}"},
      {"/api/network/listen", "{}"},
  };
  networkSettings->remember("Church", CHURCH_KEY);
  for (const auto& change : changes) {
    const Reply reply = postUnder(id, change.path, change.body);

    TEST_ASSERT_EQUAL_INT_MESSAGE(400, reply.code, change.path);
    TEST_ASSERT_EQUAL_STRING_MESSAGE("An id may be at most 36 bytes",
                                     errorOf(reply), change.path);
  }
  TEST_ASSERT_TRUE(NetworkMode::JOIN == networkSettings->mode());
  TEST_ASSERT_EQUAL_STRING("Church",
                           networkSettings->networks()[0].ssid.c_str());
  TEST_ASSERT_EQUAL_INT(1, networkSettings->networks().size());
  TEST_ASSERT_TRUE(networkSettings->routerOffered());
  TEST_ASSERT_FALSE(supervisor->nearby().listening);
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
  RUN_TEST(test_a_run_is_cut_unless_the_body_leaves_the_cut_out);
  RUN_TEST(test_a_cut_that_is_not_true_or_false_is_refused);
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
  RUN_TEST(test_a_run_in_progress_says_how_long_it_has_left);
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
  RUN_TEST(test_a_run_can_stop_once_the_label_being_pressed_is_finished);
  RUN_TEST(test_a_stop_after_anything_but_a_label_is_refused);
  RUN_TEST(test_a_stop_with_nothing_running_says_the_machine_is_idle);
  RUN_TEST(test_a_stop_of_what_cannot_be_stopped_is_refused_as_a_conflict);
  RUN_TEST(test_only_a_run_of_labels_can_stop_after_a_label);
  RUN_TEST(test_a_stop_asked_for_with_get_is_told_to_post);
  RUN_TEST(test_a_command_sent_again_under_its_id_does_not_run_twice);
  RUN_TEST(test_a_command_under_a_new_id_runs_again);
  RUN_TEST(test_a_command_under_no_id_runs_each_time_it_is_sent);
  RUN_TEST(test_a_command_that_was_refused_runs_when_it_is_sent_again);
  RUN_TEST(test_a_stop_sent_again_does_not_stop_the_next_command);
  RUN_TEST(test_a_stop_that_found_nothing_running_is_remembered_too);
  RUN_TEST(test_an_id_too_long_to_keep_is_refused);
  RUN_TEST(test_an_id_as_long_as_a_uuid_is_kept);
  RUN_TEST(test_the_newest_eight_ids_are_remembered);
  RUN_TEST(test_the_status_names_the_id_of_the_command_the_device_took);
  RUN_TEST(test_the_status_names_a_command_after_it_has_ended);
  RUN_TEST(test_the_status_names_only_a_command_the_device_took);
  RUN_TEST(test_the_status_names_no_id_for_a_command_sent_under_none);
  RUN_TEST(test_the_capabilities_say_how_many_ids_are_remembered);
  RUN_TEST(test_a_command_stopped_before_it_arrived_is_not_started);
  RUN_TEST(test_a_stop_for_the_command_that_is_running_stops_it);
  RUN_TEST(test_a_stop_for_one_command_can_wait_for_the_label);
  RUN_TEST(test_a_stop_for_a_command_that_has_ended_says_the_machine_is_idle);
  RUN_TEST(test_a_stop_for_one_command_leaves_the_one_after_it_running);
  RUN_TEST(test_a_stop_for_a_command_refused_as_busy_keeps_it_from_starting);
  RUN_TEST(test_a_second_stop_for_a_command_that_never_arrived_says_the_same);
  RUN_TEST(test_a_stop_for_an_id_too_long_to_keep_is_refused);
  RUN_TEST(test_the_log_is_what_the_machine_logged_as_plain_text);
  RUN_TEST(test_the_log_posted_to_is_told_to_get);
  RUN_TEST(test_a_run_is_estimated_from_the_body_that_would_send_it);
  RUN_TEST(test_a_run_can_be_estimated_while_another_prints);
  RUN_TEST(test_an_estimate_refuses_what_the_run_would_refuse);
  RUN_TEST(test_an_estimate_is_asked_for_with_a_post);
  RUN_TEST(test_only_a_run_of_labels_can_be_estimated);
  RUN_TEST(test_a_machine_on_a_network_says_how_it_is_reached);
  RUN_TEST(
      test_the_network_reply_says_the_limits_and_the_times_the_machine_goes_by);
  RUN_TEST(test_a_machine_that_remembers_no_network_says_its_own_is_open);
  RUN_TEST(test_a_machine_running_its_own_network_says_so);
  RUN_TEST(test_the_network_is_answered_for_before_the_link_has_started);
  RUN_TEST(
      test_the_networks_it_remembers_are_named_in_the_order_they_are_tried);
  RUN_TEST(test_a_machine_that_cannot_join_says_what_the_last_try_came_to);
  RUN_TEST(test_a_network_that_is_not_there_is_named_as_the_cause);
  RUN_TEST(test_a_network_that_gives_no_address_is_named_as_the_cause);
  RUN_TEST(test_a_cause_with_no_name_of_its_own_still_has_what_the_radio_said);
  RUN_TEST(test_the_fullest_network_reply_has_every_field);
  RUN_TEST(test_no_reply_carries_a_password);
  RUN_TEST(test_what_says_how_the_machine_is_reached_is_asked_for_with_a_get);
  RUN_TEST(test_a_listen_is_asked_for_and_what_it_heard_is_read_afterwards);
  RUN_TEST(test_the_fullest_list_of_networks_in_reach_has_every_field);
  RUN_TEST(test_the_mode_is_changed_by_name);
  RUN_TEST(test_a_mode_the_machine_does_not_have_is_refused);
  RUN_TEST(test_a_network_is_remembered_from_its_name_and_password);
  RUN_TEST(test_a_network_sent_without_a_password_is_one_that_has_none);
  RUN_TEST(test_a_name_json_has_to_escape_comes_back_as_it_went_in);
  RUN_TEST(test_a_network_the_machine_cannot_keep_is_refused_in_words);
  RUN_TEST(test_one_network_more_than_it_remembers_is_refused_as_a_conflict);
  RUN_TEST(test_a_network_is_forgotten_by_its_name);
  RUN_TEST(test_a_forget_that_names_no_network_is_refused);
  RUN_TEST(test_whether_its_own_network_offers_a_router_is_set_strictly);
  RUN_TEST(test_what_changes_the_network_reads_its_body_as_a_command_does);
  RUN_TEST(test_what_changes_the_network_is_asked_for_with_a_post);
  RUN_TEST(test_a_network_sent_again_under_its_id_does_not_start_its_try_over);
  RUN_TEST(test_the_same_network_under_a_new_id_asks_for_another_try);
  RUN_TEST(
      test_a_change_sent_again_under_its_id_does_not_undo_the_one_after_it);
  RUN_TEST(test_a_change_that_was_refused_is_made_when_it_is_sent_again);
  RUN_TEST(test_a_listen_sent_again_under_its_id_does_not_listen_again);
  RUN_TEST(test_a_change_under_an_id_too_long_to_keep_is_refused);
  return UNITY_END();
}
