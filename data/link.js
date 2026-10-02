// The link to the device, which can be slow or lose what is sent over it: a
// basement, or a hall with a thousand phones on the same Wi-Fi. What the page
// does about that is decided here, where node tests it in
// test/panel/link.test.js. Nothing here touches the page.
//
// A reply can be lost after the device has done what it was asked. So every
// command and every stop goes out under an id, which the device remembers
// with what it answered: one sent again under its id is told again what it
// was told, and nothing runs twice. So does every change to how the device
// is reached, and every listen for the networks in its reach.
//
// The device also names the command it last took, in every status, by the id
// that command was sent under. So a poll that gets through says the command
// arrived when its own answer does not, and the page waits for that answer no
// longer.
//
// A stop can be tapped while its command is still on its way. That stop goes
// at once, names the command by its id, and is the end of sending the
// command: the device stops it if it has it, and refuses it when it arrives
// if it has not. Either way the stop's answer says which.
//
// A poll can be lost too, and one that is says little. The page goes on
// showing what it last heard until several in a row have gone unanswered, and
// then says how old that is. An answer to anything else it sent is the device
// heard from as well, and starts that count again.

import { duration } from "./timing.js";

// Polls in a row that go unanswered before the page says it cannot reach the
// device, and takes away the buttons that need it. One is a dropped packet on
// a busy access point, and two in a row are still a slow network more often
// than a machine that is off.
const OFFLINE_AFTER_MISSES = 3;

// Whether the device is answering the page, and when it last did.
export class Link {
  // Polls that have gone unanswered in a row, with nothing else answered
  // between them.
  #misses = 0;
  // When the device last answered, at a time from performance.now(), or null
  // before it has.
  #heardAt = null;

  // The device answered a poll, at a time from performance.now().
  heard(at) {
    this.#misses = 0;
    this.#heardAt = at;
  }

  // Something else the page sent was answered, at a time from
  // performance.now(). Whatever the answer says, a refusal as much as an
  // acceptance, it is the device that gave it, and the page is in touch as
  // surely as after a poll. Unless it is a failure, in the 500s: that is the
  // device unable to answer, or something standing in for one that is not
  // there, and it says nothing for the link.
  answered(response, at) {
    if (response.status < 500) {
      this.heard(at);
    }
  }

  // A poll went unanswered. Returns whether it is the one the page loses
  // touch at, which is the one worth logging.
  missed() {
    this.#misses += 1;
    return this.#misses === OFFLINE_AFTER_MISSES;
  }

  // Whether the page has lost touch with the device.
  lost() {
    return this.#misses >= OFFLINE_AFTER_MISSES;
  }

  // How long ago the device last answered, at a time from performance.now(),
  // or null when it never has.
  sinceHeard(now) {
    return this.#heardAt === null ? null : now - this.#heardAt;
  }
}

// What the page says of how long ago the device was last heard, as
// Link.sinceHeard() has it, once it has lost touch. Nothing of a device that
// never answered.
export function lastHeardText(sinceMs) {
  return sinceMs === null ? "" : "Last heard " + duration(sinceMs) + " ago.";
}

// How long the page waits, once a try has had no answer, before it sends the
// same thing again.
export const RETRY_WAIT_MS = 1000;

// How long after the first try the page stops starting new ones, and says it
// cannot reach the device. Someone is standing at the machine waiting, and
// past this they are better told than kept.
export const GIVE_UP_AFTER_MS = 20000;

// Random bytes in an id. The device keeps its newest few, and among those
// eight bytes do not come up twice.
const ID_BYTES = 8;

// The id a command or a stop is sent under, and sent again under, and so is
// a change to how the device is reached, and a listen for the networks in
// its reach. The device keeps one of up to 36 bytes.
export function newCommandId() {
  // Not crypto.randomUUID(): the device serves the page over plain HTTP, and
  // a browser offers that only to a page served securely.
  const bytes = crypto.getRandomValues(new Uint8Array(ID_BYTES));
  return Array.from(bytes, (byte) => byte.toString(16).padStart(2, "0")).join("");
}

