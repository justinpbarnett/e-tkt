#pragma once

#include <Arduino.h>
#include <U8g2lib.h>

#include <map>

#include "CharacterSet.h"
#include "pitches.h"

// CHARACTERS, CUT_CHARACTER, CHARACTER_ALIASES and printableCharacters()
// live in CharacterSet.h, which stays free of U8g2 so the host tests can
// reach them. They are part of this module's interface all the same.

const int NOTES[] = {G4, G6, A4, D4, E4, F4, G5, A5, B5, C5, D5, 0,  E5, F5, C6,
                     D6, E6, F6, A6, B6, C4, C7, D7, E7, F7, G7, B4, A7, B7, C8,
                     D8, C3, D3, E3, F3, G3, A3, B3, E2, F2, G2, A2, B2, 0};

struct FontInfo {
  const uint8_t* font;
  int code;
  int width;
  int width_offset;
  int height_offset;
  bool isSymbol() { return font != u8g2_font_6x13_te; }

  FontInfo(const uint8_t* font, int code, int width, int width_offset,
           int height_offset) {
    this->font = font;
    this->code = code;
    this->width = width;
    this->width_offset = width_offset;
    this->height_offset = height_offset;
  }
};

/**
 * For each non-ascii "glyph" character, maps it to a tuple of (font, symbol
 * code, width, x offset, y offset).  These values are used to align the redered
 * glyph with the rest of the label text which is from a font with different
 * spacing.
 */
const std::map<String, FontInfo*> GLYPHS = {
    {"♡", new FontInfo(u8g2_font_6x12_t_symbols, 0x2664, 5, -1, -1)},
    {"☆", new FontInfo(u8g2_font_6x12_t_symbols, 0x2605, 5, -1, -1)},
    {"♪", new FontInfo(u8g2_font_siji_t_6x10, 0xE271, 5, -3, 0)},
    {"€", new FontInfo(u8g2_font_6x12_t_symbols, 0x20AC, 6, -1, -1)}};
/**
 * @brief Manages the characters that can be printed on the
 * E-TKT.
*/
class Characters {
 private:
  int characterCount;

 public:
  void initialize();
  int getCharacterIndex(String character);
  FontInfo getFont(String character);
  int getWheelCharacterCount();
  int getCharacterFrequency(String character);
};
