#pragma once

#include <Arduino.h>

#include "Calibration.h"

// How long the two confirmation screens stay up before whatever comes next
// replaces them. These used to be delay() calls inside the renderers, which
// meant Display decided how long its caller blocked.
#define SAVED_SCREEN_MS 3000
#define REBOOT_SCREEN_MS 2000

/**
 * @brief The fixed screens the machine shows while it is doing something.
 *
 * Every one of these was its own public method with the same nine lines
 * inside it and a different word in the middle. They are data now: the table
 * in OledDisplay.cpp holds the word, the icon and where both sit, and one
 * renderer draws all of them. Adding a screen is a row in that table.
 *
 * The two screens that carry values -- the idle screen and the save
 * confirmation -- are not in here, because a fixed-text table cannot express
 * them. Print progress is not either; it animates.
 */
enum class Screen {
  WIFI_RESET,
  CUTTING,
  FEEDING,
  REELING,
  UNLOADING,
  TESTING,
  FINISHED,
  NEW_ROLL,
  REBOOTING,
};

/**
 * @brief What the idle screen says about how the machine is reached.
 *
 * The link supervisor writes it and knows what each line means. The screen
 * only lays the three out.
 */
struct ConnectionInfo {
  // The network: the one the machine is on or is joining, or its own.
  String name = "";
  // The line under it: the machine's address there, the password of its own
  // network, or a word for what it is doing about one.
  String detail = "";
  // What the QR code holds: the address of the panel, or how to join the
  // machine's own network. Empty for no code.
  String qr = "";
};

inline bool operator==(const ConnectionInfo& a, const ConnectionInfo& b) {
  return a.name == b.name && a.detail == b.detail && a.qr == b.qr;
}

inline bool operator!=(const ConnectionInfo& a, const ConnectionInfo& b) {
  return !(a == b);
}

/**
 * @brief What the machine shows on its own screen, told in terms of the job
 * rather than of pixels.
 *
 * An interface, so the job runner can be built and tested off the board. Two
 * adapters sit behind it: OledDisplay, which draws on the 128x64 OLED the
 * machine carries, and FakeDisplay in test/fakes, which records what it was
 * asked to show.
 *
 * Every method draws and returns. None of them waits: how long a screen
 * stays up is its caller's decision. The idle screen used to wait a second
 * and every screen a tenth of one more, which the job runner spent holding
 * the machine after it was parked.
 */
class Display {
 public:
  virtual ~Display() {}

  /**
   * @brief Starts the screen and blanks it.
   *
   * Once, at boot. Starting it again blanks the glass and sets its contrast
   * back, so no job does.
   */
  virtual void initialize() = 0;

  /**
   * @brief Shows one of the fixed screens and returns as soon as it is on the
   * glass.
   *
   * It does not wait afterwards. Two of these screens are meant to be looked
   * at for a few seconds; SAVED_SCREEN_MS and REBOOT_SCREEN_MS say how long,
   * and the caller does the waiting, because how long a person stares at a
   * confirmation is not something a renderer should decide.
   */
  virtual void render(Screen screen) = 0;

  /**
   * @brief Renders the screen the machine shows most often, when it is idle:
   * how it is reached, as setConnectionInfo() last said, with a QR code when
   * there is something to scan.
   *
   * @param stopped true when the job before was stopped partway. The screen
   *        then says "stopped" in place of "ready", with the square stop
   *        symbol in place of the tick, until the next job takes the screen
   *        over. An operator looking at the machine rather than the panel can
   *        tell a label that was cut short from one that finished.
   */
  virtual void renderIdle(bool stopped) = 0;

  /**
   * @brief Updates what the idle screen says about how the machine is
   * reached, for the next time it is drawn. It draws nothing.
   *
   * The one method here that is called from another task than the one that
   * draws: the link changes while the machine prints, and the link's task is
   * the one that knows.
   */
  virtual void setConnectionInfo(const ConnectionInfo& info) = 0;

  /**
   * @brief Whether setConnectionInfo() has said anything since this was last
   * asked, or since the idle screen was last drawn. An idle machine asks, and
   * draws its idle screen again when it has.
   */
  virtual bool connectionChanged() = 0;

  /**
   * @brief Renders print progress, for use in the middle of printing a label.
   *
   * @param charactersDone how many characters have finished pressing. A
   *        count, not an index, and the same number ETKT feeds to
   *        progressPercent(), so the OLED caption and the web UI cannot
   *        disagree.
   * @param copy which label of a run this is, counting from 1.
   * @param copies how many labels the run is. Above one, the screen says
   *        which of them is being pressed, "3/10", opposite the percentage.
   */
  virtual void renderProgress(int charactersDone, const String& label, int copy,
                              int copies) = 0;

  /**
   * @brief Renders the save confirmation, showing the calibration that was
   * just written to EEPROM.
   *
   * Its own method rather than a Screen, because it is the one fixed screen
   * that carries numbers. Returns immediately; the caller waits
   * SAVED_SCREEN_MS.
   */
  virtual void renderSaved(const Calibration& saved) = 0;
};
