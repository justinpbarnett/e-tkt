import assert from "node:assert/strict";
import { test } from "node:test";

import { Estimates, estimateText, timeLeftText, timeText } from "../../data/timing.js";
import { labelMaker, running } from "./device.js";

test("a run of labels is estimated label by label and in all before it is sent", () => {
  // What api/tag/estimate answered for 100 of " HELLO ", cut: a tenth of a
  // second on the label, so leaving the cut out shows what it saves.
  assert.equal(
    estimateText({ label_ms: 14_900, run_ms: 1_495_000 }, 100),
    "About 14.9\u00a0s a label, 25\u00a0min in all.",
  );
});

test("a single label is estimated as the time it takes, tune and home and all", () => {
  // Its label time and its run are the same label, and the run is the one
  // that counts the tune and the home the label waits for.
  assert.equal(estimateText({ label_ms: 14_900, run_ms: 22_400 }, 1), "Takes about 22\u00a0s.");
});

test("a run under ten minutes is estimated to the second", () => {
  // At that length a minute either way is a good part of it.
  assert.equal(
    estimateText({ label_ms: 14_900, run_ms: 82_000 }, 5),
    "About 14.9\u00a0s a label, 1\u00a0min 22\u00a0s in all.",
  );
  assert.equal(
    estimateText({ label_ms: 14_900, run_ms: 240_300 }, 16),
    "About 14.9\u00a0s a label, 4\u00a0min in all.",
  );
  assert.equal(
    estimateText({ label_ms: 14_900, run_ms: 599_400 }, 40),
    "About 14.9\u00a0s a label, 9\u00a0min 59\u00a0s in all.",
  );
});

test("a run of an hour or more is estimated in hours and minutes", () => {
  // The most one run prints is 500 labels, a couple of hours of them.
  assert.equal(
    estimateText({ label_ms: 14_900, run_ms: 7_455_000 }, 500),
    "About 14.9\u00a0s a label, 2\u00a0h 4\u00a0min in all.",
  );
  assert.equal(
    estimateText({ label_ms: 14_400, run_ms: 7_190_000 }, 500),
    "About 14.4\u00a0s a label, 2\u00a0h in all.",
  );
  assert.equal(
    estimateText({ label_ms: 14_900, run_ms: 3_570_000 }, 239),
    "About 14.9\u00a0s a label, 1\u00a0h in all.",
  );
});

test("a label of a minute or more is estimated as long as any other length of time", () => {
  // The longest label the device takes is 249 characters, which is minutes
  // of pressing, and tenths of a second are nothing at that length.
  assert.equal(
    estimateText({ label_ms: 83_040, run_ms: 420_000 }, 5),
    "About 1\u00a0min 23\u00a0s a label, 7\u00a0min in all.",
  );
  assert.equal(
    estimateText({ label_ms: 59_960, run_ms: 310_000 }, 5),
    "About 1\u00a0min a label, 5\u00a0min 10\u00a0s in all.",
  );
});

test("without an estimate the page can read, it says nothing of the time", () => {
  // None yet, a failed request, or a reply from firmware that says
  // something else: nothing is better than a time made up.
  assert.equal(estimateText(null, 3), "");
  assert.equal(estimateText({}, 3), "");
  assert.equal(estimateText({ label_ms: "14900", run_ms: 44_700 }, 3), "");
  assert.equal(estimateText({ label_ms: 14_900, run_ms: 0 }, 1), "");
  assert.equal(estimateText({ label_ms: 0, run_ms: 44_700 }, 3), "");
});

// A run of " HELLO " the device reports on, as the page hands it to
// timeLeftText(): label 3 of 100, and what the device says it has left.
function printing(status = {}, stop = null) {
  return running({
    command: "tag",
    status: {
      busy: true,
      command: "tag",
      copy: 3,
      copies: 100,
      progress: 40,
      label_ms: 14_830,
      remaining_ms: 1_442_000,
      ...status,
    },
    stop: stop,
    sentCopies: 100,
  });
}

