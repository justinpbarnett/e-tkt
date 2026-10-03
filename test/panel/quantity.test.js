import assert from "node:assert/strict";
import { test } from "node:test";

import { Quantity } from "../../data/quantity.js";
import { labelMaker } from "./device.js";

// What a quantity is measured against: a label of 9 characters as sent on a
// full 3 m roll, but for what changes says. 37 mm a label, so 81 fit.
function measure(changes) {
  return { labelLength: 9, tapeLeftMm: 3000, ...changes };
}

// A quantity with text typed into its count field.
function counted(text) {
  const quantity = new Quantity();
  quantity.typed(text);
  return quantity;
}

// A quantity with Max pressed, over the count that was typed before it.
function maxed(text = "1") {
  const quantity = counted(text);
  quantity.toggleMax();
  return quantity;
}

test("a quantity starts at one label", () => {
  const one = new Quantity().chosen(measure(), labelMaker());
  assert.equal(one.max, false);
  assert.equal(one.copies, 1);
  assert.equal(one.text, "1");
  assert.equal(one.printText, "Print label");
});

test("the count typed is the count printed, once it is one the device takes", () => {
  // From 1 to the most one run prints. Anything else leaves the print button
  // with no number to print.
  const device = labelMaker();
  const count = (text) => counted(text).chosen(measure(), device);
  assert.equal(count("12").copies, 12);
  assert.equal(count("12").printText, "Print 12 labels");
  assert.equal(count(" 7 ").copies, 7);
  assert.equal(count("1").copies, 1);
  assert.equal(count("1").printText, "Print label");
  assert.equal(count("500").copies, 500);
  assert.equal(count("501").copies, null);
  assert.equal(count("0").copies, null);
  assert.equal(count("2.5").copies, null);
  assert.equal(count("").copies, null);
  assert.equal(count("").printText, "Print labels");
});

test("the count field shows what was typed, as it was typed", () => {
  // Not put right while it is being typed: a field emptied on the way from 2
  // to 12 stays empty.
  const device = labelMaker();
  for (const text of ["12", "", "007", "9000"]) {
    assert.equal(counted(text).chosen(measure(), device).text, text);
  }
});

test("Max prints the labels that fit, up to the most one run prints", () => {
  // 81 of a 9-character label fit on 3 m. 1351 of a 1-character label fit
  // on 10 m, and one run prints 500. The count field shows the number. With
  // no label to measure, or no roll to measure it against, there is no
  // number to print or to show.
  const device = labelMaker();
  const max = (changes) => maxed().chosen(measure(changes), device);
  assert.equal(max({}).max, true);
  assert.equal(max({}).copies, 81);
  assert.equal(max({}).text, "81");
  assert.equal(max({}).printText, "Print 81 labels");
  assert.equal(max({ labelLength: 1, tapeLeftMm: 10000 }).copies, 500);
  assert.equal(max({ labelLength: 1, tapeLeftMm: 10000 }).text, "500");
  assert.equal(max({ tapeLeftMm: 40 }).copies, 1);
  assert.equal(max({ tapeLeftMm: 40 }).printText, "Print label");
  for (const unknown of [max({ labelLength: null }), max({ tapeLeftMm: null })]) {
    assert.equal(unknown.max, true);
    assert.equal(unknown.copies, null);
    assert.equal(unknown.text, "");
    assert.equal(unknown.printText, "Print labels");
  }
});

test("Max stays on, and follows the label and the roll", () => {
  // A 7-character label takes 29.6 mm, so 101 of them fit where 81 of the
  // 9-character one did, and 67 of them on 2 m.
  const device = labelMaker();
  const quantity = maxed();
  assert.equal(quantity.chosen(measure(), device).copies, 81);
  assert.equal(quantity.chosen(measure({ labelLength: 7 }), device).copies, 101);
  assert.equal(quantity.chosen(measure({ labelLength: 7, tapeLeftMm: 2000 }), device).copies, 67);
});

