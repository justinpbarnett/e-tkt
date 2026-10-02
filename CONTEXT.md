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
A run homes once more before its first label, so a lost wheel stops it before any tape moves, and no label homes of its own.
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

**Cut** - the cut mark pressed as hard as the calibration's force, easing in a degree at a time.
The cut that ends a label is one press, and so is the one that ends the full test.
The `cut` command, which is the Cut button, presses three times, as the machine always did: it is one cut, not one a label, so it can take the time.
The blade does not go all the way through either way, so a label still comes off with scissors.
A stop that comes during a press lets that press finish and presses no more.
`Printhead::cut()`.

**Printhead** - the daisy wheel and the press worked together: everything that comes down on the tape.
A character and the align test's press are each a turn of the wheel and then a press, and the cut is a turn and then as many presses as it is asked for, unless a stop comes in between.
The press never comes down on a slot the wheel did not reach, nor on tape that is still moving: the tape feeds up to the character while the wheel turns to it, and the press waits for both.
`Printhead`, which the job runner drives in place of the wheel and the press.

**Calibration** - an align and a force together, the pair one job presses at from its first character to its cut.
The job runner picks it as the job begins: the pair being trialled for the two tests, and the saved pair for everything else.
Before there was one, each press looked its own up, and the full test cut at the saved align after it stamped its characters at the align it was testing.
`Calibration`, in `Calibration.h`.

**Button** - the one tact switch on the machine, which works it with no phone and no network to reach it over.
While a job runs, a press stops it the moment the button goes down, as the panel's stop does.
While nothing runs, a press prints the **last run** as the button comes up: what is left of it when it was cut short, and all of it again when it ran to its end.
Held for `BUTTON_HOLD_MS` while nothing runs, it unloads the roll, and the next press loads one in place of printing.
So a roll is changed with a hold and two presses: unload, load, and the run carries on.
A press comes before those when the tape ran out while the run printed, to stop it.
Held down as the machine starts, for all of `BUTTON_BOOT_HOLD_MS`, it does none of that: the machine forgets every **remembered network** and the password of its **own network**, and restarts.
A reading that has not held for `BUTTON_DEBOUNCE_MS` is no press, because the pin has read low with nobody near it.
A press that comes down before the machine has sat idle for `BUTTON_ARMING_MS` starts nothing, so a finger on its way to stop a job that has just ended does not print the run again.
It used to be the wifi reset button and nothing else, and the pin is still named `WIFI_RESET_PIN`.
`Button`, read every `BUTTON_POLL_MS` from a task of its own, since a press is to stop a job while the command loop is busy running it.

## What a label is

**Label** - the text a user asks for.
A **tag** is the command that prints one; the two words are used interchangeably in the older code and the command is named `tag` on the wire.
A label is counted and walked in characters, not bytes, because the wheel's symbols are three bytes of UTF-8 each.
`Utility::characters()` is the one walk: pressing a label, drawing it, playing it and checking it all go through it.

**Printable set** - every character a label may contain: a space, plus every wheel character except the cut mark.
`printableCharacters()`.
Served from `/api/capabilities` so the panel does not keep its own list, which it used to and which disagreed.

**Typed length vs sent length** - not the same number.
The panel centres a label by padding spaces onto both sides before it posts it: the margin picked on the page, none or 1 space a side, and more on a short label, to bring it up to the minimum.
`MIN_LABEL_CHARACTERS` and `MAX_LABEL_CHARACTERS` are both bounds on the sent length, because that is what the device receives and checks; the panel subtracts its widest margin from the maximum to cap what anyone can type.
Reading the maximum as a typed length is what first made it 247 and made the device refuse the longest label the panel could produce.

**Character set** - what the wheel carries and what a label may say: which character sits in which slot, the aliases, the printable set, and the note each slot sounds in a label's tune.
`CharacterSet`, with no Arduino display code, so the host tests can reach it.
A second module named `Characters` used to sit a letter away from it, holding the notes and the OLED glyphs.
The notes belong to the slots, so they moved here, and the glyphs are the screen's business, so they moved into `OledDisplay`.