test("a run printing says how long it has left, and how long its labels are taking", () => {
  // The label time is the one the device has timed its labels at, which
  // is what the time left is worked out from.
  assert.equal(timeLeftText(printing(), labelMaker()), "About 24\u00a0min left, at 14.8\u00a0s a label.");
});

test("a single label says only how long it has left", () => {
  // Its label time is the whole of it.
  const single = printing({ copy: 1, copies: 1, remaining_ms: 12_300 });
  assert.equal(timeLeftText(single, labelMaker()), "About 12\u00a0s left.");
});

test("a run with nothing left to count says nothing of the time", () => {
  // The device says 0 once it has been told to stop now, and the bar
  // already says it is stopping.
  assert.equal(timeLeftText(printing({ remaining_ms: 0 }), labelMaker()), "");
  assert.equal(timeLeftText(printing({ remaining_ms: 0, copies: 1, copy: 1 }), labelMaker()), "");
});

test("a run the page has asked to stop now says nothing of the time", () => {
  // The device has not said so yet, but the run is not going to take the
  // time it had left.
  assert.equal(timeLeftText(printing({}, "now"), labelMaker()), "");
});

test("a run the page has asked to stop after the label says nothing of the time until the device has the stop", () => {
  // Until then the time the device has left is the whole run.
  assert.equal(timeLeftText(printing({}, "after_label"), labelMaker()), "");
  // Once it has, the time left is to the end of the label it is on.
  const stopping = printing({ stop: "after_label", remaining_ms: 9_200 }, "after_label");
  assert.equal(timeLeftText(stopping, labelMaker()), "About 9\u00a0s left, at 14.8\u00a0s a label.");
});

test("a command that is not a run of labels says nothing of the time", () => {
  // Only a run is timed, and before the first status there is nothing to go
  // on.
  const feeding = { ...printing(), command: "feed", status: { busy: true, command: "feed", progress: 50 } };
  assert.equal(timeLeftText(feeding, labelMaker()), "");
  assert.equal(timeLeftText({ ...printing(), status: null }, labelMaker()), "");
});

test("a run the page has lost touch with says nothing of the time", () => {
  // What the device last said it had left goes further out of date every
  // second, with no telling by how much.
  assert.equal(timeLeftText({ ...printing(), offline: true }, labelMaker()), "");
});

test("a run whose label time cannot be read says only how long it has left", () => {
  assert.equal(timeLeftText(printing({ label_ms: 0 }), labelMaker()), "About 24\u00a0min left.");
  assert.equal(timeLeftText(printing({ label_ms: undefined }), labelMaker()), "About 24\u00a0min left.");
});

// A run of " HELLO " as the page posts it, to api/tag and to api/tag/estimate.
function hello(copies = 100, cut = true) {
  return { tag: " hello ", copies: copies, cut: cut };
}

test("the page asks about each run once", () => {
  const estimates = new Estimates();
  const request = estimates.ask(hello());
  assert.deepEqual(request.run, hello());
  // The form has not changed since.
  assert.equal(estimates.ask(hello()), null);
  // It has: the cut is off now.
  assert.deepEqual(estimates.ask(hello(100, false)).run, hello(100, false));
});

// What api/tag/estimate answers for hello().
const HELLO_ESTIMATE = { label_ms: 14_900, run_ms: 1_495_000 };

test("the answer the device gives for a run is what the page says of it", () => {
  const estimates = new Estimates();
  assert.equal(estimates.text(hello()), "");
  estimates.answered(estimates.ask(hello()), HELLO_ESTIMATE);
  assert.equal(estimates.text(hello()), "About 14.9\u00a0s a label, 25\u00a0min in all.");
  // As estimateText() says it, for the copies the answer was for.
  const single = hello(1);
  estimates.answered(estimates.ask(single), { label_ms: 14_900, run_ms: 22_400 });
  assert.equal(estimates.text(single), "Takes about 22\u00a0s.");
});

test("a form with no run on it says nothing of the time", () => {
  // Cleared, say, or a count typed that the device would not take.
  const estimates = new Estimates();
  estimates.answered(estimates.ask(hello()), HELLO_ESTIMATE);
  assert.equal(estimates.text(null), "");
});

