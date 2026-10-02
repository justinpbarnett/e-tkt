"""Checks that the simulator hands the panel's requests to the firmware.

The simulator is the firmware. main.cpp builds this machine's own job runner
and Api on the host, and server.py relays each request under /api/ to it and
each reply back. What a reply says is tested once, in test/test_api and
test/test_etkt. This covers the relay: that a request reaches the Api the way
the webserver on the device hands it over, and that the reply leaves the way
the device sends it. Run it from the repository root with

    python3 -m unittest discover -s src/simulator

It needs aiohttp, as the simulator does, and PlatformIO to build the
firmware, and skips itself without either.
"""

import asyncio
import contextlib
import io
import json
import os
import shutil
import subprocess
import sys
import unittest

try:
    from aiohttp import ClientTimeout
    from aiohttp.test_utils import TestClient, TestServer
except ImportError:
    raise unittest.SkipTest("the simulator's server needs aiohttp")

# server.py is reached as part of the package, which means the repository
# root on the path rather than this folder.
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(
    os.path.abspath(__file__))))
sys.path.insert(0, ROOT)

from src.simulator import server  # noqa: E402  (needs the path)

# How many times faster than the machine the simulated one runs. Fast enough
# that a label is over before a test could wait on it, and slow enough that a
# run of the longest label is still pressing when a test stops it.
SPEED = 1000

# How long a test waits for the machine to finish what it is doing. The
# longest job a test waits on is a few labels, which the machine takes a
# minute or so over, and SPEED makes a few hundredths of a second.
JOB_SECONDS = 10

# The firmware, built once for every test here.
PROGRAM = None


def setUpModule():
    global PROGRAM
    try:
        PROGRAM = server.build()
    except server.NoPlatformIO as missing:
        raise unittest.SkipTest(str(missing))


class RelayTestCase(unittest.IsolatedAsyncioTestCase):
    # How many times faster than the machine this case's runs.
    speed = SPEED

    # Where what the machine prints down its serial port goes. Nowhere,
    # unless a case reads it: it would bury the results.
    serial = asyncio.subprocess.DEVNULL

    async def asyncSetUp(self):
        self.device = server.Server(PROGRAM, self.speed, serial=self.serial,
                                    loses=self.loses)
        await self.device.start()
        self.client = TestClient(TestServer(self.device.application()))
        await self.client.start_server()

    async def asyncTearDown(self):
        await self.client.close()
        await self.device.close()

    def loses(self):
        """What the link loses of each request, as Server takes it: nothing,
        unless a case says otherwise."""
        return None

    async def status(self):
        response = await self.client.get("/api/status")
        self.assertEqual(200, response.status)
        return await response.json()

    async def until(self, path, there):
        """What the device answers at `path`, once `there` says the answer
        is the one a test is waiting for."""
        clock = asyncio.get_running_loop()
        deadline = clock.time() + JOB_SECONDS
        while True:
            response = await self.client.get(path)
            self.assertEqual(200, response.status)
            answer = await response.json()
            if there(answer):
                return answer
            if clock.time() > deadline:
                self.fail("%s is still not there after %d seconds: %r"
                          % (path, JOB_SECONDS, answer))
            await asyncio.sleep(0.01)

    async def until_idle(self):
        """The status once the machine has finished what it is doing."""
        return await self.until("/api/status",
                                lambda status: not status["busy"])

    async def press(self, tag, copies=1):
        """Asks for a run of labels, which the machine starts on."""
        response = await self.client.post(
            "/api/tag", json={"tag": tag, "copies": copies})
        self.assertEqual(200, response.status)
        self.assertEqual({"result": "success"}, await response.json())

    async def stop_a_run(self):
        """Starts a long run of labels and stops it, and returns what the
        status says was cut short."""
        await self.press(" HELLO ", copies=500)
        response = await self.client.post("/api/stop")
        self.assertEqual({"result": "stopping"}, await response.json())
        return (await self.until_idle())["stopped"]

    async def save(self, align, force):
        """Saves a calibration, which the machine reboots to take up, and
        returns the status once it has."""
        response = await self.client.post(
            "/api/save", json={"align": align, "force": force})
        self.assertEqual(200, response.status)
        return await self.until_idle()

    async def restart_simulator(self):
        """Stops the simulator and starts it again, as its operator would."""
        await self.asyncTearDown()
        await self.asyncSetUp()


