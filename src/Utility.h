#pragma once

#include <Arduino.h>

#include <vector>

class Utility {
 public:
  /**
   * @brief Splits text into its characters, one per UTF-8 code point, in a
   * single pass.
   *
   * Every walk over a label goes through this: pressing it, drawing it,
   * playing it and checking it. The wheel's symbols are three bytes each, so
   * a label's bytes are not its characters.
   *
   * A character cut short by the end of the text comes back as the bytes
   * that are there, so a malformed label is refused by name rather than read
   * past its end.
   */
  static std::vector<String> characters(const String& text);
};
