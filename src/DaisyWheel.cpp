#include "DaisyWheel.h"

#include "Arduino.h"
#include "CharacterSet.h"
#include "Configuration.h"
#include "Drivers.h"
#include "HallSwitch.h"
#include "Logger.h"
#include "Motion.h"
#include "StopSignal.h"

DaisyWheel::DaisyWheel(Logger* logger, HallSwitch* hall, StepperDriver* stepper,
                       StopSignal* stop, Background* background) {
  this->logger = logger;
  this->hall = hall;
  this->stepper = stepper;
  this->stop = stop;
  this->background = background;
}

DaisyWheel::~DaisyWheel() {
  // The stepper is handed in, not built here, so it is not ours to delete.
}

// The motor as initialize() sets it up, and as the estimates count it.
static const StepperTiming TIMING = {CHARACTER_STEPPER_MAX_SPEED,
                                     CHARACTER_STEPPER_MAX_ACCELERATION};

void DaisyWheel::initialize() {
  // The wheel's sensor, so nothing else has to know the wheel has one.
  this->hall->initialize();
  digitalWrite(PIN_STEPPER_CHAR_ENABLE, HIGH);
  this->stepper->setMaxSpeed(TIMING.maxSpeed);
  this->stepper->setAcceleration(TIMING.acceleration);
  this->stepper->setPinsInverted(true, false, true);
  this->stepper->setEnablePin(PIN_STEPPER_CHAR_ENABLE);
}

Turn DaisyWheel::home(int align) {
  // Before the coils are powered, so a stop that is already up leaves the
  // wheel as it was.
  if (this->stop->shouldStop()) {
    this->lose();
    return Turn::STOPPED;
  }
  this->stepper->enableOutputs();
  // runs the char stepper clockwise until triggering the hall sensor, then call
  // it home at char 21
  const long alignedAt = this->alignPosition(align);

  logger->log(String("Homing with align: ") + align + ", to " + alignedAt +
              " steps from the magnet");

  // Check to see if the hall sensor on the stepper is already trigerred
  // and if so, move it a little bit to get the sensor into an un-trigerred
  // position.
  if (this->hall->triggered()) {
    long position = -this->stepsPerChar * 4;
    logger->log(String("Moving to position: ") + position +
                " because the hall sensor is already triggered.");
    if (!runToNewPosition(this->stepper, position, this->stop,
                          this->background)) {
      this->lose();
      return Turn::STOPPED;
    }
  }

  // TODO: Change the above to only move as long as the hall sensor is
  // triggered, which could save a little time while printing.

  // Move the daisy wheel until the hall sensor triggers, and then treat
  // wherever that is as the new home position.
  this->stepper->move(-this->searchSteps);
  auto hallState = hall->triggered();
  // Give up once the 1.5-revolution sweep is exhausted rather than spinning
  // here forever: the stepper has stopped by then, so the hall reading can no
  // longer change and the original loop could never exit.
  while (!hallState && this->stepper->distanceToGo() != 0) {
    if (this->stop->shouldStop()) {
      halt(this->stepper);
      this->lose();
      return Turn::STOPPED;
    }
    this->stepper->run();
    this->background->keepGoing();
    // TODO: less intrusive way to avoid triggering watchdog?
    delayMicroseconds(HOME_SWEEP_POLL_US);

    hallState = hall->triggered();
  }

  if (!hallState) {
    logger->error(
        "HOMING FAILED: swept 1.5 revolutions with no hall trigger. "
        "Check the magnet on the hub, the sensor gap, and the 10k "
        "pull-up to 3V3.");
    this->lose();
    return Turn::LOST;
  }

  this->stepper->setCurrentPosition(0);

  if (!runToNewPosition(this->stepper, alignedAt, this->stop,
                        this->background)) {
    this->lose();
    return Turn::STOPPED;
  }
  this->stepper->setCurrentPosition(0);
  // Where the wheel now is, asked of the same table every move consults.
  const int homeChar = wheelSlot(CHAR_HOME_CHARACTER);
  if (homeChar < 0) {
    // Only reachable if CHAR_HOME_CHARACTER names something the wheel does
    // not carry, which is a build-configuration mistake rather than a
    // runtime one. Say so: every move from here would be off by the
    // difference.
    this->logger->error(String("Home character '") + CHAR_HOME_CHARACTER +
                        "' is not on the daisy wheel");
  }
  this->currentChar = homeChar;

  pause(HOME_SETTLE_MS, this->background);
  return Turn::REACHED;
}

