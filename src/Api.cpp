#include "Api.h"

#include <strings.h>

#include <vector>

#include "ArduinoJson.h"
#include "CharacterSet.h"
#include "Configuration.h"
#include "PressGeometry.h"
#include "Tape.h"
#include "Utility.h"
#include "esp_heap_caps.h"

static const char* const JSON_TYPE = "application/json";
static const char* const TEXT_TYPE = "text/plain";

// Room for a reply of one field: an error, or a result.
static const size_t ONE_FIELD_JSON_BYTES = 512;

// Room for a command's body once parsed.
static const size_t REQUEST_JSON_BYTES = 2048;

// Room for a capabilities reply, which lists every command with each of its
// facts: about a hundred values to a status's thirty, and near 1.7 KB on the
// board once the descriptor table is in it.
static const size_t CAPABILITIES_JSON_BYTES = 4096;

// Room for a status reply. ArduinoJson drops a field that does not fit
// without a word, and at the library's default of 1024 bytes a status
// carrying the longest label of symbols -- three bytes each -- has room for
// two more fields.
static const size_t STATUS_JSON_BYTES = 2048;

// The document as the body of a reply. A document that ran out of room has
// dropped the fields that did not fit without a word, and is not sent short:
// that is the device failing, and the reply says so instead. Written out
// rather than built, so it cannot run out of room itself.
static Reply jsonReply(int code, const JsonDocument& doc) {
  if (doc.overflowed()) {
    Reply failure = {500, JSON_TYPE,
                     String("{\"error\":\"The reply is larger than the device "
                            "has room for\"}"),
                     NULL};
    return failure;
  }
  std::vector<char> text(measureJson(doc) + 1);
  serializeJson(doc, text.data(), text.size());
  Reply reply = {code, JSON_TYPE, String(text.data()), NULL};
  return reply;
}

// A refusal, which the panel shows the operator as it is.
static Reply errorReply(int code, const String& message) {
  DynamicJsonDocument doc(ONE_FIELD_JSON_BYTES);
  doc["error"] = message;
  return jsonReply(code, doc);
}

// A path asked for with a method it does not answer to. HTTP asks for the
// one it does answer to in an Allow header; the words say it too, for
// whoever reads the reply rather than its headers.
static Reply wrongMethod(const Request& request, Method allowed) {
  const char* name = allowed == Method::POST ? "POST" : "GET";
  Reply reply =
      errorReply(405, String("Please use ") + name + " for " + request.path);
  reply.allow = name;
  return reply;
}

// Whether a Content-Type says JSON. The type is not case-sensitive, and may
// carry parameters after it: "application/json; charset=utf-8".
static bool isJson(const String& contentType) {
  static const char type[] = "application/json";
  static const size_t length = sizeof(type) - 1;
  if (strncasecmp(contentType.c_str(), type, length) != 0) {
    return false;
  }
  const char after = contentType.c_str()[length];
  return after == '\0' || after == ';' || after == ' ' || after == '\t';
}

// Reads one 1-9 calibration field out of the request body. On success it
// writes the value through and returns true; otherwise it writes what to tell
// the caller and returns false, so a handler can chain the fields it needs
// and let the first failure stand. askFor is the refusal for a missing
// field, "Please provide an align value", which a value out of range adds
// the range to.
//
// Refusing out-of-range values here rather than clamping them is the point:
// Settings and pressPeakAngle() both clamp as a backstop, but a clamp is
// silent -- the panel would report success while the machine used a different
// number than the one on screen.
static bool readCalibrationField(const JsonObjectConst& body, const char* field,
                                 const char* askFor, int* value,
                                 String* refusal) {
  if (!body.containsKey(field)) {
    *refusal = askFor;
    return false;
  }
  const int parsed = body[field].as<int>();
  if (!isValidCalibrationValue(parsed)) {
    *refusal = String(askFor) + " between " + CALIBRATION_VALUE_MIN + " and " +
               CALIBRATION_VALUE_MAX + ", got " + parsed;
    return false;
  }
  *value = parsed;
  return true;
}

