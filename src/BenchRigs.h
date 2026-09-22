#pragma once

#include <Arduino.h>

#include "Configuration.h"
#include "Feeder.h"
#include "Light.h"
#include "Logger.h"
#include "Press.h"
#include "Sound.h"

/**
 * @brief The temporary bring-up rigs, lifted out of ETKT::initialize().
 *
 * Every rig is compiled out unless its BENCH_* flag in Configuration.h is
 * true, so with all five false both methods below are empty and cost nothing.
 *
 * The two entry points are not interchangeable. Each rig documents a hardware
 * precondition, and between them those preconditions reduce to exactly two
 * points in the bring-up order:
 *
 *  - beforePeripherals() runs while only the 3.3 V parts are alive, so a rig
 *    can exercise them with no motor attached and nothing able to move.
 *  - beforeHoming() runs once press, feeder and hall are up but before
 *    DaisyWheel::initialize(), which blocks in home() until the hall triggers.
 *    A rig placed after that point would never run on an unaligned machine.
 *
 * Delete this module, its two calls in ETKT::initialize(), the member and
 * constructor parameter in ETKT, and the line in LabelMaker.cpp once machine 3
 * is finished. The compiler finds all six.
 */
class BenchRigs {
 private:
  Logger* logger;
  Sound* sound;
  Press* press;
  Feeder* feeder;
  Light* ledChar;
  Light* ledFinish;

 public:
  BenchRigs(Logger* logger, Sound* sound, Press* press, Feeder* feeder,
            Light* ledChar, Light* ledFinish);

  /**
   * @brief Rigs that must run before the peripherals come up.
   *
   * Call after sound->initialize() and before settings, display, hall, press
   * and feeder initialize.
   */
  void beforePeripherals();

  /**
   * @brief Rigs that must run after the peripherals and before homing.
   *
   * Call after feeder->initialize() and before daisywheel->initialize().
   */
  void beforeHoming();
};
