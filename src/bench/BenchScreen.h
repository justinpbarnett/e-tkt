// The screen, for the bench rigs.
//
// The screen keeps the last image it was sent for as long as it has power, so
// a rig that never draws on it shows the last rig's image. On machine 2 the
// self-test's "SELFTEST DONE" stayed up over a running servo rig and read as
// the servo rig having finished. Every rig names itself and its step here.
//
// Header only: each rig is a firmware of its own, built from its one file in
// src/bench/.

#pragma once

#include <Arduino.h>
#include <U8g2lib.h>

class BenchScreen {
 public:
  // Started as OledDisplay::initialize() starts it for the label maker. A
  // machine with no screen on the bus only goes unanswered.
  void begin() {
    this->screen.begin();
    this->screen.setContrast(8);
    this->screen.setDrawColor(1);
  }

  // A frame on the outermost pixels and two lines of up to 19 characters
  // inside it. A panel that is not a 128x64 SSD1306 (a 1.3" SH1106 sits two
  // columns off) shows a missing edge or a stripe of noise down one side.
  void show(const String& line1, const String& line2) {
    this->screen.clearBuffer();
    this->screen.drawFrame(0, 0, 128, 64);
    this->screen.setFont(u8g2_font_6x13_te);
    this->screen.drawStr(6, 26, line1.c_str());
    this->screen.drawStr(6, 44, line2.c_str());
    this->screen.sendBuffer();
  }

  // Every pixel lit, so a dark one stands out.
  void fill() {
    this->screen.clearBuffer();
    this->screen.drawBox(0, 0, 128, 64);
    this->screen.sendBuffer();
  }

 private:
  U8G2_SSD1306_128X64_NONAME_F_HW_I2C screen{U8G2_R0, U8X8_PIN_NONE};
};