class Status(RelayTestCase):
    async def test_an_idle_device_reports_the_firmwares_status(self):
        # The status the job runner reports on a machine that has just
        # booted: nothing running, and a roll no one has declared, which the
        # device takes to be the default length and full.
        response = await self.client.get("/api/status")
        self.assertEqual(200, response.status)
        body = await response.json()
        self.assertFalse(body["busy"])
        self.assertEqual("idle", body["command"])
        self.assertEqual({"length_mm": 3000, "remaining_mm": 3000},
                         body["roll"])


class Commands(RelayTestCase):
    async def test_a_commands_body_reaches_the_api(self):
        # A run of no labels is refused, in the Api's words, which it can
        # only say about the copies it was sent.
        response = await self.client.post(
            "/api/tag", json={"tag": "HELLO", "copies": 0})
        self.assertEqual(400, response.status)
        self.assertEqual(
            {"error": "Please provide a copies value between 1 and 500, "
                      "got 0"},
            await response.json())

    async def test_a_body_that_is_not_json_is_refused_for_its_type(self):
        # The Api reads the Content-Type header, so it has to arrive.
        response = await self.client.post(
            "/api/feed", data="{}", headers={"Content-Type": "text/plain"})
        self.assertEqual(415, response.status)
        self.assertEqual(
            {"error": "Please send the body as application/json"},
            await response.json())

    async def test_a_body_too_long_for_the_device_is_refused_for_its_length(
            self):
        # The device keeps what the Api allows and a byte more, to know that
        # there was more. The relay has to pass on at least as much, or this
        # would be a body cut off partway and refused as broken JSON.
        response = await self.client.post(
            "/api/tag", data=b'{"tag":"' + b"A" * 3000 + b'"}',
            headers={"Content-Type": "application/json"})
        self.assertEqual(413, response.status)
        self.assertEqual({"error": "The body may be at most 2048 bytes"},
                         await response.json())


class Stops(RelayTestCase):
    async def test_the_query_reaches_the_api(self):
        # Only /api/stop reads its query, and refuses a stop after anything
        # but a label. Without the query this would be a stop now, which an
        # idle machine answers with success.
        response = await self.client.post("/api/stop?after=cut")
        self.assertEqual(400, response.status)
        self.assertEqual(
            {"error": "Please provide after=label to stop once the label "
                      "being pressed is finished, or leave it out to stop "
                      "now"},
            await response.json())

    async def test_a_name_given_twice_in_the_query_takes_its_last_value(self):
        # As the device's webserver reads a query. The first value alone
        # would be refused; the last one asks an idle machine to stop after
        # its label, which it answers by saying it is idle.
        response = await self.client.post("/api/stop?after=cut&after=label")
        self.assertEqual(200, response.status)
        self.assertEqual({"result": "idle"}, await response.json())

    async def test_a_stop_can_name_a_command_that_has_not_arrived(self):
        # On a weak link a stop can overtake the command it was sent after.
        # It names that command by its id, in the query beside its own, and
        # the command is refused when it does arrive. If the name did not
        # reach the Api, this would be a stop of an idle machine, and the
        # run would start.
        response = await self.client.post(
            "/api/stop?id=5d7e21b6843f9a0c&for=3f9a0c5d7e21b684")
        self.assertEqual(200, response.status)
        self.assertEqual({"result": "not_started"}, await response.json())
        response = await self.client.post(
            "/api/tag?id=3f9a0c5d7e21b684",
            json={"tag": " HELLO ", "copies": 500})
        self.assertEqual(409, response.status)
        self.assertEqual(
            {"error": "Stopped before it started", "result": "not_started"},
            await response.json())
        self.assertFalse((await self.status())["busy"])


