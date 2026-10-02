#include "Button.h"

#include <Arduino.h>

#include "Configuration.h"

Button::Button(uint8_t pin, ETKT* etkt, Logger* logger) {
  this->pin = pin;
  this->etkt = etkt;
  this->logger = logger;
}

void Button::initialize() {
  // The PCB pulls this pin high and a bare devkit does not, so the pull-up
  // is the chip's own. The switch then only has to short the pin to ground.
  pinMode(this->pin, INPUT_PULLUP);

  const unsigned long now = millis();
  this->readingDown = false;
  this->readingSinceMs = now;
  this->down = false;
  this->spent = false;
  // The machine has been busy until now, booting. So a button that is down
  // already, or a pin that reads low before the pull-up has it, is a press
  // that comes too soon after that to start anything.
  this->idleSinceMs = now;
  this->armed = false;
}

void Button::poll() {
  const unsigned long now = millis();

  const bool busy = this->etkt->busy();
  if (busy) {
    this->idleSinceMs = now;
    this->armed = false;
  } else if (!this->armed && now - this->idleSinceMs >= BUTTON_ARMING_MS) {
    this->armed = true;
  }

  // A reading counts once it has held: a contact bounces as it closes, and
  // a wire beside a motor picks up noise.
  const bool reading = digitalRead(this->pin) == LOW;
  if (reading != this->readingDown) {
    this->readingDown = reading;
    this->readingSinceMs = now;
  }
  if (this->readingDown != this->down &&
      now - this->readingSinceMs >= BUTTON_DEBOUNCE_MS) {
    this->down = this->readingDown;
    if (this->down) {
      this->downSinceMs = now;
      this->pressed(busy);
    } else {
      this->released();
    }
  }

  if (this->down && !this->spent && now - this->downSinceMs >= BUTTON_HOLD_MS) {
    this->held();
  }
}

void Button::pressed(bool busy) {
  if (busy) {
    // As the button goes down, not as it comes up: a stop is wanted now.
    // Whatever the job runner answers, this press was for that job and does
    // nothing else.
    this->spent = true;
    this->logger->log("Button pressed while a job runs");
    this->etkt->stop();
    return;
  }
  // On an idle machine a press waits to see whether it is let go or held.
  // One that comes this soon after a job was meant for the job.
  this->spent = !this->armed;
}

void Button::released() {
  if (this->spent) {
    return;
  }
  try {
    if (this->etkt->createStatus().roll.out) {
      // What the notice on the screen asks for. The roll is taken to be as
      // long as the last one, since the button cannot say otherwise.
      CommandOptions load;
      load.command = Command::REEL;
      this->etkt->submit(load);
      this->logger->log("Button pressed: loading the new roll");
    } else if (this->etkt->repeat()) {
      this->logger->log("Button pressed: printing the last run again");
    } else {
      this->logger->log("Button pressed, and there is no run to print again");
    }
  } catch (const PrinterBusyException&) {
    // A job from the panel got there first, between the button going down
    // and coming up. The press has nothing to do with that job.
    this->logger->log("Button pressed, and the machine was busy");
  }
}

void Button::held() {
  this->spent = true;
  try {
    CommandOptions unload;
    unload.command = Command::UNLOAD;
    this->etkt->submit(unload);
    this->logger->log("Button held: unloading the roll");
  } catch (const PrinterBusyException&) {
    this->logger->log("Button held, and the machine was busy");
  }
}
