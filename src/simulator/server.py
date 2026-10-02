"""A stand-in for the machine, so data/ can be worked on without one.

The simulator is the firmware. main.cpp beside this file builds this
machine's own job runner, link supervisor and Api for the host, on the fake
motors, screen, flash and radio the native tests drive, and reads requests on
stdin. This serves the panel's files out of data/, as the device serves them
out of SPIFFS, and relays every request under /api/ to that program and its
reply back. Nothing the device says is written down here, so a command, a
refusal or a status field added to the firmware is in the simulator with
nothing to remember.

It needs aiohttp, which nothing else in this repo does, and PlatformIO, which
builds the firmware. From the repo root:

    python3 -m pip install aiohttp
    python3 -m src.simulator

then open http://localhost/. The panel's URLs are relative, so any port
serves it. macOS lets anyone listen on 80; on Linux, --port 8080 does
without sudo. --speed 10 runs the machine ten times as fast. --lose 30 is a
weak network, which leaves 30 in a hundred requests to the api unanswered.

The machine has no network to start with, so it opens its own. The radio it
has is a list of networks, which fillAir() in main.cpp names, each with what
it does to a machine that tries it. AIR_PASSWORD there is the password of the
ones that have one.
"""

import asyncio
import json
import os
import random
import shutil
import subprocess
import sys

from aiohttp import web

# The repository: the panel is served out of it, and the firmware is built in
# it.
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(
    os.path.abspath(__file__))))
DATA = os.path.join(ROOT, "data")

# The PlatformIO environment that builds the firmware for the host, and where
# the build leaves the program. See [env:simulator] in platformio.ini.
ENVIRONMENT = "simulator"
PROGRAM = os.path.join(ROOT, ".pio", "build", ENVIRONMENT, "program")

# How much of a request's body is passed on. The device keeps the first
# Api::MAX_BODY_BYTES + 1 bytes and drops the rest, and main.cpp cuts what it
# is sent to the same, so any bound above that one will do. This one only
# keeps a runaway body out of memory.
BODY_KEPT_BYTES = 64 * 1024

# The longest reply line read back. The longest reply is the log's, which is
# a few kilobytes.
REPLY_LIMIT_BYTES = 1024 * 1024

# How long the program may take to exit once its stdin closes. It exits at
# once when idle, and mid-job at the next wait.
EXIT_SECONDS = 5

# How often a request the link has lost is looked at again, to see whether
# whoever sent it has given up on it.
LOST_CHECK_SECONDS = 0.1


class NoPlatformIO(Exception):
    """PlatformIO, which builds the firmware, is not installed."""


class BuildFailed(Exception):
    """The firmware did not build for the host."""


class DeviceError(Exception):
    """The firmware could not be started, or stopped before it booted."""


def platformio():
    """The PlatformIO command, or None if it is not installed."""
    for name in ("pio", "platformio"):
        found = shutil.which(name)
        if found is not None:
            return found
    # Where PlatformIO's own installer puts it, which is not always on PATH.
    installed = os.path.expanduser("~/.platformio/penv/bin/pio")
    return installed if os.access(installed, os.X_OK) else None


def build():
    """Builds the firmware for the host, and returns the program's path.

    PlatformIO works out what has changed, so a build with nothing to do
    takes a second or two.
    """
    pio = platformio()
    if pio is None:
        raise NoPlatformIO(
            "The simulator builds the firmware with PlatformIO, which is not "
            "installed. See https://platformio.org/install/cli")
    done = subprocess.run(
        [pio, "run", "--silent", "--environment", ENVIRONMENT,
         "--project-dir", ROOT],
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        encoding="utf-8", errors="replace")
    if done.returncode != 0:
        raise BuildFailed(
            "The firmware did not build for the host:\n" + done.stdout)
    return PROGRAM


def request_line(asked):
    """One request, as the line main.cpp reads.

    The body's bytes go through as they came, whether or not they are
    UTF-8, the way the device's webserver hands them to the Api.
    """
    text = json.dumps(asked, ensure_ascii=False)
    return text.encode("utf-8", "surrogateescape") + b"\n"


def read_reply(line):
    """The reply main.cpp sent, as a dict.

    Loosely, because ArduinoJson leaves most control characters in a string
    unescaped, which strict JSON does not allow.
    """
    return json.loads(line.decode("utf-8", "surrogateescape"), strict=False)


def weak_link(share, chance=random.random):
    """What a network that loses `share` of what is sent over it does to
    each request, as Server takes it. Half of what it loses never reaches
    the machine. The other half the machine answers, and the answer never
    gets back, which is the half that used to run a command twice.

    `chance` draws a number from 0 up to 1 for each request.
    """
    def loses():
        drawn = chance()
        if drawn < share / 2:
            return "request"
        return "reply" if drawn < share else None
    return loses


async def read_body(request):
    """The request's body, up to BODY_KEPT_BYTES of it."""
    kept = bytearray()
    async for chunk in request.content.iter_any():
        room = BODY_KEPT_BYTES - len(kept)
        if room > 0:
            kept += chunk[:room]
    return bytes(kept)


async def ask_again(request, response):
    """Has a browser ask for each of the panel's files again before it
    uses it. aiohttp dates the files it serves, and a browser keeps a dated
    file for a while without asking, so a reload after an edit to data/
    showed the page from before it. The device dates nothing, so a browser
    asks it every time. The api's replies are the firmware's, and are left
    as it sent them."""
    if not request.path.startswith("/api/"):
        response.headers["Cache-Control"] = "no-cache"