**Screen** - one of the device's fixed OLED banners: wifi reset, cutting, finished, new roll, and so on.
One table in `OledDisplay.cpp` says what each one draws, rather than one method each.
None of them is about joining a network: the machine starts and works without one, so how it is reached is a part of the **idle screen**.
`Screen`.

**Display** - what the machine shows on its own screen, told in terms of the job rather than of pixels: a screen, the idle screen, a label's progress, a saved calibration.
`Display` is an interface with two adapters.
`OledDisplay` draws on the 128x64 OLED, and `FakeDisplay` in `test/fakes` records what the job runner asked for, and when.
A display draws and returns: how long a screen stays up is for its caller to say, and the screen is started once, at boot.

**Idle screen** - what the machine shows while it waits for a job: ready, or stopped after a job that was stopped, and how the machine is reached.
That last part is the connection info: a network's name, a line under it, and a QR code when there is something to scan.
On a network, it is the network, the machine's address there, and a code that opens the panel.
With its **own network** open, it is that network's name, its password, and a code a phone's camera joins the network from.
Once a phone has joined, it is the panel's address there and a code that opens it, for `WIFI_ADDRESS_SHOWN_MS`, and then the code to join again, which is what the next phone needs.
The **link** writes all three and knows what they mean, and the screen only lays them out.
The link changes while the machine prints, so the link's task only says what the screen is to show, and the command loop draws it once the machine is idle.
While the **roll is out** the machine shows the new roll screen in its place.
`ConnectionInfo` in `Display.h`, `LinkSupervisor::publish()`, and `ETKT::showIdle()`.

**Progress** - how far through a label the machine is, 0 to 99.
It stops at 99 rather than 100 because feeding the tail, and the cut on a label that is cut, still have to happen after the last character is pressed.
`Progress.h`.

## The roll

**Feed** - one step of the tape: an eighth of a turn of the feed motor, which pulls `FEED_LENGTH_UM` of tape through.
What a job uses is counted in feeds, and turned into millimetres only to be shown or taken off a roll's length.
Backing the tape out, in **unloading a roll**, leaves the count as it was.
`Feeder::feed()`, and `Feeder::feeds()` for the count so far.

**Feeding in the background** - how a label moves its tape: `Feeder::start()` asks for feeds and returns at once, and the tape moves while the daisy wheel turns, because every loop and wait of the wheel's keeps it going.
So the wheel turns to the next character while the tape moves up to it, and the press waits for both.
Before, each waited for the other, and the machine waited another half second after every character.
Nothing on the board turns a motor by itself, so anything that holds the command loop up stalls a feed under way, and the screen is drawn only once the tape has stopped.
`Background` in `Motion.h`, which `Feeder` is.

**Roll** - the tape in the machine, and the two things known about it: the length it was declared at when it went in, and the feeds taken from it since.
Neither is measured, because the machine cannot see the tape.
The panel says roll, and `reel`, the command that loads one, is the older word for the same thing.
`Roll` keeps both numbers in EEPROM, so a reboot does not refill the roll.

**Loading a roll** - the `reel` command.
Whatever was fed before it is charged to the old roll, the count starts again at the declared length, and `REEL_FEEDS` then pull the new tape through from the cog to past the cutter, charged to the new one.
A length is `ROLL_LENGTH_MIN_MM` to `ROLL_LENGTH_MAX_MM`.
A request without one keeps the last roll's, and a device that has never been told one starts at `DEFAULT_ROLL_LENGTH_MM`, 3 m.

