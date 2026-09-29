# E-TKT -- domain terms

What the words mean in this codebase, so a name in `src/` can be read against the machine rather than guessed at.
The machine is an embossing label maker: it presses characters into plastic tape one at a time and cuts the finished strip off.

This file names concepts, not classes.
Where a concept has one module, the module is named; where it does not, that is worth noticing.

## The machine

**Tape** - the plastic strip a label is pressed into.
Loaded from a **roll**, advanced by the **feeder**, and cut off at the end.

**Daisy wheel** - the disc carrying one raised character per slot, which rotates to bring the wanted character in front of the press.
`DaisyWheel`.
Which character sits in which slot is `CHARACTERS` in `CharacterSet.h`.

**Slot** - one position on the daisy wheel.
Two characters can share one: the wheel carries no 0 and no 1, so those print from the O and the I the way a typewriter does.
`CHARACTER_ALIASES` says which, and the panel warns before the tape is spent rather than after.

**Home** - the daisy wheel's known position, found by turning until the **hall switch** sees the magnet.
Everything else is counted from there, so every turn to a slot homes first.
A wheel that turns a turn and a half without the sensor seeing the magnet is **lost**: nothing counted from where the search gave up lands on the slot it was meant for.
A job that finds the wheel lost presses nothing more and is stopped, with the wheel as the cause.
Only the home at boot is let off, because every character homes again before it counts.
`HallSwitch`, and `DaisyWheel::home()`, which answers with how the turn ended.

**Press** - the servo-driven arm that drives the tape into the daisy wheel.
`Press`.
Its angles are geometry rather than policy, so the arithmetic lives apart from the driver in `PressGeometry.h`.

**Rest angle / stamp angle** - where the press sits between characters, and where it would sit at full force.
A LOW angle drives the press *into* the wheel, which is the opposite of what the numbers suggest at a glance.

**Force** - how hard a character is pressed, 1 to 9.
It chooses a peak angle between the taught touch point and the stamp angle; it is not a duration and not a current.

**Align** - how far the daisy wheel is offset from where homing left it, 1 to 9.
Takes up the slack in where the hall sensor ended up on this particular machine.

**Calibration value** - either of the above.
Both are 1 to 9, both are refused outside that range at the HTTP boundary and clamped again deeper in, and both are stored in EEPROM.
`CALIBRATION_VALUE_MIN` / `_MAX` in `PressGeometry.h` are the one statement of the range; the panel is told it rather than carrying a copy.

**Cut mark** - the wheel slot the machine drives to in order to cut, rather than a character a label can contain.
`CUT_CHARACTER`.

**Printhead** - the daisy wheel and the press worked together: everything that comes down on the tape.
A character, the cut and the align test's press are each a turn of the wheel and then a press, unless a stop comes in between.
The press never comes down on a slot the wheel did not reach.
`Printhead`, which the job runner drives in place of the wheel and the press.

**Calibration** - an align and a force together, the pair one job presses at from its first character to its cut.
The job runner picks it as the job begins: the pair being trialled for the two tests, and the saved pair for everything else.
Before there was one, each press looked its own up, and the full test cut at the saved align after it stamped its characters at the align it was testing.
`Calibration`, in `Calibration.h`.

## What a label is

**Label** - the text a user asks for.
A **tag** is the command that prints one; the two words are used interchangeably in the older code and the command is named `tag` on the wire.
A label is counted and walked in characters, not bytes, because the wheel's symbols are three bytes of UTF-8 each.
`Utility::characters()` is the one walk: pressing a label, drawing it, playing it and checking it all go through it.

**Printable set** - every character a label may contain: a space, plus every wheel character except the cut mark.
`printableCharacters()`.
Served from `/api/capabilities` so the panel does not keep its own list, which it used to and which disagreed.