class Methods(RelayTestCase):
    async def test_a_command_asked_for_with_get_names_the_method_it_takes(
            self):
        # HTTP asks a 405 to say which method the path does take, in an
        # Allow header, and the Api says it for the relay to send.
        response = await self.client.get("/api/tag")
        self.assertEqual(405, response.status)
        self.assertEqual("POST", response.headers["Allow"])
        self.assertEqual({"error": "Please use POST for /api/tag"},
                         await response.json())

    async def test_a_method_the_device_does_not_know_is_refused(self):
        # The device's webserver reads anything but GET and POST as a method
        # no route takes.
        response = await self.client.put("/api/tag", json={"tag": "HELLO"})
        self.assertEqual(405, response.status)
        self.assertEqual("POST", response.headers["Allow"])

    async def test_a_reply_without_a_method_to_name_has_no_allow_header(self):
        response = await self.client.get("/api/status")
        self.assertNotIn("Allow", response.headers)


class Replies(RelayTestCase):
    async def test_the_log_is_sent_as_the_plain_text_it_is(self):
        # The one reply that is not JSON. What the machine logged as it
        # booted is in it, so it is the firmware's log and not a stand-in.
        response = await self.client.get("/api/log")
        self.assertEqual(200, response.status)
        self.assertEqual("text/plain", response.content_type)
        self.assertIn("Align factor: 5", await response.text())

    async def test_a_path_under_api_the_device_does_not_know_is_its_to_refuse(
            self):
        # And not the webserver's, which would say so in plain text.
        response = await self.client.get("/api/nothing")
        self.assertEqual(404, response.status)
        self.assertEqual({"error": "Not found"}, await response.json())


class Jobs(RelayTestCase):
    async def test_a_run_of_labels_is_pressed_off_the_roll(self):
        # Three labels of " HELLO " take 8 feeds of 3.7 mm each, 88.8 mm in
        # all, off a roll that starts at 3000 mm. The device counts it off
        # as it feeds, in whole millimetres.
        await self.press(" HELLO ", copies=3)
        status = await self.until_idle()
        self.assertEqual({"length_mm": 3000, "remaining_mm": 2912},
                         status["roll"])

    async def test_a_label_takes_the_time_it_takes_on_the_machine(self):
        # However well the host keeps up with SPEED, the wall clock only
        # holds the machine back and never moves its clock on, so the label
        # time the device measures from its third label on is the one it
        # works out, as on the machine in the native tests.
        response = await self.client.post(
            "/api/tag/estimate", json={"tag": " HELLO ", "copies": 500})
        estimate = (await response.json())["label_ms"]
        await self.press(" HELLO ", copies=500)
        clock = asyncio.get_running_loop()
        deadline = clock.time() + JOB_SECONDS
        status = await self.status()
        while status.get("copy", 0) < 3:
            if clock.time() > deadline:
                self.fail("Not on label 3 after %d seconds: %r"
                          % (JOB_SECONDS, status))
            await asyncio.sleep(0.001)
            status = await self.status()
        self.assertAlmostEqual(estimate, status["label_ms"],
                               delta=estimate / 100)

    async def test_a_run_can_be_stopped_while_it_is_pressed(self):
        # The stop is answered between the machine's waits, as the device
        # answers one while the job runner is busy, and what it cut short is
        # in the status afterwards.
        await self.press(" HELLO ", copies=500)
        response = await self.client.post("/api/stop")
        self.assertEqual(200, response.status)
        self.assertEqual({"result": "stopping"}, await response.json())
        stopped = (await self.until_idle())["stopped"]
        self.assertEqual("tag", stopped["command"])
        self.assertEqual("operator", stopped["cause"])
        self.assertEqual(500, stopped["copies"])
        self.assertLess(stopped["printed"], 500)

    async def test_a_second_command_is_refused_while_the_first_runs(self):
        await self.press(" HELLO ", copies=500)
        response = await self.client.post("/api/feed", json={})
        self.assertEqual(409, response.status)
        self.assertEqual(
            {"error": "The printer is already busy executing a command."},
            await response.json())

    async def test_a_command_sent_again_under_its_id_is_not_run_again(self):
        # The panel sends a command again when it hears nothing back, under
        # the id it sent it under the first time, which is in the query. If
        # the id did not reach the Api, the second of these would be refused,
        # the machine being busy with the first.
        for _ in range(2):
            response = await self.client.post(
                "/api/tag?id=3f9a0c5d7e21b684",
                json={"tag": " HELLO ", "copies": 500})
            self.assertEqual(200, response.status)
            self.assertEqual({"result": "success"}, await response.json())

    async def test_requests_that_arrive_together_each_get_their_own_reply(
            self):
        # The panel polls while a click is on its way, and two browsers can
        # be open at once. Each refusal names the copies its own request
        # asked for, so a reply read by the wrong request would show.
        asked = range(501, 521)
        responses = await asyncio.gather(*[
            self.client.post("/api/tag", json={"tag": "A", "copies": copies})
            for copies in asked])
        self.assertEqual(
            [{"error": "Please provide a copies value between 1 and 500, "
                       "got %d" % copies} for copies in asked],
            [await response.json() for response in responses])