**Unloading a roll** - the `unload` command, which undoes a load, so the roll can come out without being cut or fed the rest of the way through.
The press goes to rest, and the tape backs out `REEL_FEEDS`, as far as a load pulls it in: from the cutter, where the last cut left its end, to behind the cog.
Tape fed past the cutter since then is further to go, so the panel asks for any printed label to be cut off first.
The roll's count is left as it was.
The tape that comes back is still on the roll, but the machine cannot tell how much came back, because once the end is out of the cog the motor turns without moving it.
So the roll reads a little shorter than it is rather than longer, and a stop partway cannot say whether the tape is still in the cog, which the panel asks the user to check.
The panel offers it in Setup beside loading a new roll, not beside feed and cut: an unload by mistake during a shift means loading the roll again, which starts its count again as if it were new.
A hold of the **button** asks for it too.
`ETKT::unloadCommandInternal()` and `Feeder::backOut()`.

**Roll out** - what an unload leaves: the tape is backed away from the cutter, and no roll has been threaded through to it since.
The roll is out from the first step back, however far a stop lets the unload get, because nothing can say whether the end is still in the cog.
Only a load that reaches the cutter puts it in again, so a load that was stopped leaves it out.
It is kept in EEPROM, so a machine switched off between the two still knows.
While the roll is out the machine's screen says new roll where the **idle screen** would be, and a press of the button loads a roll in place of printing.
Only the machine's screen and its button know: the panel does not say it, and a label sent from the panel is not refused for it.
`RollState::out`, `Roll::takeOut()` and `Roll::putIn()`.

**Lead** - the blank feed ahead of a label's first character, `LEAD_FEEDS`.
It leaves a margin ahead of the text for the cut at the end of the label before.

**Top-up** - the blank feeds after a short label's last character that bring it up to `MIN_LABEL_CHARACTERS`, so there is something to hold when it is cut.
A one-character label is left short on purpose.
The panel pads its own labels to the minimum, so a top-up is what a label posted some other way gets.
`topUpFeeds()`.

**Tape left** - what the roll's two numbers leave: the declared length less the feeds times `FEED_LENGTH_UM`, and never less than nothing.
It is an estimate, only as good as `FEED_LENGTH_UM` and the length somebody typed in.
So the panel warns when a print looks like more than is left, but does not refuse it: the tape on the spool is the better judge.
The panel says it, like the length of a label or a run, in whole millimetres under a metre and in metres to the centimetre from there, rounded down so it never says more is left than the estimate: `formatLength()` in `data/tape.js`.
`remainingMm()` in `Tape.h`, reported by `/api/status` whether or not anything is running.

**Labels that fit** - how many of a label the tape left holds, each one taking the lead, a feed per character and the top-up, and the cut taking none.
Rounded down, because a label that would run off the end of the tape is not one that fits.
`labelsThatFit()` in `Tape.h`, restated in `data/tape.js`, which is where it is used.
The worked cases in `test/vectors/tape.json` hold both to the same answers, from `test/test_tape` and `test/panel/tape.test.js`.

## Commands

**Command** - one job the machine can be asked to do: cut, feed, reel, unload, testalign, testfull, save, tag, home, move.
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

**Command id** - what the panel sends a command or a stop under, as `?id=`, so that one whose answer was lost on the way back can be sent again and not run twice.
The device keeps what it answered under its newest ids, and a request that comes again under one of them is told what it was told the first time, with nothing more done.
Of a command it keeps only the answer that took it: one that was refused ran nothing, so sent again it is judged again, since the machine may be free by then.
Of a stop it keeps every answer, a refusal too, because a stop sent again must not reach a command that began after it.
A change to how the machine is reached goes out under one as well, and so does a listen for the **networks in reach**.
Of a change it keeps only that it was made, and one sent again is answered with the network as it is by then, so it neither undoes a change made after it nor starts a try over.
Of a listen it keeps the answer that took it, so one sent again starts no second listen.
`/api/status` names the command the machine last took as `last_command_id`, so a poll that gets through says the command arrived when its own answer does not.
The answers are kept in memory, so a restart forgets them, and a command sent again across one runs again.
An id is at most 36 bytes, and a request under none is taken as it always was.
How many are kept is `remembered_ids` in `/api/capabilities`.
`Api::once()`, and `data/link.js` for the panel's half.

