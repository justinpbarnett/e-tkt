#include "CharacterSet.h"

#include <Arduino.h>

#include <map>

#include "Utility.h"

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
  const int length = Utility::utf8Length(printed);
  for (int i = 0; i < length; i++) {
    const String character = Utility::utf8CharAt(printed, i);
    if (!isPrintableCharacter(character)) {
      return character;
    }
  }
  return "";
}
