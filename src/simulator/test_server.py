"""Checks that the simulator answers the way Api.cpp does.

test_firmware.py covers what the simulator reads out of src/. This covers
what it does with it: the requests it refuses and in what words, the runs of
labels and the stop, and the count of tape those leave on the roll. The
panel is built against these answers, so a difference here is a panel that
works on the simulator and not on the machine. Run it with

    python3 -m unittest discover -s src/simulator

It needs aiohttp, as the simulator does, and skips itself without it.
"""

import asyncio
import os
import sys
import unittest

try:
    from aiohttp.test_utils import TestClient, TestServer
except ImportError:
    raise unittest.SkipTest("the simulator's server needs aiohttp")

# server.py imports firmware.py relatively, so it has to be reached as part
# of the package -- which means the repository root on the path, not this
# folder the way test_firmware.py puts it.
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(
    os.path.abspath(__file__))))
sys.path.insert(0, ROOT)

from src.simulator import firmware, server  # noqa: E402  (needs the path)


class Gate:
    """Stands in for the simulator's waits. Open, every wait is over at once.
    Closed, the machine stops where it is until the test opens it again.
    close_after() shuts it partway into a job instead: that many more waits
    go through, and the one after them is held."""

    def __init__(self):
        self.opened = asyncio.Event()
        self.opened.set()
        self.holding = asyncio.Event()
        self.through = None

    def close_after(self, waits):
        self.through = waits

    async def held(self):
        """Returns once the machine is held at a wait."""
        await self.holding.wait()

    async def pause(self, seconds):
        if self.through is not None:
            if self.through == 0:
                self.opened.clear()
                self.through = None
            else:
                self.through -= 1
        if not self.opened.is_set():
            self.holding.set()
        await self.opened.wait()


def what_stopped(body):
    """What the status says the last stop cut short, less the two fields
    every stop here has: an id of its own, and the operator as its cause.
    Stops.test_every_stop_has_an_id_of_its_own covers those two."""
    stopped = dict(body["stopped"])
    del stopped["id"]
    del stopped["cause"]
    return stopped


def label_waits(label):
    """The waits one label takes: one for each character, one for the top-up
    and the cut, and one for the cut's last press, which no stop cuts short."""
    return len(label) + 2