**Run** - one `tag` request: the same label pressed `copies` times, one after another.
Whether a command prints one is a **command fact**, and `tag` is the only command it holds for.
`copies` is 1 to `MAX_COPIES`, and a request without it prints one.
`cut` says whether each label is cut off as it finishes, and a request without it cuts.
Uncut, a run comes out as one strip to cut apart by hand, and a little sooner.
The panel's "Cut after each label" box is `cut`, and the browser keeps the choice.
`/api/status` reports the label being pressed as `copy` of `copies`, and the roll is charged after each one, so the tape left moves label by label.

**Quantity** - the panel's way of asking for a run: one, multiple (2 up to `MAX_COPIES`), or max.
Max is the labels that fit, capped at `MAX_COPIES`, worked out in the panel and sent as a number; the device has no idea of the end of the roll.

**Last run** - the last run the machine started, and how far it got: the label, how many of it, whether each is cut, and how many of them are printed.
It is what a press of the **button** prints, so a run can be repeated, or carried on after a roll change, with no phone and no network to ask it over.
A run that printed all of its labels comes whole again.
One that ended before that is carried on at the first label it did not finish, whichever way it ended: a **stop**, a **stop after this label**, or a **lost** wheel.
The run carried on keeps the numbers it was asked for, so it starts at 26 of 40 and not at 1 of 15: on the screen, in the status, and in a **stopped** record if it is stopped again.
The same run sent from the panel is a new run, and starts at its first label.
The machine cannot tell that the tape has run out, so the labels it pressed on no tape before somebody stopped it count as printed, and are not printed again.
It is kept in EEPROM, so it is still there after a reboot.
It is written only when it differs from what is kept: as a run begins, and as it ends for a run that ends short or that was carried on, never at every label.
So printing the same run over and over writes nothing, and a machine that loses power in the middle of a run knows the run, but not how far it got since it began.
The tape left is not looked at, as it is not for a run sent any other way.
A machine that has never started a run has none, and a press then does nothing.
`LastRun`, and `ETKT::printLastRun()`.

**Estimate** - how long a run takes, worked out before it is sent: `POST /api/tag/estimate`, with the body the `tag` would be sent with.
It answers with how long one label takes once the run is under way, `label_ms`, and how long the whole run takes, `run_ms`.
The whole run also counts the press settling, the tune, the home before the first label and the celebration after the last.
It is worked out from the ramps and waits the machine runs rather than timed, so it is answered whatever the machine is doing, even while another run prints.
AccelStepper has no formula for how long a move takes, so `StepperTiming` in `Motion.h` runs the library's own recurrence and adds the steps up.
What it leaves out, such as the time the board spends drawing its screen, is why a run times its own labels once it is under way.
`ETKT::estimate()`.

**Time left** - how long a run that is printing has left, which `/api/status` reports as `remaining_ms`, with how long its labels take as `label_ms`.
Both start from the **estimate**.
From the third label on, the label time is what the run's own labels have taken from the second on, since the first also waits for the tune and the home.
A run asked to stop after its label has only that label left, and one stopping now reports nothing left.
The panel says the estimate under the print button, and the time left while the run prints.

**Stop** - `POST /api/stop`, for when something has gone wrong: it stops what the machine is doing, now.
The motors halt within a step and a tune within a note.
The press is the exception: once it is on its way down it finishes the stroke and comes back up, because a servo stopped partway is a press held against the wheel.
Whatever was being pressed is left on the tape as far as it got, uncut.
A command can be stopped when its row says it is **stoppable**, which is anything but a save, since a save moves nothing and ends in a reboot.
The panel offers it, as the red stop button, whenever a stoppable command runs, and a press of the **button** on the machine asks for it too.
It is not a command: it does not wait its turn, because it is about the command that is running.
A stop with nothing running is not an error, since the job may have just ended.
A stop tapped while its command is still on its way to the machine names that command, as `?for=` and the **command id** it was sent under.
The machine then stops that command if it is the one running, and leaves a command that is somebody else's to run on.
A command that has not arrived is refused when it does, with a 409, because the operator has already said stop, and the stop is answered `not_started`.
`/api/status` reports one that has been asked for and not yet obeyed as `stop`, so a panel opened partway through a stop says so too.
The operator is not the only cause: a job that finds the wheel **lost** stops itself the same way.
The first cause stands, so pressing stop while a lost wheel parks does not hide why the job ended.
`ETKT::stop()`, and `StopSignal`, which carries it from the web server's task to the command loop and keeps its cause.

