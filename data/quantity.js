// How many labels to print: the One, Multiple and Max options, the count in
// the Multiple field, and what the choice will take out of the roll. The
// capabilities come in as `device`, as readCapabilities() in status.js
// returns them, or null while api/capabilities has not answered yet.

import { formatLength, labelLengthMm, labelsThatFit } from "./tape.js";

// The smallest run the Multiple option offers. One label is the One option.
export const MIN_MULTIPLE = 2;

// Everything the quantity options come to, from the options as a person
// has left them:
//
//   mode         "one", "multiple" or "max", the option picked
//   copiesText   what is in the Multiple field
//   labelLength  the label's length as sent, in characters, or null while
//                there is no label the device would take
//   tapeLeftMm   the tape left on the roll, or null before a status says
export function quantity(choice, device) {
  const { labelLength, tapeLeftMm } = choice;
  const count = wholeNumber(choice.copiesText);
  const typed = typedCopies(count, device);
  // How many of the label fit on what is left of the roll, or null while
  // there is no label to measure or no roll to measure it against.
  const fit = tapeLeftMm === null || labelLength === null ? null : labelsThatFit(tapeLeftMm, labelLength, device);
  const maxAvailable = !(tapeLeftMm !== null && tapeLeftMm <= 0) && fit !== 0;
  // Nothing left to print to the end of. Back to one, rather than leave a
  // choice selected that cannot be made.
  const mode = choice.mode === "max" && !maxAvailable ? "one" : choice.mode;
  let copies;
  switch (mode) {
    case "multiple":
      copies = typed;
      break;
    case "max":
      // Never none: none that fit is One, above.
      copies = fit === null ? null : Math.min(fit, device.copies.maximum);
      break;
    default:
      copies = 1;
  }
  return {
    mode: mode,
    copies: copies,
    maxAvailable: maxAvailable,
    multipleAvailable: device === null || device.copies.maximum >= MIN_MULTIPLE,
    copiesInvalid: mode === "multiple" && typed === null,
    // From a count out of range, the first step lands back inside it.
    fewerAvailable: count === null || count > MIN_MULTIPLE,
    moreAvailable: device !== null && (count === null || count < device.copies.maximum),
    note: note(mode, typed, labelLength, fit, tapeLeftMm, device),
    printText: printText(mode, copies),
  };
}

// The way out of a roll that is spent, on a line of its own under what is
// wrong with it.
const LOAD_NEW_ROLL = "\nLoad a new roll in Setup.";

// The line under the quantity: what the choice will take out of the roll.
// A warning, not a refusal, when the choice is more than the roll is
// estimated to hold -- the estimate is a count of feeds against a length
// somebody typed in, and the tape on the spool is the better judge.
function note(mode, typed, labelLength, fit, tapeLeftMm, device) {
  if (tapeLeftMm === null || device === null) {
    return { text: "", tone: null };
  }
  if (tapeLeftMm <= 0) {
    return { text: "The roll is estimated to be empty." + LOAD_NEW_ROLL, tone: "warning" };
  }
  const left = formatLength(tapeLeftMm);
  if (labelLength === null) {
    return { text: left + " of tape left on the roll.", tone: null };
  }
  if (fit === 0) {
    return {
      text: "Only " + left + " left, not enough for a label this long." + LOAD_NEW_ROLL,
      tone: "warning",
    };
  }

  if (mode === "multiple") {
    if (typed === null) {
      return { text: "Enter a number from " + MIN_MULTIPLE + " to " + device.copies.maximum + ".", tone: "warning" };
    }
    if (typed > fit) {
      return { text: "Only about " + plural(fit, "fits", "fit") + " on the " + left + " left.", tone: "warning" };
    }
    return {
      text: "Uses about " + formatLength(typed * labelLengthMm(labelLength, device)) + " of the " + left + " left.",
      tone: null,
    };
  }
  if (mode === "max") {
    if (fit > device.copies.maximum) {
      return {
        text: plural(device.copies.maximum, "label", "labels") + ", the most one run prints. About " + fit + " fit.",
        tone: null,
      };
    }
    return { text: plural(fit, "label", "labels") + ", to the end of the roll.", tone: null };
  }
  return {
    text: "About " + plural(fit, "label this long fits", "labels this long fit") + " on the " + left + " left.",
    tone: null,
  };
}

export function plural(count, one, many) {
  return count + " " + (count === 1 ? one : many);
}

// Where the Multiple field settles once it is left: on the count the device
// will take nearest to what was typed, or the smallest when nothing
// readable was.
export function settledCopies(text, device) {
  const trimmed = text.trim();
  const typed = Number(trimmed);
  const copies = trimmed === "" || !Number.isFinite(typed) ? MIN_MULTIPLE : Math.round(typed);
  return clamp(copies, MIN_MULTIPLE, device.copies.maximum);
}

// Where the Multiple field's minus (-1) or plus (1) takes the count. From a
// count out of range, the first step lands back inside the range.
export function steppedCopies(text, step, device) {
  const copies = wholeNumber(text);
  const next = copies === null ? MIN_MULTIPLE : copies + step;
  return clamp(next, MIN_MULTIPLE, device.copies.maximum);
}

function clamp(value, min, max) {
  return Math.min(Math.max(value, min), max);
}

// The whole number in the Multiple field, in range or not, or null while it
// is not one. A number field that holds something it cannot read as a
// number reports an empty value, so anything else typed is null here too.
function wholeNumber(text) {
  const trimmed = text.trim();
  return /^\d+$/.test(trimmed) ? Number(trimmed) : null;
}

// The count in the Multiple field, or null while it is not one the device
// would take.
function typedCopies(copies, device) {
  if (device === null || copies === null) {
    return null;
  }
  return copies >= MIN_MULTIPLE && copies <= device.copies.maximum ? copies : null;
}

function printText(mode, copies) {
  if (mode === "one" || copies === 1) {
    return "Print label";
  }
  return copies === null ? "Print labels" : "Print " + copies + " labels";
}
