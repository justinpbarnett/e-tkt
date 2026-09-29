#include "CharacterSet.h"

#include <Arduino.h>

#include <map>

#include "Utility.h"
// Last: it defines one- and two-letter macros such as A4 and E5, which would
// turn any later header that uses those names into nonsense.
#include "pitches.h"

// One copy, in one translation unit. These used to live in Characters.h, and
// a const map at namespace scope in a header is a separate object in every
// file that includes it -- eight of them here, each built again at startup.
const std::map<String, int> CHARACTERS = {
    {"$", 0},  {"-", 1},  {".", 2},  {"0", 26}, {"1", 20}, {"2", 3},  {"3", 4},
    {"4", 5},  {"5", 6},  {"6", 7},  {"7", 8},  {"8", 9},  {"9", 10}, {"*", 11},
    {"A", 12}, {"B", 13}, {"C", 14}, {"D", 15}, {"E", 16}, {"F", 17}, {"G", 18},
    {"H", 19}, {"I", 20}, {"J", 21}, {"K", 22}, {"L", 23}, {"M", 24}, {"N", 25},
    {"O", 26}, {"P", 27}, {"Q", 28}, {"R", 29}, {"S", 30}, {"T", 31}, {"U", 32},
    {"V", 33}, {"W", 34}, {"X", 35}, {"Y", 36}, {"Z", 37}, {"♡", 38}, {"☆", 39},
    {"♪", 40}, {"€", 41}, {"@", 42}};

const std::map<String, String> CHARACTER_ALIASES = {{"0", "O"}, {"1", "I"}};

// The note each slot sounds, indexed by slot. The cut mark's slot has none.
static const int NOTES[] = {G4, G6, A4, D4, E4, F4, G5, A5, B5, C5, D5,
                            0,  E5, F5, C6, D6, E6, F6, A6, B6, C4, C7,
                            D7, E7, F7, G7, B4, A7, B7, C8, D8, C3, D3,
                            E3, F3, G3, A3, B3, E2, F2, G2, A2, B2};

static_assert(sizeof(NOTES) / sizeof(NOTES[0]) == WHEEL_SLOT_COUNT,
              "every wheel slot needs a note in NOTES");

int wheelSlot(const String& character) {
  const std::map<String, int>::const_iterator found =
      CHARACTERS.find(character);
  return found == CHARACTERS.end() ? -1 : found->second;
}

int characterNote(const String& character) {
  const int slot = wheelSlot(character);
  return slot < 0 ? 0 : NOTES[slot];
}

// The one rule, so that the list served to the webapp and the check the
// device runs on an incoming label cannot drift apart. A space earns its
// place here rather than in CHARACTERS: it is the feeder advancing with
// nothing pressed into it, so it has no slot to sit in.
static bool isPrintableCharacter(const String& character) {
  if (character == " ") {
    return true;
  }
  if (character == CUT_CHARACTER) {
    return false;
  }
  return CHARACTERS.find(character) != CHARACTERS.end();
}

String printableCharacters() {
  String printable = " ";
  for (std::map<String, int>::const_iterator it = CHARACTERS.begin();
       it != CHARACTERS.end(); ++it) {
    if (!isPrintableCharacter(it->first)) {
      continue;
    }
    printable += it->first;
  }
  return printable;
}

String unprintableCharacter(const String& label) {
  String printed = label;
  printed.toUpperCase();
  for (const String& character : Utility::characters(printed)) {
    if (!isPrintableCharacter(character)) {
      return character;
    }
  }
  return "";
}