**Stop after this label** - `POST /api/stop?after=label`, for a run that is going fine and is longer than it needs to be.
The run ends once the label being pressed is finished: topped up and, if the run cuts, cut off.
So no tape is spent on a label nobody finishes.
Only a run of labels has a label to stop after, and anything else is refused with a 409.
Nothing is cut short, so it leaves no stopped record, and the run ends with the usual celebration.
`ETKT::stopAfterLabel()`.

**Stopped** - what the last stop cut short: the command, how many of a run's labels were finished, and whether it left a label **unfinished** on the tape.
It also says what stopped it, the operator or a lost wheel, and carries an id of its own, so two stops that say the same thing are still two stops and a panel can dismiss one without hiding the next.
The ids count up from a random start at every boot, so a stop after a reboot is not taken for one dismissed before it.
An unfinished label is still joined to the roll and would come out on the front of the next one, so the panel offers to cut it off.
`/api/status` reports it as `stopped` until the next command is accepted, so a panel that was not watching when a job was stopped can still say it was.
A stop that arrived as the job was finishing anyway cut nothing short and leaves no record: once the last press of the last cut was on its way down, once the last feed of a run that does not cut had arrived, or during the celebration after either.
The machine still skips what is left of the celebration, because a stop was asked for.
The OLED says stopped where it would say ready.
`StoppedCommand`.

## How the machine is reached

**Link** - how the panel gets to the machine: over a network the machine has joined, or over the machine's **own network**.
The machine does not wait for either.
It starts, takes a job from its **button**, and answers the panel as soon as there is a way in, and the link is kept up beside it from a task of its own.
Joining, it tries the networks it remembers in turn, for ever.
A network that is there and turns the machine away gets `WIFI_TRIES_PER_NETWORK` tries before the next one gets its turn, and one that is not there gets one.
Every try is started from here, because the core's own reconnect gives up on most of the reasons a weak signal produces.
After `WIFI_OWN_AFTER_MS` with no network joined, the machine's own network opens beside the tries, so there is a way in whatever the Wi-Fi is doing.
The tries are then spaced out, and more so with a phone on the own network, because every try takes the radio away from that phone.
A change made on the panel brings them close together again for `WIFI_OWN_AFTER_MS`, because somebody is waiting to see whether it worked, and a network that was lost does not.
Once a network is joined and nobody is on the own network, it closes again, `WIFI_OWN_LINGER_MS` later.
Before there was a link the machine did nothing until it had joined a network, and opened a setup portal when it could not.
`LinkSupervisor`, whose `step()` runs every `WIFI_STEP_MS`, and whose timings are in `Configuration.h`.

**Network mode** - which of the two the machine is set to: join, or own.
In join mode it joins a **remembered network** and falls back on its own.
In own mode it joins nothing and its own network is always open, which is for a room whose Wi-Fi cannot be relied on.
The panel changes it under Setup, and it is kept in EEPROM.
`NetworkMode`, kept by `NetworkSettings`.

