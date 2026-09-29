"""A stand-in for the machine, so data/ can be worked on without one.

Serves the same files the device serves out of SPIFFS, and answers the same
endpoints it answers out of Network.cpp -- one POST per runnable row of the
command table, plus stop, status, capabilities and the log. None of that is
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

# The one command that starts the count of tape again, for the same reason:
# the table says reel may be given a roll length, not what it does with one.
REEL_COMMAND = "reel"

# The feeds taken by the two commands that feed but are neither a label nor a
# new roll, restated from their handlers in ETKT.cpp. feedCommandInternal()
# feeds once. testCommandFullInternal() feeds ahead of "E-TKT", once for each
# of its characters, and once after. A label's feeds are labelFeeds() and a
# new roll's are REEL_FEEDS, both of which firmware.py reads.
COMMAND_FEEDS = {
    "feed": 1,
    "testfull": 1 + len("E-TKT") + 1,
}

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
        # What the running command was asked for, as CommandOptions carries
        # it on the device: how many labels, and how long a new roll is, 0
        # meaning as long as the last one.
        self.copies = 1
        self.new_roll_mm = 0
        # Where a run of labels has got to. Both are cleared with the command.
        self.copy = 0
        self.stopping = False
        # What Roll keeps in EEPROM. Kept in memory here, so every start of
        # the simulator is a device that has never counted a roll.
        self.roll_mm = device.default_roll_mm
        self.feeds_used = 0
        self.log = deque(maxlen=LOG_LINES)
        self.started = time.monotonic()

        # What Settings::initialize() and Roll::initialize() say on the way
        # up.
        self.record("INFO", "Align factor: %d" % self.align)
        self.record("INFO", "Force factor: %d" % self.force)
        self.record("INFO", "Roll: %d mm, %d feeds used"
                    % (self.roll_mm, self.feeds_used))

    def application(self):
        """Every route the device answers, not yet listening anywhere."""
        app = web.Application()
        routes = [
            web.get('/api/status', self.status),
            web.post('/api/stop', self.stop),
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
        return app

    async def start(self):
        runner = web.AppRunner(self.application())
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
        }
        if running is not None and running.name == PRINTING_COMMAND:
            body['current_label'] = self.label
            body['copy'] = self.copy
            body['copies'] = self.copies
            body['stopping'] = self.stopping
        # Busy or not, as the device sends it.
        body['roll'] = {
            'length_mm': self.roll_mm,
            'remaining_mm': self.device.remaining_mm(self.roll_mm,
                                                     self.feeds_used),
        }
        body['mem_heap_free_bytes'] = FAKE_HEAP_BYTES
        body['mem_largest_free_block_bytes'] = FAKE_LARGEST_BLOCK_BYTES
        body['uptime_ms'] = int((time.monotonic() - self.started) * 1000)
        return web.json_response(body)

    async def stop(self, request):
        """Mirrors Network::stopPostHandler() and ETKT::stop()."""
        if self.command is None:
            # Not an error. The run most likely finished while the tap was
            # on its way.
            return web.json_response({'result': 'idle'})
        if self.command.name != PRINTING_COMMAND:
            return self.refuse("Only printing can be stopped", status=409)
        self.stopping = True
        self.record("INFO", "Stopping after this label")
        return web.json_response({'result': 'stopping'})

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
            'copies': {
                'minimum': 1,
                'maximum': self.device.max_copies,
            },
            'roll': {
                'minimum_mm': self.device.roll_min_mm,
                'maximum_mm': self.device.roll_max_mm,
                'default_mm': self.device.default_roll_mm,
            },
            'feed': {
                'length_um': self.device.feed_length_um,
                'lead': self.device.lead_feeds,
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
        if not isinstance(body, dict):
            # Valid JSON that is not an object. as<JsonObject>() makes that a
            # null object on the device, which contains nothing -- where a
            # string here would answer `in` by searching itself.
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

        # Optional, unlike the fields above: a body without them asks for
        # what every body asked for before they existed -- one label, and a
        # new roll as long as the last.
        copies = 1
        if spec.uses_copies and "copies" in body:
            copies = _as_int(body["copies"])
            if not self.device.valid_copies(copies):
                return self.refuse(
                    "Please provide a copies value between 1 and %d, got %d"
                    % (self.device.max_copies, copies))
        new_roll_mm = 0
        if spec.uses_roll_length and "length_mm" in body:
            new_roll_mm = _as_int(body["length_mm"])
            if not self.device.valid_roll_length(new_roll_mm):
                return self.refuse(
                    "Please provide a length_mm value between %d and %d, "
                    "got %d" % (self.device.roll_min_mm,
                                self.device.roll_max_mm, new_roll_mm))

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
        self.copies = copies
        self.new_roll_mm = new_roll_mm
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
        value = _as_int(body[field])
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
            await self.print_labels()
        elif spec.name == REEL_COMMAND:
            await self.reel()
        else:
            # Any other command takes the same amount of time
            await self.pause(OTHER_COMMAND_SECONDS)
            self.use(COMMAND_FEEDS.get(spec.name, 0))
        # All together, as ETKT::loop() does when it clears the command:
        # leaving progress behind reports an idle printer stuck at 99%, and
        # leaving the stop behind would end the next run after one label.
        self.command = None
        self.running_task = None
        self.progress = 0
        self.copy = 0
        self.stopping = False

    async def reel(self):
        # ETKT::reelCommandInternal(). The new roll is as long as the request
        # said, or as long as the last one, and the feeds that thread it
        # through to the cutter are tape off the new roll.
        self.roll_mm = self.new_roll_mm or self.roll_mm
        self.feeds_used = 0
        # Word for word what Roll::load() logs.
        self.record("INFO", "New roll: %d mm" % self.roll_mm)
        await self.pause(OTHER_COMMAND_SECONDS)
        self.use(self.device.reel_feeds)

    async def print_labels(self):
        # The device uppercases a copy and leaves the submitted label alone,
        # which is why /api/status hands back exactly what the panel sent.
        label = self.label.upper()
        copies = self.copies
        if copies > 1:
            self.record("INFO", "print %s x %d" % (label, copies))
        else:
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
        for copy in range(1, copies + 1):
            self.copy = copy
            self.progress = 0
            for done in range(1, total + 1):
                await self.pause(PRINT_CHARACTER_SECONDS)
                self.progress = self.device.progress_percent(done, total)

            # The top-up and the cut, which is the stretch the device holds
            # its last percentage point back for.
            await self.pause(FINISH_SECONDS)
            # Charged a label at a time, as the device charges it, so what
            # /api/status says is left steps down once per cut.
            self.use(self.device.label_feeds(total))

            # A stop is only ever looked at here, between one cut and the
            # next label, as ETKT::tagCommandInternal() looks at it.
            if self.stopping and copy < copies:
                self.record("INFO", "Stopped after %d of %d" % (copy, copies))
                break
        self.record("INFO", "Printing Complete")

    def use(self, feeds):
        """Counts feeds taken from the roll. Mirrors Roll::use()."""
        self.feeds_used += feeds

    async def pause(self, seconds):
        """Every wait the simulated machine makes goes through here, so a test
        can hold it part way through a job."""
        await asyncio.sleep(seconds)

    def record(self, level, message):
        """One line of the log, formatted the way Logger::recent() is."""
        self.log.append("%.3f %-5s %s"
                        % (time.monotonic() - self.started, level, message))

    def data_path(self, path):
        return os.path.join(os.path.dirname(__file__), '../../data/', path)


def _as_int(value):
    """A JSON value read as a number the way ArduinoJson's as<int>() reads
    one: a number, a numeric string or a boolean, truncated to a whole one,
    and 0 for anything else -- including a number too big for an int, which
    then fails whichever range check the caller makes."""
    try:
        number = int(value)
    except (TypeError, ValueError, OverflowError):
        return 0
    if not -2 ** 31 <= number < 2 ** 31:
        return 0
    return number
