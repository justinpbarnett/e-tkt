"""What the device is, read out of src/ rather than written down again here.

The simulator used to restate the firmware from memory, and the firmware
moved on without it. It served one endpoint, /api/task, that the device
stopped answering to, so every button on the panel it hosts returned a 404.
It had no idea which characters the wheel carries. Its calibration defaults
were a pair of numbers nobody had checked since they were typed.

Every one of those was a copy of something in src/, and every copy drifted.
So nothing here is a copy. Each fact is parsed out of the file that defines
it for the device, and a parse that finds nothing raises rather than falling
back to a guess: a simulator that quietly serves fewer routes than the
device is worse than one that refuses to start.
"""

import os
import re


class FirmwareParseError(Exception):
    """A firmware fact could not be read. Says which file it looked in."""


class Command:
    """One row of ETKT::COMMANDS, which is one endpoint on the device.

    The fields are the row's, so see the comments on CommandSpec in
    src/ETKT.h for what each one means. `runnable` is that row having a
    handler: IDLE is a status rather than a job, and gets no route.
    """

    def __init__(self, name, uses_align, uses_force, label_field,
                 field_is_label, runnable):
        self.name = name
        self.uses_align = uses_align
        self.uses_force = uses_force
        self.label_field = label_field
        self.field_is_label = field_is_label
        self.runnable = runnable

    def __repr__(self):
        return "<Command %s>" % self.name


class Firmware:
    """Everything the simulator needs to answer the way the device would."""

    def __init__(self, commands, printable, aliases, calibration_min,
                 calibration_max, default_align, default_force, progress_max,
                 min_label_characters, max_label_characters):
        self.commands = commands
        self.printable = printable
        self.aliases = aliases
        self.calibration_min = calibration_min
        self.calibration_max = calibration_max
        self.default_align = default_align
        self.default_force = default_force
        self.progress_max = progress_max
        self.min_label_characters = min_label_characters
        self.max_label_characters = max_label_characters

    def unprintable_character(self, label):
        """The first character of `label` a label may not contain, or "".

        Mirrors unprintableCharacter() in CharacterSet.cpp, upper-casing
        first for the same reason: the panel sends what was typed and the
        device upper-cases it on the way to the press.
        """
        for character in label.upper():
            if character not in self.printable:
                return character
        return ""

    def command(self, name):
        """The row with this name, or None. Mirrors commandSpecByName()."""
        for spec in self.commands:
            if spec.name == name:
                return spec
        return None

    def routes(self):
        """The commands that get a POST endpoint. Mirrors Network.cpp."""
        return [spec for spec in self.commands if spec.runnable]

    def valid_calibration(self, value):
        """Mirrors isValidCalibrationValue()."""
        return (isinstance(value, int) and not isinstance(value, bool) and
                self.calibration_min <= value <= self.calibration_max)

    def progress_percent(self, characters_done, label_length):
        """Mirrors progressPercent() in src/Progress.h, cap and all."""
        if characters_done <= 0 or label_length <= 0:
            return 0
        percent = 100 * characters_done // label_length
        return min(percent, self.progress_max)


def load(src_dir=None):
    """Reads every firmware fact. Raises FirmwareParseError if one is missing.

    `src_dir` is the firmware source directory; it defaults to the src/ this
    file lives under, so the simulator describes the checkout it was started
    from and not some other one.
    """
    if src_dir is None:
        src_dir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

    etkt = _read(os.path.join(src_dir, "ETKT.cpp"))
    characters_source = _read(os.path.join(src_dir, "CharacterSet.cpp"))
    characters_header = _read(os.path.join(src_dir, "CharacterSet.h"))
    geometry = _read(os.path.join(src_dir, "PressGeometry.h"))
    settings = _read(os.path.join(src_dir, "Settings.h"))
    progress = _read(os.path.join(src_dir, "Progress.h"))
    configuration = _read(os.path.join(src_dir, "Configuration.h"))

    return Firmware(
        commands=parse_commands(etkt),
        printable=parse_printable(characters_source, characters_header),
        aliases=parse_aliases(characters_source),
        calibration_min=_number(geometry, "CALIBRATION_VALUE_MIN"),
        calibration_max=_number(geometry, "CALIBRATION_VALUE_MAX"),
        default_align=_number(settings, "DEFAULT_ALIGN_FACTOR"),
        default_force=_number(settings, "DEFAULT_FORCE_FACTOR"),
        progress_max=_number(progress, "PROGRESS_MAX_WHILE_PRINTING"),
        min_label_characters=_number(
            configuration, "MIN_LABEL_CHARACTERS"),
        max_label_characters=_number(
            configuration, "MAX_LABEL_CHARACTERS"),
    )


# One row of the table: enumerator, wire name, the two calibration flags, the
# label field or NULL, whether that field is text to emboss, and the handler
# or NULL.
_COMMAND_ROW = re.compile(
    r'\{\s*Command::\w+\s*,'
    r'\s*"([^"]*)"\s*,'
    r'\s*(true|false)\s*,'
    r'\s*(true|false)\s*,'
    r'\s*(NULL|"[^"]*")\s*,'
    r'\s*(true|false)\s*,'
    r'\s*(NULL|&ETKT::\w+)\s*,?\s*\}', re.S)


