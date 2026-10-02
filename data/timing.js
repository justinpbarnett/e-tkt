// How long printing takes, as the page says it: before a run of labels is
// sent, from what api/tag/estimate works out for it, and while one prints,
// from what api/status says it has left. Plain data in and out, and nothing
// here touches the page, so node tests every answer in
// test/panel/timing.test.js.

import { printingRun, printsRun } from "./status.js";

// What the page says of a run before it is sent, from the reply to
// api/tag/estimate for it: how long one of its labels takes, and the run
// in all. A single label is only the run, which is the one that counts the
// tune and the home it waits for. Nothing without a reply it can read.
export function estimateText(reply, copies) {
  if (reply === null || !isTime(reply.label_ms) || !isTime(reply.run_ms)) {
    return "";
  }
  if (copies === 1) {
    return "Takes about " + duration(reply.run_ms) + ".";
  }
  return "About " + labelDuration(reply.label_ms) + " a label, " + duration(reply.run_ms) + " in all.";
}

// The estimates this page asks api/tag/estimate for: one for each run the
// form comes to, and the answer that goes with it.
export class Estimates {
  // The run last asked about, as it was posted, so the same run is not
  // asked about twice in a row.
  #asked = null;
  // The request for that run. Only its answer counts: one for a run asked
  // about before it can come back after it, and is for a form that is gone.
  #latest = null;
  // What the device answered, and the run it answered for, as it was
  // posted, or null.
  #answer = null;

  // Whether a run, as the page would post it to api/tag, is the one this
  // page asked about last.
  askedLast(run) {
    return JSON.stringify(run) === this.#asked;
  }

  // Asks about a run, as the page would post it to api/tag. Returns the
  // request to post to api/tag/estimate, or null when it is the run asked
  // about last.
  ask(run) {
    if (this.askedLast(run)) {
      return null;
    }
    this.#asked = JSON.stringify(run);
    this.#latest = { run: run };
    return this.#latest;
  }

  // What the device answered a request with: the body of its reply as
  // JSON, or null when it had none. A refusal is no estimate, and says
  // nothing, as estimateText() says nothing of any reply it cannot read.
  answered(request, reply) {
    if (request !== this.#latest) {
      return;
    }
    this.#answer = { run: JSON.stringify(request.run), copies: request.run.copies, reply: reply };
  }

  // A request that never got an answer. The next ask() about its run asks
  // again.
  unreachable(request) {
    if (request === this.#latest) {
      this.#asked = null;
    }
  }

  // What the page says of the run on the form, as estimateText() says it:
  // nothing while the form has no run on it, or one the device has not
  // answered for. run is as ask() takes it, or null.
  text(run) {
    if (run === null || this.#answer === null || JSON.stringify(run) !== this.#answer.run) {
      return "";
    }
    return estimateText(this.#answer.reply, this.#answer.copies);
  }
}

// What the page says while a run prints, from what the device says of it:
// how long the run has left, and how long its labels are taking. A single
// label, or a run whose label time cannot be read, is only the time it has
// left. running is what the page knows of the command running, as
// activity() in status.js takes it.
//
// Nothing for any other command, once the run is stopping now, or while a
// stop the page has asked for is one the device has not taken yet: until it
// has, its time left is for a run that is not going to happen. Nothing
// either while the page has lost touch with the device, whose last word on
// the time left gets older every second.
export function timeLeftText(running, device) {
  const run = printingRun(running.status, device);
  if (
    run === null ||
    running.offline ||
    running.stop === "now" ||
    running.stop !== (run.stop ?? null) ||
    !isTime(run.remaining_ms)
  ) {
    return "";
  }
  const left = "About " + duration(run.remaining_ms) + " left";
  if (run.copies === 1 || !isTime(run.label_ms)) {
    return left + ".";
  }
  return left + ", at " + labelDuration(run.label_ms) + " a label.";
}

// What the page says of the time under the print button: while a run
// prints, how long it has left, and before then, formEstimate, which is
// what Estimates.text() says of the run on the form. Nothing while the
// machine does anything else. running is what the page knows of the
// command running, as activity() in status.js takes it.
export function timeText(running, device, formEstimate) {
  if (printingRun(running.status, device) !== null) {
    return timeLeftText(running, device);
  }
  return running.command === null || printsRun(running.command, device) ? formEstimate : "";
}

// A time the device worked out, in milliseconds. It says 0 for one it has
// nothing to work out from.
function isTime(ms) {
  return Number.isInteger(ms) && ms > 0;
}

// One label's time, to a tenth of a second, so what the cut takes out of
// it shows. A label of a minute or more is as long as anything else is.
function labelDuration(ms) {
  const tenths = Math.round(ms / 100);
  return tenths < 600 ? (tenths / 10).toFixed(1) + " s" : duration(ms);
}

// A length of time to the second under ten minutes, where a minute either
// way is a good part of it, and to the minute past that.
export function duration(ms) {
  const seconds = Math.round(ms / 1000);
  if (seconds < 60) {
    return seconds + " s";
  }
  if (seconds < 600) {
    const minutes = Math.floor(seconds / 60) + " min";
    return seconds % 60 === 0 ? minutes : minutes + " " + (seconds % 60) + " s";
  }
  const minutes = Math.round(seconds / 60);
  if (minutes < 60) {
    return minutes + " min";
  }
  const hours = Math.floor(minutes / 60) + " h";
  return minutes % 60 === 0 ? hours : hours + " " + (minutes % 60) + " min";
}
