#include "Characters.h"

#include <Arduino.h>
#include <U8g2lib.h>

#include <map>

#include "pitches.h"

// One copy, in one translation unit, the way CHARACTERS already is in
// CharacterSet.cpp. Both used to be const at namespace scope in
// Characters.h, which gives them internal linkage: a separate object in each
// of the seven files that include it, each built again at startup, and for
// the symbol map four heap allocations each that nothing ever freed.
//
// The note each wheel slot sounds, indexed by slot. A zero is a slot with
// nothing to sound.
static const int NOTES[] = {G4, G6, A4, D4, E4, F4, G5, A5, B5, C5, D5,
                            0,  E5, F5, C6, D6, E6, F6, A6, B6, C4, C7,
                            D7, E7, F7, G7, B4, A7, B7, C8, D8, C3, D3,
                            E3, F3, G3, A3, B3, E2, F2, G2, A2, B2, 0};

// For each non-ascii "glyph" character, the font, symbol code, width, x
// offset and y offset it is drawn with. The offsets align the glyph with the
// rest of the label text, which comes from a font that spaces differently.
//
// Held by value. A map of pointers meant a new per entry per translation
// unit, and getFont() copied the pointee on the way out anyway.
static const std::map<String, FontInfo> GLYPHS = {
    {"♡", FontInfo(u8g2_font_6x12_t_symbols, 0x2664, 5, -1, -1)},
    {"☆", FontInfo(u8g2_font_6x12_t_symbols, 0x2605, 5, -1, -1)},
    {"♪", FontInfo(u8g2_font_siji_t_6x10, 0xE271, 5, -3, 0)},
    {"€", FontInfo(u8g2_font_6x12_t_symbols, 0x20AC, 6, -1, -1)}};

void Characters::initialize() {
  // Iterate over the keys in CHARACTERS and return the maximum
  // value.  This is the number of characters in the wheel.
  this->characterCount = 0;
  for (auto const& x : CHARACTERS) {
    if (x.second > this->characterCount) {
      this->characterCount = x.second;
    }
  }

  this->characterCount++;
}

int Characters::getCharacterIndex(String character) {
  auto candidate = CHARACTERS.find(character);
  if (candidate == CHARACTERS.end()) {
    return -1;
  }
  return candidate->second;
}

FontInfo Characters::getFont(String character) {
  auto candidate = GLYPHS.find(character);
  if (candidate == GLYPHS.end()) {
    return FontInfo(u8g2_font_6x13_te, 0, 7, 0, 0);
  }
  return candidate->second;
}

int Characters::getCharacterFrequency(String character) {
  auto index = this->getCharacterIndex(character);
  if (index < 0) {
    return 0;
  }
  return NOTES[index];
}

int Characters::getWheelCharacterCount() { return this->characterCount; }