**Typed length vs sent length** - not the same number, and the difference is two.
The panel centres a label by padding a space onto each side before it posts it, so a label is two characters longer on the wire than it was in the box.
`MIN_LABEL_CHARACTERS` and `MAX_LABEL_CHARACTERS` are both bounds on the sent length, because that is what the device receives and checks; the panel subtracts its own margin from the maximum to cap what anyone can type.
Reading the maximum as a typed length is what first made it 247 and made the device refuse the longest label the panel could produce.

**Character set** - what the wheel carries and what a label may say: which character sits in which slot, the aliases, the printable set, and the note each slot sounds in a label's tune.
`CharacterSet`, with no Arduino display code, so the host tests can reach it.
A second module named `Characters` used to sit a letter away from it, holding the notes and the OLED glyphs.
The notes belong to the slots, so they moved here, and the glyphs are the screen's business, so they moved into `OledDisplay`.

**Screen** - one of the device's fixed OLED banners: wifi setup, wifi reset, finished, and so on.
One table in `OledDisplay.cpp` says what each one draws, rather than one method each.
`Screen`.

**Display** - what the machine shows on its own screen, told in terms of the job rather than of pixels: a screen, the idle screen, a label's progress, a saved calibration.
`Display` is an interface with two adapters.
`OledDisplay` draws on the 128x64 OLED, and `FakeDisplay` in `test/fakes` records what the job runner asked for, and when.
A display draws and returns: how long a screen stays up is for its caller to say, and the screen is started once, at boot.

**Progress** - how far through a label the machine is, 0 to 99.
It stops at 99 rather than 100 because feeding the tail and cutting still have to happen after the last character is pressed.
`Progress.h`.

## The roll

**Feed** - one step of the tape: an eighth of a turn of the feed motor, which pulls `FEED_LENGTH_UM` of tape through.
What a job uses is counted in feeds, and turned into millimetres only to be shown or taken off a roll's length.
`Feeder::feed()`, and `Feeder::feeds()` for the count so far.

**Roll** - the tape in the machine, and the two things known about it: the length it was declared at when it went in, and the feeds taken from it since.
Neither is measured, because the machine cannot see the tape.
The panel says roll, and `reel`, the command that loads one, is the older word for the same thing.
`Roll` keeps both numbers in EEPROM, so a reboot does not refill the roll.

**Loading a roll** - the `reel` command.
Whatever was fed before it is charged to the old roll, the count starts again at the declared length, and `REEL_FEEDS` then pull the new tape through from the cog to past the cutter, charged to the new one.
A length is `ROLL_LENGTH_MIN_MM` to `ROLL_LENGTH_MAX_MM`.
A request without one keeps the last roll's, and a device that has never been told one starts at `DEFAULT_ROLL_LENGTH_MM`, 3 m.

**Lead** - the blank feed ahead of a label's first character, `LEAD_FEEDS`.
It leaves a margin ahead of the text for the cut at the end of the label before.

**Top-up** - the blank feeds after a short label's last character that bring it up to `MIN_LABEL_CHARACTERS`, so there is something to hold when it is cut.
A one-character label is left short on purpose.
The panel pads its own labels past the minimum, so a top-up is what a label posted some other way gets.
`topUpFeeds()`.

**Tape left** - what the roll's two numbers leave: the declared length less the feeds times `FEED_LENGTH_UM`, and never less than nothing.
It is an estimate, only as good as `FEED_LENGTH_UM` and the length somebody typed in.
So the panel warns when a print looks like more than is left, but does not refuse it: the tape on the spool is the better judge.
`remainingMm()` in `Tape.h`, reported by `/api/status` whether or not anything is running.

**Labels that fit** - how many of a label the tape left holds, each one taking the lead, a feed per character and the top-up, and the cut taking none.
Rounded down, because a label that would run off the end of the tape is not one that fits.
`labelsThatFit()` in `Tape.h`, restated in `data/tape.js`, which is where it is used.
The worked cases in `test/vectors/tape.json` hold both to the same answers, from `test/test_tape` and `test/panel/tape.test.js`.

## Commands

**Command** - one job the machine can be asked to do: cut, feed, reel, testalign, testfull, save, tag, home, move.
Plus `idle`, which is a status rather than a job.

