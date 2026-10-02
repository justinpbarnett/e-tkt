// What the device says about itself, and what the page says of it. The
// device says what it will accept, in api/capabilities, and what it is
// doing, in api/status; the page turns that into the words for each
// command, the activity bar and the stops on offer. Plain data in and out,
// and nothing here touches the page, so node tests every answer in
// test/panel/status.test.js.

import { clamp } from "./tape.js";

// Raised when api/capabilities answers but leaves out something this page
// needs, which means the device is running older firmware than this page.
// Named, so the console says which of the two went wrong.
export class CapabilitiesMismatch extends Error {
  name = "CapabilitiesMismatch";
}

// Everything api/capabilities has to say, checked before any of it is used,
// or a CapabilitiesMismatch naming what is missing.
export function readCapabilities(response) {
  const need = (ok, what) => {
    if (!ok) {
      throw new CapabilitiesMismatch("api/capabilities served no " + what);
    }
  };
  need(typeof response.printable === "string" && response.printable.length > 0, "printable set");
  const calibration = response.calibration;
  need(calibration && Number.isInteger(calibration.min) && Number.isInteger(calibration.max), "calibration range");
  const label = response.label;
  need(label && Number.isInteger(label.minimum), "minimum label length");
  need(Number.isInteger(label.maximum), "maximum label length");
  const copies = response.copies;
  need(copies && Number.isInteger(copies.minimum) && Number.isInteger(copies.maximum), "copies range");
  const roll = response.roll;
  need(
    roll && Number.isInteger(roll.minimum_mm) && Number.isInteger(roll.maximum_mm) && Number.isInteger(roll.default_mm),
    "roll lengths",
  );
  const feed = response.feed;
  need(feed && Number.isInteger(feed.length_um) && feed.length_um > 0 && Number.isInteger(feed.lead), "feed length");
  // Not a number the page counts with. That the device keeps any is what lets
  // the page send a command again without it running twice: see link.js.
  need(Number.isInteger(response.remembered_ids) && response.remembered_ids > 0, "count of remembered ids");
  const commands = response.commands;
  need(
    commands !== null &&
      typeof commands === "object" &&
      !Array.isArray(commands) &&
      Object.values(commands).every(
        (facts) =>
          facts !== null &&
          typeof facts.stoppable === "boolean" &&
          typeof facts.prints_run === "boolean" &&
          typeof facts.uses_align === "boolean" &&
          typeof facts.uses_force === "boolean" &&
          typeof facts.presses_label === "boolean",
      ),
    "command facts",
  );

  return {
    printable: response.printable,
    aliases: response.aliases || {},
    calibration: calibration,
    label: label,
    copies: copies,
    roll: roll,
    feed: feed,
    commands: new Map(Object.entries(commands)),
  };
}

// What the page says until the device has said what it accepts. The page
// asks as soon as it opens, and goes on asking, and on a slow link the answer
// can be several tries away. Until it is in there is nothing to check a label
// or a setting against, so whatever needs it cannot be pressed, and this is
// the page saying why.
export const WAITING_TO_HEAR = "Waiting to hear from the label maker…";

// What the page says of something it sent that has had no answer, while it
// sends it again: a command, a change to how the label maker is reached, or
// a listen for the networks in its reach.
export const NO_ANSWER_YET = "No answer yet. Trying again…";

// What the panel says of each command, keyed by the name the device answers
// to. That name is also the path this panel posts to, api/<name>, and the
// string /api/status reports back while the command runs.
//
// Wording only. Which commands exist, and what each one does, is the
// device's to say, in api/capabilities: whether it can be stopped, whether
// it prints a run of labels, whether it presses a label into the tape. This
// table is checked against the device's list at startup and a disagreement
// is reported rather than guessed at. The same check runs in
// test/panel/status.test.js, where a disagreement fails.
//
// The wording used to be spread across the panel's senders and the status
// switch in handleData(), and home and move had already fallen out of both:
// a home or a move started from outside the panel left the button showing
// whatever it said last. No button posts either one today. They are here
// because the device can still be running one, and the panel has to be able
// to say so.
//
//   busy     what the page says while the command runs
//   stop     what the red stop says while it runs, for a command the
//            device can stop
//   pressed  what the label is called, for a command that presses one
//   stopped  what the page says once a stop has ended the command, for one
//            that presses no label
const COMMAND_WORDING = {
  cut: {
    busy: "Cutting…",
    stop: "Stop cutting",
    stopped: "Stopped the cut before it was through. Cut again to finish.",
  },
  feed: { busy: "Feeding…", stop: "Stop feeding", stopped: "Stopped the feed." },
  reel: {
    busy: "Loading the new roll…",
    stop: "Stop loading",
    stopped: "Stopped loading the new roll before the tape was all the way through. Load it again to finish.",
  },
  unload: {
    busy: "Unloading the roll…",
    stop: "Stop unloading",
    stopped: "Stopped unloading the roll. If the tape is still in the feed cog, unload it again.",
  },
  testalign: { busy: "Testing the alignment…", stop: "Stop test", stopped: "Stopped the alignment test." },
  testfull: { busy: "Printing a test label…", stop: "Stop test print", pressed: "test label" },
  save: { busy: "Saving…" },
  tag: { busy: "Printing…", stop: "Stop printing", pressed: "label" },
  home: { busy: "Finding home…", stop: "Stop the wheel", stopped: "Stopped the wheel." },
  move: { busy: "Moving the wheel…", stop: "Stop the wheel", stopped: "Stopped the wheel." },
};