// What became of a command, when something other than its own answer says:
// the device took it, which a status that names it says, and so does a stop
// sent after it that found it running or over.
export const TAKEN = Symbol("taken");
// A stop sent after it got to the device first. The device has not started
// it, and refuses it from then on.
export const NEVER_STARTED = Symbol("never started");
// A stop was sent after it, and could not be got through, or was refused
// without a word about the command.
export const UNKNOWN = Symbol("unknown");

// A command on its way to the device: the id it goes under, and what became
// of it when something other than its own answer says.
export class Sending {
  // The id every try of it goes under.
  id = newCommandId();
  // Says what became of it, to whoever waits on outcome.
  #say = null;
  // Resolves with what became of it: TAKEN, NEVER_STARTED or UNKNOWN.
  outcome = new Promise((resolve) => {
    this.#say = resolve;
  });
  // Whether a stop has been sent after it.
  #followedByStop = false;
  // Whether a status has named it.
  #named = false;

  // A stop has been sent after it, which names it.
  stopSent() {
    this.#followedByStop = true;
  }

  // Whether a stop has been sent after it. The page then sends it no more,
  // and what became of it is for the stop to find out.
  followedByStop() {
    return this.#followedByStop;
  }

  // What became of it. Said once: whatever is said after that changes
  // nothing.
  became(outcome) {
    this.#say(outcome);
  }

  // A status. Returns whether it names this command as the last the device
  // took, which is the device saying it took it.
  statusArrived(status) {
    if (status.last_command_id !== this.id) {
      return false;
    }
    this.#named = true;
    this.became(TAKEN);
    return true;
  }

  // Whether a status has named it. That status is on the page, with what has
  // become of the command since.
  named() {
    return this.#named;
  }
}

// What the wait before the next try came to, when nothing else did first.
const WAITED = Symbol("waited");

// Sends something to the device until the device answers it, and returns the
// answer, whatever it says: a refusal is an answer too, and is not sent
// again.
//
//   attempt     sends it once, under the same id every time. Resolves with
//               the device's answer, and rejects when none came
//   wanted      whether it is still worth sending. A try that has gone out
//               is not called back, but no other follows it
//   unanswered  told each time a try has had no answer and another is to
//               follow, so the page can say so
//   command     the Sending this is, when it is a command. Left out for
//               anything else, which nothing but its own answer says
//               anything of
//   now         the time, in milliseconds
//   pause       resolves once that many milliseconds have passed
//
// Resolves with what became of a command, with no answer and no other try,
// as soon as something else says: an answer could say no more than that.
//
// A command a stop has been sent after is not sent again. The operator has
// said stop, and another try could only start what they stopped. Whether a
// try that went out got there is then for the stop to find out, and this
// waits to be told.
//
// Rejects with what the last try came to once it is no longer wanted, or the
// device has not answered for GIVE_UP_AFTER_MS.
export async function deliver({ attempt, wanted, unanswered, command = null, now, pause }) {
  const startedAt = now();
  // One of its own for every delivery of anything else, and never settled:
  // nothing then holds on to the deliveries that are over.
  const outcome = command === null ? new Promise(() => {}) : command.outcome;
  const followedByStop = () => command !== null && command.followedByStop();
  for (;;) {
    try {
      return await Promise.race([outcome, attempt()]);
    } catch (error) {
      if (followedByStop()) {
        return await outcome;
      }
      if (!wanted() || now() - startedAt >= GIVE_UP_AFTER_MS) {
        throw error;
      }
      unanswered();
      const first = await Promise.race([outcome, pause(RETRY_WAIT_MS).then(() => WAITED)]);
      if (first !== WAITED) {
        return first;
      }
      if (followedByStop()) {
        return await outcome;
      }
      if (!wanted()) {
        throw error;
      }
    }
  }
}
