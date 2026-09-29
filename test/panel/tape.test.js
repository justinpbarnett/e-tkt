import assert from "node:assert/strict";
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

test("labels that fit count the lead, a feed per character and the top-up", () => {
  // The same sums as labelsThatFit() in Tape.h. Seven characters take eight
  // feeds of 4 mm, so 3 m holds 93 of them and not the 94 that would run off
  // the end. Three characters are topped up to six and take seven feeds.
  const device = labelMaker();
  assert.equal(labelsThatFit(3000, 7, device), 93);
  assert.equal(labelsThatFit(3000, 3, device), 107);
  assert.equal(labelsThatFit(3000, 1, device), 375);
  assert.equal(labelsThatFit(31, 7, device), 0);
  assert.equal(labelsThatFit(0, 7, device), 0);
});

test("a label is as long on the tape as the feeds it takes", () => {
  // What the line under the tape says the label will come out at. A short
  // label comes out as long as the minimum, because of its top-up.
  const device = labelMaker();
  assert.equal(labelLengthMm(9, device), 40);
  assert.equal(labelLengthMm(2, device), 28);
});

test("a length of tape reads in the unit a person would say it in, rounded down", () => {
  // Rounded down so what is left is never said to be more than the device's
  // own estimate of it.
  assert.equal(formatLength(9.9), "9 mm");
  assert.equal(formatLength(-4), "0 mm");
  assert.equal(formatLength(10), "1 cm");
  assert.equal(formatLength(999), "99 cm");
  assert.equal(formatLength(1000), "1 m");
  assert.equal(formatLength(2999), "2.9 m");
  assert.equal(formatLength(10000), "10 m");
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
  assert.equal(rollRange(labelMaker()), "From 0.5 to 10 m. A new roll is usually 3 m.");
});

test("Setup shows how much of the roll is left, and warns when it runs low", () => {
  // Low is under a tenth of the length the roll went in at. The share is of
  // that length, so a roll that has been over-declared never shows more
  // than full, and one declared at nothing shows empty.
  const gauge = (length_mm, remaining_mm) => rollGauge({ roll: { length_mm, remaining_mm } });
  assert.deepEqual(gauge(3000, 1500), { left: "1.5 m", of: "left of 3 m", share: 0.5, low: false });
  assert.deepEqual(gauge(3000, 300), { left: "30 cm", of: "left of 3 m", share: 0.1, low: false });
  assert.equal(gauge(3000, 299).low, true);
  assert.equal(gauge(3000, 4000).share, 1);
  assert.equal(gauge(0, 0).share, 0);
  assert.equal(rollGauge(null), null);
  assert.equal(rollGauge({ busy: false }), null);
});
