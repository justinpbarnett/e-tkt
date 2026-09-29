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
        self.assertFalse(cut.prints_run)
        self.assertFalse(cut.uses_roll_length)

    def test_only_a_tag_may_be_asked_for_more_than_one(self):
        # A run of any other command would be the same thing done again with
        # nothing to show for it.
        self.assertEqual(["tag"], [c.name for c in self.fw.commands
                                   if c.prints_run])

    def test_a_roll_length_is_declared_by_loading_a_new_roll(self):
        self.assertEqual(["reel"], [c.name for c in self.fw.commands
                                    if c.uses_roll_length])

    def test_every_command_but_saving_can_be_stopped(self):
        # A save moves nothing and ends in a reboot. The panel reads this
        # column to decide whether to offer its stop button at all.
        self.assertEqual(["save"], [c.name for c in self.fw.routes()
                                    if not c.stoppable])

    def test_a_tag_and_the_full_test_press_a_label(self):
        # The two a stop can leave a pressed label behind for, which the
        # stopped report then says is on the tape and not cut off.
        self.assertEqual({"tag", "testfull"},
                         {c.name for c in self.fw.commands
                          if c.presses_label})

    def test_an_unknown_command_has_no_row(self):
        # commandSpecByName() returns NULL for one; so does this.
        self.assertIsNone(self.fw.command("emboss"))

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

    # --- label length ------------------------------------------------------

    def test_minimum_label_length_comes_from_the_firmware(self):
        # The panel pads short labels up to this. It read 7 out of its own
        # source while the device called it 6.
        self.assertEqual(6, self.fw.min_label_characters)

    def test_maximum_label_length_comes_from_the_firmware(self):
        # What the device will accept in a request, which is two more than
        # the longest thing anyone can type: the panel centres a label by
        # padding a space onto each side before it sends it.
        self.assertEqual(249, self.fw.max_label_characters)

    # --- the roll ----------------------------------------------------------
    # The same values test/test_tape pins Tape.h to. The arithmetic is
    # restated here rather than parsed, so this is what holds the two
    # together.

    def test_the_roll_is_described_by_the_firmware(self):
        self.assertEqual(4000, self.fw.feed_length_um)
        self.assertEqual(1, self.fw.lead_feeds)
        self.assertEqual(16, self.fw.reel_feeds)
        self.assertEqual(3000, self.fw.default_roll_mm)
        self.assertEqual(500, self.fw.roll_min_mm)
        self.assertEqual(10000, self.fw.roll_max_mm)
        self.assertEqual(500, self.fw.max_copies)

    def test_a_label_is_a_lead_then_a_feed_per_character(self):
        # The panel pads every label to one past the minimum, so this is the
        # shortest thing it sends.
        self.assertEqual(8, self.fw.label_feeds(7))
        self.assertEqual(31, self.fw.label_feeds(30))

    def test_a_short_label_is_topped_up_to_the_minimum(self):
        self.assertEqual(3, self.fw.top_up_feeds(3))
        self.assertEqual(7, self.fw.label_feeds(3))
        self.assertEqual(0, self.fw.top_up_feeds(6))

    def test_a_single_letter_is_left_short(self):
        self.assertEqual(0, self.fw.top_up_feeds(1))
        self.assertEqual(2, self.fw.label_feeds(1))

    def test_an_empty_label_still_feeds_a_whole_minimum(self):
        self.assertEqual(7, self.fw.label_feeds(0))
        self.assertEqual(7, self.fw.label_feeds(-4))

    def test_what_is_left_is_the_roll_less_what_was_fed(self):
        self.assertEqual(0, self.fw.tape_used_mm(-5))
        self.assertEqual(4, self.fw.tape_used_mm(1))
        self.assertEqual(3000, self.fw.remaining_mm(3000, 0))
        self.assertEqual(2968, self.fw.remaining_mm(3000, 8))

    def test_an_overrun_roll_reads_empty_not_negative(self):
        self.assertEqual(0, self.fw.remaining_mm(3000, 750))
        self.assertEqual(0, self.fw.remaining_mm(3000, 790))

    def test_labels_that_fit_rounds_down(self):
        # 3 m at 32 mm a label is 93.75 labels. The 94th runs off the end.
        self.assertEqual(93, self.fw.labels_that_fit(3000, 7))
        self.assertEqual(1, self.fw.labels_that_fit(32, 7))
        self.assertEqual(0, self.fw.labels_that_fit(31, 7))

    def test_an_empty_roll_fits_nothing(self):
        self.assertEqual(0, self.fw.labels_that_fit(0, 7))
        self.assertEqual(0, self.fw.labels_that_fit(-10, 7))

    def test_the_copy_limit_never_cuts_a_roll_short(self):
        # The longest roll of the panel's shortest label comes in under it.
        fit = self.fw.labels_that_fit(self.fw.roll_max_mm,
                                      self.fw.min_label_characters + 1)
        self.assertLessEqual(fit, self.fw.max_copies)

    def test_what_a_request_may_say_is_bounded(self):
        self.assertFalse(self.fw.valid_copies(0))
        self.assertTrue(self.fw.valid_copies(1))
        self.assertTrue(self.fw.valid_copies(500))
        self.assertFalse(self.fw.valid_copies(501))
        self.assertFalse(self.fw.valid_roll_length(499))
        self.assertTrue(self.fw.valid_roll_length(500))
        self.assertTrue(self.fw.valid_roll_length(10000))
        self.assertFalse(self.fw.valid_roll_length(10001))

    # --- the panel ---------------------------------------------------------

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
                 '    {Command::CUT, "cut", NULL, CommandFact::STOPPABLE,\n'
                 '     &ETKT::cut},\n'
                 '    {Command::FEED, "feed", NULL, true, &f},\n'
                 '};')
        with self.assertRaises(firmware.FirmwareParseError) as caught:
            firmware.parse_commands(table)
        self.assertIn("2 commands but only 1", str(caught.exception))

    def test_a_fact_the_simulator_does_not_know_fails_the_table(self):
        # Read as false it would quietly be a command that, say, cannot be
        # stopped here while the device stops it.
        table = ('const CommandSpec ETKT::COMMANDS[] = {\n'
                 '    {Command::CUT, "cut", NULL,\n'
                 '     CommandFact::STOPPABLE | CommandFact::GLOWS,\n'
                 '     &ETKT::cut},\n'
                 '};')
        with self.assertRaises(firmware.FirmwareParseError) as caught:
            firmware.parse_commands(table)
        self.assertIn("CommandFact::GLOWS", str(caught.exception))


if __name__ == "__main__":
    unittest.main()
