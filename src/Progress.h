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

/**
 * @brief Where the left edge of the label sits relative to the screen, in
 * pixels, while a label longer than the screen is being pressed.
 *
 * @param progressWidth width of the characters already pressed. This is
 *        where on the label strip the press currently is.
 * @param totalWidth width of the whole label strip.
 * @param screenWidth width of the glass.
 *
 * The label is drawn as one strip and the screen is a window onto it; the
 * return value is how far the strip has slid to the left. Zero means the
 * label starts at the left edge, which is where it stays for anything that
 * fits.
 *
 * Two rules, in order. The press is kept at the middle of the glass once it
 * has travelled that far, so the character going down is always visible.
 * Then the strip is stopped at the right edge, because sliding further would
 * only bring blank space on. Never negative: a label barely wider than the
 * screen would otherwise be pushed off to the left at the end.
 *
 * Pure arithmetic, so the host test suite can reach it. It was written
 * inline in the middle of Display::renderProgress, between two loops over
 * the label and a dozen u8g2 calls, where nothing could test it.
 */
inline int scrollOffset(int progressWidth, int totalWidth, int screenWidth) {
  if (totalWidth <= screenWidth) {
    return 0;
  }
  int offset = 0;
  if (progressWidth > screenWidth / 2) {
    offset = progressWidth - screenWidth / 2;
  }
  if (totalWidth - offset < screenWidth) {
    offset = totalWidth - screenWidth;
  }
  return offset < 0 ? 0 : offset;
}
