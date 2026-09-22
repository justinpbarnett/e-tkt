import asyncio
import sys

from . import Server, firmware


async def main(device):
    server = Server(device)
    await server.start()
    while (True):
        await asyncio.sleep(1000)

if __name__ == "__main__":
    try:
        device = firmware.load()
    except firmware.FirmwareParseError as problem:
        # Refuse to start rather than serve a device that is not this one.
        # A simulator that is quietly missing a route is worse than no
        # simulator: the panel gets a 404 and nothing says why.
        print("Cannot read the firmware in src/: %s" % problem,
              file=sys.stderr)
        sys.exit(1)
    asyncio.run(main(device))