**Descriptor table** - `ETKT::COMMANDS` in `ETKT.cpp`, one row per command.
A row carries the name the command answers to on the wire, the body field its text arrives in, the **command facts** that hold for it, and the handler that runs it.
It is the single statement of what the commands are: the HTTP routes, the dispatch and the name `/api/status` reports are all read from it rather than restated.
`/api/capabilities` serves every row with a handler, keyed by name, so the panel reads what a command is from the device as well.
No code outside the table compares a command against a name to decide what it does: it asks the command's row.
The panel keys only its wording by name, and decides the rest by the facts `/api/capabilities` serves.
A command is added by adding an enumerator and a row.

**Command fact** - one thing a row of the **descriptor table** says is true of its command, from `CommandFact` in `ETKT.h`.
Which calibration fields the body carries, whether its text is a label, whether it prints a **run**, whether it takes a roll length, whether it is **stoppable**, and whether it presses a label into the tape.
A row names the facts that hold and leaves the rest out.

**Stoppable** - a **command fact**: `ETKT::stop()` stops the command.
Every command that moves is stoppable; a save is not.
The panel offers the red stop for every command the device says is stoppable, and never for one it says is not.

**Job runner** - `ETKT`, which takes one command at a time and runs it.
The web server's task hands it a command with `submit()`, which holds one at a time, and the command loop takes it from there in `loop()`: it runs the command, parks the motors, lets the command go, and then draws the idle screen, so a job posted while that draws is taken.
Every stop rule lives here: what can be stopped, what a stop leaves on the tape, and what the panel is told afterwards.
Everything it presses goes through the **printhead**, at the job's one **calibration**.
It builds and runs on a host against fakes, so `test/test_etkt` checks those rules without a machine or a roll of tape.

**Busy** - a command is running.
The machine runs one at a time, and a second request is refused with a 409: the request was fine, the machine was not.

**Run** - one `tag` request for more than one label: the same label pressed `copies` times, one after another, each cut before the next begins.
Whether a command prints one is a **command fact**, and `tag` is the only command it holds for.
`copies` is 1 to `MAX_COPIES`, and a request without it prints one.
`/api/status` reports the label being pressed as `copy` of `copies`, and the roll is charged after each one, so the tape left moves label by label.

**Quantity** - the panel's way of asking for a run: one, multiple (2 up to `MAX_COPIES`), or max.
Max is the labels that fit, capped at `MAX_COPIES`, worked out in the panel and sent as a number; the device has no idea of the end of the roll.

**Stop** - `POST /api/stop`, for when something has gone wrong: it stops what the machine is doing, now.
The motors halt within a step and a tune within a note.
The press is the exception: once it is on its way down it finishes the stroke and comes back up, because a servo stopped partway is a press held against the wheel.
Whatever was being pressed is left on the tape as far as it got, uncut.
A command can be stopped when its row says it is **stoppable**, which is anything but a save, since a save moves nothing and ends in a reboot.
The panel offers it, as the red stop button, for printing, the two tests and loading a roll; a feed or a cut is over before a finger could get there.
It is not a command: it does not wait its turn, because it is about the command that is running.
A stop with nothing running is not an error, since the job may have just ended.
`/api/status` reports one that has been asked for and not yet obeyed as `stop`, so a panel opened partway through a stop says so too.
The operator is not the only cause: a job that finds the wheel **lost** stops itself the same way.
The first cause stands, so pressing stop while a lost wheel parks does not hide why the job ended.
`ETKT::stop()`, and `StopSignal`, which carries it from the web server's task to the command loop and keeps its cause.

**Stop after this label** - `POST /api/stop?after=label`, for a run that is going fine and is longer than it needs to be.
The run ends once the label being pressed is cut, so no tape is spent on a label nobody finishes.
Only a run of labels has a label to stop after, and anything else is refused with a 409.
Nothing is cut short, so it leaves no stopped record, and the run ends with the usual celebration.
`ETKT::stopAfterLabel()`.

