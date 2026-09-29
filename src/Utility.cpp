#include "Utility.h"

#include <Arduino.h>

// How many bytes the UTF-8 character starting at `position` says it has.
// See https://en.wikipedia.org/wiki/UTF-8#Encoding
static int utf8CharLength(const String& str, int position) {
  // Cast, do not assume. The leading byte of a multi-byte character has its
  // high bit set, and these shifts only give the right answer if that bit is
  // not a sign bit. char happens to be unsigned on the xtensa toolchain, so
  // this worked on the device, but it is signed on the host that runs the
  // tests -- and there every one of the wheel's symbols came back one byte
  // long, which would have walked a label straight through the middle of a
  // heart.
  const int start = (unsigned char)str[position];
  if (start >> 3 == 30) {
    return 4;
  } else if (start >> 4 == 14) {
    return 3;
  } else if (start >> 5 == 6) {
    return 2;
  } else {
    return 1;
  }
}

std::vector<String> Utility::characters(const String& text) {
  // One pass. The walk this replaced found the i-th character by counting
  // from the start of the label every time, so every loop over a label
  // walked it once per character.
  std::vector<String> out;
  unsigned int position = 0;
  while (position < text.length()) {
    const int bytes = utf8CharLength(text, position);
    // substring() stops at the end of the text, so a character cut short
    // comes back as the bytes that are there.
    out.push_back(text.substring(position, position + bytes));
    position += bytes;
  }
  return out;
}
