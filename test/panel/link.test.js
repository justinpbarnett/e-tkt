import assert from "node:assert/strict";
import { test } from "node:test";

import {
  GIVE_UP_AFTER_MS,
  Link,
  NEVER_STARTED,
  RETRY_WAIT_MS,
  Sending,
  TAKEN,
  UNKNOWN,
  deliver,
  lastHeardText,
  newCommandId,
} from "../../data/link.js";

const ACCEPTED = { ok: true, status: 200 };
const NO_ANSWER = new Error("no answer");

// A device on a link that loses things, for deliver() to send to. answers is
// what each try gets, in order: an Error for a try that gets no answer, which
// is what the last of them goes on getting. takes is how long a try waits
// before it is known to have had none.
function lossyLink(answers, { takes = 0 } = {}) {
  const link = {
    at: 0,
    tries: 0,
    pauses: [],
    unanswered: 0,
    wanted: true,
    attempt: async () => {
      const answer = answers[Math.min(link.tries, answers.length - 1)];
      link.tries += 1;
      if (answer instanceof Error) {
        link.at += takes;
        throw answer;
      }
      return answer;
    },
    isWanted: () => link.wanted,
    sayUnanswered: () => {
      link.unanswered += 1;
    },
    now: () => link.at,
    pause: async (ms) => {
      link.pauses.push(ms);
      link.at += ms;
    },
  };
  return link;
}

// Sends over the link the way the page sends a stop, or the way it sends the
// command given, which is a Sending.
function deliverOver(link, command = null) {
  return deliver({
    attempt: link.attempt,
    wanted: link.isWanted,
    unanswered: link.sayUnanswered,
    command: command,
    now: link.now,
    pause: link.pause,
  });
}

const WAITING = Symbol("waiting");

// What a delivery has come to once everything already due has run, or WAITING
// while it has come to nothing yet.
function soFar(delivery) {
  return Promise.race([delivery, new Promise((resolve) => setImmediate(() => resolve(WAITING)))]);
}

test("a command the device answers is sent once", async () => {
  // The usual case, and the page says nothing about trying again.
  const link = lossyLink([ACCEPTED]);
  assert.equal(await deliverOver(link), ACCEPTED);
  assert.equal(link.tries, 1);
  assert.equal(link.unanswered, 0);
});

test("a command that gets no answer is sent again until it gets one", async () => {
  // The reply can be lost as easily as the command. Either way the page has
  // heard nothing: it says so, waits a moment, and sends it again.
  const link = lossyLink([NO_ANSWER, NO_ANSWER, ACCEPTED]);
  assert.equal(await deliverOver(link), ACCEPTED);
  assert.equal(link.tries, 3);
  assert.deepEqual(link.pauses, [RETRY_WAIT_MS, RETRY_WAIT_MS]);
  assert.equal(link.unanswered, 2);
});

test("a refusal is an answer, and is not sent again", async () => {
  // The device said no, and would say it again. What it said is for the page
  // to show.
  const refused = { ok: false, status: 409 };
  const link = lossyLink([refused]);
  assert.equal(await deliverOver(link), refused);
  assert.equal(link.tries, 1);
});

test("the page gives up on a device that has not answered for too long", async () => {
  // Someone is standing at the machine waiting. After this long they are
  // told it cannot be reached, with what the last try came to.
  const link = lossyLink([NO_ANSWER]);
  await assert.rejects(deliverOver(link), { message: "no answer" });
  assert.equal(link.at, GIVE_UP_AFTER_MS);
  assert.equal(link.tries, GIVE_UP_AFTER_MS / RETRY_WAIT_MS + 1);
  // A try can take seconds to be known to have had no answer, and none is
  // started once it has been too long.
  const slow = lossyLink([NO_ANSWER], { takes: 8000 });
  await assert.rejects(deliverOver(slow), { message: "no answer" });
  assert.equal(slow.tries, 3);
});

