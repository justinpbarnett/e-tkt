#pragma once

#include <Arduino.h>

#include <condition_variable>
#include <mutex>
#include <vector>

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
 * @brief One fact a row of ETKT::COMMANDS can state about its command.
 *
 * A row names the facts that hold for its command and leaves the rest out,
 * so it reads as what is true of that command. Seven booleans in a row could
 * only be read by counting. Each fact sets the CommandSpec field of the same
 * name, which says what it means.
 */
enum class CommandFact : unsigned {
  NONE = 0,
  USES_ALIGN = 1u << 0,
  USES_FORCE = 1u << 1,
  TEXT_IS_LABEL = 1u << 2,
  PRINTS_RUN = 1u << 3,
  USES_ROLL_LENGTH = 1u << 4,
  STOPPABLE = 1u << 5,
  PRESSES_LABEL = 1u << 6,
};

/**
 * @brief Both sets of facts, which is how a row names more than one.
 */
constexpr CommandFact operator|(CommandFact a, CommandFact b) {
  return static_cast<CommandFact>(static_cast<unsigned>(a) |
                                  static_cast<unsigned>(b));
}

/**
 * @brief Returns whether `fact` is one of `facts`.
 */
constexpr bool holds(CommandFact facts, CommandFact fact) {
  return (static_cast<unsigned>(facts) & static_cast<unsigned>(fact)) != 0;
}

/**
 * @brief Everything outside the ETKT needs to know about one command.
 *
 * One row per enumerator, in ETKT::COMMANDS. The device used to restate this
 * list in four places -- a name switch, a factory method per command, a
 * dispatch switch, and the route table in Network.cpp -- and they drifted.
 * Code outside the table asks a row about its command rather than comparing
 * the command against a name, so a new command is a row and nothing else.
 */
struct CommandSpec {
  constexpr CommandSpec(Command command, const char* name,
                        const char* textField, CommandFact facts,
                        void (ETKT::*run)())
      : command(command),
        name(name),
        usesAlign(holds(facts, CommandFact::USES_ALIGN)),
        usesForce(holds(facts, CommandFact::USES_FORCE)),
        textField(textField),
        textIsLabel(holds(facts, CommandFact::TEXT_IS_LABEL)),
        printsRun(holds(facts, CommandFact::PRINTS_RUN)),
        usesRollLength(holds(facts, CommandFact::USES_ROLL_LENGTH)),
        stoppable(holds(facts, CommandFact::STOPPABLE)),
        pressesLabel(holds(facts, CommandFact::PRESSES_LABEL)),
        run(run) {}

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
  const char* textField;

  // Whether what arrives in that field is a label -- text to emboss -- rather
  // than the name of a slot on the wheel. A label is checked character by
  // character against what a label may contain, and the request is refused
  // when one of them is not. Without that check DaisyWheel::move() refuses
  // the character on its own and the press comes down regardless, on
  // whichever slot the wheel last stopped at.
  //
  // A move's field is not a label: it names a slot, the cut mark included,
  // and no label may say that. So the two commands that carry text need the
  // two answers, which is why this cannot be read off textField.
  bool textIsLabel;

  // Whether this command prints a run of labels: the same label pressed
  // "copies" times, one after another. The body may say how many, and in
  // "cut" whether each label is cut off as it finishes. Unlike the fields
  // above those are optional: a body without them prints one label and cuts
  // it, which is all a body could ask for before the fields existed. Only a
  // run reports which of its labels it is on and how long it has left, has
  // a label to stop after, and says how many it finished when it is stopped.
  bool printsRun;

  // Whether the body may declare how long a newly loaded roll is, in
  // "length_mm". Also optional: without it the new roll is taken to be as
  // long as the last one was.
  bool usesRollLength;

  // Whether ETKT::stop() stops this command. Everything that moves can be
  // stopped. A save moves nothing and ends in a reboot, and one cut off
  // partway would leave half a calibration behind. The panel offers its stop
  // button by this alone: for every command it holds for, and never for one
  // the device would refuse.
  bool stoppable;

  // Whether this command presses a label into the tape, which a stop can
  // leave there unfinished: fed and pressed only as far as it got. A tag and
  // the full test press one. A reel and a feed move tape with nothing pressed
  // into it, so stopping one of them leaves nothing to cut off.
  bool pressesLabel;

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
  // Whether each label of the run is cut off the tape as it finishes. The
  // blade never goes all the way through, so the labels come off with
  // scissors either way, and a run can leave the cut out to save the time.
  bool cut = true;
  // How long the roll being loaded is, in millimetres, or 0 for "as long as
  // the last one".
  int rollLengthMm = 0;
};

/**
 * @brief A stop the running command has been asked for and not yet obeyed.
 */
enum class PendingStop {
  NONE,
  // Once the label being pressed is finished. See ETKT::stopAfterLabel().
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
  // What stopped it: the operator, or a wheel that could not find home.
  StopCause cause = StopCause::NONE;
  // Which stop this was, so that two saying the same thing still differ and
  // a panel can dismiss one without hiding the next. 0 while nothing has
  // been stopped. Counts up from a random start at every boot, so a stop
  // after a reboot is not taken for one dismissed before it.
  uint32_t id = 0;
  // For a run of labels, how many were finished before the stop, and how
  // many the run was. Both 0 for anything but a tag.
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
  // For a run of labels: how long one of its labels takes, from its start to
  // the start of the next, and how long the run has left, the finish
  // included. Both 0 unless a tag is running.
  uint32_t labelMs = 0;
  uint32_t remainingMs = 0;
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
  // What is running cannot be stopped that way. Its row in ETKT::COMMANDS
  // says whether it can be stopped at all, and whether it prints a run of
  // labels, which is the only thing with a label to stop after.
  UNSTOPPABLE,
};

