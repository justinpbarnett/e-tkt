"""Checks that the simulator still recognises the firmware's own tables.

No aiohttp and no device: this is the parsing half of the simulator, which is
where the drift the simulator exists to prevent would actually happen. Run it
with

    python3 -m unittest discover -s src/simulator

A failure here means one of the tables in src/ changed shape, and the
simulator would otherwise have quietly served fewer routes than the device.
"""

import os
import re
import sys
import unittest

# The simulator is a directory of scripts, not an installed package, so the
# only thing that makes `import firmware` resolve is this file's own folder
# being on the path. `unittest discover -s src/simulator` puts it there;
# running this file by its path, or discovering from the repository root,
# does not.
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import firmware  # noqa: E402  (needs the path above)


class LoadFirmware(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.fw = firmware.load()

    # --- the command table -------------------------------------------------

    def test_every_command_in_the_table_is_found(self):
        # One row per Command enumerator; ETKT.cpp static_asserts the same.
        self.assertEqual(10, len(self.fw.commands))

    def test_commands_are_named(self):
        names = [c.name for c in self.fw.commands]
        self.assertIn("tag", names)
        self.assertIn("testalign", names)
        self.assertIn("idle", names)

    def test_idle_is_not_something_you_can_post(self):
        idle = self.fw.command("idle")
        self.assertFalse(idle.runnable)

    def test_the_routes_are_the_runnable_commands(self):
        routes = [c.name for c in self.fw.routes()]
        self.assertNotIn("idle", routes)
        self.assertEqual(9, len(routes))

    def test_a_label_command_says_which_field_carries_it(self):
        self.assertEqual("tag", self.fw.command("tag").label_field)
        self.assertEqual("character", self.fw.command("move").label_field)
        self.assertIsNone(self.fw.command("cut").label_field)

    def test_a_tag_carries_a_label_and_a_move_names_a_slot(self):
        # The column that decides whether the field is checked against what
        # the wheel carries. A move names a slot on the wheel instead, the
        # cut mark included, so it is left to DaisyWheel::move().
        self.assertTrue(self.fw.command("tag").field_is_label)
        self.assertFalse(self.fw.command("move").field_is_label)
        self.assertFalse(self.fw.command("cut").field_is_label)

    def test_save_reads_both_calibration_fields(self):
        save = self.fw.command("save")
        self.assertTrue(save.uses_align)
        self.assertTrue(save.uses_force)

    def test_the_alignment_test_reads_align_but_not_force(self):
        # testalign presses at the minimum force by design, so a force in the
        # body is ignored rather than refused.
        spec = self.fw.command("testalign")
        self.assertTrue(spec.uses_align)
        self.assertFalse(spec.uses_force)

    def test_a_plain_command_reads_nothing(self):
        cut = self.fw.command("cut")
        self.assertFalse(cut.uses_align)
        self.assertFalse(cut.uses_force)
        self.assertIsNone(cut.label_field)

    # --- what a label may contain ------------------------------------------

    def test_the_printable_set_matches_the_device(self):
        # Same rule as printableCharacters(): a space, then every key in
        # CHARACTERS except the cut mark, in the order a std::map walks them.
        self.assertEqual(" $-.0123456789@ABCDEFGHIJKLMNOPQRSTUVWXYZ€"
                         "☆♡♪", self.fw.printable)

    def test_the_cut_mark_is_not_printable(self):
        self.assertNotIn("*", self.fw.printable)

    def test_a_printable_label_has_no_unprintable_character(self):
        # Mirrors unprintableCharacter(), which is what api/tag refuses on.
        self.assertEqual("", self.fw.unprintable_character("HELLO WORLD"))
        self.assertEqual("", self.fw.unprintable_character(""))

    def test_the_first_character_the_wheel_lacks_comes_back(self):
        self.assertEqual("?", self.fw.unprintable_character("HI?"))
        self.assertEqual("É", self.fw.unprintable_character("CAFÉ"))

    def test_a_typed_label_is_checked_in_the_case_it_prints_in(self):
        # The panel sends what was typed and the device upper-cases it.
        self.assertEqual("", self.fw.unprintable_character("hello world"))

    def test_the_cut_mark_is_not_allowed_in_a_label(self):
        self.assertEqual("*", self.fw.unprintable_character("A*B"))

    def test_the_aliases_are_read(self):
        self.assertEqual({"0": "O", "1": "I"}, self.fw.aliases)

    # --- the calibration range ---------------------------------------------

    def test_the_calibration_range_matches_the_device(self):
        self.assertEqual(1, self.fw.calibration_min)
        self.assertEqual(9, self.fw.calibration_max)


    def test_an_unknown_command_has_no_row(self):
        # commandSpecByName() returns NULL for one; so does this.
        self.assertIsNone(self.fw.command("emboss"))

    def test_the_range_is_what_a_value_is_checked_against(self):
        # Mirrors isValidCalibrationValue(), which is what actually refuses.
        self.assertFalse(self.fw.valid_calibration(0))
        self.assertTrue(self.fw.valid_calibration(1))
        self.assertTrue(self.fw.valid_calibration(9))
        self.assertFalse(self.fw.valid_calibration(10))

    # --- the numbers the device boots with ---------------------------------

    def test_the_startup_calibration_matches_the_device(self):
        # A fresh device has align 5 and force 1, not 5 and 5.
        self.assertEqual(5, self.fw.default_align)
        self.assertEqual(1, self.fw.default_force)

    # --- progress ----------------------------------------------------------

    def test_progress_holds_the_last_point_back(self):
        # Feeding and cutting still have to happen after the last character.
        self.assertEqual(99, self.fw.progress_max)
        self.assertEqual(99, self.fw.progress_percent(5, 5))

    def test_progress_counts_characters_done(self):
        self.assertEqual(0, self.fw.progress_percent(0, 4))
        self.assertEqual(50, self.fw.progress_percent(2, 4))

    def test_an_empty_label_is_no_progress_rather_than_a_crash(self):
        self.assertEqual(0, self.fw.progress_percent(0, 0))


    def test_minimum_label_length_comes_from_the_firmware(self):
        # The panel pads short labels up to this. It read 7 out of its own
        # source while the device called it 6.
        self.assertEqual(6, self.fw.min_label_characters)

    def test_maximum_label_length_comes_from_the_firmware(self):
        # What the device will accept in a request, which is two more than
        # the longest thing anyone can type: the panel centres a label by
        # padding a space onto each side before it sends it.
        self.assertEqual(249, self.fw.max_label_characters)

    def test_the_panel_knows_every_command_the_device_offers(self):
        # api/capabilities serves this list and data/script.js checks its
        # wording table against it at startup. The check is only worth
        # anything if the two are in step to begin with.
        here = os.path.dirname(os.path.abspath(__file__))
        panel = os.path.join(here, "..", "..", "data", "script.js")
        with open(panel, encoding="utf-8") as handle:
            source = handle.read()
        table = re.search(r"const COMMAND_LABELS = \{(.*?)\n\};", source,
                          re.S)
        self.assertIsNotNone(table, "no COMMAND_LABELS table in script.js")
        named = set(re.findall(r"^\s*(\w+):", table.group(1), re.M))
        self.assertEqual({spec.name for spec in self.fw.routes()}, named)


class Failures(unittest.TestCase):
    """The simulator refuses to start rather than serve a part device."""

    def test_a_missing_table_is_reported_with_the_file_it_looked_in(self):
        with self.assertRaises(firmware.FirmwareParseError) as caught:
            firmware.load(src_dir="/nonexistent")
        self.assertIn("nonexistent", str(caught.exception))

    def test_an_unrecognisable_table_is_reported(self):
        with self.assertRaises(firmware.FirmwareParseError):
            firmware.parse_commands("no table here at all")

    def test_one_unreadable_row_fails_the_whole_table(self):
        # Not eight commands and a shrug: a route quietly going missing is
        # the drift this module exists to catch.
        table = ('const CommandSpec ETKT::COMMANDS[] = {\n'
                 '    {Command::CUT, "cut", false, false, NULL, false,\n'
                 '     &ETKT::cut},\n'
                 '    {Command::FEED, "feed", false, false, NULL, 7, &f},\n'
                 '};')
        with self.assertRaises(firmware.FirmwareParseError) as caught:
            firmware.parse_commands(table)
        self.assertIn("2 commands but only 1", str(caught.exception))


if __name__ == "__main__":
    unittest.main()