test("typing a count turns Max off", () => {
  const quantity = maxed();
  quantity.typed("5");
  const five = quantity.chosen(measure(), labelMaker());
  assert.equal(five.max, false);
  assert.equal(five.copies, 5);
  assert.equal(five.text, "5");
});

test("Max pressed again gives back the count that was there before it", () => {
  const device = labelMaker();
  const quantity = counted("12");
  quantity.toggleMax();
  assert.equal(quantity.chosen(measure(), device).copies, 81);
  quantity.toggleMax();
  const back = quantity.chosen(measure(), device);
  assert.equal(back.max, false);
  assert.equal(back.copies, 12);
  assert.equal(back.text, "12");
});

test("the minus and the plus step the count, back into range first", () => {
  // From a count out of range the first step lands back inside it, and from
  // no count at all it starts at the smallest.
  const device = labelMaker();
  const stepped = (text, step) => {
    const quantity = counted(text);
    quantity.step(step, measure(), device);
    return quantity.chosen(measure(), device).text;
  };
  assert.equal(stepped("12", 1), "13");
  assert.equal(stepped("12", -1), "11");
  assert.equal(stepped("1", -1), "1");
  assert.equal(stepped("500", 1), "500");
  assert.equal(stepped("900", -1), "500");
  assert.equal(stepped("0", 1), "1");
  assert.equal(stepped("", 1), "1");
  assert.equal(stepped("2.5", -1), "1");
});

test("a step with Max on goes from the number Max came to, and turns Max off", () => {
  // One fewer than the roll holds is a count like any other, and the one
  // that comes back when Max is pressed and pressed again.
  const device = labelMaker();
  const quantity = maxed();
  quantity.step(-1, measure(), device);
  const fewer = quantity.chosen(measure(), device);
  assert.equal(fewer.max, false);
  assert.equal(fewer.copies, 80);
  quantity.toggleMax();
  quantity.toggleMax();
  assert.equal(quantity.chosen(measure(), device).copies, 80);

  // With no number for Max to come to, a step starts at the smallest.
  const unmeasured = maxed("12");
  unmeasured.step(1, measure({ labelLength: null }), device);
  assert.equal(unmeasured.chosen(measure(), device).max, false);
  assert.equal(unmeasured.chosen(measure(), device).text, "1");
});

test("the minus and the plus stop at the ends of what the device takes", () => {
  // A count out of range still has a way back in, and a field with no number
  // in it leaves both, which start again from 1. Before the device has said
  // how many it takes, there is nowhere to step to.
  const device = labelMaker();
  const steps = (quantity, changes = {}, on = device) => {
    const chosen = quantity.chosen(measure(changes), on);
    return [chosen.fewerAvailable, chosen.moreAvailable];
  };
  assert.deepEqual(steps(counted("12")), [true, true]);
  assert.deepEqual(steps(counted("1")), [false, true]);
  assert.deepEqual(steps(counted("0")), [false, true]);
  assert.deepEqual(steps(counted("500")), [true, false]);
  assert.deepEqual(steps(counted("900")), [true, false]);
  assert.deepEqual(steps(counted("")), [true, true]);
  assert.deepEqual(steps(counted("12"), { labelLength: null }, null), [false, false]);
  // With Max on, they go by the number Max comes to.
  assert.deepEqual(steps(maxed()), [true, true]);
  assert.deepEqual(steps(maxed(), { tapeLeftMm: 40 }), [false, true]);
  assert.deepEqual(steps(maxed(), { labelLength: 1, tapeLeftMm: 10000 }), [true, false]);
  assert.deepEqual(steps(maxed(), { labelLength: null }), [true, true]);
});

test("a count left in the field settles on the nearest one the device takes", () => {
  // Or on the smallest, when nothing readable was typed.
  const device = labelMaker();
  const settled = (text) => {
    const quantity = counted(text);
    quantity.settle(device);
    return quantity.chosen(measure(), device).text;
  };
  assert.equal(settled("12"), "12");
  assert.equal(settled(" 2.6 "), "3");
  assert.equal(settled("0"), "1");
  assert.equal(settled("9000"), "500");
  assert.equal(settled(""), "1");
  assert.equal(settled("many"), "1");
});