class Pacing(RelayTestCase):
    # At the machine's own speed, where each of its milliseconds is one of
    # the wall clock's.
    speed = 1

    async def uptime(self):
        """The machine's clock, and the wall clock either side of asking
        for it, all in milliseconds."""
        wall = asyncio.get_running_loop()
        asked = wall.time() * 1000
        status = await self.status()
        return asked, status["uptime_ms"], wall.time() * 1000

    async def test_a_status_has_the_time_it_was_answered_at(self):
        # Partway through a wait too, such as the tune a run starts with: the
        # machine's clock is never already at the end of it, which would put
        # it ahead of the wall clock. It is at most a millisecond ahead,
        # the one it has just moved into, and idle it is set to the wall
        # clock's millisecond below. A host that is late, or busy, leaves
        # the machine behind by as long, so from below all there is to say
        # is that the clock moves on.
        first_asked, first, first_answered = await self.uptime()
        await self.press(" HELLO ")
        wall = asyncio.get_running_loop()
        end = wall.time() + 1.5
        last = first
        while wall.time() < end:
            asked, uptime, answered = await self.uptime()
            self.assertLessEqual(uptime - first, answered - first_asked + 2)
            self.assertGreaterEqual(uptime, last)
            last = uptime
            await asyncio.sleep(0.01)
        self.assertGreater(last, first)


class Reboots(RelayTestCase):
    async def test_a_saved_calibration_is_what_the_machine_boots_with(self):
        # The save ends in a reboot, and the flash is all that survives it.
        # The log is the new boot's, which read the calibration back.
        status = await self.save(align=3, force=2)
        self.assertEqual((3, 2), (status["align"], status["force"]))
        log = await (await self.client.get("/api/log")).text()
        self.assertIn("Align factor: 3", log)
        self.assertNotIn("Align factor: 5", log)

    async def test_a_stop_after_a_reboot_is_not_taken_for_one_before_it(self):
        # The panel stays open across a reboot, and hides a stop it has been
        # told to dismiss by the stop's id. The device starts its ids
        # somewhere new at every boot, so a new stop cannot be hidden as an
        # old one.
        before = await self.stop_a_run()
        await self.save(align=5, force=5)
        after = await self.stop_a_run()
        self.assertNotEqual(before["id"], after["id"])

    async def test_a_stop_after_a_restart_is_not_taken_for_one_before_it(
            self):
        # The same, for the panel left open while the simulator is stopped
        # and started again.
        before = await self.stop_a_run()
        await self.restart_simulator()
        after = await self.stop_a_run()
        self.assertNotEqual(before["id"], after["id"])


