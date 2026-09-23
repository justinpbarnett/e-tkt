# E-TKT -- domain terms

What the words mean in this codebase, so a name in `src/` can be read
against the machine rather than guessed at. The machine is an embossing
label maker: it presses characters into plastic tape one at a time and cuts
the finished strip off.

This file names concepts, not classes. Where a concept has one module, the
module is named; where it does not, that is worth noticing.

## The machine

**Tape** - the plastic strip a label is pressed into. Loaded from a
**reel**, advanced by the **feeder**, and cut off at the end.

**Daisy wheel** - the disc carrying one raised character per slot, which
rotates to bring the wanted character in front of the press. `DaisyWheel`.
Which character sits in which slot is `CHARACTERS` in `CharacterSet.h`.

**Slot** - one position on the daisy wheel. Two characters can share one:
the wheel carries no 0 and no 1, so those print from the O and the I the way
a typewriter does. `CHARACTER_ALIASES` says which, and the panel warns
before the tape is spent rather than after.

**Home** - the daisy wheel's known position, found by turning until the
**hall switch** sees the magnet. Everything else is counted from there.
`HallSwitch`, `DaisyWheel::home()`.

**Press** - the servo-driven arm that drives the tape into the daisy wheel.
`Press`. Its angles are geometry rather than policy, so the arithmetic lives
apart from the driver in `PressGeometry.h`.

**Rest angle / stamp angle** - where the press sits between characters, and
where it would sit at full force. A LOW angle drives the press *into* the
wheel, which is the opposite of what the numbers suggest at a glance.

**Force** - how hard a character is pressed, 1 to 9. It chooses a peak angle
between the taught touch point and the stamp angle; it is not a duration and
not a current.

**Align** - how far the daisy wheel is offset from where homing left it,
1 to 9. Takes up the slack in where the hall sensor ended up on this
particular machine.

**Calibration value** - either of the above. Both are 1 to 9, both are
refused outside that range at the HTTP boundary and clamped again deeper in,
and both are stored in EEPROM. `CALIBRATION_VALUE_MIN` / `_MAX` in
`PressGeometry.h` are the one statement of the range; the panel is told it
rather than carrying a copy.

**Cut mark** - the wheel slot the machine drives to in order to cut, rather
than a character a label can contain. `CUT_CHARACTER`.

## What a label is

**Label** - the text a user asks for. A **tag** is the command that prints
one; the two words are used interchangeably in the older code and the
command is named `tag` on the wire.

**Printable set** - every character a label may contain: a space, plus every
wheel character except the cut mark. `printableCharacters()`. Served from
`/api/capabilities` so the panel does not keep its own list, which it used to
and which disagreed.

**Typed length vs sent length** - not the same number, and the difference is
two. The panel centres a label by padding a space onto each side before it
posts it, so a label is two characters longer on the wire than it was in the
box. `MIN_LABEL_CHARACTERS` and `MAX_LABEL_CHARACTERS` are both bounds on the
sent length, because that is what the device receives and checks; the panel
subtracts its own margin from the maximum to cap what anyone can type. Reading
the maximum as a typed length is what first made it 247 and made the device
refuse the longest label the panel could produce.

**Character set vs Characters** - two modules with names a letter apart.
`CharacterSet` is what the *wheel* carries and what a label may say: a map,
the aliases, the printable set, and no Arduino display code, so the host
tests can reach it. `Characters` is what a character *looks and sounds
like*: the OLED glyph and font offsets for the four symbols, and the note
the sounder plays. A label's rules are the first one.

**Screen** - one of the device's fixed OLED banners: wifi setup, wifi reset,
finished, and so on. One table says what each one draws, rather than one
method each. `Display`, `Screen`.

**Progress** - how far through a label the machine is, 0 to 99. It stops at
99 rather than 100 because feeding the tail and cutting still have to happen
after the last character is pressed. `Progress.h`.

## Commands

**Command** - one job the machine can be asked to do: cut, feed, reel,
testalign, testfull, save, tag, home, move. Plus `idle`, which is a status
rather than a job.

**Descriptor table** - `ETKT::COMMANDS` in `ETKT.cpp`, one row per command.
A row carries the name the command answers to on the wire, which body fields
it reads, and the handler that runs it. It is the single statement of what
the commands are: the HTTP routes, the dispatch, the name `/api/status`
reports and the simulator's endpoints are all read from it rather than
restated. A command is added by adding an enumerator and a row.

**Busy** - a command is running. The machine runs one at a time, and a
second request is refused with a 409: the request was fine, the machine was
not.

## Where the parts meet

**Driver seam** - `Drivers.h` declares the servo and stepper interfaces the
modules take, so `Press` and `Feeder` can be built against recording fakes on
a host and against ESP32Servo and AccelStepper on the board. Adapters:
`ArduinoDrivers.h` for the device, `test/fakes` for the tests.

**Per-machine calibration** - `Machine.h`. The seven numbers that differ
between two physically built E-TKTs: the two press angles, the press bite,
the hall sensor's polarity and threshold, the align offset, and the feeder
direction. Everything that is the same on every E-TKT ever built stays in
`Configuration.h`. The question that decides which file a value belongs in
is "would you change this when you build a second machine from the same
design?".

**Bench rig** - a `BENCH_*` block in `Configuration.h` that replaces
`loop()` with a jig for teaching one of those numbers: sweeping the servo,
watching the hall sensor. Lifted into `BenchRigs` so the device's own code
does not carry them. They come out once the last machine is built.

**Simulator** - `src/simulator`, a Python stand-in that serves the panel
without a machine. It reads the descriptor table, the character set and the
calibration range out of `src/` rather than restating them, because when it
restated them it drifted and every button returned a 404.

## Reading the machine

**Log** - the last 32 lines the device said, kept in memory and served as
plain text from `/api/log`, because the machine is on a bench on wifi and a
serial cable is not always the answer.

**Panel** - the web UI in `data/`, served from SPIFFS. It asks the device
what it will accept at startup (`/api/capabilities`) instead of deciding for
itself, and polls `/api/status` while a command runs.