test("settling changes nothing with Max on, or before the device has said what it takes", () => {
  // With Max on the field holds what Max comes to, and Enter in it is not a
  // count typed.
  const device = labelMaker();
  const max = maxed("9000");
  max.settle(device);
  assert.equal(max.chosen(measure(), device).max, true);
  max.toggleMax();
  assert.equal(max.chosen(measure(), device).text, "9000");

  const early = counted("9000");
  early.settle(null);
  assert.equal(early.chosen(measure({ labelLength: null }), null).text, "9000");
});

test("the count field is marked wrong while it holds no count the device takes", () => {
  // Not with Max on, when the count is not what prints, and not before the
  // device has said what it takes.
  const device = labelMaker();
  const marked = (quantity, on = device) => quantity.chosen(measure(), on).copiesInvalid;
  assert.equal(marked(counted("12")), false);
  assert.equal(marked(counted("0")), true);
  assert.equal(marked(counted("501")), true);
  assert.equal(marked(counted("")), true);
  assert.equal(marked(maxed("")), false);
  assert.equal(marked(counted("0"), null), false);
});

test("Max goes off, and stays off, once there is nothing to print to the end of", () => {
  // A roll that is spent, or too short for one of the label, leaves Max with
  // nothing to mean. Rather than stay on for a choice that cannot be made, it
  // goes off, the count that was there before it comes back, and Max is not
  // offered. A new roll does not turn it on again: that would be a roll of
  // labels from one press of Print.
  const device = labelMaker();
  const spentRolls = [
    { tapeLeftMm: 0 },
    { tapeLeftMm: 0, labelLength: null },
    // A 7-character label takes 29.6 mm, so 29 mm is not one of them.
    { tapeLeftMm: 29, labelLength: 7 },
  ];
  for (const spent of spentRolls) {
    const quantity = maxed("12");
    quantity.measured(measure(spent), device);
    const off = quantity.chosen(measure(spent), device);
    assert.equal(off.max, false);
    assert.equal(off.maxAvailable, false);
    assert.equal(off.copies, 12);
    assert.equal(off.text, "12");
    const reloaded = quantity.chosen(measure(), device);
    assert.equal(reloaded.max, false);
    assert.equal(reloaded.maxAvailable, true);
    assert.equal(reloaded.copies, 12);
  }
});

test("Max counts for nothing on a spent roll, even before the roll has been measured", () => {
  // What a press of Print would send goes by the roll as it is now, whether
  // or not the page has been drawn since it ran out.
  const off = maxed("12").chosen(measure({ tapeLeftMm: 0 }), labelMaker());
  assert.equal(off.max, false);
  assert.equal(off.copies, 12);
  assert.equal(off.text, "12");
});

test("Max stays on while there is a roll to print to the end of, or none to say there is not", () => {
  // Before a status says how much is left, and while there is no label to
  // measure, Max stays on and stays offered.
  const device = labelMaker();
  for (const known of [{}, { tapeLeftMm: null }, { labelLength: null }]) {
    const quantity = maxed();
    quantity.measured(measure(known), device);
    const on = quantity.chosen(measure(known), device);
    assert.equal(on.max, true);
    assert.equal(on.maxAvailable, true);
  }
});

test("the quantity note says nothing until a status says how much tape is left", () => {
  const device = labelMaker();
  assert.deepEqual(new Quantity().chosen(measure({ tapeLeftMm: null }), device).note, { text: "", tone: null });
  assert.deepEqual(new Quantity().chosen(measure({ labelLength: null }), null).note, { text: "", tone: null });
});

