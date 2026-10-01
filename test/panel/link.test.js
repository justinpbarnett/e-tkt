import assert from "node:assert/strict";
import { test } from "node:test";

import { GIVE_UP_AFTER_MS, RETRY_WAIT_MS, deliver, newCommandId } from "../../data/link.js";

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

function deliverOver(link) {
  return deliver({
    attempt: link.attempt,
    wanted: link.isWanted,
    unanswered: link.sayUnanswered,
    now: link.now,
    pause: link.pause,
  });
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
  // A stop tapped while its command was still on its way, or a stop whose
  // command has finished since: another try could only reach whatever the
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

test("every command goes out under an id of its own, short enough for the device to keep", () => {
  // The device refuses an id over 36 bytes. Sixteen characters from 8 random
  // bytes are as good as unique among the handful it remembers.
  const id = newCommandId();
  assert.match(id, /^[0-9a-f]{16}$/);
  assert.notEqual(newCommandId(), id);
});