// Reads the body fields this command declares it needs. Returns false with
// the refusal written if one is missing or out of range, so the caller can
// stop at the first failure.
static bool readCommandOptions(const CommandSpec* spec,
                               const JsonObjectConst& body,
                               CommandOptions* options, String* refusal) {
  if (spec->usesAlign &&
      !readCalibrationField(body, "align", "Please provide an align value",
                            &options->align, refusal)) {
    return false;
  }
  if (spec->usesForce &&
      !readCalibrationField(body, "force", "Please provide a force value",
                            &options->force, refusal)) {
    return false;
  }
  if (spec->labelField != NULL) {
    const char* text = body[spec->labelField].as<const char*>();
    if (text == NULL) {
      *refusal = String("Please provide a ") + spec->labelField + " value";
      return false;
    }
    options->label = text;

    // Only for a field that carries a label. A move's field names a slot on
    // the wheel instead, and DaisyWheel::move() is the one that knows which
    // slots exist.
    if (spec->fieldIsLabel) {
      const int length = Utility::characters(options->label).size();
      if (length > MAX_LABEL_CHARACTERS) {
        *refusal = String("A ") + spec->labelField + " may be at most " +
                   MAX_LABEL_CHARACTERS + " characters, got " + length;
        return false;
      }
      const String unprintable = unprintableCharacter(options->label);
      if (unprintable.length() > 0) {
        *refusal = String("The daisy wheel cannot print '") + unprintable + "'";
        return false;
      }
    }
  }

  // Optional, unlike the fields above: a body without them asks for what every
  // body asked for before they existed -- one label, and a new roll as long as
  // the last.
  if (spec->printsRun && body.containsKey("copies")) {
    const int copies = body["copies"].as<int>();
    if (!isValidCopies(copies)) {
      *refusal = String("Please provide a copies value between 1 and ") +
                 MAX_COPIES + ", got " + copies;
      return false;
    }
    options->copies = copies;
  }
  if (spec->usesRollLength && body.containsKey("length_mm")) {
    const int length = body["length_mm"].as<int>();
    if (!isValidRollLength(length)) {
      *refusal = String("Please provide a length_mm value between ") +
                 ROLL_LENGTH_MIN_MM + " and " + ROLL_LENGTH_MAX_MM + ", got " +
                 length;
      return false;
    }
    options->rollLengthMm = length;
  }
  return true;
}

const size_t Api::MAX_BODY_BYTES;

Api::Api(ETKT* etkt, Logger* logger) {
  this->etkt = etkt;
  this->logger = logger;
}

Reply Api::handle(const Request& request) {
  const Reply reply = this->route(request);
  // A 500 is the device failing rather than the caller. The panel reads a
  // poll that failed as the device being out of reach and shows nothing of
  // what the reply said, so it goes in the log too, where somebody at the
  // bench can find it.
  if (reply.code == 500) {
    this->logger->error(request.path + " failed with 500: " + reply.body);
  }
  return reply;
}

Reply Api::route(const Request& request) {
  // One route per command, straight off the table in ETKT.cpp. A command
  // added there gets its endpoint here for free, and cannot get one whose
  // name disagrees with the name /api/status reports for it.
  static const char prefix[] = "/api/";
  if (request.path.startsWith(prefix)) {
    const CommandSpec* spec =
        commandSpecByName(request.path.substring(sizeof(prefix) - 1));
    // Nothing to run means nothing to post to.
    if (spec != NULL && spec->run != NULL) {
      return request.method == Method::POST
                 ? this->command(spec, request)
                 : wrongMethod(request, Method::POST);
    }
  }

  if (request.path == "/api/stop") {
    return request.method == Method::POST ? this->stop(request)
                                          : wrongMethod(request, Method::POST);
  }

  if (request.path == "/api/capabilities") {
    return request.method == Method::GET ? this->capabilities()
                                         : wrongMethod(request, Method::GET);
  }

  if (request.path == "/api/status") {
    return request.method == Method::GET ? this->status()
                                         : wrongMethod(request, Method::GET);
  }

  // What the machine has been doing. Plain text, because the only reader is
  // somebody stood at the bench opening http://e-tkt.local/api/log in a
  // phone browser to find out why the last label came out wrong.
  if (request.path == "/api/log") {
    return request.method == Method::GET ? this->log()
                                         : wrongMethod(request, Method::GET);
  }
  return errorReply(404, "Not found");
}