test("something no longer wanted is not sent again", async () => {
  // A stop whose command has finished since, or a save, which restarts the
  // device and is sent the once: another try could only reach whatever the
  // machine does next.
  const link = lossyLink([NO_ANSWER, ACCEPTED]);
  link.wanted = false;
  await assert.rejects(deliverOver(link), { message: "no answer" });
  assert.equal(link.tries, 1);
  assert.equal(link.unanswered, 0);
  // Nor when it stops being wanted during the wait before the next try.
  const waiting = lossyLink([NO_ANSWER, ACCEPTED]);
  waiting.pause = async () => {
    waiting.wanted = false;
  };
  await assert.rejects(deliverOver(waiting), { message: "no answer" });
  assert.equal(waiting.tries, 1);
});

// A try that is never answered, and never known to have had no answer: the
// request is still out there.
function stillOut() {
  return new Promise(() => {});
}

test("a status that names the command ends the wait for its answer", async () => {
  // The answer can be lost, or seconds late, while a poll gets through and
  // names the command by the id it was sent under. That says all the answer
  // would have: the device took it.
  const link = lossyLink([ACCEPTED]);
  link.attempt = () => {
    link.tries += 1;
    return stillOut();
  };
  const command = new Sending();
  const delivery = deliverOver(link, command);
  // A status that names another command, or none, says nothing of this one.
  assert.equal(command.statusArrived({ busy: true, last_command_id: "5d7e21b6843f9a0c" }), false);
  assert.equal(command.statusArrived({ busy: false }), false);
  assert.equal(command.named(), false);
  assert.equal(await soFar(delivery), WAITING);
  assert.equal(command.statusArrived({ busy: true, last_command_id: command.id }), true);
  assert.equal(command.named(), true);
  assert.equal(await delivery, TAKEN);
  assert.equal(link.tries, 1);
  // Nor is the page told there was no answer: it has had one.
  assert.equal(link.unanswered, 0);
});

test("a command the device says it took is not sent again", async () => {
  // The status comes in while the page waits to send the command again. It
  // waits no longer, and no other try follows.
  const link = lossyLink([NO_ANSWER, ACCEPTED]);
  const command = new Sending();
  link.pause = () => {
    command.statusArrived({ busy: true, last_command_id: command.id });
    return stillOut();
  };
  assert.equal(await deliverOver(link, command), TAKEN);
  assert.equal(link.tries, 1);
});

test("a command a stop was sent after is not sent again, and waits for the stop to say what became of it", async () => {
  // The operator has said stop, and another try could only start what they
  // stopped. Whether the try that went out got there is for the stop to find
  // out: it names the command, and the device says what it found.
  const link = lossyLink([NO_ANSWER, ACCEPTED]);
  const command = new Sending();
  assert.equal(command.held(), false);
  command.stopSent();
  assert.equal(command.held(), true);
  const delivery = deliverOver(link, command);
  assert.equal(await soFar(delivery), WAITING);
  assert.equal(link.tries, 1);
  // The page does not say it is trying again, because it is not.
  assert.equal(link.unanswered, 0);
  command.became(NEVER_STARTED);
  assert.equal(await delivery, NEVER_STARTED);
  assert.equal(link.tries, 1);
});

test("a stop sent while the page waits to send the command again is the end of trying", async () => {
  // The try before it may have got there all the same, so this one too waits
  // for the stop to say, and takes what it says: here, that it does not know.
  const link = lossyLink([NO_ANSWER, ACCEPTED]);
  const command = new Sending();
  link.pause = async () => {
    command.stopSent();
  };
  const delivery = deliverOver(link, command);
  assert.equal(await soFar(delivery), WAITING);
  command.became(UNKNOWN);
  assert.equal(await delivery, UNKNOWN);
  assert.equal(link.tries, 1);
});

test("the answer to a try already out still counts once a stop has been sent", async () => {
  // The stop holds back the next try, not the answer to the last. When that
  // comes it says what became of the command as well as any stop could.
  const link = lossyLink([ACCEPTED]);
  let answer = null;
  link.attempt = () =>
    new Promise((resolve) => {
      answer = resolve;
    });
  const command = new Sending();
  const delivery = deliverOver(link, command);
  command.stopSent();
  answer(ACCEPTED);
  assert.equal(await delivery, ACCEPTED);
});

