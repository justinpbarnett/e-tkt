// Host-side tests for JsonText: a JSON document written out as text, and a
// text read back into a document.
//
// Every reply of the Api goes out through the first. The two things the
// machine keeps in EEPROM as one JSON entry each, the last run and the
// networks it remembers, go in through the first and come back through the
// second.
//
// Run with:  pio test -e native
#include <Arduino.h>
#include <unity.h>

#include "ArduinoJson.h"
#include "JsonText.h"

void setUp(void) {}
void tearDown(void) {}

void test_a_document_is_written_out_whole(void) {
  StaticJsonDocument<JSON_OBJECT_SIZE(3)> doc;
  doc["label"] = "ABC";
  doc["copies"] = 3;
  doc["cut"] = true;
  TEST_ASSERT_EQUAL_STRING("{\"label\":\"ABC\",\"copies\":3,\"cut\":true}",
                           jsonText(doc).c_str());
}

void test_a_text_is_read_with_room_for_every_string_in_it(void) {
  // The caller counts the arrays and objects, which it knows. The strings it
  // does not count: four networks with the longest name and the longest
  // password the radio takes are read whole.
  const String name = "The Longest Network Name Allowed";
  String password = "";
  for (int i = 0; i < 63; i++) {
    password += 'p';
  }
  const size_t structureBytes = JSON_ARRAY_SIZE(4) + 4 * JSON_OBJECT_SIZE(2);
  DynamicJsonDocument written(structureBytes);
  for (int i = 0; i < 4; i++) {
    JsonObject entry = written.createNestedObject();
    entry["ssid"] = name.c_str();
    entry["password"] = password.c_str();
  }

  const DynamicJsonDocument read =
      parsedJson(jsonText(written), structureBytes);
  TEST_ASSERT_FALSE(read.overflowed());
  TEST_ASSERT_EQUAL(4, read.as<JsonArrayConst>().size());
  TEST_ASSERT_EQUAL_STRING(name.c_str(), read[3]["ssid"].as<const char*>());
  TEST_ASSERT_EQUAL_STRING(password.c_str(),
                           read[3]["password"].as<const char*>());
}

void test_a_text_that_is_not_json_reads_as_nothing(void) {
  TEST_ASSERT_TRUE(parsedJson("", JSON_OBJECT_SIZE(1)).isNull());
  TEST_ASSERT_TRUE(parsedJson("label", JSON_OBJECT_SIZE(1)).isNull());
}

void test_a_text_cut_short_reads_as_nothing(void) {
  // The parser has the label by the time it finds that the text has no end.
  // A caller that asked for the label alone would take it for a whole entry.
  const DynamicJsonDocument doc =
      parsedJson("{\"label\":\"ABC\",\"copies\":", JSON_OBJECT_SIZE(2));
  TEST_ASSERT_TRUE(doc.isNull());
  TEST_ASSERT_FALSE(doc["label"].is<const char*>());
}

void test_a_text_with_more_in_it_than_there_is_room_for_reads_as_nothing(void) {
  // Forty members, read into the room for one.
  String text = "{";
  for (int i = 0; i < 40; i++) {
    text += String(i > 0 ? "," : "") + "\"m" + i + "\":0";
  }
  text += "}";
  TEST_ASSERT_TRUE(parsedJson(text, JSON_OBJECT_SIZE(1)).isNull());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_a_document_is_written_out_whole);
  RUN_TEST(test_a_text_is_read_with_room_for_every_string_in_it);
  RUN_TEST(test_a_text_that_is_not_json_reads_as_nothing);
  RUN_TEST(test_a_text_cut_short_reads_as_nothing);
  RUN_TEST(test_a_text_with_more_in_it_than_there_is_room_for_reads_as_nothing);
  return UNITY_END();
}
