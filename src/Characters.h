#pragma once

#include <Arduino.h>
#include <U8g2lib.h>

#include <map>

#include "CharacterSet.h"

// CHARACTERS, CUT_CHARACTER, CHARACTER_ALIASES and printableCharacters()
// live in CharacterSet.h, which stays free of U8g2 so the host tests can
// reach them. They are part of this module's interface all the same.
//
// The note each wheel slot sounds, and the font each symbol is drawn from,
// used to sit here as const arrays at namespace scope. const at namespace
// scope has internal linkage, so every file that included this header built
// its own copy at startup -- and the symbol map newed four FontInfos into
// each of them and never freed one. Nothing outside Characters.cpp reads
// either table, so both moved there.

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
