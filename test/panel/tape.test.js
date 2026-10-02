import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { test } from "node:test";

import {
  formatLength,
  labelLengthMm,
  labelsThatFit,
  offeredRollLength,
  rollGauge,
  rollRange,
  steppedRollLength,
  tapeLeftMm,
  typedRoll,
} from "../../data/tape.js";
import { labelMaker } from "./device.js";

// The tape cases, worked by hand. test/test_tape/test_tape.cpp holds the
// firmware's own sums in src/Tape.h to the same file, so the page and the
// device cannot come to disagree about how much tape a label takes.
const vectors = JSON.parse(readFileSync(new URL("../vectors/tape.json", import.meta.url), "utf8"));

// The cases under one key. Never empty: a list that is missing would
// otherwise pass, having no case in it to fail.
function cases(key) {
  const list = vectors[key];
  assert.ok(Array.isArray(list) && list.length > 0, key);
  return list;
}

test("the shared tape cases are worked for this label maker", () => {
  // As api/capabilities serves it. A change to one of these has to be worked
  // through the cases by hand.
  const device = labelMaker();
  assert.deepEqual(
    {
      label: { minimum: device.label.minimum, maximum: device.label.maximum },
      feed: { lead: device.feed.lead, length_um: device.feed.length_um },
    },
    vectors.device,
  );
});

test("a label is as long on the tape as the feeds the shared cases say it takes", async (t) => {
  // What the line under the tape says the label will come out at.
  const device = labelMaker();
  for (const label of cases("labels")) {
    await t.test(label.case, () => assert.equal(labelLengthMm(label.characters, device), label.mm));
  }
});

test("the tape left fits as many labels as the shared cases say", async (t) => {
  const device = labelMaker();
  for (const fit of cases("fit")) {
    await t.test(fit.case, () => assert.equal(labelsThatFit(fit.left_mm, fit.characters, device), fit.labels));
  }
});

test("a length of tape reads to the millimetre under a metre, and to the centimetre from there, rounded down", () => {
  // A label is a few centimetres, so a length said in whole centimetres
  // could be a good share of one out: two labels of 28 mm read as 5 cm.
  // Rounded down so what is left is never said to be more than the device's
  // own estimate of it.
  assert.equal(formatLength(9.9), "9\u00a0mm");
  assert.equal(formatLength(-4), "0\u00a0mm");
  assert.equal(formatLength(56), "56\u00a0mm");
  assert.equal(formatLength(999.9), "999\u00a0mm");
  assert.equal(formatLength(1000), "1\u00a0m");
  assert.equal(formatLength(1009), "1\u00a0m");
  assert.equal(formatLength(1010), "1.01\u00a0m");
  assert.equal(formatLength(1500), "1.5\u00a0m");
  assert.equal(formatLength(2824), "2.82\u00a0m");
  assert.equal(formatLength(10000), "10\u00a0m");
  // 350 labels of 6 feeds of 3.9 mm are 8190 mm, which the sum lands a hair
  // under in floating point.
  assert.equal(formatLength(350 * ((6 * 3900) / 1000)), "8.19\u00a0m");
});

test("the tape left is the last status's estimate, and unknown without one", () => {
  // Unknown is not empty: before the first poll, or from firmware that does
  // not count the roll, the page must not say the roll has run out.
  assert.equal(tapeLeftMm({ busy: false, roll: { length_mm: 3000, remaining_mm: 2968 } }), 2968);
  assert.equal(tapeLeftMm({ busy: false, roll: { length_mm: 3000, remaining_mm: 0 } }), 0);
  assert.equal(tapeLeftMm({ busy: false }), null);
  assert.equal(tapeLeftMm(null), null);
});

test("a roll length is typed in metres and loaded in whole millimetres", () => {
  // People measure a roll in metres, and the device counts it in
  // millimetres. Nothing typed is no length at all, not a length of zero.
  const device = labelMaker();
  assert.equal(typedRoll("2.5", device).lengthMm, 2500);
  assert.equal(typedRoll(" 3 ", device).lengthMm, 3000);
  assert.equal(typedRoll("2.3456", device).lengthMm, 2346);
  assert.equal(typedRoll("", device).lengthMm, null);
  assert.equal(typedRoll("two", device).lengthMm, null);
});