class Network(RelayTestCase):
    # The link supervisor and the network settings are the firmware's own,
    # on a radio with an air where the board has an antenna. main.cpp says
    # which networks are in it. Nothing here waits for a job: the machine's
    # link is looked at whatever the machine is doing, as on the board,
    # where it has a task of its own.

    async def network(self, there):
        """How the machine is reached, once it is as a test waits for."""
        return await self.until("/api/network", there)

    async def remember(self, ssid, password):
        """Has the machine remember a network, as the panel does."""
        response = await self.client.post(
            "/api/network/remember",
            json={"ssid": ssid, "password": password})
        self.assertEqual(200, response.status)

    async def test_a_machine_never_set_up_opens_its_own_network(self):
        # With no network to join, its own is the one way in. It is named
        # after the machine, as the machine's host name is.
        network = await self.network(lambda network: network["own"]["open"])
        self.assertEqual("join", network["mode"])
        self.assertEqual("off", network["station"])
        self.assertEqual("E-TKT-9C4F", network["own"]["name"])
        self.assertEqual("e-tkt-9c4f.local", network["host"])
        self.assertEqual([], network["remembered"])

    async def test_a_network_in_the_air_is_joined_once_it_is_remembered(
            self):
        await self.remember("Workshop", "labelmaker")
        network = await self.network(
            lambda network: network["station"] == "joined")
        self.assertEqual("Workshop", network["network"])
        self.assertEqual("192.168.1.50", network["address"])
        self.assertNotIn("failure", network)

    async def test_a_wrong_password_is_told_as_the_network_refusing(self):
        # The radio's own reason for it comes with the cause: a handshake
        # that the network never finished.
        await self.remember("Workshop", "not the one")
        network = await self.network(lambda network: "failure" in network)
        self.assertEqual("joining", network["station"])
        self.assertEqual(
            {"network": "Workshop", "cause": "refused", "reason": 15,
             "reason_name": "4WAY_HANDSHAKE_TIMEOUT"},
            network["failure"])

    async def test_a_listen_names_the_networks_in_the_air(self):
        # Each name once and the loudest first, without the one that hides
        # its name, which is the firmware's doing. A name comes through the
        # relay as it is on the air: one that is not all ASCII, and one
        # with markup in it, which is the panel's to show as text.
        response = await self.client.post("/api/network/listen")
        self.assertEqual(200, response.status)
        after = (await response.json())["after"]
        nearby = await self.until(
            "/api/network/nearby", lambda nearby: nearby["listens"] > after)
        self.assertEqual(
            ["Workshop", "E-TKT-51B2", "Church Guest",
             "<b>Cafe</b> & \"Friends\"", "Full House",
             "The Longest Network Name Allowed", "Jugendcafé \U0001f3b8",
             "Far Corner"],
            [network["ssid"] for network in nearby["networks"]])
        self.assertIn({"ssid": "Church Guest", "rssi": -63, "secured": False},
                      nearby["networks"])

    async def test_the_link_is_kept_up_while_a_run_is_pressed(self):
        # A network remembered as a long run starts is joined well before
        # the run ends: the link is looked at between the machine's
        # milliseconds, and not only between its jobs.
        await self.press(" HELLO ", copies=500)
        await self.remember("Workshop", "labelmaker")
        await self.network(lambda network: network["station"] == "joined")
        self.assertTrue((await self.status())["busy"])

    async def test_a_reboot_keeps_the_networks_the_machine_remembers(self):
        # They are in the flash, beside the calibration a save reboots to
        # take up, and the new boot joins the one it finds there.
        await self.remember("Workshop", "labelmaker")
        await self.save(align=5, force=5)
        network = await self.network(
            lambda network: network["station"] == "joined")
        self.assertEqual(["Workshop"], network["remembered"])


class Serial(RelayTestCase):
    serial = asyncio.subprocess.PIPE

    async def test_an_idle_machine_says_what_its_link_does_as_it_does_it(
            self):
        # The serial port is where the link tells its story, and most of it
        # happens while the machine is idle. It is not kept back until the
        # next job has something to say.
        await self.until("/api/network",
                         lambda network: network["own"]["open"])
        said = self.device.process.stderr
        try:
            await asyncio.wait_for(
                said.readuntil(b"its own network E-TKT-9C4F is open"),
                JOB_SECONDS)
        except asyncio.TimeoutError:
            self.fail("Nothing about its own network after %d seconds"
                      % JOB_SECONDS)


class Loss(RelayTestCase):
    async def test_a_firmware_that_has_ended_is_reported_once(self):
        # To the panel in every reply, which says it has lost touch with the
        # label maker, and to the operator once, in the terminal.
        self.device.process.kill()
        await self.device.process.wait()
        gone = ("The simulated firmware was stopped by signal 9. Restart "
                "the simulator to bring it back.")
        with contextlib.redirect_stderr(io.StringIO()) as said:
            for _ in range(2):
                response = await self.client.get("/api/status")
                self.assertEqual(502, response.status)
                self.assertEqual({"error": gone}, await response.json())
        self.assertEqual(gone + "\n", said.getvalue())


