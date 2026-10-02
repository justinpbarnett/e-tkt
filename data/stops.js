// The stops this page asks for, and what they came to. The page posts them
// and paints what this says; everything it decides on the way is here, where
// node tests it in test/panel/stops.test.js.
//
// A stop is "now" or "after_label". Now is at the next press or turn of a
// motor: a press already on its way down always finishes, so the wheel is
// never left jammed in the tape. After the label is once the label being
// pressed is finished, and cut if the run cuts its labels, so none is left
// half done.
//
// A stop is posted from the tap, whatever the page is waiting for. One tapped
// while its command is still on its way is for that command alone, and names
// it by the id it was sent under: the stop can get to the device first, and
// the command is then not to start when it arrives. What the device answers
// such a stop is also what became of the command, which link.js is told.

import { NEVER_STARTED, TAKEN, UNKNOWN } from "./link.js";
import { stoppedText } from "./status.js";

// What the page says when a stop reaches the device after the command it
// was meant for has finished.
const TOO_LATE_TO_STOP = "Too late to stop: the label maker had already finished.";

// What the page says when a stop reaches the device before the command it
// was meant for, which then never starts.
const STOPPED_IN_TIME = "Stopped in time: the label maker had not started.";

export class Stops {
  // The stop this page has asked for: its kind, the command it is for alone,
  // as a Sending, or null when it is for whatever is running, when it was
  // sent, and when the device took it.
  #request = null;
  // What the page has to say about a stop when the device has no record of
  // it to say it with: that it came too late to stop anything, or in time to
  // keep its command from starting.
  #note = null;
  // The device's record of the last stop, as stopKey() has it, once it has
  // been dismissed here, so the next poll does not bring it straight back.
  #dismissed = null;

  // Asks for a stop, "now" or "after_label", and returns it to post to the
  // device now, which is at a time from performance.now(). Whatever the last
  // stop had to say, this one is the end of it.
  //
  // command is the command still on its way, as a Sending, or null when the
  // device has the command the stop is for. One still on its way is sent no
  // more.
  ask(kind, command, at) {
    this.#request = { kind: kind, command: command, sentAt: at, acceptedAt: null };
    this.#note = null;
    if (command !== null) {
      command.stopSent();
    }
    return this.#request;
  }

  // Whether the page still waits on this stop. Not once it has been
  // overtaken, or the device has nothing left for it to stop: the page then
  // stops trying to get it through.
  wanted(request) {
    return this.#request === request;
  }

  // A stop posted to the device that has had no answer yet, which the page
  // goes on trying to get through. Returns the problem to show meanwhile, or
  // null when the stop is no longer wanted.
  unanswered(request) {
    if (this.#request !== request) {
      return null;
    }
    return request.kind === "now"
      ? "No answer from the label maker yet. Still trying to stop it. If it has to stop now, switch it off."
      : "No answer from the label maker yet. Still trying to stop it after this label.";
  }

  // What the device answered a stop posted to it with, at a time from
  // performance.now(). response is the fetch Response, and reply its body as
  // JSON, or null when it had none. Returns the problem to show, or null.
  answered(request, response, reply, at) {
    if (this.#request !== request) {
      // Overtaken by a command sent since, which is the end of this one, or
      // by another stop.
      return null;
    }
    if (request.command !== null) {
      request.command.became(becameOf(response, reply));
    }
    if (!response.ok) {
      const reason = reply && typeof reply.error === "string" ? reply.error : null;
      this.#request = null;
      return reason ?? "The label maker would not stop, and did not say why (HTTP " + response.status + ").";
    }
    if (reply !== null && reply.result === "not_started") {
      // Not a failure either: the stop got there before its command, which
      // the device now refuses.
      this.#request = null;
      this.#note = STOPPED_IN_TIME;
    } else if (reply !== null && reply.result === "idle") {
      // Not a failure: what it was doing finished while the tap was on its
      // way.
      this.#request = null;
      this.#note = TOO_LATE_TO_STOP;
    } else {
      request.acceptedAt = at;
    }
    return null;
  }

  // A stop posted to the device that never got an answer, and that the page
  // has given up on. Returns the problem to show, or null when the stop has
  // been overtaken since.
  unreachable(request) {
    if (this.#request !== request) {
      return null;
    }
    if (request.command !== null) {
      request.command.became(UNKNOWN);
    }
    this.#request = null;
    return request.kind === "now"
      ? "Couldn’t reach the label maker to stop it. If it has to stop now, switch it off."
      : "Couldn’t reach the label maker to stop it. The rest of the labels will still print.";
  }

  // A command is on its way to the device. Whatever the last stop had to
  // say, a new command is the end of it.
  commandStarting() {
    this.#request = null;
    this.#note = null;
  }

