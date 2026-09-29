import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { test } from "node:test";

import {
  codePoints,
  hintFor,
  isValidLabelText,
  paddedLabel,
  typedLengthLimit,
  unprintableCharacters,
} from "../../data/label.js";
import { labelMaker } from "./device.js";

test("a short label is padded evenly to one past the device's minimum", () => {
  // The device tops a label under its minimum up with trailing feeds, which
  // puts the text off centre, so the panel pads it with spaces on both
  // sides first: to 7, as the device's minimum is 6. The loose margin's own
  // space on each side counts toward that, and is not lost to it.
  const device = labelMaker();
  assert.equal(paddedLabel("AB", "loose", device), "   AB   ");
  assert.equal(paddedLabel("AB", "tight", device), "   AB   ");
  assert.equal(paddedLabel("ABCDEF", "tight", device), " ABCDEF ");
  assert.equal(paddedLabel("ABCDEF", "loose", device), " ABCDEF ");
  assert.equal(paddedLabel("ABCDEFG", "tight", device), "ABCDEFG");
  assert.equal(paddedLabel("ABCDEFG", "loose", device), " ABCDEFG ");
});

test("each margin the page offers pads a label as its button says", () => {
  // The page hands paddedLabel() the value of the margin button that is
  // checked, so each value in data/index.html has to be a margin it knows.
  const page = readFileSync(new URL("../../data/index.html", import.meta.url), "utf8");
  const offered = [...page.matchAll(/name="margin" value="([^"]*)"[^>]*>\s*<span>([^<]*)<\/span>/g)].map(
    ([, value, button]) => [button, paddedLabel("ABCDEFG", value, labelMaker())],
  );
  assert.deepEqual(offered, [
    ["None", "ABCDEFG"],
    ["1 space", " ABCDEFG "],
  ]);
});

test("before the device has said its minimum, a label gets only its margin", () => {
  // A guessed minimum would drift from the device's as the old bare 7 did,
  // so the page shows the margin alone until api/capabilities answers.
  assert.equal(paddedLabel("AB", "loose", null), " AB ");
  assert.equal(paddedLabel("AB", "tight", null), "AB");
});

test("a label can be typed up to the device's maximum, less the widest margin", () => {
  // The margin goes on after typing, a space a side for a label this long,
  // so a label typed to the full 249 would go out over it and be refused.
  assert.equal(typedLengthLimit(labelMaker()), 247);
});

test("a label typed to the limit still fits once either margin is on", () => {
  // The limit and the padding are worked out apart, so this holds them to
  // each other: whichever margin is picked, the label the device is sent is
  // never over its maximum of 249.
  const device = labelMaker();
  const longest = "A".repeat(typedLengthLimit(device));
  assert.equal(codePoints(paddedLabel(longest, "loose", device)), 249);
  assert.equal(codePoints(paddedLabel(longest, "tight", device)), 247);
});

test("the characters of a label the wheel does not carry are named once each, as typed", () => {
  // Case does not matter: the label is sent lowercase and the firmware
  // upper-cases it again. Nothing is unprintable before the device has
  // said what it prints.
  const device = labelMaker();
  assert.deepEqual(unprintableCharacters("hello 42", device), []);
  assert.deepEqual(unprintableCharacters("a!b?c!", device), ["!", "?"]);
  assert.deepEqual(unprintableCharacters("sale 5€ ☆", device), []);
  assert.deepEqual(unprintableCharacters("a!", null), []);
});

test("a label can be printed once it has text and the wheel carries all of it", () => {
  // Checked against the set the device served, so this answer and the
  // device's cannot drift apart, and refused outright until it has served
  // one.
  const device = labelMaker();
  assert.equal(isValidLabelText("hello", device), true);
  assert.equal(isValidLabelText("", device), false);
  assert.equal(isValidLabelText("hello!", device), false);
  assert.equal(isValidLabelText("hello", null), false);
});

test("the line under the tape names what the wheel does not carry", () => {
  // In the danger tone, because the device will refuse the label.
  assert.deepEqual(hintFor("a!b?", labelMaker()), { text: "Not on the wheel: ! ?", tone: "danger" });
});

test("the line under the tape warns of a character the wheel prints as another", () => {
  // The wheel has no 0 or 1, and presses O and I for them. That is the only
  // warning before the tape is spent.
  const device = labelMaker();
  assert.deepEqual(hintFor("room 101", device), { text: "0 prints O, 1 prints I", tone: "warning" });
  assert.deepEqual(hintFor("a0", device), { text: "0 prints O", tone: "warning" });
});

test("otherwise the line under the tape lists every character that may be typed", () => {
  // Three or more letters or digits in a row collapse to a range, and
  // nothing is left out, so the line cannot stop matching what the device
  // accepts. Before the device has said, there is nothing to list.
  const device = labelMaker();
  assert.deepEqual(hintFor("hello", device), { text: "$ - . 0-9 @ A-Z € ☆ ♡ ♪ space", tone: null });
  assert.deepEqual(hintFor("", { ...device, printable: "ABDEFXZ" }), { text: "A B D-F X Z", tone: null });
  assert.deepEqual(hintFor("", null), { text: "", tone: null });
});

test("a label is measured in characters as the device counts them", () => {
  // Four of the wheel's characters are three bytes each in UTF-8, the way
  // the label goes over the wire, and the device counts them as one.
  const device = labelMaker();
  assert.equal(codePoints("€☆♡♪"), 4);
  assert.equal(paddedLabel("€☆♡♪", "tight", device), "  €☆♡♪  ");
});
