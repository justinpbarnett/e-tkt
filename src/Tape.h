#pragma once

#include "Configuration.h"

// How much tape a job takes, and what that leaves on the roll.
//
// Pure arithmetic, no Arduino, no Preferences, so the host test suite can
// reach it. The machine cannot see the tape, so every number here is a count
// of feeds turned into millimetres -- an estimate, and only as good as
// FEED_LENGTH_UM and the length the roll was declared at.
//
// The panel does the same sums to say how many labels will fit before the
// roll runs out. It is served the numbers -- the lead, the feed length, the
// roll -- from /api/capabilities and /api/status rather than keeping copies,
// but labelFeeds() itself is restated in data/tape.js. The worked cases in
// test/vectors/tape.json hold both copies to the same answers.

// The blank feed before a label's first character. It leaves a margin ahead
// of the text for the cut at the end of the previous label.
constexpr int LEAD_FEEDS = 1;

// How many feeds loading a new roll takes to pull the tape through the
// mechanism, from the cog to past the cutter.
constexpr int REEL_FEEDS = 16;

/**
 * @brief The blank feeds added after a short label's last character.
 *
 * A label shorter than MIN_LABEL_CHARACTERS is topped up to it, so there is
 * something to take hold of when the tape is cut. A one-character label is
 * left alone deliberately: it is the single-letter tag the machine has always
 * printed short.
 *
 * @param labelLength characters in the label, spaces included, in UTF-8 code
 *        points rather than bytes.
 */
inline int topUpFeeds(int labelLength) {
  if (labelLength >= MIN_LABEL_CHARACTERS || labelLength == 1) {
    return 0;
  }
  return MIN_LABEL_CHARACTERS - (labelLength < 0 ? 0 : labelLength);
}

/**
 * @brief Every feed one label takes: the lead, one per character -- a space
 * is a feed with no press -- and the top-up. The cut takes none.
 */
inline int labelFeeds(int labelLength) {
  const int characters = labelLength < 0 ? 0 : labelLength;
  return LEAD_FEEDS + characters + topUpFeeds(labelLength);
}

/**
 * @brief How much tape a number of feeds pulls through, in millimetres,
 * rounded down.
 *
 * In 64 bits: a feed count times a length in micrometres passes 32 bits at
 * about 537,000 feeds. No roll comes near that, but the count is only reset
 * by loading a new one, and a wider multiply is cheaper than the argument
 * that it can never happen.
 */
inline long long tapeUsedMm(long long feeds) {
  if (feeds <= 0) {
    return 0;
  }
  return feeds * FEED_LENGTH_UM / 1000;
}

/**
 * @brief What is left on a roll that started at rollLengthMm after the given
 * feeds, in millimetres. Never negative: past the end of the roll the
 * estimate says empty, rather than owing tape.
 */
inline long long remainingMm(long long rollLengthMm, long long feeds) {
  const long long left = rollLengthMm - tapeUsedMm(feeds);
  return left < 0 ? 0 : left;
}

/**
 * @brief How many labels of this length fit in what is left on the roll.
 *
 * This is what the panel means by printing to the end of the roll, worked
 * out there from the roll /api/status reports. Rounded down: a label that
 * would run off the end of the tape is not one that fits.
 */
inline long long labelsThatFit(long long leftMm, int labelLength) {
  const long long perLabelUm =
      (long long)labelFeeds(labelLength) * FEED_LENGTH_UM;
  if (leftMm <= 0 || perLabelUm <= 0) {
    return 0;
  }
  return leftMm * 1000 / perLabelUm;
}

/**
 * @brief Whether a roll may be declared at this length, in millimetres.
 */
inline bool isValidRollLength(long long lengthMm) {
  return lengthMm >= ROLL_LENGTH_MIN_MM && lengthMm <= ROLL_LENGTH_MAX_MM;
}

/**
 * @brief Whether one request may ask for this many labels.
 */
inline bool isValidCopies(long long copies) {
  return copies >= 1 && copies <= MAX_COPIES;
}
