"""Builds the firmware for this computer and serves the panel in front of it.

    python3 -m src.simulator [--port PORT] [--speed N] [--lose PERCENT]

from the repository root. See server.py.
"""

import argparse
import asyncio
import math
import os
import sys

from . import BuildFailed, DeviceError, NoPlatformIO, Server, build, weak_link

# Where the device serves the panel, so the address is the same one.
DEFAULT_PORT = 80


def speed(text):
    """A --speed: a number above 0, as the firmware's own --speed takes."""
    try:
        value = float(text)
    except ValueError:
        raise argparse.ArgumentTypeError("%r is not a number" % text)
    if not (value > 0 and math.isfinite(value)):
        raise argparse.ArgumentTypeError("%r is not above 0" % text)
    return value


def percent(text):
    """A --lose: a number from 0 to 100."""
    try:
        value = float(text)
    except ValueError:
        raise argparse.ArgumentTypeError("%r is not a number" % text)
    if not 0 <= value <= 100:
        raise argparse.ArgumentTypeError("%r is not from 0 to 100" % text)
    return value


def arguments():
    parser = argparse.ArgumentParser(
        prog="python3 -m src.simulator",
        description="Serves the panel in data/ in front of the firmware, "
                    "built for this computer, so the panel can be worked on "
                    "without a machine.")
    parser.add_argument(
        "--port", type=int, default=DEFAULT_PORT,
        help="where to serve the panel (default %d, as the device does)"
             % DEFAULT_PORT)
    parser.add_argument(
        "--speed", type=speed, default=1.0,
        help="how many times faster than the machine to run (default 1)")
    parser.add_argument(
        "--lose", type=percent, default=0.0, metavar="PERCENT",
        help="how many in a hundred requests to the api get no answer, as "
             "on a weak network: half never reach the machine, and half are "
             "answered where the answer never gets back (default 0)")
    return parser.parse_args()


async def serve(options):
    """Serves until cancelled, and returns the exit status."""
    print("Building the firmware for this computer...", file=sys.stderr)
    try:
        program = build()
    except (NoPlatformIO, BuildFailed) as problem:
        print(problem, file=sys.stderr)
        return 1
    device = Server(program, options.speed,
                    loses=weak_link(options.lose / 100))
    try:
        try:
            commands = await device.start()
        except DeviceError as problem:
            print(problem, file=sys.stderr)
            return 1
        try:
            await device.listen(options.port)
        except OSError as problem:
            # asyncio wraps the reason in the address it tried, which the
            # port already says.
            reason = (os.strerror(problem.errno) if problem.errno
                      else str(problem))
            print("Could not serve the panel at port %d: %s. Try another "
                  "with --port." % (options.port, reason), file=sys.stderr)
            return 1
        # Flushed, so the address shows when stdout is a pipe or a file too.
        print("Serving %d commands: %s" % (len(commands), ", ".join(commands)))
        print("Open http://localhost%s/"
              % ("" if options.port == DEFAULT_PORT else ":%d" % options.port),
              flush=True)
        await asyncio.Event().wait()
    finally:
        await device.close()


def main():
    options = arguments()
    # A build as root leaves files in .pio/ that only root can change, and
    # the next build, or `pio test`, run as anyone else then fails.
    if hasattr(os, "geteuid") and os.geteuid() == 0:
        print("Please run the simulator as yourself rather than as root: it "
              "builds the firmware, and would leave files in .pio/ that only "
              "root can change. A port above 1023, with --port 8080, needs no "
              "root.", file=sys.stderr)
        return 1
    try:
        return asyncio.run(serve(options))
    except KeyboardInterrupt:
        # Ctrl-C, which is how it is meant to be stopped.
        return 0


if __name__ == "__main__":
    sys.exit(main())
