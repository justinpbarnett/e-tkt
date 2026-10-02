#pragma once

#include <Arduino.h>

#include "ArduinoJson.h"

// A JSON document as text, and a text as a JSON document.
//
// The Api sends every reply out as the first. The machine keeps two things
// in EEPROM as one JSON entry each, the last run and the networks it
// remembers, and those are written as the first and read back as the second.

/**
 * @brief The document, written out as text.
 */
String jsonText(const JsonDocument& doc);

/**
 * @brief The text, read into a document with room for it.
 *
 * `structureBytes` is the room for the arrays and objects of the text, as
 * JSON_ARRAY_SIZE() and JSON_OBJECT_SIZE() count it. The room for its strings
 * is added here.
 *
 * @return the document. It has nothing in it for a text that is not JSON, or
 * that has more in it than there is room for.
 */
DynamicJsonDocument parsedJson(const String& text, size_t structureBytes);