  // The device refused the command, which leaves nothing for a stop tapped
  // meanwhile to stop. reply is the body of the refusal as JSON, or null
  // when it had none.
  //
  // Returns whether the refusal is that stop's doing: it got to the device
  // first, and the device says so to the command. Nothing went wrong then,
  // and the page has nothing to show but what it says of the stop.
  commandRefused(reply) {
    this.#request = null;
    if (reply === null || reply.result !== "not_started") {
      return false;
    }
    this.#note = STOPPED_IN_TIME;
    return true;
  }

  // A status, asked for at a time from performance.now().
  statusArrived(status, requestedAt) {
    // A stop the device took is over once a status asked for after that no
    // longer says one is coming. One still on its way is over once a status
    // asked for since it was sent says the device is idle: the stop got
    // there and its answer was lost, or the command ended on its own. Not
    // one for a command the device has yet to name, though: that command may
    // still be on its way, and the stop has to get there before it. One to
    // stop now that left no record behind had nothing left to stop, which is
    // worth saying of a command the device did have.
    const request = this.#request;
    if (request !== null) {
      const had = request.command === null || status.last_command_id === request.command.id;
      const taken = request.acceptedAt !== null && requestedAt >= request.acceptedAt;
      const sent = requestedAt >= request.sentAt;
      if ((taken && !(status.busy && status.stop)) || (sent && had && !status.busy)) {
        if (request.kind === "now" && had && !status.busy && lastStop(status) === null) {
          this.#note = TOO_LATE_TO_STOP;
        }
        this.#request = null;
      }
    }
    if (status.busy) {
      this.#note = null;
    }
    if (lastStop(status) === null) {
      this.#dismissed = null;
    }
  }

  // The stop the device has been asked for, by this page or any other, or
  // null. A stop now outranks one after the label.
  pending(status) {
    const kinds = [
      this.#request === null ? null : this.#request.kind,
      status !== null && status.busy ? status.stop : null,
    ];
    if (kinds.includes("now")) {
      return "now";
    }
    return kinds.includes("after_label") ? "after_label" : null;
  }

  // What to say once a stop has ended a command, or null: a note of this
  // page's own, or the device's record, told by what the device says of the
  // command it stopped. The words for a record are stoppedText() in
  // status.js, with the rest of each command's wording.
  //
  // The note comes first. It is of a stop that left no record, so a record
  // the device still holds is of an older stop: it keeps one until it takes
  // another command, and a command stopped in time is one it never took.
  notice(status, device) {
    if (this.#note !== null) {
      return { text: this.#note, unfinished: false };
    }
    const stopped = lastStop(status);
    if (stopped === null || stopKey(stopped) === this.#dismissed) {
      return null;
    }
    return { text: stoppedText(stopped, device), unfinished: stopped.unfinished === true };
  }

  // Takes down the notice() the page shows for this status.
  dismiss(status) {
    const stopped = lastStop(status);
    if (stopped !== null) {
      this.#dismissed = stopKey(stopped);
    }
    this.#note = null;
  }
}

// Where a stop is posted, under an id of its own: with what it is to wait
// for, and the command it is for when it is for one alone.
export function stopPath(request, id) {
  const query = new URLSearchParams({ id: id });
  if (request.kind === "after_label") {
    query.set("after", "label");
  }
  if (request.command !== null) {
    query.set("for", request.command.id);
  }
  return "api/stop?" + query;
}

// What the device's answer to a stop says became of the command the stop was
// for. response is the fetch Response, and reply its body as JSON, or null
// when it had none.
function becameOf(response, reply) {
  if (response.ok) {
    // Running, or over, unless it says the command had not started. So is an
    // answer whose body was lost taken, until a status says otherwise.
    return reply !== null && reply.result === "not_started" ? NEVER_STARTED : TAKEN;
  }
  // The device refuses a stop of what it is running, when that cannot be
  // stopped the way it was asked. Any other refusal is not its word on the
  // command.
  return response.status === 409 ? TAKEN : UNKNOWN;
}

// The device's record of what the last stop cut short, or null. It keeps
// one until the next command, and has none when nothing was cut short.
function lastStop(status) {
  const stopped = status === null ? null : status.stopped;
  return stopped !== null && typeof stopped === "object" ? stopped : null;
}

// Every stop has an id of its own, so two that say the same thing are still
// two stops, and dismissing the first does not hide the second. A device on
// firmware older than this page sends none, and its stops are told apart by
// what they say.
function stopKey(stopped) {
  if (Number.isInteger(stopped.id)) {
    return String(stopped.id);
  }
  return [stopped.command, stopped.printed, stopped.copies, stopped.unfinished].join();
}
