#pragma once

#include <Arduino.h>
#include <FreeRTOS.h>

#include <map>
#include <mutex>
#include <thread>

#include "BenchRigs.h"
#include "Characters.h"
#include "Configuration.h"
#include "DaisyWheel.h"
#include "Display.h"
#include "Feeder.h"
#include "HallSwitch.h"
#include "Light.h"
#include "Logger.h"
#include "Press.h"
#include "Settings.h"
#include "Sound.h"

/**
 * @brief The different types of command the E-TKT can execute.
 *
 * The enumerator is a command's identity inside the firmware. Everything
 * else about it -- the name it answers to on the wire, which of the
 * calibration and label fields it reads, and the code that carries it out --
 * lives in one row of ETKT::COMMANDS. Adding a command means adding an
 * enumerator and a row and nothing else; a static_assert in ETKT.cpp fails
 * the build if the two ever fall out of step.
 */
enum Command {
  CUT = 0,
  FEED = 1,
  REEL = 2,
  TEST_ALIGN = 3,
  TEST_FULL = 4,
  SAVE = 5,
  TAG = 6,
  HOME = 7,
  MOVE = 8,
  IDLE = 9
};

// Declared here only so CommandSpec below can name a handler on it.
class ETKT;

/**
 * @brief Everything outside the ETKT needs to know about one command.
 *
 * One row per enumerator, in ETKT::COMMANDS. The device used to restate this
 * list in four places -- a name switch, a factory method per command, a
 * dispatch switch, and the route table in Network.cpp -- and they drifted.
 */
struct CommandSpec {
  Command command;

  // The name the command answers to outside the device: the string
  // /api/status reports, and the path the webapp posts to, /api/<name>.
  const char* name;

  // Which of CommandOptions' optional fields this command reads, and so
  // which fields /api/<name> requires in its body. A field a command does
  // not read is ignored rather than refused, so a stale cached script.js
  // that sends too much still works.
  bool usesAlign;
  bool usesForce;

  // The body field this command's text arrives in, or NULL if it takes none.
  // Two commands take one and they disagree on the name: a tag is a whole
  // label, a move is a single character.
  const char* labelField;

  // Whether what arrives in that field is a label -- text to emboss -- rather
  // than the name of a slot on the wheel. A label is checked character by
  // character against what a label may contain, and the request is refused
  // when one of them is not. Without that check DaisyWheel::move() refuses
  // the character on its own and the press comes down regardless, on
  // whichever slot the wheel last stopped at.
  //
  // A move's field is not a label: it names a slot, the cut mark included,
  // and no label may say that. So the two commands that carry text need the
  // two answers, which is why this cannot be read off labelField.
  bool fieldIsLabel;

  // The handler ETKT::loop() runs for this command. NULL means there is
  // nothing to run: IDLE is a status, not a job.
  void (ETKT::*run)();
};

/**
 * @brief Returns the row for a command, or NULL if the command has no row.
 */
const CommandSpec* commandSpec(Command command);

/**
 * @brief Returns the row whose name matches, or NULL if none does.
 *
 * The name is the bare command, "cut", not the route, "/api/cut".
 */
const CommandSpec* commandSpecByName(const String& name);

/**
 * @brief Returns the wire name of a command, or "unknown" if it has no row.
 */
const char* commandName(Command command);

/**
 * @brief A struct for storing the options for a command.
 *
 * The struct includes the superset of options a command can include. The
 * command's row in ETKT::COMMANDS says which of them that command reads.
 */
struct CommandOptions {
  Command command = Command::IDLE;
  String label = "";
  int align = 0;
  int force = 0;
};

/**
 * @brief One consistent look at what the device is doing right now.
 *
 * A snapshot, not a view: the command and its progress are read together
 * under one lock, so the percentage reported here belongs to the command
 * reported beside it. Served to the webapp by GET /api/status, which polls
 * once a second.
 *
 * currentCommand is IDLE when nothing is running, and the other fields are
 * then at their defaults. Whether the device is busy is that comparison and
 * nothing else -- there is no separate flag to keep in step with it.
 */
struct StatusUpdate {
  int progress = 0;  // percent, 0 to 99. See Progress.h.
  int align = 0;
  int force = 0;
  String currentLabel = "";
  Command currentCommand = Command::IDLE;
};

class PrinterBusyException : public std::exception
{
public:
    const char *what() const throw()
    {
        return "The printer is already busy executing a command.";
    }
};

class ETKT {
 private:
  // Device hardware
  Logger* logger;
  Light* ledFinish;
  Light* ledChar;
  Settings* settings;
  Display* display;
  DaisyWheel* daisywheel;
  HallSwitch* hall;
  Feeder* feeder;
  Press* press;
  Sound* sound;
  Characters* characters;

  // Temporary. Delete with the rest of BenchRigs once machine 3 is finished.
  BenchRigs* benchRigs;

  // Device state, which should onyl ever be modified inside an exclusive lock.
  CommandOptions* command = NULL;
  int progress;  // percent, 0 to 99. See Progress.h.
  std::mutex* lock;

  // Event group that the main loop blocks on for new commands.
  EventGroupHandle_t eventGroup;

  /**
   * @brief Cuts the tape at the saved force calibration.
   */
  void cut();

  /**
   * @brief Cuts the tape at the given force, 1 to 9.
   *
   * Separate from cut() rather than a defaulted parameter: force is a 1-9
   * value, so there is no number left over to mean "caller did not say".
   * The full test button is the one caller that has a force of its own --
   * the one being trialled -- so the cut is made at the same setting as the
   * characters it just stamped.
   */
  void cutAt(int force);

  /**
   * Interanl handlers for each type of command the device can do.
   */
  void cutCommandInternal();
  void feedCommandInternal();
  void reelCommandInternal();
  void testCommandInternal();
  void testCommandFullInternal();
  void saveCommandInternal();
  void homeCommandInternal();
  void moveCommandInternal();
  void tagCommandInternal();

 public:
  ETKT(Logger* logger, Settings* settings, Characters* characters,
       Display* display, DaisyWheel* daisywheel, HallSwitch* hall,
       Feeder* feeder, Press* press, Sound* sound, Light* ledFinish,
       Light* ledChar, BenchRigs* benchRigs);
  ~ETKT();

  /**
   * @brief Initializes the E-TKT by initializing each component of hardware.
   */
  void initialize();

  /**
   * The main loop for the device, which proceses commands as they come in.
   */
  void loop();

  /**
   * @brief Queues a command, or refuses it if one is already running.
   *
   * The caller fills in whichever of align, force and label the command's
   * row in COMMANDS says it reads; anything else in `options` is ignored.
   * Throws PrinterBusyException if a command is already in flight, and
   * queues nothing in that case.
   *
   * The device takes a copy, so the caller keeps what it passed in either
   * way.
   */
  void submit(const CommandOptions& options);

  /**
   * @brief One row per Command: the firmware's only list of what exists.
   *
   * Public because the webserver registers its routes from it and reports
   * command names out of it. The order is not meaningful and the index is
   * not the enumerator, so reach rows through commandSpec() or
   * commandSpecByName() rather than by subscript.
   */
  static const CommandSpec COMMANDS[];
  static const size_t COMMAND_COUNT;

  /**
   * @brief Returns a snapshot of what the device is doing right now.
   *
   * By value: the caller gets a copy it owns and nothing has to be freed.
   * Cheap enough at one poll a second, and the alternative -- handing back
   * a pointer the caller must delete -- put ownership in the interface for
   * no gain.
   */
  StatusUpdate createStatus();
};
