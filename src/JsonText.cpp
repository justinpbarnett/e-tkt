#include "JsonText.h"

#include <vector>

String jsonText(const JsonDocument& doc) {
  // Into a buffer of the length the document says it comes to, so it cannot
  // come out short.
  std::vector<char> text(measureJson(doc) + 1);
  serializeJson(doc, text.data(), text.size());
  return String(text.data());
}

DynamicJsonDocument parsedJson(const String& text, size_t structureBytes) {
  // Every string in the text is copied into the document, and none is longer
  // there than it was in the text.
  DynamicJsonDocument doc(structureBytes + text.length());
  if (deserializeJson(doc, text.c_str(), text.length())) {
    // What was read before the text went wrong is not kept: a text that
    // cannot be read whole is not read in part.
    doc.clear();
  }
  return doc;
}
