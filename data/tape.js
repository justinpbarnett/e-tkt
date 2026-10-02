// How much tape a label takes, and what that leaves on the roll.
//
// The same sums as src/Tape.h, over the numbers api/capabilities serves, so
// the page can say how many labels fit before the roll runs out. The worked
// cases in test/vectors/tape.json hold both to the same answers. The machine
// cannot see the tape: every length here is a count of feeds, and an
// estimate. Each function takes those numbers as `device`, the capabilities
// as readCapabilities() in status.js returns them.

// A label shorter than the minimum is topped up to it with blank feeds, so
// there is something to take hold of when the tape is cut -- except a
// one-character label, which the machine has always printed short.
function topUpFeeds(length, device) {
  if (length >= device.label.minimum || length === 1) {
    return 0;
  }
  return device.label.minimum - Math.max(length, 0);
}

// The lead, one per character -- a space is a feed with no press -- and the
// top-up. The cut takes none.
function labelFeeds(length, device) {
  return device.feed.lead + Math.max(length, 0) + topUpFeeds(length, device);
}

export function labelLengthMm(length, device) {
  return (labelFeeds(length, device) * device.feed.length_um) / 1000;
}

// Rounded down: a label that would run off the end of the tape is not one
// that fits.
export function labelsThatFit(remainingMm, length, device) {
  const perLabelUm = labelFeeds(length, device) * device.feed.length_um;
  if (remainingMm <= 0 || perLabelUm <= 0) {
    return 0;
  }
  return Math.floor((remainingMm * 1000) / perLabelUm);
}

// What the last api/status said is left on the roll, in millimetres, or
// null before there is a status to say it.
export function tapeLeftMm(status) {
  const roll = status && status.roll;
  return roll && Number.isFinite(roll.remaining_mm) ? roll.remaining_mm : null;
}

// A length of tape, in whole millimetres under a metre and in metres to the
// centimetre from there. A label is a few centimetres, so anything coarser
// could be a good share of one out. Rounded down, so what is left is never
// more than the device's own estimate. It counts from whole micrometres,
// the unit a feed is measured in, so a sum that floating point leaves a
// hair short of a whole unit is not rounded down past it.
export function formatLength(mm) {
  const um = Math.max(Math.round(mm * 1000), 0);
  if (um < 1000000) {
    return withUnit(Math.floor(um / 1000), "mm");
  }
  return withUnit(Math.floor(um / 10000) / 100, "m");
}

// A roll length as typed into Setup's dialog, in metres, which is how people
// measure a roll: the length in millimetres to load the roll at, or null
// while it is not one the device takes, and whether the minus and the plus
// still have somewhere to go.
export function typedRoll(text, device) {
  const typedMm = millimetres(text);
  const roll = device.roll;
  const takes = typedMm !== null && typedMm >= roll.minimum_mm && typedMm <= roll.maximum_mm;
  return {
    lengthMm: takes ? typedMm : null,
    lessAvailable: typedMm === null || typedMm > roll.minimum_mm,
    moreAvailable: typedMm === null || typedMm < roll.maximum_mm,
  };
}

// What the roll length's minus and plus move it by, in millimetres. Rolls
// come in whole and half metres; anything in between can still be typed.
const ROLL_STEP_MM = 500;

// Where the minus (-1) or the plus (1) takes a roll length typed into Setup's
// dialog, in millimetres. It moves to the next half metre up or down, rather
// than by half a metre from wherever the field is, so a typed 2.7 steps to 3
// and not to 3.2.
export function steppedRollLength(text, direction, device) {
  const roll = device.roll;
  const current = millimetres(text) ?? roll.default_mm;
  const next =
    direction > 0
      ? (Math.floor(current / ROLL_STEP_MM) + 1) * ROLL_STEP_MM
      : (Math.ceil(current / ROLL_STEP_MM) - 1) * ROLL_STEP_MM;
  return clamp(next, roll.minimum_mm, roll.maximum_mm);
}

// The length Setup's dialog offers a new roll at, in millimetres: the length
// the last roll went in at, since most rolls are the same as the one before,
// or a new roll's length before there is a status to say.
export function offeredRollLength(status, device) {
  const roll = device.roll;
  const last = status && status.roll ? status.roll.length_mm : roll.default_mm;
  return clamp(last, roll.minimum_mm, roll.maximum_mm);
}

// What Setup shows of the roll in the machine: the tape left, the length it
// went in at, and the share of that left, which is low under a tenth. Null
// before there is a status to say.
export function rollGauge(status) {
  const roll = status && status.roll;
  if (!roll) {
    return null;
  }
  const share = roll.length_mm > 0 ? clamp(roll.remaining_mm / roll.length_mm, 0, 1) : 0;
  return {
    left: formatLength(roll.remaining_mm),
    of: "left of " + formatLength(roll.length_mm),
    share: share,
    low: share < 0.1,
  };
}

// What Setup's dialog says of the lengths a roll can be loaded at.
export function rollRange(device) {
  const roll = device.roll;
  return (
    "From " +
    roll.minimum_mm / 1000 +
    " to " +
    withUnit(roll.maximum_mm / 1000, "m") +
    ". A new roll is usually " +
    formatLength(roll.default_mm) +
    "."
  );
}

// The value, moved into min..max when it is outside. The other modules take
// it from here: this one imports nothing, so importing it cannot make a
// cycle, and a file of its own would be one more request for the device.
export function clamp(value, min, max) {
  return Math.min(Math.max(value, min), max);
}

// A number and its unit, with a space between them that a line does not
// break at: "2.95" at the end of one line and "m left." at the start of the
// next read as two things. The other modules take it from here, as they take
// clamp().
export function withUnit(value, unit) {
  return value + " " + unit;
}

// Metres typed, as whole millimetres, or null when what is typed is not a
// number. Nothing typed is no length at all, not a length of zero.
function millimetres(text) {
  const trimmed = text.trim();
  const metres = Number(trimmed);
  return trimmed === "" || !Number.isFinite(metres) ? null : Math.round(metres * 1000);
}