**Stopped** - what the last stop cut short: the command, how many of a run's labels were finished, and whether it left a label **unfinished** on the tape.
It also says what stopped it, the operator or a lost wheel, and carries an id of its own, so two stops that say the same thing are still two stops and a panel can dismiss one without hiding the next.
The ids count up from a random start at every boot, so a stop after a reboot is not taken for one dismissed before it.
An unfinished label is still joined to the roll and would come out on the front of the next one, so the panel offers to cut it off.
`/api/status` reports it as `stopped` until the next command is accepted, so a panel that was not watching when a job was stopped can still say it was.
A stop that arrived as the job was finishing anyway, on the last press of the last cut or during the celebration after it, cut nothing short and leaves no record; the machine still skips what is left of the celebration, because a stop was asked for.
The OLED says stopped where it would say ready.
`StoppedCommand`.

## Where the parts meet

**Driver seam** - `Drivers.h` declares the servo and stepper interfaces the modules take, and `Display.h` the screen's.
So the job runner and every module it drives build against recording fakes on a host, and against ESP32Servo, AccelStepper and the OLED on the board.
Adapters: `ArduinoDrivers.h` and `OledDisplay` for the device, `test/fakes` for the tests and the **simulator**.
`test/stubs` stands in for the rest of what the board supplies: the Arduino core, Preferences and the sounder.

**Api** - `Api.cpp`, everything the device answers under `/api/`.
That is the routes, the checks on a request, the words of every refusal, and the JSON of every reply.
It takes a `Request` and gives back a `Reply`, so the host tests check every reply.
The webserver in `Network.cpp` is an adapter in front of it.
It turns each HTTP request under `/api/` into a `Request`, sends the `Reply` back as it comes, and has no rules of its own.
A reply too large for its document is a 500 and not a reply with fields missing, and every 500 also goes in the **log**.

**Per-machine calibration** - `Machine.h`.
The seven numbers that differ between two physically built E-TKTs: the two press angles, the press bite, the hall sensor's polarity and threshold, the align offset, and the feeder direction.
Everything that is the same on every E-TKT ever built stays in `Configuration.h`.
The question that decides which file a value belongs in is "would you change this when you build a second machine from the same design?".

**Bench rig** - a firmware of its own that proves out one part of the machine or teaches one of those numbers, such as sweeping the servo or watching the hall sensor.
Each one is a file in `src/bench/` with its own `setup()` and `loop()`, built by its own `bench-*` env in place of `LabelMaker.cpp`, so the label maker carries none of them and every one of them still compiles.
A rig uses the label maker's own modules, so the hall rig reads the sensor through `HallSwitch` and sees the same edges that homing sees.
Flash `serial-upload` afterwards to put the label maker back.

**Simulator** - `src/simulator`, the firmware built for a computer, with the panel in front of it, so that work on the panel needs no machine.
`main.cpp` builds the job runner and the **Api** on the fakes the native tests use, and reads requests on stdin.
`server.py` serves `data/` and relays each request under `/api/` to that program, and its reply back.
So every command, refusal and status field the firmware has, the simulator has too, with nothing written down twice.
It used to be a Python copy of the firmware, which drifted until every button returned a 404.
A wait takes its time on the machine divided by `--speed`, and a move of the wheel or the tape takes no time.

## Reading the machine

**Log** - the last 32 lines the device said, kept in memory and served as plain text from `/api/log`, because the machine is on a bench on wifi and a serial cable is not always the answer.

**Panel** - the web UI in `data/`, served from SPIFFS.
It asks the device what it will accept at startup (`/api/capabilities`) instead of deciding for itself.
It polls `/api/status` every second, and every five while the page is hidden, so the tape left and a label sent from another phone show without a reload.
`script.js` reads the page and draws it.
What the page says and decides is worked out in the ES modules beside it, which never touch the page, so `node --test "test/panel/*.test.js"` covers them.
`test/panel/capabilities.json` is the `/api/capabilities` reply those tests run against, and the simulator's tests hold it to the firmware's.