test("what became of a command is said once", async () => {
  // A stop that found the command never started is not gainsaid by anything
  // heard later: the device refuses the command from then on.
  const link = lossyLink([NO_ANSWER]);
  const command = new Sending();
  command.stopSent();
  const delivery = deliverOver(link, command);
  command.became(NEVER_STARTED);
  command.became(TAKEN);
  assert.equal(await delivery, NEVER_STARTED);
});

test("every command goes out under an id of its own, short enough for the device to keep", () => {
  // The device refuses an id over 36 bytes. Sixteen characters from 8 random
  // bytes are as good as unique among the handful it remembers.
  const id = newCommandId();
  assert.match(id, /^[0-9a-f]{16}$/);
  assert.notEqual(newCommandId(), id);
  // A command on its way has one, which every try of it goes under.
  const command = new Sending();
  assert.match(command.id, /^[0-9a-f]{16}$/);
  assert.notEqual(new Sending().id, command.id);
});

test("the page is in touch until more than two polls in a row go unanswered", () => {
  // One is a dropped packet, and two in a row are still a slow network more
  // often than a machine that is off. Saying so takes the buttons away, so
  // the page waits for the third.
  const link = new Link();
  assert.equal(link.lost(), false);
  assert.equal(link.missed(), false);
  assert.equal(link.missed(), false);
  assert.equal(link.lost(), false);
  // The poll the page loses touch at is told apart, to log it the once.
  assert.equal(link.missed(), true);
  assert.equal(link.lost(), true);
  assert.equal(link.missed(), false);
  assert.equal(link.lost(), true);
});

test("one answer puts the page back in touch", () => {
  // And the count starts again: it is polls in a row that lose touch.
  const link = new Link();
  link.missed();
  link.missed();
  link.missed();
  link.heard(9000);
  assert.equal(link.lost(), false);
  link.missed();
  link.missed();
  assert.equal(link.lost(), false);
});

test("an answer to anything the page sent puts it back in touch, not only one to a poll", () => {
  // A command that got through says the machine is there as well as a status
  // does, and so does a refusal: it is the device that gave it. The page used
  // to go by its polls alone, and said it could not reach a machine that had
  // answered a command seconds before, with the time of an older answer.
  const link = new Link();
  link.heard(1000);
  link.missed();
  link.missed();
  link.answered({ ok: true, status: 200 }, 9000);
  assert.equal(link.missed(), false);
  assert.equal(link.missed(), false);
  assert.equal(link.lost(), false);
  assert.equal(link.sinceHeard(18000), 9000);
  link.answered({ ok: false, status: 409 }, 15000);
  assert.equal(link.sinceHeard(18000), 3000);
});

test("an answer that is a failure says nothing for the link", () => {
  // One in the 500s is the device unable to answer, or something standing in
  // for a device that is not there, as the simulator does once its firmware
  // has ended. Neither is the machine heard from, and the polls go on saying
  // whether the page is in touch.
  const link = new Link();
  link.heard(1000);
  link.missed();
  link.missed();
  link.answered({ ok: false, status: 502 }, 9000);
  assert.equal(link.sinceHeard(18000), 17000);
  assert.equal(link.missed(), true);
  assert.equal(link.lost(), true);
});

test("the page says how long ago the device was last heard", () => {
  // What the page shows of the machine is as old as its last answer, which
  // is worth knowing once no more are coming.
  const link = new Link();
  link.heard(1000);
  link.missed();
  assert.equal(link.sinceHeard(18000), 17000);
  assert.equal(lastHeardText(17000), "Last heard 17\u00a0s ago.");
  assert.equal(lastHeardText(125000), "Last heard 2\u00a0min 5\u00a0s ago.");
  assert.equal(lastHeardText(3 * 3600000), "Last heard 3\u00a0h ago.");
  // It goes by the last answer, not the first.
  link.heard(15000);
  assert.equal(link.sinceHeard(18000), 3000);
});

test("a device that has never answered has no time to give", () => {
  // A page opened as the machine went away. It says it cannot reach it, and
  // nothing about when it last did.
  const link = new Link();
  link.missed();
  assert.equal(link.sinceHeard(18000), null);
  assert.equal(lastHeardText(null), "");
});
