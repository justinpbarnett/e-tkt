#pragma once

// The magnet on the daisy wheel's hub, as the hall sensor sees it.
//
// Unscripted, every analog pin reads 0, which on this machine is the magnet
// in front of the sensor all the way round. The wheel then homes wherever it
// happens to be, and nothing it does afterwards is counted from anywhere in
// particular. With a FakeMagnet the sensor sees the magnet only while the
// character stepper has the wheel turned to it, the way it does on the
// machine. So homing finds the same place every time, and where the wheel
// stands says which slot is under the press.

#include <stdint.h>

#include "Arduino.h"
#include "Configuration.h"
#include "FakeDrivers.h"

class FakeMagnet {
 private:
  const FakeStepper* stepper;
  long at;

 public:
  // One turn of the wheel, in steps of the character stepper.
  static const long REVOLUTION = CHAR_STEP_COUNT * CHAR_MICROSTEPS;

  // How many steps the magnet stays in front of the sensor as the wheel
  // turns past it. The hall bench rig measures this arc on a machine.
  static const long ARC = 20;

  // Whether the magnet is on the hub. Without it the sensor sees nothing
  // however far the wheel turns, which is what homing meets when the magnet
  // has come off or the sensor has come unplugged.
  bool present = true;

  /**
   * @param at where the shaft is, in steps from where it started, when the
   *        magnet first comes in front of the sensor.
   */
  FakeMagnet(const FakeStepper* stepper, long at) : stepper(stepper), at(at) {}

  /**
   * @brief How far round the wheel stands from the magnet, 0 to
   * REVOLUTION - 1. Two jobs that put the same slot under the press at the
   * same align leave the wheel at the same bearing.
   */
  long bearing() const {
    const long offset = (this->stepper->shaft - this->at) % REVOLUTION;
    return offset < 0 ? offset + REVOLUTION : offset;
  }

  bool inFront() const { return this->present && this->bearing() < ARC; }

  /**
   * @brief Wires the hall pin to this magnet, until the next stubReset().
   * Every other analog pin goes on reading 0.
   */
  void install() {
    stubAnalogRead() = [this](uint8_t pin) {
      if (pin != HALL_PIN) {
        return 0;
      }
      // Measured on machine 1: 0 with the magnet in front, 4095 clear. A
      // machine built with INVERT_HALL_SENSOR_LOGIC reads the other way.
      const bool low = this->inFront() != (bool)INVERT_HALL_SENSOR_LOGIC;
      return low ? 0 : 4095;
    };
  }
};