// Said of a command this copy of the panel has never heard of, which means a
// cached copy of the panel is talking to newer firmware.
const UNKNOWN_COMMAND = { busy: "Working…", stop: "Stop", pressed: "label", stopped: "The label maker was stopped." };

// The wording for a command, the words for one it has none of filled in.
function wordingFor(command) {
  return { ...UNKNOWN_COMMAND, ...COMMAND_WORDING[command] };
}

// Not fatal: an unknown command already falls back to UNKNOWN_COMMAND and
// the panel keeps working. Worth saying out loud, though, because the usual
// cause is a cached copy of the panel talking to newer firmware, and that is
// invisible from the bench. What to say, or null when the two agree.
export function commandListDisagreement(device) {
  const offered = [...device.commands.keys()];
  const missing = offered.filter((name) => !(name in COMMAND_WORDING));
  const extra = Object.keys(COMMAND_WORDING).filter((name) => !offered.includes(name));
  if (missing.length === 0 && extra.length === 0) {
    return null;
  }
  return (
    "This panel and the firmware disagree about the command list." +
    (missing.length ? " No wording here for: " + missing.join(", ") + "." : "") +
    (extra.length ? " Device does not offer: " + extra.join(", ") + "." : "")
  );
}

// What the page says while the command runs.
export function busyText(command) {
  return wordingFor(command).busy;
}

// What the device says is true of a command, as api/capabilities serves it,
// or null before that has come in or for a command it does not list.
function commandFacts(command, device) {
  return device !== null && device.commands.has(command) ? device.commands.get(command) : null;
}

// Whether the command prints a run of labels, which is the only kind that
// counts its labels, and so the only kind a stop can let finish a label.
export function printsRun(command, device) {
  const facts = commandFacts(command, device);
  return facts !== null && facts.prints_run;
}

// Whether the command presses a label into the tape, which a stop can leave
// there half pressed.
function pressesLabel(command, device) {
  const facts = commandFacts(command, device);
  return facts !== null && facts.presses_label;
}

// The status, while it reports a run of labels being printed, or null.
export function printingRun(status, device) {
  return status !== null && status.busy && printsRun(status.command, device) ? status : null;
}

// What the page knows of the command running, as activity(), stopOffer() and
// setupText() take it:
//
//   command     what is running, or about to. Null when nothing is, which
//               only setupText() is asked about
//   status      the last api/status, or null before there is one
//   stop        the stop the device has been asked for, as Stops.pending()
//               has it, or null
//   sentCopies  how many labels this page last asked for
//   offline     whether the page has lost touch with the device
//   unanswered  whether the command is still on its way and has had no
//               answer, so the page is sending it again

// What the activity bar says while a command runs, and how far through it
// is as a whole percentage, or null while there is no number to show.
export function activity(running, device) {
  const run = printingRun(running.status, device);
  let text = busyText(running.command);
  let percentage = null;
  if (printsRun(running.command, device) && run !== null) {
    percentage = printPercentage(run);
    if (Number.isInteger(run.copies) && Number.isInteger(run.copy) && run.copies > 1) {
      // Kept together when a narrow screen puts the words over two lines.
      const count = ["label", run.copy, "of", run.copies].join(" ");
      text = (running.stop === "after_label" ? "Stopping after " : "Printing ") + count;
      // The whole run, not the label it is on: the tape above already shows
      // how far into this label it is, and a bar that emptied at every label
      // would say nothing about when the run ends.
      percentage = Math.floor(((clamp(run.copy, 1, run.copies) - 1) * 100 + percentage) / run.copies);
    }
  }
  // The device has not said it has the command, so the page does not say it
  // is doing it.
  if (running.unanswered) {
    text = NO_ANSWER_YET;
  }
  if (running.stop === "now") {
    text = "Stopping…";
  }
  return { text: text, percentage: percentage };
}