Turn DaisyWheel::move(String c, int alignFactor) {
  if (!ENABLE_DAISYWHEEL) {
    pause(500, this->background);
    return Turn::REACHED;
  }
  // Before the coils are powered, so a stop that is already up leaves the
  // wheel as it was.
  if (this->stop->shouldStop()) {
    return Turn::STOPPED;
  }

  // reaches out for a specific character
  logger->log(String("Moving to character ") + c);
  this->stepper->enableOutputs();
  auto charIndex = wheelSlot(c);
  if (charIndex < 0) {
    // Nothing on the wheel prints this. Drop the coil current on the way out:
    // the enableOutputs() above has the motor holding position for a move
    // that can never happen, and this path used to return with it still hot.
    logger->error(String("No character '") + c + "' on the daisy wheel");
    this->deenergize();
    return Turn::NO_SLOT;
  }
  if (charIndex == this->currentChar) {
    // No need to move, we're already there.
    logger->log("Already in position");
    return Turn::REACHED;
  }

  // calls home everytime to avoid accumulating errors
  const Turn homing = this->home(alignFactor);
  if (homing != Turn::REACHED) {
    // Nowhere to count from.
    return homing;
  }

  auto charDelta = charIndex - this->currentChar;
  logger->log(String("New character index is ") + charIndex +
              " and character delta is " + charDelta);
  // matches the character to the list and gets delta steps from home
  while (charDelta < 0) {
    charDelta += WHEEL_SLOT_COUNT;
  }

  if (charDelta == 0) {
    // No need to move, we're already there
    logger->log("Already in position");
    return Turn::REACHED;
  }

  const long position = this->slotPosition(charDelta);
  logger->log(String("Moving ") + charDelta + " characters to position " +
              position);
  if (!runToNewPosition(this->stepper, position, this->stop,
                        this->background)) {
    this->lose();
    return Turn::STOPPED;
  }
  this->currentChar = charIndex;

  pause(TURN_SETTLE_MS, this->background);
  return Turn::REACHED;
}

// How many slots on from the J the wheel turns to reach `slot`, as move()
// counts them.
static int slotsPastHome(int slot) {
  const int slots = (slot - wheelSlot(CHAR_HOME_CHARACTER)) % WHEEL_SLOT_COUNT;
  return slots < 0 ? slots + WHEEL_SLOT_COUNT : slots;
}

unsigned long DaisyWheel::homeUs(const String& from, int align) const {
  const int slot = wheelSlot(from);
  std::lock_guard<std::mutex> guard(this->estimateLock);
  return this->homeFromUs(slot < 0 ? wheelSlot(CHAR_HOME_CHARACTER) : slot,
                          align);
}

unsigned long DaisyWheel::moveUs(const String& from, const String& to,
                                 int align) const {
  const int fromSlot = wheelSlot(from);
  const int toSlot = wheelSlot(to);
  if (toSlot < 0 || toSlot == fromSlot) {
    return 0;
  }
  std::lock_guard<std::mutex> guard(this->estimateLock);
  const int homedFrom =
      fromSlot < 0 ? wheelSlot(CHAR_HOME_CHARACTER) : fromSlot;
  return this->homeFromUs(homedFrom, align) + this->turnToUs(toSlot);
}

unsigned long DaisyWheel::homeFromUs(int slot, int align) const {
  if (align != this->estimatedAlign) {
    for (int i = 0; i < WHEEL_SLOT_COUNT; i++) {
      this->homeFromSlotUs[i] = 0;
    }
    this->estimatedAlign = align;
  }
  unsigned long& known = this->homeFromSlotUs[slot];
  if (known != 0) {
    return known;
  }

  const long alignedAt = this->alignPosition(align);
  // Homing turns the way the slots count, and the magnet is found where it
  // comes in front of the sensor. The J is alignedAt steps on from there,
  // and the slot its own steps on from the J, so that is how far round the
  // search has to go to come back to it.
  const long start = alignedAt + this->slotPosition(slotsPastHome(slot));
  long steps = start % this->stepsPerRevolution;
  if (steps < 0) {
    steps += this->stepsPerRevolution;
  }
  unsigned long us =
      TIMING.stepsUs(-this->searchSteps, steps, HOME_SWEEP_POLL_US);
  // The look that sees the magnet.
  us += HOME_SWEEP_POLL_US;
  if (alignedAt != 0) {
    // The align starts from rest, so its first step comes a whole first
    // interval after the search's last.
    us += waitedUs(TIMING.firstStepUs() - HOME_SWEEP_POLL_US, MOTOR_LOOP_US);
    us += TIMING.moveUs(alignedAt, MOTOR_LOOP_US);
  }
  us += HOME_SETTLE_MS * 1000UL;
  known = us;
  return us;
}

unsigned long DaisyWheel::turnToUs(int slot) const {
  const int slots = slotsPastHome(slot);
  if (slots == 0) {
    // Homing has put it there.
    return 0;
  }
  unsigned long& known = this->turnToSlotUs[slot];
  if (known == 0) {
    // From the J, where homing left the wheel at rest, so the first step
    // comes at once.
    known = TIMING.moveUs(this->slotPosition(slots), MOTOR_LOOP_US) +
            TURN_SETTLE_MS * 1000UL;
  }
  return known;
}

long DaisyWheel::alignPosition(int align) const {
  const float a = (align - 5.0f) / 10.0f;
  return -this->stepsPerChar + (this->stepsPerChar * a) +
         (ASSEMBLY_CALIBRATION_ALIGN * this->stepsPerChar);
}

long DaisyWheel::slotPosition(int slots) const {
  // runs char stepper clockwise to reach the target position
  return -this->stepsPerChar * slots;
}

void DaisyWheel::lose() { this->currentChar = -1; }

void DaisyWheel::deenergize() {
  this->stepper->disableOutputs();

  // Disabling the motors loses position information, so we need to reset it.
  this->currentChar = -1;
}