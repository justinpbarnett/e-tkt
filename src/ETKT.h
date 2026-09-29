#pragma once

#include <Arduino.h>

#include <condition_variable>
#include <mutex>

#include "Configuration.h"
#include "Display.h"
#include "Feeder.h"
#include "Light.h"
#include "Logger.h"
#include "Printhead.h"
#include "Roll.h"
#include "Settings.h"
#include "Sound.h"
#include "StopSignal.h"

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

  // Whether the body may say how many labels to print, in "copies". Unlike
  // the fields above this one is optional: a body without it prints one
  // label, which is all a body could ask for before the field existed.
  bool usesCopies;

  // Whether the body may declare how long a newly loaded roll is, in
  // "length_mm". Also optional: without it the new roll is taken to be as
  // long as the last one was.
  bool usesRollLength;

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
  // How many of the label to print, one after another, 1 to MAX_COPIES.
  int copies = 1;
  // How long the roll being loaded is, in millimetres, or 0 for "as long as
  // the last one".
  int rollLengthMm = 0;
};

/**
 * @brief A stop the running command has been asked for and not yet obeyed.
 */
enum class PendingStop {
  NONE,
  // Once the label being pressed is cut. See ETKT::stopAfterLabel().
  AFTER_LABEL,
  // Now. See ETKT::stop().
  NOW,
};

/**
 * @brief What a stop cut short.
 *
 * Only a stop that cut work short is recorded. One that arrived as a job was
 * finishing anyway has nothing to explain, and the command is then IDLE.
 */
struct StoppedCommand {
  Command command = Command::IDLE;
  // For a run of labels, how many were finished and cut before the stop, and
  // how many the run was. Both 0 for anything but a tag.
  int printed = 0;
  int copies = 0;
  // Whether the stop left a label on the tape: pressed as far as it got, or
  // only begun, and not cut off. It is still joined to the roll, so the next
  // label out would bring it along, and the panel offers to cut it off first.
  // Only a tag and the full test press labels, so it is false for anything
  // else; and false for a run stopped between two labels, before the next
  // one had fed any tape.
  bool unfinished = false;
};

/**
 * @brief One consistent look at what the device is doing right now.
 *
 * A snapshot, not a view: the command and its progress are read together
 * under one lock, so the percentage reported here belongs to the command
 * reported beside it. Served to the webapp by GET /api/status, which polls
 * once a second.
 *
 * currentCommand is IDLE when nothing is running, and the fields that
 * describe a command are then at their defaults. Whether the device is busy
 * is that comparison and nothing else -- there is no separate flag to keep in
 * step with it. The roll is filled in whether or not anything is running.
 */
struct StatusUpdate {
  int progress = 0;  // percent of the current label, 0 to 99. See Progress.h.
  int align = 0;
  int force = 0;
  String currentLabel = "";
  Command currentCommand = Command::IDLE;
  // Which label of a run is being pressed, counting from 1, and how many the
  // run is. Both 0 unless a tag is running.
  int copy = 0;
  int copies = 0;
  // Whether a stop has been asked for, and which. A stop after the label can
  // be waiting for a label's worth of time; a stop now for as long as the
  // press takes to finish its stroke.
  PendingStop stop = PendingStop::NONE;
  // What the last stop cut short. Filled in whether or not anything is
  // running, and kept until the next command is accepted, so a panel that
  // was not watching when a job was stopped can still say it was.
  StoppedCommand stopped;
  RollState roll;
};

/**
 * @brief What asking the device to stop did.
 */
enum class StopResult {
  // The command will end at the next point it can. See ETKT::stop() and
  // ETKT::stopAfterLabel() for where that is.
  STOPPING,
  // Nothing was running. Not an error: the job may have finished between the
  // tap and the request arriving.
  IDLE,
  // What is running cannot be stopped that way: saving cannot be stopped at
  // all, and only a run of labels has a label to stop after.
  UNSTOPPABLE,
};

