"""Checks that the simulator answers the way Network.cpp does.

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
    Closed, the machine stops where it is until the test opens it again."""

    def __init__(self):
        self.opened = asyncio.Event()
        self.opened.set()

    async def pause(self, seconds):
        await self.opened.wait()


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
        self.assertFalse(body["stopping"])

    async def test_a_run_is_not_reported_once_it_is_over(self):
        await self.post("/api/tag", {"tag": "HI", "copies": 2})
        await self.finish()
        body = await self.status()
        self.assertFalse(body["busy"])
        for field in ("copy", "copies", "stopping", "current_label"):
            self.assertNotIn(field, body)

    async def test_a_stop_ends_the_run_after_the_label_being_pressed(self):
        self.gate.opened.clear()
        await self.post("/api/tag", {"tag": " HELLO ", "copies": 5})
        status, body = await self.post("/api/stop")
        self.assertEqual(200, status)
        self.assertEqual({"result": "stopping"}, body)
        self.assertTrue((await self.status())["stopping"])

        self.gate.opened.set()
        await self.finish()
        self.assertTrue(self.logged("Stopping after this label"))
        self.assertTrue(self.logged("Stopped after 1 of 5"))
        self.assertTrue(self.logged("Printing Complete"))
        body = await self.status()
        self.assertEqual(3000 - self.device.label_feeds(7) * 4,
                         body["roll"]["remaining_mm"])

    async def test_a_stop_during_the_last_label_changes_nothing(self):
        self.gate.opened.clear()
        await self.post("/api/tag", {"tag": "HI"})
        await self.post("/api/stop")
        self.gate.opened.set()
        await self.finish()
        self.assertFalse(any("Stopped after" in line
                             for line in self.sim.log))

    async def test_a_stop_is_forgotten_with_the_run_it_stopped(self):
        self.gate.opened.clear()
        await self.post("/api/tag", {"tag": "HI", "copies": 3})
        await self.post("/api/stop")
        self.gate.opened.set()
        await self.finish()
        await self.post("/api/tag", {"tag": "HI", "copies": 3})
        await self.finish()
        self.assertEqual(1, sum("Stopped after" in line
                                for line in self.sim.log))

    async def test_stopping_an_idle_machine_is_not_an_error(self):
        status, body = await self.post("/api/stop")
        self.assertEqual(200, status)
        self.assertEqual({"result": "idle"}, body)

    async def test_only_printing_can_be_stopped(self):
        self.gate.opened.clear()
        await self.post("/api/feed", {})
        status, body = await self.post("/api/stop")
        self.assertEqual(409, status)
        self.assertEqual("Only printing can be stopped", body["error"])

    async def test_a_run_holds_the_machine_until_its_last_label(self):
        self.gate.opened.clear()
        await self.post("/api/tag", {"tag": "HI", "copies": 2})
        status, body = await self.post("/api/feed", {})
        self.assertEqual(409, status)
        self.assertEqual(server.BUSY_MESSAGE, body["error"])


if __name__ == "__main__":
    unittest.main()
