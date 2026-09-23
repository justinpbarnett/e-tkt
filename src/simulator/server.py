"""A stand-in for the machine, so data/ can be worked on without one.

Serves the same files the device serves out of SPIFFS, and answers the same
endpoints it answers out of Network.cpp -- one POST per runnable row of the
command table, plus status, capabilities and the log. None of that is
written down here. firmware.py reads it out of src/, so a command added to
the device shows up in the simulator with nothing to remember.

It needs aiohttp, which nothing else in this repo does. From the repo root:

    python3 -m pip install aiohttp
    sudo python3 -m src.simulator

then open http://localhost/. The sudo is PORT below: the device listens on
80 and the panel's URLs are relative, so serving it anywhere else means the
simulator is not answering the address the panel was written against.
"""

import asyncio
import os
import time
from collections import deque

from aiohttp import web

from . import firmware

# The port the webserver will start on
PORT = 80

# How long it should take to simulate pressing one character
PRINT_CHARACTER_SECONDS = 1

# The feeding and cutting after the last character. Progress sits at the
# device's cap through this, which is what the cap is for.
FINISH_SECONDS = 2

# How long it should take to simulate any other action (eg cut, feed, etc)
OTHER_COMMAND_SECONDS = 5

# The one command that walks a label character by character, which makes it
# the only one with progress worth reporting and the only one whose label
# /api/status hands back. Network.cpp special-cases it the same way. The
# table can say a command takes a label; it cannot say what that means.
PRINTING_COMMAND = "tag"

# The one command whose effect outlives it. Which command that is, is
# behaviour rather than a row in the table: the table says save reads align
# and force, not that save is the only one that keeps them.
SAVE_COMMAND = "save"

# Word for word what PrinterBusyException says in src/ETKT.h.
BUSY_MESSAGE = "The printer is already busy executing a command."

# As deep as the device's Logger. Nothing depends on the two agreeing.
LOG_LINES = 32

# There is no heap here. The panel does not read these; they are served so
# that anything else looking at /api/status sees the shape the device sends.
FAKE_HEAP_BYTES = 200000
FAKE_LARGEST_BLOCK_BYTES = 110000