test("the quantity note says when the roll is spent, and where to load another", () => {
  // A warning rather than a refusal: the estimate is a count of feeds
  // against a length somebody typed in, and the tape on the spool is the
  // better judge.
  const spent = new Quantity().chosen(measure({ tapeLeftMm: 0 }), labelMaker()).note;
  assert.deepEqual(spent, { text: "The roll is estimated to be empty.\nLoad a new roll in Setup.", tone: "warning" });
});

test("the quantity note says how much tape is left while there is no label to measure", () => {
  const left = new Quantity().chosen(measure({ labelLength: null, tapeLeftMm: 2968 }), labelMaker()).note;
  assert.deepEqual(left, { text: "2.96 m of tape left on the roll.", tone: null });
});

test("the quantity note warns when not even one of the label fits", () => {
  // A 7-character label takes 29.6 mm, and 29 mm is left.
  const short = new Quantity().chosen(measure({ labelLength: 7, tapeLeftMm: 29 }), labelMaker()).note;
  assert.deepEqual(short, {
    text: "Only 29 mm left, not enough for a label this long.\nLoad a new roll in Setup.",
    tone: "warning",
  });
});

test("for a count that fits, the quantity note says about how many of the label fit", () => {
  const device = labelMaker();
  const note = (text, changes) => counted(text).chosen(measure(changes), device).note;
  for (const text of ["1", "12", "81"]) {
    assert.deepEqual(note(text), { text: "About 81 labels this long fit on the 3 m left.", tone: null });
  }
  assert.deepEqual(note("1", { tapeLeftMm: 40 }), {
    text: "About 1 label this long fits on the 40 mm left.",
    tone: null,
  });
});

test("for a count of more than fit, the quantity note warns, and the count still prints", () => {
  // A warning, not a refusal: the tape on the spool is the better judge of
  // what is left.
  const device = labelMaker();
  const more = counted("82").chosen(measure(), device);
  assert.deepEqual(more.note, { text: "Only about 81 fit on the 3 m left.", tone: "warning" });
  assert.equal(more.copies, 82);
  assert.deepEqual(counted("2").chosen(measure({ tapeLeftMm: 40 }), device).note, {
    text: "Only about 1 fits on the 40 mm left.",
    tone: "warning",
  });
});

test("the quantity note asks for a count the device takes before it says anything of the roll", () => {
  // The count is what keeps the print button from printing, so it comes
  // first: before a status, on a spent roll, with no label to measure.
  const device = labelMaker();
  const wanted = { text: "Enter a number from 1 to 500.", tone: "warning" };
  for (const text of ["0", "501", "", "2.5"]) {
    assert.deepEqual(counted(text).chosen(measure(), device).note, wanted);
  }
  for (const changes of [{ tapeLeftMm: null }, { tapeLeftMm: 0 }, { labelLength: null }]) {
    assert.deepEqual(counted("0").chosen(measure(changes), device).note, wanted);
  }
  // Not with Max on, when the count is not what prints, and not before the
  // device has said what it takes.
  assert.deepEqual(maxed("0").chosen(measure(), device).note, {
    text: "81 labels, to the end of the roll.",
    tone: null,
  });
  assert.deepEqual(counted("0").chosen(measure({ labelLength: null }), null).note, { text: "", tone: null });
});

test("with Max on, the quantity note says how many that is, and when a run cannot hold them all", () => {
  // 500 single-letter labels take 3700 mm, so that roll ends on the copy
  // limit. 1351 fit on 10 m, and one run prints 500 of them.
  const device = labelMaker();
  const max = (changes) => maxed().chosen(measure(changes), device).note;
  assert.deepEqual(max({}), { text: "81 labels, to the end of the roll.", tone: null });
  assert.deepEqual(max({ tapeLeftMm: 40 }), { text: "1 label, to the end of the roll.", tone: null });
  assert.deepEqual(max({ labelLength: 1, tapeLeftMm: 3700 }), {
    text: "500 labels, to the end of the roll.",
    tone: null,
  });
  assert.deepEqual(max({ labelLength: 1, tapeLeftMm: 10000 }), {
    text: "500 labels, the most one run prints. About 1351 fit.",
    tone: null,
  });
});