// Every command endpoint. There used to be nine of these, alike down to the
// catch block, and the differences that mattered -- which fields the body
// must carry -- were buried in the sameness. The table in ETKT.cpp holds
// those differences now and this reads them.
Reply Api::command(const CommandSpec* spec, const Request& request) {
  if (!isJson(request.contentType)) {
    return errorReply(415, "Please send the body as application/json");
  }
  if (request.body.length() > MAX_BODY_BYTES) {
    return errorReply(
        413, String("The body may be at most ") + MAX_BODY_BYTES + " bytes");
  }

  DynamicJsonDocument parsed(REQUEST_JSON_BYTES);
  const DeserializationError error =
      deserializeJson(parsed, request.body.c_str(), request.body.length());
  if (error == DeserializationError::NoMemory) {
    return errorReply(413, "The body has more fields than the device can read");
  }
  if (error || !parsed.is<JsonObject>()) {
    return errorReply(400, "The body must be a JSON object");
  }
  const JsonObjectConst body = parsed.as<JsonObjectConst>();

  CommandOptions options;
  options.command = spec->command;
  String refusal;
  if (!readCommandOptions(spec, body, &options, &refusal)) {
    return errorReply(400, refusal);
  }

  try {
    this->etkt->submit(options);
  } catch (const PrinterBusyException& e) {
    // 409, not 400. The request was fine; the machine was not. A caller
    // that gets a 400 has something to fix in what it sent, and retrying
    // the same body would be pointless -- here it is the only sensible
    // thing to do.
    return errorReply(409, e.what());
  } catch (const std::exception& e) {
    // Nothing else escapes submit() today. If something does it is the
    // device failing, not the caller.
    return errorReply(500, e.what());
  }

  DynamicJsonDocument doc(ONE_FIELD_JSON_BYTES);
  doc["result"] = "success";
  return jsonReply(200, doc);
}

// What the device is doing, which the panel polls once a second.
Reply Api::status() {
  DynamicJsonDocument doc(STATUS_JSON_BYTES);
  const StatusUpdate status = this->etkt->createStatus();
  doc["progress"] = status.progress;
  doc["busy"] = status.currentCommand != Command::IDLE;
  // Named from the table, so this is the same string /api/<name> answers to.
  doc["command"] = commandName(status.currentCommand);
  doc["align"] = status.align;
  doc["force"] = status.force;

  // The label being pressed, and where the run of them is.
  const CommandSpec* running = commandSpec(status.currentCommand);
  if (running != NULL && running->printsRun) {
    doc["current_label"] = status.currentLabel;
    doc["copy"] = status.copy;
    doc["copies"] = status.copies;
  }

  // A stop that has been asked for and not yet obeyed, so a panel opened
  // partway through a stop says so too. Left out when there is none.
  if (status.stop != PendingStop::NONE) {
    doc["stop"] = status.stop == PendingStop::NOW ? "now" : "after_label";
  }

  // What the last stop cut short, until the next command is accepted. Left
  // out when the last command was not stopped, or stopped with nothing left
  // to cut short.
  if (status.stopped.command != Command::IDLE) {
    const JsonObject stopped = doc.createNestedObject("stopped");
    stopped["id"] = status.stopped.id;
    stopped["command"] = commandName(status.stopped.command);
    stopped["cause"] =
        status.stopped.cause == StopCause::HOMING ? "homing" : "operator";
    const CommandSpec* cutShort = commandSpec(status.stopped.command);
    if (cutShort != NULL && cutShort->printsRun) {
      stopped["printed"] = status.stopped.printed;
      stopped["copies"] = status.stopped.copies;
    }
    stopped["unfinished"] = status.stopped.unfinished;
  }

  // What is estimated to be left on the roll, busy or not, so the panel can
  // say how many labels fit before anything has been printed. The panel
  // works that out; see labelsThatFit() in Tape.h.
  const JsonObject roll = doc.createNestedObject("roll");
  roll["length_mm"] = status.roll.lengthMm;
  roll["remaining_mm"] =
      (long)remainingMm(status.roll.lengthMm, status.roll.feedsUsed);

  doc["mem_heap_free_bytes"] = heap_caps_get_free_size(MALLOC_CAP_8BIT);
  doc["mem_largest_free_block_bytes"] =
      heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
  doc["uptime_ms"] = millis();
  return jsonReply(200, doc);
}

// The big red button. What to stop after is in the query string rather than
// a body, so a stop is one bare POST.
Reply Api::stop(const Request& request) {
  const std::map<String, String>::const_iterator after =
      request.query.find("after");
  const bool afterLabel = after != request.query.end();
  if (afterLabel && after->second != "label") {
    return errorReply(400,
                      "Please provide after=label to stop once the label "
                      "being pressed is cut, or leave it out to stop now");
  }
  DynamicJsonDocument doc(ONE_FIELD_JSON_BYTES);
  switch (afterLabel ? this->etkt->stopAfterLabel() : this->etkt->stop()) {
    case StopResult::STOPPING:
      doc["result"] = "stopping";
      return jsonReply(200, doc);
    case StopResult::IDLE:
      // Not an error. The job most likely finished while the tap was on its
      // way, and the panel is about to see that on its next poll anyway.
      doc["result"] = "idle";
      return jsonReply(200, doc);
    case StopResult::UNSTOPPABLE:
      break;
  }
  return errorReply(409, afterLabel
                             ? "Only a run of labels can stop after a label"
                             : "The command running now cannot be stopped");
}

