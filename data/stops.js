// The stops this page asks for, and what they came to. The page posts them
// and paints what this says; everything it decides on the way is here, where
// node tests it in test/panel/stops.test.js.
//
// A stop is "now" or "after_label". Now is at the next press or turn of a
// motor: a press already on its way down always finishes, so the wheel is
// never left jammed in the tape. After the label is once the label being
// pressed is finished, and cut if the run cuts its labels, so none is left
// half done.

import { stoppedText } from "./status.js";

// What the page says when a stop reaches the device after the command it
// was meant for has finished.
const TOO_LATE_TO_STOP = "Too late to stop: the label maker had already finished.";

export class Stops {
  // The stop this page has asked for: its kind, when it was sent, when the
  // device took it, and whether it was sent blind, for a command the device
  // never said it had.
  #request = null;
  // What the page has to say about a stop when the device has no record of
  // one to say it with: that it came too late to stop anything.
  #note = null;
  // The device's record of the last stop, as stopKey() has it, once it has
  // been dismissed here, so the next poll does not bring it straight back.
  #dismissed = null;

  // Asks for a stop, "now" or "after_label". Whatever the last stop had to
  // say, this one is the end of it.
  ask(kind) {
    this.#request = { kind: kind, sentAt: null, acceptedAt: null, blind: false };
    this.#note = null;
  }

  // The stop asked for, to post to the device at a time from
  // performance.now(), or null when there is none that has not been posted
  // already.
  takeUnsent(at) {
    const request = this.#request;
    if (request === null || request.sentAt !== null) {
      return null;
    }
    request.sentAt = at;
    return request;
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
    if (!response.ok) {
      const reason = reply && typeof reply.error === "string" ? reply.error : null;
      this.#request = null;
      return reason ?? "The label maker would not stop, and did not say why (HTTP " + response.status + ").";
    }
    if (reply !== null && reply.result === "idle") {
      // Not a failure: what it was doing finished while the tap was on its
      // way. Of a stop sent blind there is nothing to say: its command may
      // never have got there.
      this.#request = null;
      if (!request.blind) {
        this.#note = TOO_LATE_TO_STOP;
      }
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
  // meanwhile to stop.
  commandRefused() {
    this.#request = null;
  }

  // The device never answered the command, and the page has given up on it.
  // The device may have taken it all the same, with only its answer lost, so
  // a stop tapped meanwhile is still posted, at a time from
  // performance.now(). Returns that stop, or null when none was tapped.
  commandUnanswered(at) {
    const request = this.takeUnsent(at);
    if (request !== null) {
      request.blind = true;
    }
    return request;
  }

  // A status, asked for at a time from performance.now().
  statusArrived(status, requestedAt) {
    // A stop the device took is over once a status asked for after that no
    // longer says one is coming. One still on its way is over once a status
    // asked for since it was sent says the device is idle: the stop got
    // there and its answer was lost, or the command ended on its own. One to
    // stop now that left no record behind had nothing left to stop, which is
    // worth saying unless it was sent blind.
    const request = this.#request;
    if (request !== null) {
      const taken = request.acceptedAt !== null && requestedAt >= request.acceptedAt;
      const sent = request.sentAt !== null && requestedAt >= request.sentAt;
      if ((taken && !(status.busy && status.stop)) || (sent && !status.busy)) {
        if (request.kind === "now" && !request.blind && !status.busy && lastStop(status) === null) {
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

  // What to say once a stop has ended a command, or null: the device's
  // record, told by what the device says of the command it stopped, or
  // without a record a note of this page's own. The words for a record are
  // stoppedText() in status.js, with the rest of each command's wording.
  notice(status, device) {
    const stopped = lastStop(status);
    if (stopped !== null) {
      if (stopKey(stopped) === this.#dismissed) {
        return null;
      }
      return { text: stoppedText(stopped, device), unfinished: stopped.unfinished === true };
    }
    return this.#note === null ? null : { text: this.#note, unfinished: false };
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