class SimulatorTestCase(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        self.device = firmware.load()
        self.sim = server.Server(self.device)
        self.gate = Gate()
        self.sim.pause = self.gate.pause
        self.client = TestClient(TestServer(self.sim.application()))
        await self.client.start_server()

    async def asyncTearDown(self):
        self.gate.opened.set()
        await self.finish()
        await self.client.close()

    async def post(self, path, body=None):
        response = await self.client.post(path, json=body)
        return response.status, await response.json()

    async def status(self):
        response = await self.client.get("/api/status")
        return await response.json()

    async def finish(self):
        """Waits for whatever the simulator is running to be done."""
        task = self.sim.running_task
        if task is not None:
            await task

    def logged(self, message):
        return any(line.endswith(" " + message) for line in self.sim.log)


class Requests(SimulatorTestCase):
    async def test_copies_outside_the_range_are_refused_in_the_devices_words(
            self):
        status, body = await self.post("/api/tag", {"tag": "HELLO",
                                                    "copies": 0})
        self.assertEqual(400, status)
        self.assertEqual("Please provide a copies value between 1 and 500, "
                         "got 0", body["error"])
        status, body = await self.post("/api/tag", {"tag": "HELLO",
                                                    "copies": 501})
        self.assertEqual(400, status)
        self.assertIsNone(self.sim.command)

    async def test_copies_that_are_not_a_number_read_as_nothing(self):
        # What as<int>() makes of them, which the range then refuses.
        status, body = await self.post("/api/tag", {"tag": "HELLO",
                                                    "copies": "lots"})
        self.assertEqual(400, status)
        self.assertTrue(body["error"].endswith("got 0"))

    async def test_a_numeric_string_is_read_as_its_number(self):
        status, _ = await self.post("/api/tag", {"tag": "HI", "copies": "2"})
        self.assertEqual(200, status)
        self.assertEqual(2, self.sim.copies)

    async def test_a_roll_length_outside_the_range_is_refused(self):
        status, body = await self.post("/api/reel", {"length_mm": 20000})
        self.assertEqual(400, status)
        self.assertEqual("Please provide a length_mm value between 500 and "
                         "10000, got 20000", body["error"])

    async def test_a_field_a_command_does_not_read_is_ignored(self):
        # Only a tag reads copies, so a feed with one is one feed.
        status, _ = await self.post("/api/feed", {"copies": 0})
        self.assertEqual(200, status)

    async def test_a_body_that_is_not_an_object_carries_no_fields(self):
        status, body = await self.post("/api/tag", ["tag", "copies"])
        self.assertEqual(400, status)
        self.assertEqual("Please provide a tag value", body["error"])

    async def test_capabilities_say_how_a_label_uses_the_roll(self):
        response = await self.client.get("/api/capabilities")
        body = await response.json()
        self.assertEqual({"minimum": 1, "maximum": 500}, body["copies"])
        self.assertEqual({"minimum_mm": 500, "maximum_mm": 10000,
                          "default_mm": 3000}, body["roll"])
        self.assertEqual({"length_um": 4000, "lead": 1}, body["feed"])

    async def test_capabilities_state_every_fact_about_a_command(self):
        # The panel decides whether to offer a stop, and whether a status
        # counts a run, from these rows and never from a command's name.
        response = await self.client.get("/api/capabilities")
        commands = (await response.json())["commands"]
        self.assertEqual({"uses_align": False, "uses_force": False,
                          "label_field": "tag", "field_is_label": True,
                          "prints_run": True, "uses_roll_length": False,
                          "stoppable": True, "presses_label": True},
                         commands["tag"])
        self.assertFalse(commands["save"]["stoppable"])
        self.assertNotIn("idle", commands)


class Roll(SimulatorTestCase):
    async def test_a_fresh_device_has_a_full_default_roll(self):
        body = await self.status()
        self.assertEqual({"length_mm": 3000, "remaining_mm": 3000},
                         body["roll"])
        self.assertTrue(self.logged("Roll: 3000 mm, 0 feeds used"))

    async def test_every_label_of_a_run_is_charged_to_the_roll(self):
        await self.post("/api/tag", {"tag": " HELLO ", "copies": 3})
        await self.finish()
        used = 3 * self.device.label_feeds(7) * 4
        body = await self.status()
        self.assertEqual(3000 - used, body["roll"]["remaining_mm"])
        self.assertTrue(self.logged("print  HELLO  x 3"))
        self.assertTrue(self.logged("Printing Complete"))

    async def test_one_label_is_logged_without_a_count(self):
        await self.post("/api/tag", {"tag": "HI"})
        await self.finish()
        self.assertTrue(self.logged("print HI"))

    async def test_feeding_and_the_full_test_use_tape_too(self):
        await self.post("/api/feed", {})
        await self.finish()
        await self.post("/api/testfull", {"align": 5, "force": 5})
        await self.finish()
        body = await self.status()
        self.assertEqual(3000 - (1 + 7) * 4, body["roll"]["remaining_mm"])

    async def test_a_new_roll_starts_the_count_again(self):
        await self.post("/api/tag", {"tag": " HELLO "})
        await self.finish()
        await self.post("/api/reel", {"length_mm": 2500})
        await self.finish()
        body = await self.status()
        # Threading the new roll through to the cutter comes off it.
        self.assertEqual({"length_mm": 2500,
                          "remaining_mm": 2500 - 16 * 4}, body["roll"])
        self.assertTrue(self.logged("New roll: 2500 mm"))

    async def test_a_new_roll_with_no_length_is_as_long_as_the_last(self):
        await self.post("/api/reel", {"length_mm": 5000})
        await self.finish()
        await self.post("/api/reel", {})
        await self.finish()
        body = await self.status()
        self.assertEqual(5000, body["roll"]["length_mm"])


class Runs(SimulatorTestCase):
    async def test_status_says_which_label_of_the_run_is_printing(self):
        self.gate.opened.clear()
        await self.post("/api/tag", {"tag": "HI", "copies": 4})
        body = await self.status()
        self.assertEqual("tag", body["command"])
        self.assertEqual(1, body["copy"])
        self.assertEqual(4, body["copies"])
        self.assertNotIn("stop", body)

    async def test_a_run_is_not_reported_once_it_is_over(self):
        await self.post("/api/tag", {"tag": "HI", "copies": 2})
        await self.finish()
        body = await self.status()
        self.assertFalse(body["busy"])
        for field in ("copy", "copies", "stop", "stopped", "current_label"):
            self.assertNotIn(field, body)

    async def test_the_machine_is_busy_until_the_celebration_is_over(self):
        # The finish LED blinks and fades once the last label is cut, and
        # the device goes on saying so until it has.
        self.gate.close_after(label_waits("HI"))
        await self.post("/api/tag", {"tag": "HI"})
        await self.gate.held()
        self.assertTrue(self.logged("Printing Complete"))
        body = await self.status()
        self.assertTrue(body["busy"])
        self.assertEqual(99, body["progress"])

    async def test_a_run_holds_the_machine_until_its_last_label(self):
        self.gate.opened.clear()
        await self.post("/api/tag", {"tag": "HI", "copies": 2})
        status, body = await self.post("/api/feed", {})
        self.assertEqual(409, status)
        self.assertEqual(server.BUSY_MESSAGE, body["error"])


class StopsAfterTheLabel(SimulatorTestCase):
    async def test_the_run_ends_once_the_label_being_pressed_is_cut(self):
        self.gate.opened.clear()
        await self.post("/api/tag", {"tag": " HELLO ", "copies": 5})
        status, body = await self.post("/api/stop?after=label")
        self.assertEqual(200, status)
        self.assertEqual({"result": "stopping"}, body)
        self.assertEqual("after_label", (await self.status())["stop"])

        self.gate.opened.set()
        await self.finish()
        self.assertTrue(self.logged("Stopping after this label"))
        self.assertTrue(self.logged("Stopped after 1 of 5"))
        self.assertTrue(self.logged("Printing Complete"))
        body = await self.status()
        self.assertEqual(3000 - self.device.label_feeds(7) * 4,
                         body["roll"]["remaining_mm"])
        # Every label it printed is whole and cut, so nothing was cut short.
        self.assertNotIn("stopped", body)

    async def test_during_the_last_label_it_changes_nothing(self):
        self.gate.opened.clear()
        await self.post("/api/tag", {"tag": "HI"})
        await self.post("/api/stop?after=label")
        self.gate.opened.set()
        await self.finish()
        self.assertFalse(any("Stopped after" in line
                             for line in self.sim.log))

    async def test_it_is_forgotten_with_the_run_it_stopped(self):
        self.gate.opened.clear()
        await self.post("/api/tag", {"tag": "HI", "copies": 3})
        await self.post("/api/stop?after=label")
        self.gate.opened.set()
        await self.finish()
        await self.post("/api/tag", {"tag": "HI", "copies": 3})
        await self.finish()
        self.assertEqual(1, sum("Stopped after" in line
                                for line in self.sim.log))

    async def test_only_a_run_of_labels_can_stop_after_a_label(self):
        self.gate.opened.clear()
        await self.post("/api/feed", {})
        status, body = await self.post("/api/stop?after=label")
        self.assertEqual(409, status)
        self.assertEqual("Only a run of labels can stop after a label",
                         body["error"])
        self.assertNotIn("stop", await self.status())

    async def test_anything_but_label_after_it_is_refused_in_the_devices_words(
            self):
        # Checked before anything else, so an idle machine says so too.
        status, body = await self.post("/api/stop?after=cut")
        self.assertEqual(400, status)
        self.assertEqual("Please provide after=label to stop once the label "
                         "being pressed is cut, or leave it out to stop now",
                         body["error"])
        status, _ = await self.post("/api/stop?after")
        self.assertEqual(400, status)


class Stops(SimulatorTestCase):
    async def test_a_stop_ends_the_label_being_pressed(self):
        self.gate.opened.clear()
        await self.post("/api/tag", {"tag": " HELLO ", "copies": 3})
        status, body = await self.post("/api/stop")
        self.assertEqual(200, status)
        self.assertEqual({"result": "stopping"}, body)
        # Busy still, while the machine comes to rest, and saying why.
        body = await self.status()
        self.assertTrue(body["busy"])
        self.assertEqual("now", body["stop"])
        self.assertNotIn("stopped", body)

        self.gate.opened.set()
        await self.finish()
        self.assertTrue(self.logged("Stopping now"))
        self.assertTrue(self.logged("Stopped tag"))
        self.assertFalse(self.logged("Printing Complete"))
        body = await self.status()
        self.assertFalse(body["busy"])
        self.assertNotIn("stop", body)
        # The label being pressed is left on the tape, uncut.
        self.assertEqual({"command": "tag", "printed": 0, "copies": 3,
                          "unfinished": True}, what_stopped(body))
        # The lead, and the character that was being pressed.
        self.assertEqual(3000 - 2 * 4, body["roll"]["remaining_mm"])

    async def test_the_labels_already_cut_are_counted(self):
        # Held at the third character of the second label.
        self.gate.close_after(label_waits(" HELLO ") + 2)
        await self.post("/api/tag", {"tag": " HELLO ", "copies": 3})
        await self.gate.held()
        self.assertEqual(2, (await self.status())["copy"])
        await self.post("/api/stop")
        self.gate.opened.set()
        await self.finish()
        body = await self.status()
        self.assertEqual({"command": "tag", "printed": 1, "copies": 3,
                          "unfinished": True}, what_stopped(body))
        # The first label whole, then the second's lead and the three
        # characters it got to.
        used = self.device.label_feeds(7) + 1 + 3
        self.assertEqual(3000 - used * 4, body["roll"]["remaining_mm"])

    async def test_a_stop_during_the_cut_leaves_that_label_uncut(self):
        self.gate.close_after(len("HI"))
        await self.post("/api/tag", {"tag": "HI", "copies": 2})
        await self.gate.held()
        await self.post("/api/stop")
        self.gate.opened.set()
        await self.finish()
        body = await self.status()
        self.assertEqual({"command": "tag", "printed": 0, "copies": 2,
                          "unfinished": True}, what_stopped(body))
        self.assertEqual(3000 - self.device.label_feeds(2) * 4,
                         body["roll"]["remaining_mm"])

    async def test_a_stop_as_the_cut_comes_down_ends_the_run_between_labels(
            self):
        # Held at the last press of the first label's cut, which finishes its
        # stroke: the label is cut before the stop is seen.
        self.gate.close_after(label_waits("HI") - 1)
        await self.post("/api/tag", {"tag": "HI", "copies": 3})
        await self.gate.held()
        await self.post("/api/stop")
        self.gate.opened.set()
        await self.finish()
        self.assertTrue(self.logged("Stopped tag"))
        body = await self.status()
        # Cut short, but with nothing left on the tape.
        self.assertEqual({"command": "tag", "printed": 1, "copies": 3,
                          "unfinished": False}, what_stopped(body))
        self.assertEqual(3000 - self.device.label_feeds(2) * 4,
                         body["roll"]["remaining_mm"])

    async def test_after_the_last_cut_it_is_too_late_to_cut_anything_short(
            self):
        self.gate.close_after(label_waits("HI"))
        await self.post("/api/tag", {"tag": "HI"})
        await self.gate.held()
        status, body = await self.post("/api/stop")
        self.assertEqual({"result": "stopping"}, body)
        # Over at once, with the gate still shut: the celebration is all
        # that was left, and a stop ends it.
        await self.finish()
        body = await self.status()
        self.assertFalse(body["busy"])
        self.assertNotIn("stopped", body)
        self.assertTrue(self.logged("Printing Complete"))
        self.assertFalse(self.logged("Stopped tag"))

    async def test_it_overtakes_a_stop_after_the_label(self):
        self.gate.opened.clear()
        await self.post("/api/tag", {"tag": "HI", "copies": 3})
        await self.post("/api/stop?after=label")
        await self.post("/api/stop")
        self.assertEqual("now", (await self.status())["stop"])
        self.gate.opened.set()
        await self.finish()
        body = await self.status()
        self.assertEqual({"command": "tag", "printed": 0, "copies": 3,
                          "unfinished": True}, what_stopped(body))
        self.assertFalse(any("Stopped after" in line
                             for line in self.sim.log))

    async def test_any_job_but_saving_can_be_stopped(self):
        self.gate.opened.clear()
        await self.post("/api/feed", {})
        status, body = await self.post("/api/stop")
        self.assertEqual(200, status)
        self.gate.opened.set()
        await self.finish()
        self.assertTrue(self.logged("Stopped feed"))
        body = await self.status()
        # Only a run of labels says how far it got, and a feed presses no
        # label to leave behind.
        self.assertEqual({"command": "feed", "unfinished": False},
                         what_stopped(body))
        # Charged in full: the simulator cannot tell whether the feed had
        # begun.
        self.assertEqual(3000 - 1 * 4, body["roll"]["remaining_mm"])

    async def test_a_stopped_full_test_leaves_its_label_on_the_tape(self):
        self.gate.opened.clear()
        await self.post("/api/testfull", {"align": 5, "force": 5})
        await self.post("/api/stop")
        self.gate.opened.set()
        await self.finish()
        self.assertTrue(self.logged("Stopped testfull"))
        self.assertEqual({"command": "testfull", "unfinished": True},
                         what_stopped(await self.status()))

    async def test_every_stop_has_an_id_of_its_own(self):
        # Two stops that say the same thing, which the panel tells apart by
        # id: one dismissed does not hide the next.
        ids = []
        for _ in range(2):
            self.gate.opened.clear()
            await self.post("/api/feed", {})
            await self.post("/api/stop")
            self.gate.opened.set()
            await self.finish()
            stopped = (await self.status())["stopped"]
            self.assertEqual("operator", stopped["cause"])
            ids.append(stopped["id"])
        self.assertNotEqual(ids[0], ids[1])

    async def test_saving_cannot_be_stopped(self):
        self.gate.opened.clear()
        await self.post("/api/save", {"align": 5, "force": 5})
        status, body = await self.post("/api/stop")
        self.assertEqual(409, status)
        self.assertEqual("The command running now cannot be stopped",
                         body["error"])
        self.assertNotIn("stop", await self.status())

    async def test_what_it_stopped_is_forgotten_once_a_job_is_accepted(self):
        self.gate.opened.clear()
        await self.post("/api/feed", {})
        await self.post("/api/stop")
        self.gate.opened.set()
        await self.finish()
        self.assertIn("stopped", await self.status())
        # A refused request is not a job.
        await self.post("/api/tag", {"tag": "HI", "copies": 0})
        self.assertIn("stopped", await self.status())
        self.gate.opened.clear()
        await self.post("/api/feed", {})
        self.assertNotIn("stopped", await self.status())

    async def test_the_next_job_runs_to_the_end(self):
        self.gate.opened.clear()
        await self.post("/api/feed", {})
        await self.post("/api/stop")
        self.gate.opened.set()
        await self.finish()
        await self.post("/api/tag", {"tag": "HI"})
        await self.finish()
        self.assertTrue(self.logged("Printing Complete"))
        self.assertNotIn("stopped", await self.status())

    async def test_stopping_an_idle_machine_is_not_an_error(self):
        for path in ("/api/stop", "/api/stop?after=label"):
            status, body = await self.post(path)
            self.assertEqual(200, status)
            self.assertEqual({"result": "idle"}, body)


if __name__ == "__main__":
    unittest.main()
