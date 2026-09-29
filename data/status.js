// What the device says about itself, and what the page says of it. The
// device says what it will accept, in api/capabilities, and what it is
// doing, in api/status; the page turns that into the words for each
// command, the activity bar and the stops on offer. Plain data in and out,
// and nothing here touches the page, so node tests every answer in
// test/panel/status.test.js.

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
          typeof facts.uses_force === "boolean",
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

// What the panel says while each command runs, keyed by the name the device
// answers to. That name is also the path this panel posts to, api/<name>,
// and the string /api/status reports back while the command runs.
//
// Wording only. Which commands exist is the device's to say, and it says so
// in api/capabilities; this table is checked against that list at startup
// and a disagreement is reported rather than guessed at. The same check runs
// in test/panel/status.test.js, where a disagreement fails.
//
// The wording used to be spread across the panel's senders and the status
// switch in handleData(), and home and move had already fallen out of both:
// a home or a move started from outside the panel left the button showing
// whatever it said last. No button posts either one today. They are here
// because the device can still be running one, and the panel has to be able
// to say so.
//
// A stopLabel is what the stop button says while that command runs, and
// only the commands with one are offered a stop. Which commands the device
// can stop is its own to say, in api/capabilities, but a cut or a feed is
// over before a finger could get there.
const COMMAND_LABELS = {
  cut: { busyLabel: "Cutting…" },
  feed: { busyLabel: "Feeding…" },
  reel: { busyLabel: "Loading the new roll…", stopLabel: "Stop loading" },
  testalign: { busyLabel: "Testing the alignment…", stopLabel: "Stop test" },
  testfull: { busyLabel: "Printing a test label…", stopLabel: "Stop test print" },
  save: { busyLabel: "Saving…" },
  tag: { busyLabel: "Printing…", stopLabel: "Stop printing" },
  home: { busyLabel: "Finding home…" },
  move: { busyLabel: "Moving the wheel…" },
};

// Said while the device runs a command this copy of the panel has never
// heard of, which means a cached copy of the panel is talking to newer
// firmware.
const UNKNOWN_BUSY_LABEL = "Working…";

// Not fatal: an unknown command already falls back to UNKNOWN_BUSY_LABEL and
// the panel keeps working. Worth saying out loud, though, because the usual
// cause is a cached copy of the panel talking to newer firmware, and that is
// invisible from the bench. What to say, or null when the two agree.
export function commandListDisagreement(device) {
  const offered = [...device.commands.keys()];
  const missing = offered.filter((name) => !(name in COMMAND_LABELS));
  const extra = Object.keys(COMMAND_LABELS).filter((name) => !offered.includes(name));
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
export function busyLabel(command) {
  const spec = COMMAND_LABELS[command];
  return spec ? spec.busyLabel : UNKNOWN_BUSY_LABEL;
}

// What the device says is true of a command, as api/capabilities serves it,
// or null before that has come in or for a command it does not list.
function commandFacts(command, device) {
  return device !== null && device.commands.has(command) ? device.commands.get(command) : null;
}

// Whether the command prints a run of labels, which is the only kind that
// counts its labels, and so the only kind a stop can let finish a label.
function printsRun(command, device) {
  const facts = commandFacts(command, device);
  return facts !== null && facts.prints_run;
}

// The status, while it reports a run of labels being printed, or null.
export function printingRun(status, device) {
  return status !== null && status.busy && printsRun(status.command, device) ? status : null;
}

// What the page knows of the command running, as activity() and stopOffer()
// take it:
//
//   command     what is running, or about to
//   status      the last api/status, or null before there is one
//   stop        the stop the device has been asked for, as Stops.pending()
//               has it, or null
//   sentCopies  how many labels this page last asked for
//   offline     whether the page has lost touch with the device

// What the activity bar says while a command runs, and how far through it
// is as a whole percentage, or null while there is no number to show.
export function activity(running, device) {
  const run = printingRun(running.status, device);
  let text = busyLabel(running.command);
  let percentage = null;
  if (printsRun(running.command, device) && run !== null) {
    percentage = printPercentage(run);
    if (Number.isInteger(run.copies) && Number.isInteger(run.copy) && run.copies > 1) {
      // Kept together when a narrow screen puts the words over two lines.
      const count = ["label", run.copy, "of", run.copies].join(" ");
      text = (running.stop === "after_label" ? "Stopping after " : "Printing ") + count;
      // The whole run, not the label it is on: the tape above already shows
      // how far into this label it is, and a bar that emptied at every cut
      // would say nothing about when the run ends.
      percentage = Math.floor(((clamp(run.copy, 1, run.copies) - 1) * 100 + percentage) / run.copies);
    }
  }
  if (running.stop === "now") {
    text = "Stopping…";
  }
  return { text: text, percentage: percentage };
}

// How far through the current label the device is. The device already holds
// the last point back while it finishes feeding and cutting (see Progress.h).
// Subtracting another one here is what made the browser read a point below
// the OLED beside it.
export function printPercentage(status) {
  const percentage = parseInt(status.progress, 10);
  return Number.isNaN(percentage) ? 0 : Math.min(Math.max(percentage, 0), 100);
}

// The stops the page offers while a command runs, or null when it offers
// none: never one the device says it would refuse, and before the device has
// said anything the stop is offered anyway, because it must not wait on a
// fetch.
//
//   now         the red stop: what it says, and whether it is stopping
//   afterLabel  the stop that lets a run of labels finish the label it is
//               on: what it says, whether it is stopping, and whether it
//               can be tapped. Null for anything but a run of more than one
//               label.
export function stopOffer(running, device) {
  const facts = commandFacts(running.command, device);
  const spec = COMMAND_LABELS[running.command];
  if ((facts !== null && !facts.stoppable) || !(spec && spec.stopLabel)) {
    return null;
  }
  // The stop now is never held back for the page being out of touch: the tap
  // may still get through, and if it does not, the page says what else to do.
  const now = running.stop === "now";
  const offer = { now: { text: now ? "Stopping…" : spec.stopLabel, stopping: now }, afterLabel: null };

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

function clamp(value, min, max) {
  return Math.min(Math.max(value, min), max);
}
