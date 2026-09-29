#pragma once

/**
 * @brief An align and a force together: the pair one job presses at, from
 * its first character to its cut, and the pair a save writes.
 *
 * A job picks it once, as it begins, and every press of the job uses it: the
 * calibration being trialled for the two tests, and the saved one for
 * everything else. Before there was one, each press looked its own up, and
 * the full test pressed its characters at the align it was trialling and cut
 * at the saved one.
 *
 * In a header of its own because the printhead is not the only module that
 * takes one whole: the display shows the one a save wrote.
 */
struct Calibration {
  int align;
  int force;
};
