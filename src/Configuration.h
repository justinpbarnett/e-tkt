#pragma once

#include <Arduino.h>

#include "Machine.h"

/**
 * Basic Configuration
 *
 * Every value in this file is the same on every E-TKT ever built: it describes
 * the design, not the machine in front of you. The numbers that differ from one
 * physical build to the next -- taught servo angles, the hall sensor's
 * threshold and polarity, the wheel's assembly offset, the feed direction --
 * live in Machine.h, which is included above so nothing that reads them has to
 * know they moved.
 */

/**
 * Speed and acceleration of the stepper motor that feeds the label tape,
 * measured in steps/s and steps/s^2. Use lower values if you find that the
 * printer doesn't consistently feed the correct length of tape between letters.
 * For calibrating these values the same advice about the character stepper
 * motor above applies. 4076 steps is one full revolution of the motor.
 */
#define FEED_STEPPER_MAX_SPEED 1000000
#define FEED_STEPPER_MAX_ACCELERATION 1000

/**
 * Speed and acceleration of the stepper motor that rotates the character
 * daisy wheel, measured in steps/s and steps/s^2. Use lower values if you find
 * that the printer sometimes prints the wrong letter.  Any value above zero is
 * ok but lower values will slow down printing, if you're having trouble start
 * by halving them and move up from there.  The speed you can reliably achieve
 * depends on the quality of the motor, how much current you've set it up to
 * use, and how fast the ESP-32 can talk with it. 3200 steps is a full
 * revolution of the daisy wheel: CHAR_STEP_COUNT full steps of
 * CHAR_MICROSTEPS each.
 */
#define CHARACTER_STEPPER_MAX_SPEED 320000
#define CHARACTER_STEPPER_MAX_ACCELERATION 16000

/**
 * Debugging
 *
 * Alter these to enable/disable individual parts of the hardware while
 * troubleshooting.
 */
#define ENABLE_SOUND true
#define ENABLE_FEED true
#define ENABLE_CUT true
#define ENABLE_PRESS true
#define ENABLE_DAISYWHEEL true

/**
 * Development
 *
 * Developers of the firmware may find it useful to turn on these features
 */
#define ENABLE_SERIAL true  // Enables serial output
#define ENABLE_OTA false    // Enables OTA updates at http://e-tkt.local/update
#define DEBUG_WIFI false    // Enables WiFi debugging

/**
 * Hardware Pins
 */
#define HALL_PIN 34        // hall sensor
#define WIFI_RESET_PIN 13  // tact switch
#define SERVO_PIN 14       // Press servo
#define CHARACTER_LED_PIN 17
#define FINISH_LED_PIN 5
#define PIN_STEPPER_CHAR_STEP 32
#define PIN_STEPPER_CHAR_DIR 33
#define PIN_STEPPER_CHAR_ENABLE 25
#define BUZZER_PIN 26
// The feeder's four coil pins, in the order AccelStepper wants them rather
// than the order the ULN2003 board prints on its silkscreen -- the middle two
// are conventionally swapped for a 28BYJ-48, so these are numbered by position
// in the constructor, not by the label next to the header.
//
// Flash the firmware BEFORE wiring these: 2 and 15 are boot strapping pins,
// and a coil holding either one at the wrong level stops the ESP32 entering
// the bootloader.
#define PIN_STEPPER_FEED_COIL_1 15
#define PIN_STEPPER_FEED_COIL_2 2
#define PIN_STEPPER_FEED_COIL_3 16
#define PIN_STEPPER_FEED_COIL_4 4

/**
 * Physical Characteristics
 *
 * Physical characteristics of the printer.  These are used to calculate the
 * number of steps required to move and are unlikely to ever need to be changed.
 */

#define CHAR_MICROSTEPS 16
#define CHAR_STEP_COUNT 200
// The shortest label the machine will put on tape, in characters. Anything
// shorter is topped up with blank feeds after the last character is pressed,
// so there is something to take hold of when the tape is cut.
//
// Served to the panel in api/capabilities. The panel pads short labels up
// to it with spaces on both sides, because the trailing feeds of a top-up
// would push the text off centre, and a label that reaches the minimum gets
// none.
constexpr int MIN_LABEL_CHARACTERS = 6;

// The longest label the device will print, so a label POSTed straight at
// /api/tag cannot run the feeder until the tape is spent.
//
// This counts what arrives in the request, not what somebody typed. The
// panel centres a label by padding it with a space on each side, so the
// longest thing it can send is two characters longer than the longest thing
// it lets anyone type. Reading this as the typed length is what made the
// first version of this bound 247 and refuse the panel's own longest label.
// Served to the panel in api/capabilities beside the minimum, and the panel
// subtracts its own margin from it to cap the input.
constexpr int MAX_LABEL_CHARACTERS = 249;

// Which character sits under the press once the hall sensor has found home.
// The wheel is keyed to the hub, so this is the same on every build; the
// per-machine slack in where the sensor ended up is
// ASSEMBLY_CALIBRATION_ALIGN in Machine.h.
//
// Named by the character rather than by its slot number. The number is
// already written down once, in CHARACTERS, and a second copy of it here
// would go stale silently the first time a character is inserted before
// this one on the wheel -- the wheel would then park believing it is one
// slot from where it is.
#define CHAR_HOME_CHARACTER "J"

// The press angle compensation that used to live here
// (ASSEMBLY_CALIBRATION_FORCE) was replaced on 2026-09-22 by the two taught
// angles now in Machine.h: STAMP_ANGLE is the measured point where the press
// just touches the daisy wheel, and PRESS_BITE_AT_MAX_FORCE is how much
// further force 9 drives it.
#define MICROSTEPS_FEED 8
#define FEED_MOTOR_STEPS_PER_REVOLUTION 4076

/**
 * The Roll
 *
 * Nothing in the machine can see the tape, so how much is left on the roll is
 * counted rather than measured: the length the roll started at, less every
 * feed since it went in. These are the numbers that count rests on. The
 * arithmetic itself is in Tape.h and the running total in Roll.
 */

// How far one feed moves the tape, in micrometres. A feed is an eighth of a
// turn of the feed motor (see Feeder::feed), and 4 mm is what that eighth
// pulls through the feed cog on this design -- the same figure the panel has
// always used for a label's length.
//
// Micrometres so a measured correction can be finer than a whole millimetre.
// A 3 m roll is 750 feeds, so a tenth of a millimetre out on each one adds
// up to 75 mm -- two labels -- by the end of the roll.
constexpr int FEED_LENGTH_UM = 4000;

// What a roll is assumed to hold until somebody says otherwise. A device
// starts on a roll this long, and a roll loaded without a length is taken to
// be as long as the last one, so this is every roll until one is given a
// length. The panel offers it as the usual length of a new roll. 3 m is the
// common length for 9 mm embossing tape.
constexpr int DEFAULT_ROLL_LENGTH_MM = 3000;

// The lengths a roll may be declared as when it is loaded, refused outside
// this at the HTTP boundary. The floor is low enough to declare what is left
// of a part-used roll, and the ceiling is above any roll this tape is sold
// on.
constexpr int ROLL_LENGTH_MIN_MM = 500;
constexpr int ROLL_LENGTH_MAX_MM = 10000;

// The most labels one request may ask for. Not a limit anyone should meet --
// the panel's shortest label takes 28 mm of tape, so even the longest roll
// above holds only 357 -- but without one, a number POSTed straight at
// /api/tag could keep the machine pressing air for days after the tape ran
// out.
constexpr int MAX_COPIES = 500;
