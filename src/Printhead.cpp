#include "Printhead.h"

#include <Arduino.h>

#include "CharacterSet.h"
#include "Configuration.h"
#include "DaisyWheel.h"
#include "Logger.h"
#include "Press.h"
#include "PressGeometry.h"
#include "StopSignal.h"

Printhead::Printhead(Logger* logger, DaisyWheel* daisywheel, Press* press,
                     StopSignal* stop, Background* tape) {
  this->logger = logger;
  this->daisywheel = daisywheel;
  this->press = press;
  this->stop = stop;
  this->tape = tape;
}

void Printhead::initialize(const Calibration& calibration) {
  this->press->initialize();
  this->daisywheel->initialize();
  this->daisywheel->home(calibration.align);
  this->daisywheel->deenergize();
}

Turn Printhead::stopIfLost(Turn turn) {
  if (turn == Turn::LOST) {
    this->stop->raise(StopCause::LOST_WHEEL);
    // Obeyed as it is raised. The turn was the work, and it has been
    // dropped, so a home or a move that ends here was cut short too.
    this->stop->shouldStop();
  }
  return turn;
}

bool Printhead::readyToPress(Turn turn) {
  if (turn != Turn::REACHED) {
    return false;
  }
  this->tape->finish();
  return !this->stop->shouldStop();
}

void Printhead::home(const Calibration& calibration) {
  this->stopIfLost(this->daisywheel->home(calibration.align));
}

void Printhead::turnTo(const String& slot, const Calibration& calibration) {
  const Turn turn =
      this->stopIfLost(this->daisywheel->move(slot, calibration.align));
  if (turn == Turn::NO_SLOT) {
    this->logger->warn(String("The wheel would not reach '") + slot + "'");
  }
}

void Printhead::stamp(const String& character, const Calibration& calibration) {
  // The wheel carries no space. Asked for one, it would say so in the log,
  // let go of its coils, and home again for the character after.
  if (character == " ") {
    return;
  }
  const Turn turn =
      this->stopIfLost(this->daisywheel->move(character, calibration.align));
  if (this->readyToPress(turn)) {
    this->press->press(false, calibration.force, false);
  }
}

void Printhead::cut(const Calibration& calibration) {
  if (!ENABLE_CUT) {
    pause(500, this->tape);
    return;
  }
  const Turn turn = this->stopIfLost(
      this->daisywheel->move(CUT_CHARACTER, calibration.align));
  // Pressing into whatever slot the wheel stopped at would emboss a letter
  // where the cut mark belongs, and leave the tape uncut anyway. Only a
  // wheel with no cut mark is news: a stop was asked for, and a lost wheel
  // has said so already.
  if (turn == Turn::NO_SLOT) {
    this->logger->warn("Skipped the cut: the wheel would not reach the mark");
  }
  if (this->readyToPress(turn)) {
    this->press->press(true, calibration.force, false);
  }
}

void Printhead::testPress(const Calibration& calibration) {
  if (!this->readyToPress(
          this->stopIfLost(this->daisywheel->move("M", calibration.align)))) {
    return;
  }
  // Deliberately the minimum force, matching docs/diy/calibration.md: this
  // button "will slowly and lightly press the daisy wheel letter" to check
  // that the press lands centred on the character. Force is calibrated
  // separately, with the full test button against real tape.
  //
  // It must stay at minimum force. This is the only path that passes
  // slow=true, so it is the only press that holds at peak for
  // PRESS_TEST_DWELL_MS rather than PRESS_DWELL_MS -- and the calibration doc
  // sends the user here while the force field is wound up to 9 ("take the
  // opportunity to see if the alignment is correct"). A full-bite peak held
  // for seconds is a stalled servo, which is what wears an MG996R's gears and
  // reams the P_press splines.
  this->press->press(false, CALIBRATION_VALUE_MIN, true);
}

void Printhead::rest() { this->press->rest(); }

void Printhead::park() {
  this->daisywheel->deenergize();
  this->press->rest();
}

unsigned long Printhead::homeUs(const Calibration& calibration) const {
  return this->daisywheel->homeUs(CHAR_HOME_CHARACTER, calibration.align);
}

unsigned long Printhead::stampUs(const String& from, const String& character,
                                 unsigned long tapeUs,
                                 const Calibration& calibration) const {
  // Nothing to turn to or press, and stamp() does not wait for the tape.
  if (wheelSlot(character) < 0) {
    return tapeUs;
  }
  return this->pressedUs(from, character, tapeUs, false, calibration);
}

unsigned long Printhead::cutUs(const String& from, unsigned long tapeUs,
                               const Calibration& calibration) const {
  return this->pressedUs(from, CUT_CHARACTER, tapeUs, true, calibration);
}

unsigned long Printhead::pressedUs(const String& from, const String& slot,
                                   unsigned long tapeUs, bool strong,
                                   const Calibration& calibration) const {
  const unsigned long turnUs =
      this->daisywheel->moveUs(from, slot, calibration.align);
  return max(turnUs, tapeUs) + this->press->pressUs(strong, calibration.force);
}