class PrinterBusyException : public std::exception {
 public:
  const char* what() const throw() {
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
  Printhead* printhead;
  Feeder* feeder;
  Sound* sound;
  Roll* roll;

  // Device state, which should only ever be modified inside an exclusive lock.
  CommandOptions* command = NULL;
  int progress;  // percent, 0 to 99. See Progress.h.
  int copy;      // which label of a run, from 1; 0 when no tag is running
  int printed;   // labels of the run finished and cut; 0 when no tag is
                 // running
  bool stoppingAfterLabel;     // see stopAfterLabel()
  StoppedCommand lastStopped;  // see StatusUpdate::stopped
  std::mutex lock;

  // What loop() waits on for a command, under the lock above. submit()
  // notifies it as it fills the slot.
  std::condition_variable queued;

  // Raised by stop() and obeyed all the way down, in the wheel, the feeder
  // and the tune. It keeps its own synchronisation, but it is raised and
  // cleared only under the lock above, against the command it is about.
  StopSignal* stopSignal;

  // How many of the feeder's feeds have been charged to the roll. Only the
  // command loop reads or writes it, so it is not behind the lock.
  long accountedFeeds;

  // The feeder's count at the last cut, which is how a stop tells whether it
  // left a label on the tape: see StoppedCommand::unfinished. The two
  // commands that press labels also set it as they begin, so tape fed before
  // them is not taken for theirs. Like accountedFeeds, only ever touched by
  // the command loop.
  long feedsAtLastCut;

  // What every press of the running job is made at, from its first
  // character to its cut. loop() picks it as the job begins: the align and
  // force the job is trialling, where its row says it reads them, and the
  // saved ones otherwise. Only the command loop touches it, so it is not
  // behind the lock.
  Calibration calibration;

  /**
   * @brief The align and force saved in the settings. A job that is not
   * trialling a calibration of its own presses at these.
   */
  Calibration savedCalibration();

  /**
   * @brief Charges the roll for every feed since the last time this ran.
   *
   * Calling it twice is harmless: the second call finds nothing new. It runs
   * after every command and after every label of a run, so the tape left on
   * the panel moves label by label, and a power cut loses at most the label
   * being pressed.
   */
  void accountForTape();

  /**
   * @brief Presses one label, start to cut: homes the wheel, feeds the lead,
   * presses and feeds past each character, tops the tape up and cuts.
   *
   * `copy` of `copies`, counting from 1, is only there to be shown on the
   * OLED beside the label.
   *
   * A stop leaves the label on the tape as far as it got. Whether one did is
   * the StopSignal's cutShort(), rather than anything this returns.
   */
  void printLabel(const String& label, int copy, int copies);

  /**
   * @brief Plays the tune that says a label has started: the label's own
   * notes, or for a few labels a melody everyone of a certain age knows.
   */
  void playTune(const String& label);

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
  ETKT(Logger* logger, Settings* settings, Display* display,
       Printhead* printhead, Feeder* feeder, Roll* roll, Sound* sound,
       Light* ledFinish, Light* ledChar, StopSignal* stopSignal);
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
   * The caller fills in whichever of align, force, label, copies and roll
   * length the command's row in COMMANDS says it reads; anything else in
   * `options` is ignored.
   * Throws PrinterBusyException if a command is already in flight, and
   * queues nothing in that case.
   *
   * The device takes a copy, so the caller keeps what it passed in either
   * way.
   */
  void submit(const CommandOptions& options);

  /**
   * @brief Stops what the machine is doing, now.
   *
   * For when something has gone wrong. The motors halt within a step and the
   * tune within a note; the press, if it is on its way down, finishes the
   * stroke and comes back up, because a servo stopped partway is a press
   * held against the wheel. Then every motor lets go, and whatever was being
   * pressed is left on the tape as far as it got, uncut.
   *
   * Anything but saving can be stopped. Saving moves nothing and ends in a
   * reboot, so there is nothing to stop.
   *
   * Safe to call from the webserver's task while the command loop runs: it
   * only raises the StopSignal, which the loop obeys.
   */
  StopResult stop();

  /**
   * @brief Asks a run of labels to stop once the label being pressed is cut.
   *
   * For a run that is going fine and is longer than it needs to be: no tape
   * is spent on a label nobody finishes, and the cut is what separates the
   * last label from the next. Only a run of labels has a label to stop
   * after. Safe to call from the webserver's task while the command loop
   * prints -- the loop reads the request between labels.
   */
  StopResult stopAfterLabel();

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