**Remembered network** - a network the machine has been given the name and the password of.
It keeps up to `MAX_REMEMBERED`, in the order they are tried, and a network added again takes the place of the one of its name, at the front.
A name is 1 to `Radio::MAX_NAME_BYTES` bytes of text, and a password is nothing, for an open network, or `Radio::MIN_PASSWORD_LENGTH` to `Radio::MAX_PASSWORD_LENGTH` bytes, which is what WPA2 takes.
A network sent again exactly as it is kept is still followed with a try at once, which is the panel asking for one.
The same request come again under its **command id** is not: that is the panel making sure the first one arrived, and a try under way is left alone.
The first start of this firmware takes over the one network the firmware before it kept in the radio itself.
A password goes from here to the radio and nowhere else: none is logged, and no reply of the **Api** carries one.
`NetworkSettings`, in EEPROM.

**Own network** - the Wi-Fi network the machine runs itself, with the whole panel on it at `WIFI_OWN_ADDRESS`.
Its name is `E-TKT-` and the **machine id**, so three machines on one table are three networks.
Its password is ten letters and digits the machine makes once and keeps, with none of the ones that pass for one another on a small screen.
It is shown on the **idle screen** and in the code a phone's camera joins from, and it is not served by the Api: whoever can read the screen is standing at the machine.
A network with no password is never opened, and holding the button through a start makes a new one.
It takes `WIFI_OWN_CLIENTS` phones at most.
While the machine joins nothing it sits on the quietest of channels 1, 6 and 11, and beside a joined network it has to sit on that network's channel, since there is one radio.
There is no captive portal: a phone joins, then opens the address, and both are a code on the screen.

**Router option** - whether the own network tells a phone that it is the way to the internet.
A phone treats it as an ordinary network when it does, and sends everything there, which goes nowhere.
When it does not, a phone can keep its mobile data for everything else while it reaches the panel over the machine's network.
How each phone takes it is for the phone to decide, so it is a setting to try and not a promise.
Changing it closes the own network and opens it again, since a phone reads it as it joins.
`NetworkSettings::routerOffered()`, and `offerRouter` in `Radio::openAccessPoint()`.

**Machine id** - what tells one machine from another: the end of its radio's MAC address, as four characters such as `9C4F`.
The own network is named after it, and so is the name the machine answers to on a network, `e-tkt-9c4f.local`, which was `e-tkt.local` on every machine before.
`Esp32Radio::machineId()`, `NetworkSettings::ownName()` and `hostName()`.

**Networks in reach** - the networks the radio heard the last time the panel asked it to listen, the strongest first and each name once.
The panel's Add network dialog asks for a listen as it opens.
A listen takes the radio off its channel for a few seconds, so it is done only when asked for, and it waits for a try at a network to end.
At most `MAX_NEARBY` are kept, and a network that hides its name, or whose name is not text, is left out.
A name comes off the air from whoever named the network, so the panel sets each one as text and never as markup, and the log says only how many were heard.
`LinkSupervisor::listen()` and `nearby()`.

**Join failure** - why the last try at a network failed, as far as the radio can tell: not found, refused, no address, or something else.
Refused is a network that was there and did not let the machine on, and a wrong password and a signal too weak to finish the handshake look the same from the machine.
No address is a network that took the machine on and then gave it none within `WIFI_DHCP_MS`, as a network with no addresses left to give does.
`/api/network` reports it with the network it was for and the number the radio gave, until the machine is on a network again or the remembered networks change.
`JoinFailure`, kept with that network and that number in a `FailedTry`.

## Where the parts meet

**Driver seam** - `Drivers.h` declares the servo and stepper interfaces the modules take, `Display.h` the screen's, and `Radio.h` the Wi-Fi radio's.
So the job runner, the **link** and every module they drive build against recording fakes on a host, and against ESP32Servo, AccelStepper, the OLED and the chip's radio on the board.
Adapters: `ArduinoDrivers.h`, `OledDisplay` and `Esp32Radio` for the device, `test/fakes` for the tests and the **simulator**.
`FakeRadio` has an air of its own, which a test scripts: networks that are there, that turn the machine away, that give no address, and how many phones are on the machine's network.
So `test/test_link` walks the link through an evening of bad Wi-Fi in milliseconds, and only `Esp32Radio` waits for a real radio.
`test/stubs` stands in for the rest of what the board supplies: the Arduino core, Preferences and the sounder.

