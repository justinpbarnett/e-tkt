#pragma once

#include "PressGeometry.h"

/**
 * Per-machine calibration.
 *
 * Everything in here is a number somebody read off one physical build. Two
 * E-TKTs assembled from the same files need different values, because a
 * press-fit part lands where it lands and a hall sensor is glued where it is
 * glued. Nothing here is a design decision -- those live in
 * Configuration.h, which is the same for every machine.
 *
 * This file used to be four files. The taught servo angles were in Press.h,
 * the hall sensor's threshold and polarity and the wheel's assembly offset
 * were in Configuration.h among forty constants that are not per-machine,
 * and the feed direction was a third place. Bringing a new machine up meant
 * finding them, and reteaching one meant overwriting the last machine's.
 *
 * To flash a different machine, change MACHINE below. Each block is the
 * record of what was taught on that bench, so a machine keeps its numbers
 * when another one is worked on.
 */

#define MACHINE 1

#if MACHINE == 1

// Machine 1. Breadboard ESP32 DevKit, brought up 2026-09-18 to 2026-09-22.

// --- press --------------------------------------------------------------
// LOW angle drives the press INTO the daisy wheel, HIGH swings it clear.
// Teach these with BENCH_SERVO_TEST (Configuration.h), then read
// docs/diy/assembly/04_servo.md: power the board FIRST so the servo homes to
// REST_ANGLE, and only then push the P_press on with its lateral line 100%
// vertical. Fitting the part first and teaching angles afterwards gives a
// stroke roughly twice as long as the design intends.
//
// REST_ANGLE: press clear of the wheel, P_press lateral line vertical here.
// STAMP_ANGLE: press just touching the wheel, measured on the bench.
//
// docs/diy/calibration.md gives the one hard check: at power-on the press
// should sit 2mm from the I_nema_wheel_hub. If it does not, the fix is a
// re-fit of the P_press, not a different number here.
#define REST_ANGLE 50
#define STAMP_ANGLE 15
// Degrees past STAMP_ANGLE at force 9. Force 1 lands exactly on STAMP_ANGLE,
// so this is the whole span the force slider has to work with. Usually left
// alone; raise it only if force 9 still will not emboss with the 2mm gap
// checked and the tape seated.
#define PRESS_BITE_AT_MAX_FORCE 8

// --- daisy wheel hall sensor ---------------------------------------------
// Which way the sensor reads. Invert for a "3144"; do not for a "44E 402".
// Symptom of getting it wrong: the wheel creeps forward slightly at startup
// and stops, instead of driving to the "J" position.
#define INVERT_HALL_SENSOR_LOGIC false
// analogRead() below this counts as the magnet being present, out of 4096.
// Sweep the wheel with BENCH_HALL_MONITOR and put this between the two
// readings. Machine 1, measured 2026-09-18: magnet present 0, clear 4095.
#define HALL_SENSOR_THRESHOLD 128
// Nudges the home position to take up the slack in where the sensor ended up
// sitting. -1.0 to 1.0, in characters.
#define ASSEMBLY_CALIBRATION_ALIGN 0.5f

// --- feeder ---------------------------------------------------------------
// True if the tape feeds backwards. It is obvious when it happens.
#define REVERSE_FEED_STEPPER_DIRECTION false

#elif MACHINE == 2

// Machine 2. UNTAUGHT -- these are machine 1's numbers, kept only so the
// build works. Every one of them has to be read off machine 2's own bench
// before it prints anything; see the comments in the MACHINE == 1 block for
// how each is taken.
#define REST_ANGLE 50
#define STAMP_ANGLE 15
#define PRESS_BITE_AT_MAX_FORCE 8
#define INVERT_HALL_SENSOR_LOGIC false
#define HALL_SENSOR_THRESHOLD 128
#define ASSEMBLY_CALIBRATION_ALIGN 0.5f
#define REVERSE_FEED_STEPPER_DIRECTION false

#elif MACHINE == 3

// Machine 3. UNTAUGHT -- see the note on machine 2.
#define REST_ANGLE 50
#define STAMP_ANGLE 15
#define PRESS_BITE_AT_MAX_FORCE 8
#define INVERT_HALL_SENSOR_LOGIC false
#define HALL_SENSOR_THRESHOLD 128
#define ASSEMBLY_CALIBRATION_ALIGN 0.5f
#define REVERSE_FEED_STEPPER_DIRECTION false

#else
#error "MACHINE must name a block in Machine.h"
#endif

// The two mistakes that stop the press working without stopping the build.
//
// Equal angles make the press travel nowhere: pressDirection() returns 0 and
// every force computes the same peak. A bite of zero does the same thing to
// the force slider on its own, leaving all nine values landing on
// STAMP_ANGLE.
static_assert(REST_ANGLE != STAMP_ANGLE,
              "REST_ANGLE and STAMP_ANGLE must differ, or the press has no "
              "stroke at all");
static_assert(PRESS_BITE_AT_MAX_FORCE > 0,
              "PRESS_BITE_AT_MAX_FORCE must be positive, or force 1 and force "
              "9 press identically");
static_assert(REST_ANGLE >= SERVO_ANGLE_MIN && REST_ANGLE <= SERVO_ANGLE_MAX,
              "REST_ANGLE is outside the servo's travel");
static_assert(STAMP_ANGLE >= SERVO_ANGLE_MIN && STAMP_ANGLE <= SERVO_ANGLE_MAX,
              "STAMP_ANGLE is outside the servo's travel");