def parse_commands(source):
    """Every row of ETKT::COMMANDS, in the order the table lists them."""
    body = _initializer(source, r"const\s+CommandSpec\s+ETKT::COMMANDS\[\]",
                        "ETKT::COMMANDS")
    commands = []
    for name, align, force, label, is_label, handler in _COMMAND_ROW.findall(
            body):
        commands.append(Command(
            name=name,
            uses_align=align == "true",
            uses_force=force == "true",
            label_field=None if label == "NULL" else label.strip('"'),
            field_is_label=is_label == "true",
            runnable=handler != "NULL",
        ))

    # Every row names its enumerator, so counting those says how many rows
    # the table has without depending on the shape of the rest of one. All
    # or nothing on purpose: a row that half-matches is the failure this
    # module exists to prevent, and it would show up as one command quietly
    # missing its endpoint rather than as anything going wrong here.
    declared = len(re.findall(r"\bCommand::\w+", body))
    if len(commands) != declared:
        raise FirmwareParseError(
            "ETKT::COMMANDS lists %d commands but only %d rows were "
            "recognised. Has CommandSpec changed shape?"
            % (declared, len(commands)))
    if not commands:
        raise FirmwareParseError("ETKT::COMMANDS lists no commands at all")
    return commands


# A {"key", value} pair in a std::map initializer, either map in
# CharacterSet.cpp. The value is a slot number or a quoted character.
_MAP_ENTRY = re.compile(r'\{\s*"([^"]*)"\s*,\s*(?:"([^"]*)"|(\d+))\s*\}')


def parse_printable(source, header):
    """Every character a label may contain. Mirrors printableCharacters().

    That is a space, then every CHARACTERS key except the cut mark, in the
    order std::map<String, int> holds them. Arduino's String compares with
    strcmp, so the order is by UTF-8 bytes -- which is what sorting Python
    strings by their encoding gives, and is not the same as sorting the
    strings themselves once the wheel's four symbols are in play.
    """
    body = _initializer(
        source, r"const\s+std::map<String,\s*int>\s+CHARACTERS", "CHARACTERS")
    keys = [entry[0] for entry in _MAP_ENTRY.findall(body)]
    if not keys:
        raise FirmwareParseError("CHARACTERS was found but it lists nothing")

    # Either spelling: the header says constexpr today, upstream says
    # #define, and the simulator should keep working across a merge either
    # way.
    cut = re.search(
        r'(?:#define\s+CUT_CHARACTER\s+|CUT_CHARACTER\s*=\s*)"([^"]*)"',
        header)
    if cut is None:
        raise FirmwareParseError("No CUT_CHARACTER define in CharacterSet.h")

    printable = [key for key in keys if key != cut.group(1)]
    printable.sort(key=str.encode)
    return " " + "".join(printable)


def parse_aliases(source):
    """What the characters the wheel does not carry come out as instead."""
    body = _initializer(
        source, r"const\s+std::map<String,\s*String>\s+CHARACTER_ALIASES",
        "CHARACTER_ALIASES")
    aliases = {}
    for typed, prints_as, _slot in _MAP_ENTRY.findall(body):
        aliases[typed] = prints_as
    if not aliases:
        raise FirmwareParseError(
            "CHARACTER_ALIASES was found but it lists nothing")
    return aliases


def _read(path):
    try:
        with open(path, encoding="utf-8") as handle:
            return handle.read()
    except OSError as problem:
        raise FirmwareParseError("Cannot read %s: %s" % (path, problem))


def _initializer(source, declaration, name):
    """The body of a brace initializer, with its comments taken out.

    Walks the braces rather than matching to the first `};`. A comment
    inside one of these tables is free to contain a brace or a `};` -- one
    of them says "IDLE is a status, not a job;" today -- and stopping at the
    text would cut the table short. The rows after the cut would go missing
    together with the count that is supposed to notice, which is the exact
    failure this module exists to prevent.
    """
    opening = re.search(declaration + r"\s*=\s*\{", source)
    if opening is None:
        raise FirmwareParseError("No %s initializer found" % name)

    body = []
    depth = 1
    index = opening.end()
    # Reads through source in place. Slicing the remainder off on each turn
    # copied the rest of the file once per character, which is the whole file
    # squared for a table near the top of a long one.
    while index < len(source) and depth > 0:
        if source.startswith("//", index):
            index = source.find("\n", index)
            if index < 0:
                break
            continue
        if source.startswith("/*", index):
            end = source.find("*/", index + 2)
            if end < 0:
                break
            index = end + 2
            continue
        character = source[index]
        if character in "\"'":
            end = index + 1
            while end < len(source) and source[end] != character:
                end += 2 if source[end] == "\\" else 1
            body.append(source[index:end + 1])
            index = end + 1
            continue
        if character == "{":
            depth += 1
        elif character == "}":
            depth -= 1
            if depth == 0:
                return "".join(body)
        body.append(character)
        index += 1

    raise FirmwareParseError(
        "%s is not closed: ran off the end of the file looking for its "
        "last brace" % name)


def _number(source, name):
    """An integer #define or constexpr int, whichever the file uses."""
    match = re.search(
        r"(?:#define\s+%s\s+|\b%s\s*=\s*)(-?\d+)" % (name, name), source)
    if match is None:
        raise FirmwareParseError("No %s found" % name)
    return int(match.group(1))
