#pragma once

#include <Arduino.h>
#include <U8g2lib.h>
#include <qrcode.h>

#include "Characters.h"
#include "Configuration.h"
#include "Sound.h"
#include "Utility.h"
#include "etktLogo.h"


#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

const String STARTUP_MELODY = "  E.TKT ";
const String AUTHOR_SIGNATURE = "andrei.cc";

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
 * in Display.cpp holds the word, the icon and where both sit, and one
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
  TESTING,
  FINISHED,
  REBOOTING,
};

/**
 * Renders various screens on the OLED device, such asprinting progress, 
 * boot splash animation, QR Code, etc. Its hard coded to use a 128x64 OLED,
 * but could be sub-classed to use different sized screens.
 */
class Display {
 private:
  // Handed in and not owned, like every other driver -- see LabelMaker.cpp.
  // This one keeps its concrete type rather than getting an interface of its
  // own. U8G2 has upwards of thirty methods that Display reaches for, no
  // second adapter is in prospect, and a thirty-method interface with one
  // implementation behind it buys nothing. The seam here is where the screen
  // gets built, not a place to substitute a different one.
  U8G2_SSD1306_128X64_NONAME_F_HW_I2C* u8g2;
  Characters* characters;
  Sound* sound;
  const int QRcode_Version = 3;  //  set the version (range 1->40)
  const int QRcode_ECC =
      2;  //  set the Error Correction level (range 0-3) or symbolic (ECC_LOW,
          //  ECC_MEDIUM, ECC_QUARTILE and ECC_HIGH)
  QRCode* qrcode = new QRCode();  //  create the QR code
  String ssid = "";
  String ip = "";

  /**
   * @brief Paints every pixel the given colour and leaves the draw colour set
   * to its opposite, so whatever is drawn next shows up against it.
   *
   * Private: every screen in this class starts with it and nothing outside
   * has ever called it.
   */
  void clear(int color = 0);

 public:
  Display(Sound* sound, Characters* characters,
          U8G2_SSD1306_128X64_NONAME_F_HW_I2C* u8g2);
  ~Display();
  void initialize();

  /**
   * Renders the E-TKT logo, and plays the startup melody.  Blocks until the
   * animation is comeplete.
   */
  void playSplashScreen();

  /**
   * @brief Shows one of the fixed screens and returns as soon as it is on the
   * glass.
   *
   * It does not wait afterwards. Two of these screens are meant to be looked
   * at for a few seconds; SAVED_SCREEN_MS and REBOOT_SCREEN_MS say how long,
   * and the caller does the waiting, because how long a person stares at a
   * confirmation is not something a renderer should decide.
   */
  void render(Screen screen);

  /**
   * Renders a screen with a QR code and high level info abotu the device.  Thsi
   * is the screen displayed msot ofte, when the device is idle.
   */
  void renderIdle();

  /**
   * Upadtes network info for display on the idle screen.
   */
  void setConnectionInfo(String ip, String ssid);

  /**
   * Renders print progress on the screen, for use in the middle of printing a
   * label.
   *
   * @param charactersDone how many characters have finished pressing. A
   *        count, not an index, and the same number ETKT feeds to
   *        progressPercent(), so the OLED caption and the web UI cannot
   *        disagree.
   */
  void renderProgress(int charactersDone, String label);

  /**
   * @brief Renders the save confirmation, showing the two values that were
   * just written to EEPROM.
   *
   * Its own method rather than a Screen, because it is the one fixed screen
   * that carries numbers. Returns immediately; the caller waits
   * SAVED_SCREEN_MS.
   */
  void renderSaved(int align, int force);
};