class WeakLink(RelayTestCase):
    # How long a test waits for an answer the link has lost before it gives
    # up, as the panel does. Long enough for the firmware to have answered.
    PATIENCE_SECONDS = 0.5

    def setUp(self):
        # What becomes of each request to come, in order. Past the last of
        # them nothing is lost.
        self.fates = []

    def loses(self):
        return self.fates.pop(0) if self.fates else None

    async def print_unanswered(self, lost):
        """Asks for a long run of labels over a link that loses `lost`, and
        gives up waiting for an answer, as the panel does."""
        self.fates = [lost]
        with self.assertRaises(asyncio.TimeoutError):
            await self.client.post(
                "/api/tag?id=3f9a0c5d7e21b684",
                json={"tag": " HELLO ", "copies": 500},
                timeout=ClientTimeout(total=self.PATIENCE_SECONDS))

    async def test_a_request_lost_on_its_way_never_reaches_the_firmware(self):
        # Half of what a weak network loses. The panel hears nothing, and
        # the machine was never asked.
        await self.print_unanswered("request")
        status = await self.status()
        self.assertFalse(status["busy"])
        self.assertNotIn("last_command_id", status)

    async def test_a_reply_lost_on_its_way_back_leaves_the_machine_running(
            self):
        # The other half, and the one that used to print a run twice: the
        # machine has the command, and the panel has heard nothing. Sent
        # again under its id, it is answered as it was and not refused as a
        # second command would be.
        await self.print_unanswered("reply")
        self.assertTrue((await self.status())["busy"])
        response = await self.client.post(
            "/api/tag?id=3f9a0c5d7e21b684",
            json={"tag": " HELLO ", "copies": 500})
        self.assertEqual(200, response.status)
        self.assertEqual({"result": "success"}, await response.json())

    async def test_a_command_whose_reply_was_lost_is_named_in_the_status(
            self):
        # The polls after a lost reply can get through, and they name the
        # command the machine took by the id it was sent under. So the panel
        # learns that its command arrived without waiting to be answered.
        await self.print_unanswered("reply")
        self.assertEqual("3f9a0c5d7e21b684",
                         (await self.status())["last_command_id"])

    async def test_a_lost_request_does_not_hold_up_the_next(self):
        # The panel polls while a command of its own goes unanswered, and
        # sends the command again. Neither waits for the lost one to be
        # given up on.
        self.fates = ["request"]
        lost = asyncio.ensure_future(self.client.get(
            "/api/status", timeout=ClientTimeout(total=JOB_SECONDS)))
        await asyncio.sleep(0.05)
        self.assertFalse((await self.status())["busy"])
        self.assertFalse(lost.done())
        lost.cancel()


class Losing(unittest.TestCase):
    def test_a_weak_link_loses_its_share_half_each_way(self):
        # --lose 40 loses 40 in a hundred: 20 on the way to the machine and
        # 20 on the way back.
        drawn = iter([0.0, 0.19, 0.2, 0.39, 0.4, 0.99])
        loses = server.weak_link(0.4, lambda: next(drawn))
        self.assertEqual(["request", "request", "reply", "reply", None, None],
                         [loses() for _ in range(6)])

    def test_a_link_that_loses_nothing_loses_nothing(self):
        loses = server.weak_link(0, lambda: 0.0)
        self.assertIsNone(loses())


class Starting(unittest.IsolatedAsyncioTestCase):
    async def test_a_firmware_that_is_not_there_is_reported(self):
        missing = os.path.join(ROOT, "no-such-program")
        device = server.Server(missing)
        with self.assertRaises(server.DeviceError) as raised:
            await device.start()
        await device.close()
        self.assertEqual("Could not start the firmware at %s: No such file "
                         "or directory" % missing, str(raised.exception))

    async def test_a_firmware_that_ends_before_it_boots_is_reported_once(
            self):
        # Such as one that crashes as it boots. The error is the report:
        # the operator reads it once, over what the firmware printed on its
        # way down.
        exits = shutil.which("false")
        if exits is None:
            self.skipTest("needs a false command")
        device = server.Server(exits)
        with contextlib.redirect_stderr(io.StringIO()) as said:
            with self.assertRaises(server.DeviceError) as raised:
                await device.start()
        await device.close()
        self.assertEqual("The simulated firmware exited with status 1 "
                         "before it booted.", str(raised.exception))
        self.assertEqual("", said.getvalue())