// Everything the panel needs to know about what this device will accept,
// fetched once at page load. Two things used to be written down on both
// sides of the wire and disagree.
//
// The character set was in five places: the wheel map, a stale comment in
// ETKT.cpp, a regex in script.js, a duplicate of that regex, and a hint
// line in index.html that promised less than the wheel could do.
//
// The 1-9 calibration range was in four: CALIBRATION_VALUE_MIN and MAX in
// PressGeometry.h, min and max attributes on two inputs in index.html, and
// a literal in calibrationValuesReady(). Only the constants reach the check
// that actually refuses a bad value, so the other three were a promise the
// panel made on the device's behalf.
Reply Api::capabilities() {
  DynamicJsonDocument doc(CAPABILITIES_JSON_BYTES);

  doc["printable"] = printableCharacters();

  const JsonObject aliases = doc.createNestedObject("aliases");
  for (std::map<String, String>::const_iterator it = CHARACTER_ALIASES.begin();
       it != CHARACTER_ALIASES.end(); ++it) {
    aliases[it->first.c_str()] = it->second.c_str();
  }

  // The range align and force are both offered in. readCalibrationField()
  // refuses anything outside it, so this is the panel being told the same
  // rule rather than carrying its own copy.
  const JsonObject calibration = doc.createNestedObject("calibration");
  calibration["min"] = CALIBRATION_VALUE_MIN;
  calibration["max"] = CALIBRATION_VALUE_MAX;

  // The shortest label the device will print. The panel pads up to it so the
  // device does not have to, which is what keeps short labels centred.
  const JsonObject label = doc.createNestedObject("label");
  label["minimum"] = MIN_LABEL_CHARACTERS;
  label["maximum"] = MAX_LABEL_CHARACTERS;

  // How many labels one request may ask for. readCommandOptions() refuses
  // anything outside this.
  const JsonObject copies = doc.createNestedObject("copies");
  copies["minimum"] = 1;
  copies["maximum"] = MAX_COPIES;

  // The lengths a new roll may be declared at, and the usual length of one:
  // what a device starts on, and what the panel suggests.
  const JsonObject roll = doc.createNestedObject("roll");
  roll["minimum_mm"] = ROLL_LENGTH_MIN_MM;
  roll["maximum_mm"] = ROLL_LENGTH_MAX_MM;
  roll["default_mm"] = DEFAULT_ROLL_LENGTH_MM;

  // What the panel needs to work out how much tape a label takes: how far a
  // feed moves it, and the blank feed ahead of every label. The rest of the
  // rule -- a feed per character, and the top-up to label.minimum -- is
  // labelFeeds() in Tape.h, which data/tape.js restates. The worked cases in
  // test/vectors/tape.json hold both to the same answers.
  const JsonObject feed = doc.createNestedObject("feed");
  feed["length_um"] = FEED_LENGTH_UM;
  feed["lead"] = LEAD_FEEDS;

  // Every command that can actually be asked for -- the same rows route()
  // answers a post for -- by name, with everything its row says. The panel
  // keeps its own wording for each, which is copy rather than protocol, but
  // it keeps no list of its own of what the device can do or of which
  // command does what: it offers a stop, and counts a run, by this.
  const JsonObject commands = doc.createNestedObject("commands");
  for (size_t i = 0; i < ETKT::COMMAND_COUNT; i++) {
    const CommandSpec& spec = ETKT::COMMANDS[i];
    if (spec.run == NULL) {
      continue;
    }
    const JsonObject row = commands.createNestedObject(spec.name);
    row["uses_align"] = spec.usesAlign;
    row["uses_force"] = spec.usesForce;
    // Null for a command that takes no text, so every row has every field.
    row["label_field"] = spec.labelField;
    row["field_is_label"] = spec.fieldIsLabel;
    row["prints_run"] = spec.printsRun;
    row["uses_roll_length"] = spec.usesRollLength;
    row["stoppable"] = spec.stoppable;
    row["presses_label"] = spec.pressesLabel;
  }
  return jsonReply(200, doc);
}

// The last lines the device logged, oldest first. Until this existed the only
// way to read them was a USB cable and a serial monitor, which is a problem
// for a machine that is on a bench, on wifi, and printing a label wrong.
Reply Api::log() {
  Reply reply = {200, TEXT_TYPE, this->logger->recent(), NULL};
  return reply;
}
