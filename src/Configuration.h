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
#define ENABLE_OTA false    // Enables OTA updates at http://<address>/update
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
// turn of the feed motor (see Feeder::feed). Measured on a 3 m roll of
// " FORGIVEN " with a space on each side, which is 11 feeds a label: at
// 4.0 mm a feed that roll was counted five labels short of what it held.
// 3.7 mm a feed is that roll, 73 labels.
//
// Micrometres so a measured correction can be finer than a whole millimetre.
// A 3 m roll is about 810 feeds at this length, so a tenth of a millimetre
// out on each one adds up to about 80 mm, two labels, by the end of the roll.
constexpr int FEED_LENGTH_UM = 3700;

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
// the panel's shortest label takes 25.9 mm of tape, so even the longest roll
// above holds only 386 -- but without one, a number POSTed straight at
// /api/tag could keep the machine pressing air for days after the tape ran
// out.
constexpr int MAX_COPIES = 500;

/**
 * Wi-Fi
 *
 * The radio at a machine can be weak enough that a join takes minutes,
 * and most tries die in a handshake timeout. One ten-second try, then a
 * portal that never goes back to the saved network, is how the panel
 * used to stay dark. The machine keeps trying the networks it remembers
 * for as long as it takes, and nothing waits for it: the machine starts,
 * and prints, with no network at all.
 *
 * Its own network is the way in when there is no other: an access point
 * with a password, serving the same panel at WIFI_OWN_ADDRESS. It opens
 * beside the tries when there is no network to try, or when a minute has
 * passed with none joined, and in own mode it is all there is. See
 * LinkSupervisor.
 */

// How often the link is looked at: whether a try has ended, whether the
// network is still there, whether a phone has come or gone.
constexpr uint32_t WIFI_STEP_MS = 250;

// How long to wait after a failed try before the next one. Long enough
// that the stack has finished the disconnect, short enough that a run
// of handshake timeouts still joins in the minutes this spot takes.
constexpr uint32_t WIFI_RETRY_MS = 3000;

// How many tries in a row a network gets when it is there and turns the
// machine away, before the next one the machine remembers gets its turn.
// On a weak link that is how most tries end, and the next network is one
// from another place. A network that is not there gets a single try.
constexpr uint32_t WIFI_TRIES_PER_NETWORK = 3;

// While the machine's own network is open beside them, tries are this far
// apart. A try takes the radio off the channel that network is on, so
// closer ones would leave a phone little chance of finding it.
constexpr uint32_t WIFI_RETRY_BESIDE_OWN_MS = 30000;

// And this far apart while a phone is on it. Every try stalls that phone's
// pages, and a phone on the machine's own network is somebody working the
// machine.
constexpr uint32_t WIFI_RETRY_WHILE_CLIENT_MS = 300000;

// A try that never ends, because no disconnect event arrives, is
// abandoned after this and started again. A try that is going to fail
// does it well inside this.
constexpr uint32_t WIFI_TRY_MS = 30000;

// How long an association may sit without an address before it is
// dropped and the join starts again. A measured DHCP on this link took
// 51 s and then worked, so the cap sits well past that. The ordinary
// try cap above is shorter, and applying it here would abandon a join
// that is about to succeed.
constexpr uint32_t WIFI_DHCP_MS = 120000;

// How long to try the remembered networks on their own before the
// machine's own network opens beside them. With none remembered, it opens
// at once. For this long after the networks or the mode are changed on the
// panel, the tries are WIFI_RETRY_MS apart again whoever is on the
// machine's own network: that is somebody waiting to see whether the change
// worked. A network that was lost gets no such hurry.
constexpr uint32_t WIFI_OWN_AFTER_MS = 60000;

// How often, while still offline, to say that the join is still going.
// The log keeps 32 lines, so this is a summary and not one line a try.
constexpr uint32_t WIFI_REPORT_MS = 60000;

// How long the machine's own network stays open once another is joined
// and the last phone has left it. The phone a network was typed in on is
// still on the machine's own, and that is where it reads the address to
// go to next.
constexpr uint32_t WIFI_OWN_LINGER_MS = 60000;

// How long to wait before opening the machine's own network again when it
// did not open.
constexpr uint32_t WIFI_OWN_RETRY_MS = 10000;

// How long a change made on the panel waits before the radio follows it.
// The reply to the request that made it has to leave first, over a link
// the change may take away.
constexpr uint32_t WIFI_SETTLE_MS = 2000;

// How long the screen shows the panel's address to a phone that has just
// joined the machine's own network. After that it shows how to join again,
// which is what the next phone needs.
constexpr uint32_t WIFI_ADDRESS_SHOWN_MS = 120000;

// How many phones the machine's own network takes at once. The radio
// stops at 10, and each one costs memory the panel's pages need.
constexpr int WIFI_OWN_CLIENTS = 4;

// Where the panel is on the machine's own network.
constexpr char WIFI_OWN_ADDRESS[] = "192.168.4.1";

/**
 * The Button
 *
 * The tact switch on WIFI_RESET_PIN. Held through a boot it makes the
 * machine forget its networks, which was all it did. Once the machine is
 * up it now works the machine with no phone and no network: a press stops
 * a job, a press with nothing running prints the last run, or what is left
 * of one that was cut short, and a hold unloads the roll. See Button.
 */

// How often the button is read.
constexpr uint32_t BUTTON_POLL_MS = 10;

// How long a reading has to hold before it counts. A contact bounces for a
// few milliseconds as it closes, and this pin has read low with nobody near
// it: a boot took that for the button and cleared the saved network. Longer
// than either, and too short to feel as a wait on a stop.
constexpr uint32_t BUTTON_DEBOUNCE_MS = 50;

// How long the button has to stay down as the machine starts for it to
// forget its networks. Every reading in this time has to be low, so the
// pin's false low at a boot does not count, and neither does a finger that
// only brushed it. Short enough to hold through on purpose.
constexpr uint32_t BUTTON_BOOT_HOLD_MS = 1000;

// How long the button is held, with nothing running, for the roll to be
// unloaded. A press is let go well inside this, and a hold is not long
// enough to be a chore: a roll is changed about every seventy labels.
constexpr uint32_t BUTTON_HOLD_MS = 1500;

// How long the machine has to have sat idle before a press starts anything.
// A finger on its way to stop a job can land just after the job has ended
// by itself, and that press would otherwise print the whole run again. The
// panel holds its stop buttons back for the same reason, the other way
// round.
constexpr uint32_t BUTTON_ARMING_MS = 1000;
