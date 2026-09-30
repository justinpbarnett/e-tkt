import assert from "node:assert/strict";
import { test } from "node:test";

import { quantity, settledCopies, steppedCopies } from "../../data/quantity.js";
import { labelMaker } from "./device.js";

// The quantity options as a person has left them, for a label of 9
// characters as sent on a full 3 m roll: 40 mm a label, so 75 fit.
function choice(changes) {
  return { mode: "one", copiesText: "2", labelLength: 9, tapeLeftMm: 3000, ...changes };
}

test("the One option prints one label", () => {
  // Whatever is still in the Multiple field.
  const one = quantity(choice({ copiesText: "12" }), labelMaker());
  assert.equal(one.copies, 1);
  assert.equal(one.printText, "Print label");
});

test("the Multiple option prints the count typed, once it is one the device takes", () => {
  // From 2, as one label is the One option, to the most one run prints.
  // Anything else leaves the print button with no number to print.
  const device = labelMaker();
  const multiple = (copiesText) => quantity(choice({ mode: "multiple", copiesText }), device);
  assert.equal(multiple("12").copies, 12);
  assert.equal(multiple("12").printText, "Print 12 labels");
  assert.equal(multiple(" 7 ").copies, 7);
  assert.equal(multiple("500").copies, 500);
  assert.equal(multiple("501").copies, null);
  assert.equal(multiple("1").copies, null);
  assert.equal(multiple("2.5").copies, null);
  assert.equal(multiple("").copies, null);
  assert.equal(multiple("").printText, "Print labels");
});

test("the Max option prints the labels that fit, up to the most one run prints", () => {
  // 75 of a 9-character label fit on 3 m. 1250 of a 1-character label fit
  // on 10 m, and one run prints 500. With no label to measure, or no roll
  // to measure it against, there is no number to print.
  const device = labelMaker();
  const max = (changes) => quantity(choice({ mode: "max", ...changes }), device);
  assert.equal(max({}).copies, 75);
  assert.equal(max({}).printText, "Print 75 labels");
  assert.equal(max({ labelLength: 1, tapeLeftMm: 10000 }).copies, 500);
  assert.equal(max({ tapeLeftMm: 40 }).copies, 1);
  assert.equal(max({ tapeLeftMm: 40 }).printText, "Print label");
  assert.equal(max({ labelLength: null }).copies, null);
  assert.equal(max({ tapeLeftMm: null }).copies, null);
});

test("the Max option goes back to One when there is nothing to print to the end of", () => {
  // A roll that is spent, or too short for one of the label, leaves Max
  // with nothing to mean. Rather than leave a choice picked that cannot be
  // made, it is One again, and Max is not offered. Before a status says
  // how much is left, Max stays.
  const device = labelMaker();
  const max = (changes) => quantity(choice({ mode: "max", ...changes }), device);
  const spentRolls = [
    max({ tapeLeftMm: 0 }),
    max({ tapeLeftMm: 0, labelLength: null }),
    max({ tapeLeftMm: 31, labelLength: 7 }),
  ];
  for (const spent of spentRolls) {
    assert.equal(spent.mode, "one");
    assert.equal(spent.maxAvailable, false);
    assert.equal(spent.copies, 1);
  }
  assert.equal(max({ tapeLeftMm: null }).mode, "max");
  assert.equal(max({ tapeLeftMm: null }).maxAvailable, true);
  assert.equal(max({ labelLength: null }).maxAvailable, true);
  assert.equal(max({}).mode, "max");
});

test("the Multiple option is offered while the device prints runs of two or more", () => {
  // Before the device has said, it stays offered, so the option does not go
  // off and on again while api/capabilities answers.
  const device = labelMaker();
  const single = { ...device, copies: { minimum: 1, maximum: 1 } };
  assert.equal(quantity(choice({}), device).multipleAvailable, true);
  assert.equal(quantity(choice({}), single).multipleAvailable, false);
  assert.equal(quantity(choice({ labelLength: null }), null).multipleAvailable, true);
});

test("the Multiple field's minus and plus stop at the ends of what the device takes", () => {
  // A count out of range still has a way back in, and a field with no
  // number in it leaves both, which start again from 2. Before the device
  // has said how many it takes, there is no end to step up to.
  const device = labelMaker();
  const steps = (copiesText, on = device) => {
    const multiple = quantity(choice({ mode: "multiple", copiesText, labelLength: null }), on);
    return [multiple.fewerAvailable, multiple.moreAvailable];
  };
  assert.deepEqual(steps("12"), [true, true]);
  assert.deepEqual(steps("2"), [false, true]);
  assert.deepEqual(steps("1"), [false, true]);
  assert.deepEqual(steps("500"), [true, false]);
  assert.deepEqual(steps("900"), [true, false]);
  assert.deepEqual(steps(""), [true, true]);
  assert.deepEqual(steps("12", null), [true, false]);
});

test("the Multiple field is marked wrong while it holds no count the device takes", () => {
  // Only while Multiple is the option picked: the field is hidden otherwise.
  const device = labelMaker();
  const marked = (mode, copiesText) => quantity(choice({ mode, copiesText }), device).copiesInvalid;
  assert.equal(marked("multiple", "12"), false);
  assert.equal(marked("multiple", "1"), true);
  assert.equal(marked("multiple", ""), true);
  assert.equal(marked("one", ""), false);
});

