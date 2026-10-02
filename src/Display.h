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
  WIFI_SETUP,
  WIFI_RESET,
  CUTTING,
  FEEDING,
  REELING,
  UNLOADING,
  TESTING,
  FINISHED,
  WIFI_JOINING,
  NEW_ROLL,
  REBOOTING,
};

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
   * the network it is on, its address, and a QR code of that address.
   *
   * @param stopped true when the job before was stopped partway. The screen
   *        then says "stopped" in place of "ready", with the square stop
   *        symbol in place of the tick, until the next job takes the screen
   *        over. An operator looking at the machine rather than the panel can
   *        tell a label that was cut short from one that finished.
   */
  virtual void renderIdle(bool stopped) = 0;

  /**
   * @brief Updates the network the idle screen names and the address it
   * shows, for the next time it is drawn.
   */
  virtual void setConnectionInfo(const String& ip, const String& ssid) = 0;

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