**Api** - `Api.cpp`, everything the device answers under `/api/`.
That is the routes, the checks on a request, the words of every refusal, and the JSON of every reply.
It takes a `Request` and gives back a `Reply`, so the host tests check every reply.
The webserver in `Network.cpp` is an adapter in front of it.
It turns each HTTP request under `/api/` into a `Request`, sends the `Reply` back as it comes, and has no rules of its own.
A reply too large for its document is a 500 and not a reply with fields missing, and every 500 also goes in the **log**.
The routes under `/api/network` are how the panel reads and changes the **link**.
`GET /api/network` says how the machine is reached, and `GET /api/network/nearby` the **networks in reach**, after a `POST` to `listen`.
The first also says the numbers the machine goes by there: how long a network's name and password may be, how many networks it remembers, and how long its **own network** takes to open and to close.
The panel checks a network against them and words them, and keeps no copy of its own.
`POST` to `mode`, `remember`, `forget` and `router` changes the **network mode**, a **remembered network** and the **router option**.
Each of those is made once however often it comes under its **command id**, and so is a listen.
A request to remember a network carries its password in, and nothing carries one out.

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
The fake steppers step on AccelStepper's schedule against the stubs' clock, and every millisecond of that clock waits for the wall clock, sped up `--speed` times.
So a label takes as long in the simulator as on the machine, wheel and tape and all, or a tenth of that at `--speed 10`.
A host that cannot keep up runs the machine slower than asked, and never bends its time.
`--lose PERCENT` makes the link a weak one: that many in a hundred requests to the Api get no answer, half of them lost on the way in and half answered with the answer lost on the way back, which is the half that used to run a command twice.
The **link** runs in it as well, against the `FakeRadio`, with an air that `fillAir()` in `main.cpp` fills, where each network is named with what it is there for.
So the panel's Network card is worked against the firmware's own replies.
What it cannot show is what only a radio does: a listen takes no time there, and nobody joins the machine's own network.
It has no **button** either.

## Reading the machine

**Log** - the last 32 lines the device said, kept in memory and served as plain text from `/api/log`, because the machine is on a bench on wifi and a serial cable is not always the answer.

**Panel** - the web UI in `data/`, served from SPIFFS.
It asks the device what it will accept at startup (`/api/capabilities`) instead of deciding for itself.
It polls `/api/status` every second, and every five while the page is hidden, so the tape left and a label sent from another phone show without a reload.
It is built for a **link** that loses what is sent over it: a basement, or a hall with a thousand phones on one Wi-Fi.
A poll that goes unanswered changes nothing on the page, and after three in a row the page says it cannot reach the machine and how long ago it last heard from it, and holds the buttons that need the machine.
A command or a stop goes out under a **command id** and is sent again until it is answered, and a status that names the command ends the wait as well.
After `GIVE_UP_AFTER_MS` the page stops trying and says so, because someone is standing at the machine waiting.
Setup has a Network card, which says how the machine is reached and changes it: the **network mode**, the **remembered networks**, and the **own network** with its **router option**.
A change there that can cost a phone its way to the machine asks first, and the card moves only when the machine says the change is made.
A change made there, and a listen, is sent again under its **command id** until it is answered, since the answer can be lost over the very link the change is about.
Meanwhile the card says so beside what the change was made with, and a change the page gives up on is said to be one that may or may not have been made.
`script.js` reads the page and draws it.
What the page says and decides is worked out in the ES modules beside it, which never touch the page, so `node --test "test/panel/*.test.js"` covers them: `link.js` for the sending, `network.js` for the Network card.
`test/panel/capabilities.json` is the `/api/capabilities` reply those tests run against, with `network.json` and `nearby.json` beside it for the Network card, and the simulator's tests hold all three to the firmware's.