class Program(unittest.TestCase):
    """The program on its own, as somebody drives it by hand."""

    def run_program(self, *arguments, lines=b""):
        return subprocess.run(
            [PROGRAM] + list(arguments), input=lines,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=30)

    def test_a_line_that_is_not_a_request_is_answered_all_the_same(self):
        # One reply to every line, or the next reply would be read as this
        # one's.
        done = self.run_program(lines=b"GET /api/status\n")
        self.assertEqual(0, done.returncode)
        self.assertEqual(
            [{"code": 500, "contentType": "application/json",
              "body": '{"error":"The simulator could not read the request"}',
              "allow": None}],
            [json.loads(line) for line in done.stdout.splitlines()])

    def test_a_speed_that_is_not_above_zero_is_refused(self):
        for speed in ["0", "-1", "nan", "inf", "fast", ""]:
            with self.subTest(speed=speed):
                done = self.run_program("--speed", speed)
                self.assertEqual(2, done.returncode)
                self.assertIn(b"usage: program [--speed N]", done.stderr)


class Panel(RelayTestCase):
    async def test_the_panel_is_served_at_the_root(self):
        # As the device serves index.html for /.
        response = await self.client.get("/")
        self.assertEqual(200, response.status)
        with open(os.path.join(ROOT, "data", "index.html"), "rb") as page:
            self.assertEqual(page.read(), await response.read())

    async def test_the_panels_files_are_served_out_of_data(self):
        response = await self.client.get("/style.css")
        self.assertEqual(200, response.status)
        with open(os.path.join(ROOT, "data", "style.css"), "rb") as sheet:
            self.assertEqual(sheet.read(), await response.read())

    async def test_an_edit_to_data_shows_on_the_next_reload(self):
        # A browser keeps a file it can date for a while without asking for
        # it again, and the page it builds from them is then the one from
        # before the edit. The device sends no date to keep one by.
        for path in ["/", "/style.css"]:
            response = await self.client.get(path)
            self.assertEqual("no-cache", response.headers.get("Cache-Control"))

    async def test_the_apis_replies_are_left_as_the_firmware_sent_them(self):
        response = await self.client.get("/api/status")
        self.assertNotIn("Cache-Control", response.headers)

    async def test_the_panel_tests_use_the_capabilities_the_firmware_serves(
            self):
        # test/panel/ holds every decision the panel makes to the reply in
        # capabilities.json, the wording for each command among them. A copy
        # that drifted from the firmware would keep those tests passing
        # against a device that no longer exists.
        response = await self.client.get("/api/capabilities")
        with open(os.path.join(ROOT, "test", "panel", "capabilities.json"),
                  encoding="utf-8") as reply:
            self.assertEqual(json.load(reply), await response.json())


class Text(RelayTestCase):
    async def test_a_character_of_more_than_one_byte_arrives_whole(self):
        # The Api names the one character of the label the wheel cannot
        # print. It can only name it whole if its two bytes got there, and
        # came back, as they were sent. They are sent the way the panel's
        # JSON.stringify sends them, as they are, where Python's json would
        # write them as an escape.
        response = await self.client.post(
            "/api/tag", data='{"tag":"CAFÉ"}'.encode("utf-8"),
            headers={"Content-Type": "application/json"})
        self.assertEqual(400, response.status)
        self.assertEqual({"error": "The daisy wheel cannot print 'É'"},
                         await response.json())

    async def test_a_body_that_is_not_utf8_reaches_the_api_byte_for_byte(self):
        # The device's webserver hands the Api the bytes it was sent, and the
        # Api does not ask them to be UTF-8. The refusal quotes the byte back.
        response = await self.client.post(
            "/api/tag", data=b'{"tag":"\xff"}',
            headers={"Content-Type": "application/json"})
        self.assertEqual(400, response.status)
        self.assertEqual(b'{"error":"The daisy wheel cannot print \'\xff\'"}',
                         await response.read())


if __name__ == "__main__":
    unittest.main()