class Server:
    """
    Simulates a physical E-TKT device by serving web interface, locking during
    printing, and reporting print progress.
    """

    def __init__(self, device):
        self.device = device

        # The row /api/status names when nothing is running. It is a row like
        # any other and the only one with no handler, because IDLE is a
        # status rather than a job. Checked rather than assumed: if that ever
        # stops being true, the simulator should say so at startup and not
        # report the wrong command for the rest of the session.
        idle = [spec for spec in device.commands if not spec.runnable]
        if len(idle) != 1:
            raise firmware.FirmwareParseError(
                "Expected one command with no handler, found %d: %s"
                % (len(idle), [spec.name for spec in idle]))
        self.idle = idle[0]

        self.align = device.default_align
        self.force = device.default_force
        self.command = None
        self.running_task = None
        self.label = ""
        self.progress = 0
        self.log = deque(maxlen=LOG_LINES)
        self.started = time.monotonic()

        # What Settings::initialize() says on the way up.
        self.record("INFO", "Align factor: %d" % self.align)
        self.record("INFO", "Force factor: %d" % self.force)

    async def start(self):
        app = web.Application()
        routes = [
            web.get('/api/status', self.status),
            web.get('/api/capabilities', self.capabilities),
            web.get('/api/log', self.recent_log),
        ]

        # One route per runnable command, straight off the firmware's table,
        # exactly as Network.cpp::initialize() builds the real ones.
        for spec in self.device.routes():
            routes.append(web.post('/api/' + spec.name, self.endpoint(spec)))

        routes.append(web.get('/', self.index))
        routes.append(web.static("/", self.data_path('')))
        app.add_routes(routes)

        runner = web.AppRunner(app)
        await runner.setup()
        site = web.TCPSite(runner, "0.0.0.0", PORT)
        print("Serving %d commands: %s"
              % (len(self.device.routes()),
                 " ".join(spec.name for spec in self.device.routes())))
        print(f"Starting webserver, http://localhost:{PORT}")
        await site.start()

    async def index(self, request):
        return web.FileResponse(self.data_path('index.html'))

    async def status(self, request):
        running = self.command
        body = {
            'progress': self.progress,
            'busy': running is not None,
            'command': (running or self.idle).name,
            'align': self.align,
            'force': self.force,
            'mem_heap_free_bytes': FAKE_HEAP_BYTES,
            'mem_largest_free_block_bytes': FAKE_LARGEST_BLOCK_BYTES,
            'uptime_ms': int((time.monotonic() - self.started) * 1000),
        }
        if running is not None and running.name == PRINTING_COMMAND:
            body['current_label'] = self.label
        return web.json_response(body)

    async def capabilities(self, request):
        return web.json_response({
            'printable': self.device.printable,
            'aliases': self.device.aliases,
            'calibration': {
                'min': self.device.calibration_min,
                'max': self.device.calibration_max,
            },
            'label': {
                'minimum': self.device.min_label_characters,
                'maximum': self.device.max_label_characters,
            },
            'commands': [spec.name for spec in self.device.routes()],
        })

    async def recent_log(self, request):
        # Plain text, oldest first, the way /api/log serves it.
        return web.Response(text="\n".join(self.log),
                            content_type="text/plain")

    def endpoint(self, spec):
        """The handler for one command's route."""
        async def handle(request):
            return await self.accept(spec, request)
        return handle

    async def accept(self, spec, request):
        try:
            body = await request.json()
        except ValueError:
            # The device's JSON handler never calls the command back on a
            # body it cannot read, so the fields are simply all missing.
            body = {}

        align, refusal = self.read_calibration(
            spec.uses_align, body, "align", "Please provide an align value")
        if refusal is not None:
            return refusal
        force, refusal = self.read_calibration(
            spec.uses_force, body, "force", "Please provide a force value")
        if refusal is not None:
            return refusal

        label = ""
        if spec.label_field is not None:
            if spec.label_field not in body:
                return self.refuse(
                    "Please provide a %s value" % spec.label_field)
            label = str(body[spec.label_field])

            # Only for a field that is text to emboss. A move's field names a
            # slot on the wheel instead, cut mark included, and the device
            # leaves that one to DaisyWheel::move().
            if spec.field_is_label:
                if len(label) > self.device.max_label_characters:
                    return self.refuse(
                        "A %s may be at most %d characters, got %d"
                        % (spec.label_field,
                           self.device.max_label_characters, len(label)))
                unprintable = self.device.unprintable_character(label)
                if unprintable:
                    return self.refuse(
                        "The daisy wheel cannot print '%s'" % unprintable)

        # Busy is checked after the body and not before, because that is the
        # order the device checks them in: the fields are read on the
        # request's own task and only then handed to submit(), which is what
        # throws. So a bad body on a busy machine is a 400 about the body.
        if self.command is not None:
            # 409, not 400: the request was fine, the machine was not.
            return self.refuse(BUSY_MESSAGE, status=409)

        if spec.name == SAVE_COMMAND:
            self.align = align
            self.force = force
            # Word for word what Settings::save() logs.
            self.record("INFO", "Saved align %d" % self.align)
            self.record("INFO", "Saved force %d" % self.force)

        self.command = spec
        self.label = label
        self.progress = 0
        # Held, not dropped. asyncio keeps only a weak reference to a task, so
        # a create_task() whose result nobody stores can be collected part way
        # through a label.
        self.running_task = asyncio.create_task(self.run(spec))
        return web.json_response({'result': 'success'})

    def read_calibration(self, wanted, body, field, missing):
        """One 1-9 field, or a 400. Mirrors readCalibrationField()."""
        if not wanted:
            # A field a command does not read is ignored rather than refused.
            return None, None
        if field not in body:
            return None, self.refuse(missing)
        try:
            value = int(body[field])
        except (TypeError, ValueError):
            # What ArduinoJson's as<int>() hands back for anything it cannot
            # read as a number, which then fails the range check below.
            value = 0
        if not self.device.valid_calibration(value):
            # "a align" reads wrong and is deliberate: Network.cpp builds
            # this message by concatenation and does not special-case the
            # article. Both sides saying the same words is worth more here
            # than the grammar.
            return None, self.refuse(
                "Please provide a %s value between %d and %d, got %d"
                % (field, self.device.calibration_min,
                   self.device.calibration_max, value))
        return value, None

    def refuse(self, message, status=400):
        return web.json_response({'error': message}, status=status)

    async def run(self, spec):
        if spec.name == PRINTING_COMMAND:
            await self.print_label()
        else:
            # Any other command takes the same amount of time
            await asyncio.sleep(OTHER_COMMAND_SECONDS)
        # Both together, as ETKT::loop() does when it clears the command:
        # leaving progress behind reports an idle printer stuck at 99%.
        self.command = None
        self.running_task = None
        self.progress = 0

    async def print_label(self):
        # The device uppercases a copy and leaves the submitted label alone,
        # which is why /api/status hands back exactly what the panel sent.
        label = self.label.upper()
        self.record("INFO", "print " + label)
        for character in label:
            if character not in self.device.printable:
                # What DaisyWheel::move() says when it is asked for a
                # character the wheel does not carry. Nothing posted to
                # /api/tag reaches here any more -- accept() refuses such a
                # label -- but the device still logs this line for a label
                # raised from inside itself, and skips the press for that
                # character while printing the rest.
                self.record("ERROR",
                            "No character '%s' on the daisy wheel"
                            % character)

        # Python counts code points, which is the unit progressPercent()
        # asks for -- four of the wheel's characters are more than one byte.
        total = len(label)
        for done in range(1, total + 1):
            await asyncio.sleep(PRINT_CHARACTER_SECONDS)
            self.progress = self.device.progress_percent(done, total)

        # The tail feed and the cut, which is the stretch the device holds
        # its last percentage point back for.
        await asyncio.sleep(FINISH_SECONDS)
        self.record("INFO", "Printing Complete")

    def record(self, level, message):
        """One line of the log, formatted the way Logger::recent() is."""
        self.log.append("%.3f %-5s %s"
                        % (time.monotonic() - self.started, level, message))

    def data_path(self, path):
        return os.path.join(os.path.dirname(__file__), '../../data/', path)
