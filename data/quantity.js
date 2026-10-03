// How many labels to print: a count, or Max, which is as many as the tape
// left on the roll will take, and what the choice will take out of the roll.
// The capabilities come in as `device`, as readCapabilities() in status.js
// returns them, or null while api/capabilities has not answered yet.
//
// What a quantity comes to depends on the label and on the roll, which come
// in as `measure`:
//
//   labelLength  the label's length as sent, in characters, or null while
//                there is no label the device would take
//   tapeLeftMm   the tape left on the roll, or null before a status says

import { clamp, formatLength, labelsThatFit } from "./tape.js";

// The quantity as a person has left it: the count, and whether Max is on.
//
// It is held here and the count field is drawn from it, because the field
// cannot hold all of it. With Max on the field shows what Max comes to, and
// the count that was there before is the one to give back when Max goes off.
export class Quantity {
  #max = false;
  // What the person has in the count field. Text and not a number: it is
  // theirs while they type, and a field emptied on the way from 2 to 12 is
  // not put right under their fingers.
  #text = "1";

  // The count field has changed under the person's hand. What it holds is
  // the count, so Max is off.
  typed(text) {
    this.#max = false;
    this.#text = text;
  }

  // The count field is left, or Enter is pressed in it. The count settles on
  // the one the device will take nearest to what was typed, or on the
  // smallest when nothing readable was. With Max on, the field holds what
  // Max comes to and nothing typed.
  settle(device) {
    if (this.#max || device === null) {
      return;
    }
    const trimmed = this.#text.trim();
    const typed = Number(trimmed);
    const copies = trimmed === "" || !Number.isFinite(typed) ? device.copies.minimum : Math.round(typed);
    this.#text = String(clamp(copies, device.copies.minimum, device.copies.maximum));
  }

  // The minus (-1) or the plus (1): one from the number the count field
  // shows, which with Max on is what Max comes to, and Max goes off. From a
  // count out of range the first step lands back inside the range, and from
  // no number at all it starts at the smallest.
  step(step, measure, device) {
    if (device === null) {
      return;
    }
    const shown = wholeNumber(this.chosen(measure, device).text);
    const next = shown === null ? device.copies.minimum : shown + step;
    this.#max = false;
    this.#text = String(clamp(next, device.copies.minimum, device.copies.maximum));
  }

  // The Max button: on, or off again and back to the count that was there.
  toggleMax() {
    this.#max = !this.#max;
  }

  // The label and the roll as they are now, each time the page is drawn.
  // Once there is nothing left to print to the end of, Max goes off and
  // stays off, rather than stay on for a choice that cannot be made. A new
  // roll does not turn it on again: that would be a roll of labels from one
  // press of Print.
  measured(measure, device) {
    if (!maxAvailable(measure, device)) {
      this.#max = false;
    }
  }

  // Everything the quantity comes to against the label and the roll as they
  // are now.
  chosen(measure, device) {
    const fit = labelsFit(measure, device);
    const available = maxAvailable(measure, device);
    const max = this.#max && available;
    const typed = takenCopies(wholeNumber(this.#text), device);
    // With Max on, never none: none that fit takes Max away, above.
    const copies = max ? (fit === null ? null : Math.min(fit, device.copies.maximum)) : typed;
    const text = max ? (copies === null ? "" : String(copies)) : this.#text;
    const shown = wholeNumber(text);
    return {
      max: max,
      copies: copies,
      // What the count field shows.
      text: text,
      maxAvailable: available,
      copiesInvalid: !max && device !== null && typed === null,
      // From a count out of range, the first step lands back inside it.
      fewerAvailable: device !== null && (shown === null || shown > device.copies.minimum),
      moreAvailable: device !== null && (shown === null || shown < device.copies.maximum),
      note: note(max, typed, measure, fit, device),
      printText: printText(copies),
    };
  }
}

// How many of the label fit on what is left of the roll, or null while there
// is no label to measure, no roll to measure it against, or no device to say
// how long a label comes out.
function labelsFit(measure, device) {
  const { labelLength, tapeLeftMm } = measure;
  if (device === null || labelLength === null || tapeLeftMm === null) {
    return null;
  }
  return labelsThatFit(tapeLeftMm, labelLength, device);
}

// Whether there is anything for Max to print to the end of: not on a roll
// that is spent, and not on one too short for one of the label. Until a
// status and a label say otherwise, there is.
function maxAvailable(measure, device) {
  const spent = measure.tapeLeftMm !== null && measure.tapeLeftMm <= 0;
  return !spent && labelsFit(measure, device) !== 0;
}

// The way out of a roll that is spent, on a line of its own under what is
// wrong with it.
const LOAD_NEW_ROLL = "\nLoad a new roll in Setup.";

// The line under the quantity: what the choice will take out of the roll.
// A warning, not a refusal, when the choice is more than the roll is
// estimated to hold -- the estimate is a count of feeds against a length
// somebody typed in, and the tape on the spool is the better judge.
function note(max, typed, measure, fit, device) {
  const { labelLength, tapeLeftMm } = measure;
  if (device === null) {
    return { text: "", tone: null };
  }
  // Before anything of the roll: no count the device takes is what keeps the
  // print button from printing.
  if (!max && typed === null) {
    return {
      text: "Enter a number from " + device.copies.minimum + " to " + device.copies.maximum + ".",
      tone: "warning",
    };
  }
  if (tapeLeftMm === null) {
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

  if (max) {
    if (fit > device.copies.maximum) {
      return {
        text: plural(device.copies.maximum, "label", "labels") + ", the most one run prints. About " + fit + " fit.",
        tone: null,
      };
    }
    return { text: plural(fit, "label", "labels") + ", to the end of the roll.", tone: null };
  }
  if (typed > fit) {
    return { text: "Only about " + plural(fit, "fits", "fit") + " on the " + left + " left.", tone: "warning" };
  }
  return {
    text: "About " + plural(fit, "label this long fits", "labels this long fit") + " on the " + left + " left.",
    tone: null,
  };
}

export function plural(count, one, many) {
  return count + " " + (count === 1 ? one : many);
}

// The whole number in the count field, in range or not, or null while it is
// not one. A number field that holds something it cannot read as a number
// reports an empty value, so anything else typed is null here too.
function wholeNumber(text) {
  const trimmed = text.trim();
  return /^\d+$/.test(trimmed) ? Number(trimmed) : null;
}

// The count, or null while it is not one the device would take.
function takenCopies(copies, device) {
  if (device === null || copies === null) {
    return null;
  }
  return copies >= device.copies.minimum && copies <= device.copies.maximum ? copies : null;
}

function printText(copies) {
  if (copies === 1) {
    return "Print label";
  }
  return copies === null ? "Print labels" : "Print " + copies + " labels";
}
