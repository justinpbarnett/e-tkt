#pragma once

#include <Arduino.h>
#include <U8g2lib.h>
#include <qrcode.h>

#include <mutex>

#include "Display.h"
#include "Sound.h"

/**
 * @brief The Display adapter for the machine's own screen: a 128x64 SSD1306
 * OLED on I2C.
 *
 * Only the composition root names this type. It adds the one thing the job
 * runner never asks for, the boot splash, which plays a tune as it draws.
 */
class OledDisplay : public Display {
 private:
  // Handed in and not owned, like every other driver -- see LabelMaker.cpp.
  // U8G2 gets no interface of its own: this class reaches for upwards of
  // thirty of its methods, and nothing else would sit behind them. The seam
  // is one level up, at Display, where this class and FakeDisplay are the
  // two adapters.
  U8G2_SSD1306_128X64_NONAME_F_HW_I2C* u8g2;
  Sound* sound;
  const int QRcode_Version = 3;   //  set the version (range 1->40)
  QRCode* qrcode = new QRCode();  //  create the QR code

  // What the idle screen says about how the machine is reached, and whether
  // that has changed since the screen was last drawn. The link's task writes
  // them and the job runner's reads them, so they are behind the lock.
  std::mutex lock;
  ConnectionInfo info;
  bool changed = false;

  /**
   * @brief Paints every pixel the given colour and leaves the draw colour set
   * to its opposite, so whatever is drawn next shows up against it.
   *
   * Private: every screen in this class starts with it and nothing outside
   * has ever called it.
   */
  void clear(int color = 0);

 public:
  OledDisplay(Sound* sound, U8G2_SSD1306_128X64_NONAME_F_HW_I2C* u8g2);
  ~OledDisplay();
  void initialize() override;

  /**
   * Renders the E-TKT logo, and plays the startup melody.  Blocks until the
   * animation is complete.
   */
  void playSplashScreen();

  void render(Screen screen) override;
  void renderIdle(bool stopped) override;
  void setConnectionInfo(const ConnectionInfo& info) override;
  bool takeConnectionChange() override;
  void renderProgress(int charactersDone, const String& label, int copy,
                      int copies) override;
  void renderSaved(const Calibration& saved) override;
};