// How far through the current label the device is. The device already holds
// the last point back while it finishes the label off (see Progress.h).
// Subtracting another one here is what made the browser read a point below
// the OLED beside it.
export function printPercentage(status) {
  const percentage = parseInt(status.progress, 10);
  return Number.isNaN(percentage) ? 0 : clamp(percentage, 0, 100);
}

// The stops the page offers while a command runs, or null when it offers
// none: one for everything the device says it can stop, and never one it
// says it would refuse. Before the device has said anything the stop is
// offered anyway, because it must not wait on a fetch.
//
//   now         the red stop: what it says, and whether it is stopping
//   afterLabel  the stop that lets a run of labels finish the label it is
//               on: what it says, whether it is stopping, and whether it
//               can be tapped. Null for anything but a run of more than one
//               label.
export function stopOffer(running, device) {
  const facts = commandFacts(running.command, device);
  if (facts !== null && !facts.stoppable) {
    return null;
  }
  // The stop now is never held back for the page being out of touch: the tap
  // may still get through, and if it does not, the page says what else to do.
  const now = running.stop === "now";
  const offer = {
    now: { text: now ? "Stopping…" : wordingFor(running.command).stop, stopping: now },
    afterLabel: null,
  };

  // A run of labels can instead be let finish the label it is on. How many
  // labels it has comes from the device once a poll has it, and until then
  // from what this page asked for, so the stops come up the right size.
  const run = printingRun(running.status, device);
  const copies = run !== null ? run.copies : running.sentCopies;
  if (printsRun(running.command, device) && Number.isInteger(copies) && copies > 1) {
    const gentle = running.stop === "after_label";
    offer.afterLabel = {
      text: gentle ? "Stopping after this label…" : "Stop after this label",
      stopping: gentle,
      disabled: running.stop !== null || running.offline || (run !== null && run.copy >= run.copies),
    };
  }
  return offer;
}

// What the setup view says under its cards, or nothing. A command with no
// stop to name it is named here, which leaves a save: the device can stop
// everything else, and setup's own tests, the unload and the new roll say so
// on their buttons as well. With nothing running, and until the device has
// said what it accepts, the view says the page is waiting to hear: a new
// roll, the tests and the steps cannot be pressed until then.
export function setupText(running, device) {
  if (running.command !== null) {
    return stopOffer(running, device) === null ? busyText(running.command) : "";
  }
  return device === null ? WAITING_TO_HEAR : "";
}

// What a stop cut short, from the device's record of it, and why when it
// was not the stop button: the operator who pressed that knows why.
export function stoppedText(stopped, device) {
  const lost =
    stopped.cause === "lost_wheel"
      ? " The daisy wheel could not find its home. Check the magnet on the wheel and the hall sensor."
      : "";
  return stoppedWhat(stopped, device) + lost;
}

// Tape fed for a label that was then not finished is still in the machine,
// and comes out on the front of the next label unless it is cut off first.
function stoppedWhat(stopped, device) {
  const words = wordingFor(stopped.command);
  const unfinished = stopped.unfinished === true;
  const cutFirst = " Cut it off before printing again.";
  const { printed, copies } = stopped;
  if (printsRun(stopped.command, device) && Number.isInteger(printed) && Number.isInteger(copies) && copies > 1) {
    if (unfinished) {
      return "Stopped partway through label " + (printed + 1) + " of " + copies + "." + cutFirst;
    }
    if (printed === 0) {
      return "Stopped before label 1 of " + copies + " was started.";
    }
    return "Stopped after " + printed + " of " + copies + " labels.";
  }
  // A single label, a record without the count, and anything else that
  // presses one. The record says whether a label was left, whatever the
  // command.
  if (unfinished) {
    return "Stopped partway through the " + words.pressed + "." + cutFirst;
  }
  if (pressesLabel(stopped.command, device)) {
    return "Stopped before the " + words.pressed + " was started.";
  }
  return words.stopped;
}
