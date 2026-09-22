#pragma once

// The one definition of how far through a label the machine is.
//
// Pure arithmetic, no Arduino, no display, no network, so the host test suite
// can reach it. Three places used to derive this independently and two of
// them applied the same "not finished yet" correction, which is why the web
// UI always read one point below the OLED next to it.

// Printing is not done when the last character is pressed: the feed padding
// and the cut still have to happen. Holding back the final point keeps the
// user from reaching for tape the cutter has not reached yet.
constexpr int PROGRESS_MAX_WHILE_PRINTING = 99;

/**
 * @brief Percentage of a label that has been pressed, 0 to 99.
 *
 * @param charactersDone how many characters have finished pressing. This is
 *        a count, not an index: after the character at index i is pressed,
 *        i + 1 characters are done.
 * @param labelLength total characters in the label, in UTF-8 code points
 *        rather than bytes.
 *
 * Returns 0 for any input that cannot describe real progress, which includes
 * an empty label. The simulator used to divide by zero here.
 */
inline int progressPercent(int charactersDone, int labelLength) {
  if (charactersDone <= 0 || labelLength <= 0) {
    return 0;
  }
  const int percent = 100 * charactersDone / labelLength;
  return percent > PROGRESS_MAX_WHILE_PRINTING ? PROGRESS_MAX_WHILE_PRINTING
                                               : percent;
}