test("a roll can only be loaded at a length the device takes", () => {
  // The range is the device's, from api/capabilities: 0.5 m to 10 m.
  const device = labelMaker();
  assert.equal(typedRoll("0.5", device).lengthMm, 500);
  assert.equal(typedRoll("10", device).lengthMm, 10000);
  assert.equal(typedRoll("0.499", device).lengthMm, null);
  assert.equal(typedRoll("10.001", device).lengthMm, null);
});

test("the roll length's minus and plus stop at the ends of what the device takes", () => {
  // Past an end there is nowhere for that button to go. A length that is not
  // a number yet leaves both, which step from a new roll's length.
  const device = labelMaker();
  const steps = (text) => {
    const typed = typedRoll(text, device);
    return [typed.lessAvailable, typed.moreAvailable];
  };
  assert.deepEqual(steps("3"), [true, true]);
  assert.deepEqual(steps("0.5"), [false, true]);
  assert.deepEqual(steps("0.4"), [false, true]);
  assert.deepEqual(steps("10"), [true, false]);
  assert.deepEqual(steps("12"), [true, false]);
  assert.deepEqual(steps(""), [true, true]);
});

test("the roll length's minus and plus go to the next half metre", () => {
  // Rolls come in whole and half metres, so a typed 2.7 steps to 3 and not
  // to 3.2. Nothing typed steps from a new roll's length, 3 m, and a step
  // never leaves what the device takes.
  const device = labelMaker();
  assert.equal(steppedRollLength("2.7", 1, device), 3000);
  assert.equal(steppedRollLength("2.7", -1, device), 2500);
  assert.equal(steppedRollLength("2.8", 1, device), 3000);
  assert.equal(steppedRollLength("2.2", -1, device), 2000);
  assert.equal(steppedRollLength("3", 1, device), 3500);
  assert.equal(steppedRollLength("3", -1, device), 2500);
  assert.equal(steppedRollLength("", 1, device), 3500);
  assert.equal(steppedRollLength("two", -1, device), 2500);
  assert.equal(steppedRollLength("10", 1, device), 10000);
  assert.equal(steppedRollLength("0.2", -1, device), 500);
});

test("loading a roll starts from the length the last one went in at", () => {
  // Most rolls are the same length as the one before. Before there is a
  // status to say, it is a new roll's length, and it is never a length the
  // device would refuse.
  const device = labelMaker();
  assert.equal(offeredRollLength({ roll: { length_mm: 2000, remaining_mm: 40 } }, device), 2000);
  assert.equal(offeredRollLength({ roll: { length_mm: 20000, remaining_mm: 0 } }, device), 10000);
  assert.equal(offeredRollLength(null, device), 3000);
  assert.equal(offeredRollLength({ busy: false }, device), 3000);
});

test("loading a roll says what lengths the device takes", () => {
  // Said in metres, the unit the length is typed in.
  assert.equal(rollRange(labelMaker()), "From 0.5 to 10\u00a0m. A new roll is usually 3\u00a0m.");
});

test("Setup shows how much of the roll is left, and warns when it runs low", () => {
  // Low is under a tenth of the length the roll went in at. The share is of
  // that length, so a roll that has been over-declared never shows more
  // than full, and one declared at nothing shows empty.
  const gauge = (length_mm, remaining_mm) => rollGauge({ roll: { length_mm, remaining_mm } });
  assert.deepEqual(gauge(3000, 1500), { left: "1.5\u00a0m", of: "left of 3\u00a0m", share: 0.5, low: false });
  assert.deepEqual(gauge(3000, 300), { left: "300\u00a0mm", of: "left of 3\u00a0m", share: 0.1, low: false });
  assert.equal(gauge(3000, 2824).left, "2.82\u00a0m");
  assert.equal(gauge(3000, 299).low, true);
  assert.equal(gauge(3000, 4000).share, 1);
  assert.equal(gauge(0, 0).share, 0);
  assert.equal(rollGauge(null), null);
  assert.equal(rollGauge({ busy: false }), null);
});