/**
 * @brief How long a run of labels takes, worked out rather than timed. See
 * ETKT::estimate().
 */
struct RunEstimate {
  // One label once the run is under way, from its start to the start of the
  // next. The first label waits for the tune and the home as well.
  uint32_t labelMs = 0;
  // The whole run, from the job being taken to the machine going idle: the
  // press settling, the tune, the home, every label and the finish.
  uint32_t runMs = 0;
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
  int printed;   // labels of the run finished; 0 when no tag is running
  bool stoppingAfterLabel;  // see stopAfterLabel()
  // What the running run was estimated to take, and when it began, its
  // second label began, and the label being pressed began. What
  // StatusUpdate::labelMs and remainingMs are worked out from.
  RunEstimate runEstimate;
  unsigned long runStartMs;
  unsigned long secondLabelStartMs;
  unsigned long labelStartMs;
  StoppedCommand lastStopped;  // see StatusUpdate::stopped
  uint32_t lastStopId;         // see StoppedCommand::id
  std::mutex lock;

  // What loop() waits on for a command, under the lock above. submit()
  // notifies it as it hands one over.
  std::condition_variable submitted;

  // Raised by stop() and obeyed all the way down, in the wheel, the feeder
  // and the tune. It keeps its own synchronisation, but it is raised and
  // cleared only under the lock above, against the command it is about.
  StopSignal* stopSignal;

  // How many of the feeder's feeds have been charged to the roll. Only the
  // command loop reads or writes it, so it is not behind the lock.
  long accountedFeeds;

  // The feeder's count as the label being pressed began, which is how a stop
  // tells whether it left one on the tape: see StoppedCommand::unfinished.
  // loop() sets it as every command begins, so tape fed before a command is
  // not taken for its label, and a run moves it on as each label finishes.
  // Like accountedFeeds, only ever touched by the command loop.
  long feedsAtLabelStart;

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
  Calibration savedCalibration() const;

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
   * @brief Presses one label, start to finish: feeds the lead, presses and
   * feeds past each character, tops the tape up, and cuts the label off if
   * the job cuts.
   *
   * The tape feeds in the background, one feed ahead of the press, and the
   * wheel turns to each character while the tape moves up to it. Each
   * character's wheel turn homes first, so a label needs no home of its own.
   *
   * `copy` of `copies`, counting from 1, is only there to be shown on the
   * OLED beside the label.
   *
   * A stop leaves the label on the tape as far as it got. Whether one did is
   * the StopSignal's cutShort(), rather than anything this returns.
   */
  void printLabel(const String& label, int copy, int copies);

  /**
   * @brief How long a label of the running run takes: the estimate until
   * the run has timed one, and then what it timed. Under the lock.
   */
  uint32_t labelMs() const;

  /**
   * @brief When the running run will be done, by millis(): the estimate
   * until its first label is done, and then counted on from the label being
   * pressed. Under the lock.
   */
  unsigned long runEndMs() const;

  /**
   * @brief estimate(), at a calibration of its own.
   */
  RunEstimate estimate(const CommandOptions& options,
                       const Calibration& calibration) const;

  /**
   * @brief How long printLabel() takes to press `characters`, with or
   * without the cut, from the wheel at `wheel`. Leaves `wheel` where the
   * label leaves it.
   */
  uint64_t labelUs(const std::vector<String>& characters, bool cut,
                   const Calibration& calibration, String* wheel) const;

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
   * @brief Hands the job runner a command, or refuses it while the machine
   * is busy.
   *
   * The caller fills in whichever of align, force, label, copies and roll
   * length the command's row in COMMANDS says it reads; anything else in
   * `options` is ignored.
   * Throws PrinterBusyException if a command is already in flight, and
   * takes nothing in that case.
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
   * A command stops if its row says it is stoppable, which is anything but
   * saving. Saving moves nothing and ends in a reboot, so there is nothing to
   * stop.
   *
   * Safe to call from the webserver's task while the command loop runs: it
   * only raises the StopSignal, which the loop obeys.
   */
  StopResult stop();

  /**
   * @brief Asks a run of labels to stop once the label being pressed is
   * finished.
   *
   * For a run that is going fine and is longer than it needs to be: no tape
   * is spent on a label nobody finishes, and the last label comes out whole,
   * topped up and, if the run cuts, cut off. Only a command whose row says it
   * prints a run of labels has a label to stop after. Safe to call from the
   * webserver's task while the command loop prints -- the loop reads the
   * request between labels.
   */
  StopResult stopAfterLabel();

  /**
   * @brief How long submit(options) would take, for a run of labels: worked
   * out from the ramps and waits the machine runs, not timed, so it can be
   * asked for before the run is sent, and while another one prints.
   *
   * At the saved calibration, which is what a run presses at. The home that
   * opens the run is counted as the longest search there is, so a run can
   * finish up to a second sooner; a feed that the wheel's search for its
   * magnet holds back can make it a little later. For a machine with every
   * part switched on under Debugging in Configuration.h. Anything but a run
   * of labels is estimated at 0.
   *
   * Safe to call from the webserver's task while the command loop runs.
   */
  RunEstimate estimate(const CommandOptions& options) const;

  /**
   * @brief One row per Command: the firmware's only list of what exists.
   *
   * Public because the webserver registers its routes from it and serves
   * every row's facts to the panel. The rows are in enum order, which
   * commandSpec() relies on and checks, so reach rows through commandSpec()
   * or commandSpecByName() rather than by subscript.
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
