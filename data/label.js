// The label as typed, and the label as it will be sent: the margin it gets,
// whether the wheel can print it, and what the line under the tape says
// about it. Each function takes the capabilities as `device`, as
// readCapabilities() in status.js returns them, or null while
// api/capabilities has not answered yet.

// The widest margin paddedLabel() adds to a label that is already long
// enough on its own, per side. Short labels get more, to reach the minimum,
// but a label near the maximum never does.
const WIDEST_MARGIN = 1;

// How long a label may be typed: what the device will actually take, less
// the margin this panel is about to add to it. The number used to be
// maxlength="247" written into data/index.html, which is the panel deciding
// for itself what the device accepts -- and it decided wrong, because the
// margin pushed a full 247 characters to 249 and the device refused the
// label on arrival.
export function typedLengthLimit(device) {
  return device.label.maximum - WIDEST_MARGIN * 2;
}

// How many characters the panel pads a label up to, or null while the device
// has not said yet.
//
// One past the device's minimum. A label that only just reaches the minimum
// leaves the device topping the tape up with trailing feeds, which pushes the
// text off centre; padding one further does not. The number comes from
// api/capabilities -- three places here used to write it as a bare 7 while
// the device called it 6, and two of them kept saying 7 after the third
// started asking.
export function paddedLabelTarget(device) {
  return device === null ? null : device.label.minimum + 1;
}

// The label as it will be sent, with the margin, "tight" or "loose", on both
// sides so the text stays centred.
export function paddedLabel(text, margin, device) {
  let multiplier = margin === "tight" ? 0 : 1;

  // No fallback when the device has not said yet: a guessed minimum is the
  // same drift in a different place, so only the margin's own padding is
  // applied. That shows for as long as the first api/capabilities call
  // takes -- the page draws the tape again when it lands, and nothing is
  // sent before then, because isValidLabelText() refuses until it has.
  const target = paddedLabelTarget(device);
  if (target !== null) {
    const printLength = codePoints(text) + multiplier * 2;
    if (printLength < target) {
      // Added to the margin the mode already asked for, not put in its place.
      // Assigning here discarded the loose mode's own space on each side, so
      // every short label in that mode went out two characters under the
      // minimum the device had just asked for.
      multiplier += Math.ceil((target - printLength) / 2);
    }
  }
  return " ".repeat(multiplier) + text + " ".repeat(multiplier);
}

// Characters as the device counts them. Four of the wheel's are more than
// one byte, and a count in bytes or in UTF-16 would disagree with it.
export function codePoints(text) {
  return Array.from(text).length;
}

// Each character of the label the wheel does not carry, once, in the order
// typed. Case does not matter: the label is sent lowercase and the firmware
// upper-cases it again.
export function unprintableCharacters(text, device) {
  if (device === null) {
    return [];
  }
  const found = [];
  for (const character of text.toUpperCase()) {
    if (device.printable.indexOf(character) < 0 && !found.includes(character)) {
      found.push(character);
    }
  }
  return found;
}

// Whether the label is something the device would accept. Every character
// is checked against the set the device served, so this answer and the
// device's answer cannot drift apart.
export function isValidLabelText(text, device) {
  return device !== null && text.length > 0 && unprintableCharacters(text, device).length === 0;
}

// The line under the tape. Normally it lists what may be typed. While the
// label holds a character the wheel does not carry it names it, and while it
// holds one the wheel prints as something else it says what that will come
// out as, which is the only warning before the tape is spent.
export function hintFor(text, device) {
  if (device === null) {
    return { text: "", tone: null };
  }
  const unprintable = unprintableCharacters(text, device);
  if (unprintable.length > 0) {
    return { text: "Not on the wheel: " + unprintable.join(" "), tone: "danger" };
  }
  const upper = text.toUpperCase();
  const surprises = Object.keys(device.aliases)
    .filter((character) => upper.indexOf(character) >= 0)
    .map((character) => character + " prints " + device.aliases[character]);
  if (surprises.length > 0) {
    return { text: surprises.join(", "), tone: "warning" };
  }
  return { text: summariseCharacters(device.printable), tone: null };
}

// Turns the served character set into something short enough to sit under
// the input. A run of three or more consecutive letters or digits collapses
// to a range; everything else is listed as itself. Nothing is left out, so
// the line cannot quietly stop matching what the device accepts.
function summariseCharacters(characters) {
  const sameKind = (a, b) => (/[0-9]/.test(a) && /[0-9]/.test(b)) || (/[A-Z]/.test(a) && /[A-Z]/.test(b));

  const parts = [];
  let run = [];
  const flush = () => {
    if (run.length === 0) {
      return;
    }
    parts.push(run.length >= 3 ? run[0] + "-" + run[run.length - 1] : run.join(" "));
    run = [];
  };

  for (const glyph of characters) {
    if (glyph === " ") {
      continue;
    }
    const previous = run[run.length - 1];
    const follows = previous !== undefined && glyph.codePointAt(0) === previous.codePointAt(0) + 1;
    if (previous !== undefined && !(follows && sameKind(previous, glyph))) {
      flush();
    }
    run.push(glyph);
  }
  flush();

  if (characters.indexOf(" ") >= 0) {
    parts.push("space");
  }
  return parts.join(" ");
}