class Server:
    """The firmware, and the webserver in front of it."""

    def __init__(self, program, speed=1, serial=None, loses=None):
        """`program` is what build() returned. `speed` is how many times as
        fast as the machine it runs. `serial` is where what the machine
        sends down its serial port goes: None for this process's stderr, or
        anything asyncio.create_subprocess_exec takes, such as
        asyncio.subprocess.DEVNULL. `loses` says what the network loses of
        each request under /api/, as weak_link() does: "request" for one
        that never reaches the machine, "reply" for one whose answer never
        gets back, and None for one that gets through. None for a network
        that loses nothing.
        """
        self.program = program
        self.speed = speed
        self.serial = serial
        self.loses = loses
        # Set once close() is called, which lets go of the requests the
        # network has lost.
        self.closing = False
        self.process = None
        self.runner = None
        # Held from a request going out to its reply coming back, so each
        # reply is read by the request it answers.
        self.lock = None
        # Whether the machine has booted: an end before then is start()'s to
        # report, and one after it the relay's.
        self.booted = False
        # How the firmware ended, once it has: "exited with status 1".
        self.ended = None

    async def start(self):
        """Starts the firmware, and waits for the machine to boot.

        Returns the names of the commands it runs, as its capabilities list
        them.
        """
        self.lock = asyncio.Lock()
        try:
            self.process = await asyncio.create_subprocess_exec(
                self.program, "--speed", str(self.speed),
                stdin=asyncio.subprocess.PIPE,
                stdout=asyncio.subprocess.PIPE, stderr=self.serial,
                limit=REPLY_LIMIT_BYTES)
        except OSError as problem:
            raise DeviceError("Could not start the firmware at %s: %s"
                              % (self.program, problem.strerror))
        reply = await self.ask({"method": "GET",
                                "path": "/api/capabilities", "query": {},
                                "contentType": "", "body": ""})
        if reply is None:
            raise DeviceError("The simulated firmware %s before it booted."
                              % self.ended)
        self.booted = True
        return list(json.loads(reply["body"])["commands"])

    def application(self):
        """The panel and the api, not yet listening anywhere."""
        app = web.Application()
        app.add_routes([
            # Every path under /api/, the ones the Api has no route for too,
            # so a 404 there is the Api's, as on the device. aiohttp tries
            # the longest prefix first, so no file in data/ can shadow one.
            web.route("*", "/api/{tail:.*}", self.relay),
            web.get("/", self.index),
            web.static("/", DATA),
        ])
        app.on_response_prepare.append(ask_again)
        return app

    async def listen(self, port):
        """Serves the application at `port`, on every address."""
        self.runner = web.AppRunner(self.application())
        await self.runner.setup()
        await web.TCPSite(self.runner, "0.0.0.0", port).start()

    async def close(self):
        """Stops listening, and stops the firmware."""
        self.closing = True
        if self.runner is not None:
            await self.runner.cleanup()
            self.runner = None
        if self.process is None:
            return
        self.process.stdin.close()
        try:
            await asyncio.wait_for(self.process.wait(), EXIT_SECONDS)
        except asyncio.TimeoutError:
            self.process.kill()
            await self.process.wait()

    async def index(self, request):
        return web.FileResponse(os.path.join(DATA, "index.html"))

    async def relay(self, request):
        """Hands one request under /api/ to the firmware, as ApiHandler in
        Network.cpp hands one to the Api, and sends back its reply."""
        lost = self.loses() if self.loses is not None else None
        if lost == "request":
            return await self.unanswered(request)
        asked = {
            "method": request.method,
            "path": request.path,
            # A name given twice takes the last value, as on the device.
            "query": {name: value for name, value in request.query.items()},
            "contentType": request.headers.get("Content-Type", ""),
            "body": (await read_body(request)).decode(
                "utf-8", "surrogateescape"),
        }
        reply = await self.ask(asked)
        if lost == "reply":
            return await self.unanswered(request)
        if reply is None:
            return web.json_response({"error": self.gone()}, status=502)
        headers = {"Content-Type": reply["contentType"]}
        if reply["allow"] is not None:
            headers["Allow"] = reply["allow"]
        return web.Response(
            status=reply["code"], headers=headers,
            body=reply["body"].encode("utf-8", "surrogateescape"))

    async def unanswered(self, request):
        """Leaves a request without an answer for as long as whoever sent
        it waits for one, as a network that lost it would."""
        while not self.closing:
            transport = request.transport
            if transport is None or transport.is_closing():
                break
            await asyncio.sleep(LOST_CHECK_SECONDS)
        # For aiohttp, which wants one from every handler. Nobody is left to
        # send it to.
        return web.Response(status=504)

    async def ask(self, asked):
        """Sends the firmware one request, and returns its reply, or None
        once it has stopped answering."""
        async with self.lock:
            if self.ended is not None:
                return None
            try:
                self.process.stdin.write(request_line(asked))
                await self.process.stdin.drain()
                line = await self.process.stdout.readline()
            except (BrokenPipeError, ConnectionResetError, ValueError):
                # ValueError is a reply past REPLY_LIMIT_BYTES, which leaves
                # the rest of it where the next reply would be read from.
                line = b""
            if not line.endswith(b"\n"):
                await self.lose()
                return None
        return read_reply(line)

    async def lose(self):
        """Notes that the firmware has stopped answering, and how."""
        if self.process.returncode is None:
            try:
                self.process.kill()
            except ProcessLookupError:
                pass
        code = await self.process.wait()
        if code < 0:
            self.ended = "was stopped by signal %d" % -code
        else:
            self.ended = "exited with status %d" % code
        # Once, rather than in every reply's wake. The panel goes on being
        # served, and says it has lost touch with the label maker.
        if self.booted:
            print(self.gone(), file=sys.stderr)

    def gone(self):
        """What the relay says in place of a reply, once the firmware has
        ended."""
        return ("The simulated firmware %s. Restart the simulator to bring "
                "it back." % self.ended)