test("the quantity note says nothing until a status says how much tape is left", () => {
  const device = labelMaker();
  assert.deepEqual(quantity(choice({ tapeLeftMm: null }), device).note, { text: "", tone: null });
  assert.deepEqual(quantity(choice({ labelLength: null }), null).note, { text: "", tone: null });
});

test("the quantity note says when the roll is spent, and where to load another", () => {
  // A warning rather than a refusal: the estimate is a count of feeds
  // against a length somebody typed in, and the tape on the spool is the
  // better judge.
  const spent = quantity(choice({ tapeLeftMm: 0 }), labelMaker()).note;
  assert.deepEqual(spent, { text: "The roll is estimated to be empty.\nLoad a new roll in Setup.", tone: "warning" });
});

test("the quantity note says how much tape is left while there is no label to measure", () => {
  const left = quantity(choice({ labelLength: null, tapeLeftMm: 2968 }), labelMaker()).note;
  assert.deepEqual(left, { text: "2.96 m of tape left on the roll.", tone: null });
});

test("the quantity note warns when not even one of the label fits", () => {
  // A 7-character label takes 32 mm, and 31 mm is left.
  const short = quantity(choice({ labelLength: 7, tapeLeftMm: 31 }), labelMaker()).note;
  assert.deepEqual(short, {
    text: "Only 31 mm left, not enough for a label this long.\nLoad a new roll in Setup.",
    tone: "warning",
  });
});

test("with One picked, the quantity note says about how many of the label fit", () => {
  const device = labelMaker();
  assert.deepEqual(quantity(choice({}), device).note, {
    text: "About 75 labels this long fit on the 3 m left.",
    tone: null,
  });
  assert.deepEqual(quantity(choice({ tapeLeftMm: 40 }), device).note, {
    text: "About 1 label this long fits on the 40 mm left.",
    tone: null,
  });
});

test("with Multiple picked, the quantity note says what the run takes, or why it cannot be printed", () => {
  // 12 labels of 40 mm are 480 mm, and two of a 6-character label, 28 mm
  // each, are 56 mm. More than fit is a warning, not a refusal: the tape on
  // the spool is the better judge of what is left.
  const device = labelMaker();
  const multiple = (changes) => quantity(choice({ mode: "multiple", ...changes }), device).note;
  assert.deepEqual(multiple({ copiesText: "12" }), { text: "Uses about 480 mm of the 3 m left.", tone: null });
  assert.deepEqual(multiple({ copiesText: "2", labelLength: 6 }), {
    text: "Uses about 56 mm of the 3 m left.",
    tone: null,
  });
  assert.deepEqual(multiple({ copiesText: "1" }), { text: "Enter a number from 2 to 500.", tone: "warning" });
  assert.deepEqual(multiple({ copiesText: "75" }), { text: "Uses about 3 m of the 3 m left.", tone: null });
  assert.deepEqual(multiple({ copiesText: "76" }), { text: "Only about 75 fit on the 3 m left.", tone: "warning" });
  assert.deepEqual(multiple({ copiesText: "2", tapeLeftMm: 40 }), {
    text: "Only about 1 fits on the 40 mm left.",
    tone: "warning",
  });
});

test("with Max picked, the quantity note says how many that is, and when a run cannot hold them all", () => {
  // 1250 of a 1-character label fit on 10 m, and one run prints 500.
  const device = labelMaker();
  const max = (changes) => quantity(choice({ mode: "max", ...changes }), device).note;
  assert.deepEqual(max({}), { text: "75 labels, to the end of the roll.", tone: null });
  assert.deepEqual(max({ tapeLeftMm: 40 }), { text: "1 label, to the end of the roll.", tone: null });
  assert.deepEqual(max({ labelLength: 1, tapeLeftMm: 4000 }), {
    text: "500 labels, to the end of the roll.",
    tone: null,
  });
  assert.deepEqual(max({ labelLength: 1, tapeLeftMm: 10000 }), {
    text: "500 labels, the most one run prints. About 1250 fit.",
    tone: null,
  });
});

test("a count left in the Multiple field settles on the nearest one the device takes", () => {
  // Or on the smallest, when nothing readable was typed.
  const device = labelMaker();
  assert.equal(settledCopies("12", device), 12);
  assert.equal(settledCopies(" 2.6 ", device), 3);
  assert.equal(settledCopies("0", device), 2);
  assert.equal(settledCopies("9000", device), 500);
  assert.equal(settledCopies("", device), 2);
  assert.equal(settledCopies("many", device), 2);
});

test("the Multiple field's minus and plus step the count, back into range first", () => {
  // From a count out of range the first step lands back inside it, and
  // from no count at all it starts at the smallest.
  const device = labelMaker();
  assert.equal(steppedCopies("12", 1, device), 13);
  assert.equal(steppedCopies("12", -1, device), 11);
  assert.equal(steppedCopies("2", -1, device), 2);
  assert.equal(steppedCopies("500", 1, device), 500);
  assert.equal(steppedCopies("900", -1, device), 500);
  assert.equal(steppedCopies("0", 1, device), 2);
  assert.equal(steppedCopies("", 1, device), 2);
  assert.equal(steppedCopies("2.5", -1, device), 2);
});