test("only the answer for the run last asked about counts", () => {
  // The count went from 100 to 200 while the first answer was on its way,
  // and the answers came back the other way round.
  const estimates = new Estimates();
  const first = estimates.ask(hello(100));
  const second = estimates.ask(hello(200));
  estimates.answered(second, { label_ms: 14_900, run_ms: 2_985_000 });
  estimates.answered(first, HELLO_ESTIMATE);
  assert.equal(estimates.text(hello(200)), "About 14.9\u00a0s a label, 50\u00a0min in all.");
});

test("a run the device has not answered for says nothing of the time, whatever it said of the one before", () => {
  // The count went from 100 to 200. What the device said of 100 would be a
  // time made up for 200, and a page that has lost touch never asks.
  const estimates = new Estimates();
  estimates.answered(estimates.ask(hello(100)), HELLO_ESTIMATE);
  assert.equal(estimates.text(hello(200)), "");
  // Asked about, and not answered yet.
  estimates.ask(hello(200));
  assert.equal(estimates.text(hello(200)), "");
  // Back on 100, and what the device said of it still holds.
  assert.equal(estimates.text(hello(100)), "About 14.9\u00a0s a label, 25\u00a0min in all.");
});

test("a run the device will not estimate says nothing of the time, and is not asked about again", () => {
  // Firmware from before there was an estimate to ask for, say. Asking it
  // again would get the same answer.
  const estimates = new Estimates();
  estimates.answered(estimates.ask(hello(100)), HELLO_ESTIMATE);
  const refused = estimates.ask(hello(200));
  estimates.answered(refused, { error: "Not found" });
  assert.equal(estimates.text(hello(200)), "");
  assert.equal(estimates.ask(hello(200)), null);
});

test("a run whose estimate never came back says nothing of the time, and is asked about again", () => {
  const estimates = new Estimates();
  estimates.answered(estimates.ask(hello(100)), HELLO_ESTIMATE);
  estimates.unreachable(estimates.ask(hello(200)));
  assert.equal(estimates.text(hello(200)), "");
  assert.deepEqual(estimates.ask(hello(200)).run, hello(200));
});

test("the page can tell the run it asked about last from any other", () => {
  // So it waits for the form to settle on a run before it asks, and waits
  // for nothing while the form is still on the run it asked about.
  const estimates = new Estimates();
  assert.equal(estimates.askedLast(hello()), false);
  const request = estimates.ask(hello());
  assert.equal(estimates.askedLast(hello()), true);
  assert.equal(estimates.askedLast(hello(200)), false);
  estimates.unreachable(request);
  assert.equal(estimates.askedLast(hello()), false);
});

// What the page says under the print button, from what the page knows of
// the command running and what it would say of the run on the form.
const ON_THE_FORM = "About 14.9\u00a0s a label, 25\u00a0min in all.";

test("while a run prints, the page says how long it has left", () => {
  assert.equal(timeText(printing(), labelMaker(), ON_THE_FORM), "About 24\u00a0min left, at 14.8\u00a0s a label.");
});

test("before a run prints, the page says how long the run on the form would take", () => {
  const idle = running({ command: null, status: { busy: false } });
  assert.equal(timeText(idle, labelMaker(), ON_THE_FORM), ON_THE_FORM);
  // Sent, and not yet under way: it is still the run that is about to print.
  assert.equal(timeText({ ...idle, command: "tag" }, labelMaker(), ON_THE_FORM), ON_THE_FORM);
});

test("while the machine does anything else, the page says nothing of the time", () => {
  // Under a bar that says it is feeding, the time the run on the form
  // would take reads as how long the feed takes.
  const feeding = running({ command: "feed", status: { busy: true, command: "feed" } });
  assert.equal(timeText(feeding, labelMaker(), ON_THE_FORM), "");
  // Sent, and not yet under way.
  assert.equal(timeText({ ...feeding, status: { busy: false } }, labelMaker(), ON_THE_FORM), "");
});
