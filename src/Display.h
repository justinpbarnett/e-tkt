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

 public:
  Display(Sound* sound, Characters* characters,
          U8G2_SSD1306_128X64_NONAME_F_HW_I2C* u8g2);
  ~Display();
  void initialize();
  void clear(int color = 0);

  /**
   * Renders the E-TKT logo, and plays the startup melody.  Blocks until the
   * animation is comeplete.
   */
  void playSplashScreen();

  /**
   * Renders a screen asking you you to conenct to the soft-AP point for
   * configuration.
   */
  void renderConfig();

  /**
   * Displays a screen informing you that the device is about to reboot.
   */
  void renderReset();

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
   * Renders a brief info screen for after printing has completed.
   */
  void renderFinished();

  /**
   * Renders a screen informing of a on-off cut happening.
   */
  void renderCut();

  /**
   * Renders a screen informing of a reel in progress.
   */
  void renderReel();

  /**
   * Renders a screen informing that a test print is being done.
   */
  void renderTest(int a, int f);

  /**
   * Renders a screen ifnroming that settings are being saved.
   */
  void renderSettings(int a, int f);

  /**
   * Renders a screen ifnorming that a feed is in progress.
   */
  void renderFeed();

  /**
   * Renders a creen informing that a controlled reboot is about to happen.
   */
  void renderReboot();
};
