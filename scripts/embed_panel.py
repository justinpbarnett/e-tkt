# Puts the panel into the firmware.
#
# The panel is the files in data/. The firmware carries them as constant
# data, in a header this writes into the build folder at each build, and
# the webserver sends them from where the flash is mapped. See
# src/PanelFiles.h. They were files on a SPIFFS partition before, uploaded
# apart from the firmware and copied into RAM at each start: two uploads
# that could be out of step, and a copy that took most of the memory the
# chip has free.
#
# The page's modules are one script on the device. Each of them was its own
# connection, and on the weak link a connection costs more than the bytes.
# data/ in the repo stays as separate modules so the node tests can import
# them. The version of the panel is written into the page's own addresses,
# so a new panel is a new address and a browser can keep the previous one
# for good.
#
# Each file is carried gzipped when that is smaller, and sent with
# Content-Encoding: gzip. A file that does not shrink is carried as it is.

import collections
import gzip
import hashlib
import os
import re
import subprocess
import tempfile

ENTRY = "script.js"
PAGE = "/index.html"
HEADER_NAME = "EmbeddedPanel.h"
TEXT_EXTENSIONS = (".html", ".css", ".js", ".json", ".svg")

# What the firmware tells a browser each kind of file is. A file of a kind
# that is not here stops the build: sent as nothing in particular, it is a
# file the browser does not use.
CONTENT_TYPES = {
    ".css": "text/css",
    ".html": "text/html",
    ".ico": "image/x-icon",
    ".js": "application/javascript",
    ".json": "application/json",
    ".png": "image/png",
    ".svg": "image/svg+xml",
    ".ttf": "font/ttf",
}

# An address made of what needs no escaping, in a request or in the text of
# the header.
PATH_RE = re.compile(r"^(?:/[A-Za-z0-9._-]+)+$")

# import { a, b } from "./tape.js"; including the form that spans lines.
IMPORT_RE = re.compile(
    r"import\s+[\s\S]*?\sfrom\s+['\"](\./[^'\"]+)['\"]\s*;"
)
EXPORT_RE = re.compile(
    r"(?m)^export (?=(?:async )?function |class |const |let |var )"
)

# One file as the firmware carries it. The body is what is sent, gzipped or
# not, and the tag names those bytes.
PanelFile = collections.namedtuple(
    "PanelFile", "path content_type etag body gzip"
)


def javascript_sources(src_root):
    found = {}
    for dirpath, _dirnames, filenames in os.walk(src_root):
        for name in filenames:
            if name.endswith(".js"):
                path = os.path.join(dirpath, name)
                with open(path, encoding="utf-8") as inp:
                    found[name] = inp.read()
    return found


def bundle_modules(sources):
    """One module, dependencies first. Same bindings, one request."""
    if ENTRY not in sources:
        raise SystemExit("panel has no %s" % ENTRY)

    seen = set()
    order = []

    def visit(name, stack):
        if name in stack:
            raise SystemExit("panel import cycle at %s" % name)
        if name in seen:
            return
        if name not in sources:
            raise SystemExit("panel imports missing %s" % name)
        stack.add(name)
        for spec in IMPORT_RE.findall(sources[name]):
            visit(os.path.basename(spec), stack)
        stack.remove(name)
        seen.add(name)
        order.append(name)

    visit(ENTRY, set())

    parts = []
    for name in order:
        text = IMPORT_RE.sub("", sources[name])
        text = EXPORT_RE.sub("", text)
        if re.search(r"(?m)^\s*import |^\s*export ", text):
            raise SystemExit("panel bundle left an import or export in %s" % name)
        parts.append(text)
    bundled = "\n".join(parts)
    if "import.meta" in bundled:
        raise SystemExit("panel bundle still mentions import.meta")
    check_bundle(bundled)
    return bundled, [name for name in order if name != ENTRY]


def check_bundle(bundled):
    """Parse it as a module. A repeated binding is a syntax error there,
    which is how two modules exporting the same name would come out."""
    handle, path = tempfile.mkstemp(suffix=".mjs")
    os.close(handle)
    try:
        with open(path, "w", encoding="utf-8") as out:
            out.write(bundled)
        try:
            result = subprocess.run(
                ["node", "--check", path],
                capture_output=True,
                text=True,
            )
        except FileNotFoundError:
            raise SystemExit(
                "panel bundle cannot be checked without node: install Node.js"
            )
        if result.returncode != 0:
            raise SystemExit(
                "panel bundle failed node --check:\n%s" % (result.stderr or result.stdout)
            )
    finally:
        os.remove(path)


def content_version(plain):
    """Names what the panel serves, from {address: bytes} before the version
    is written into them. So it changes when a file does, and when the way
    the files are made does."""
    digest = hashlib.sha256()
    for path in sorted(plain):
        digest.update(b"%s\0%d\0" % (path.encode("utf-8"), len(plain[path])))
        digest.update(plain[path])
    return digest.hexdigest()[:8]


def asset_names(src_root):
    names = []
    for _dirpath, _dirnames, filenames in os.walk(src_root):
        names.extend(filenames)
    return names


def stamp(text, version, names):
    """Point the page at this panel. The firmware finds the file by its
    address without the query; the browser treats a new query as a
    different file."""
    for name in names:
        for prefix in ("/", "./", ""):
            for quote in ('"', "'"):
                old = "%s%s%s%s" % (quote, prefix, name, quote)
                new = "%s%s%s?v=%s%s" % (quote, prefix, name, version, quote)
                text = text.replace(old, new)
    return text


def asks_for(text, name):
    """Whether the text names the file, in one of the ways stamp() finds."""
    return any(
        "%s%s%s%s" % (quote, prefix, name, quote) in text
        for prefix in ("/", "./", "")
        for quote in ('"', "'")
    )


def content_type(path):
    kind = CONTENT_TYPES.get(os.path.splitext(path)[1].lower())
    if kind is None:
        raise SystemExit(
            "panel file %s is of a type the firmware has no name for" % path
        )
    return kind


def stored(raw):
    """The bytes to carry, and whether they are gzip. No time goes into the
    stream, so the same file is the same bytes at every build."""
    packed = gzip.compress(raw, 9, mtime=0)
    if len(packed) < len(raw):
        return packed, True
    return raw, False


def panel_files(src_root):
    """The panel as the firmware carries it: its version, and its files in
    the order of their addresses."""
    bundled, folded = bundle_modules(javascript_sources(src_root))
    folded = set(folded)

    plain = {}
    for dirpath, _dirnames, filenames in os.walk(src_root):
        for name in filenames:
            if name in folded:
                continue
            src = os.path.join(dirpath, name)
            path = "/" + os.path.relpath(src, src_root).replace(os.sep, "/")
            if not PATH_RE.match(path):
                raise SystemExit(
                    "panel file %s has a name an address cannot carry" % path
                )
            if name == ENTRY:
                plain[path] = bundled.encode("utf-8")
            else:
                with open(src, "rb") as inp:
                    plain[path] = inp.read()
            if not plain[path]:
                raise SystemExit("panel file %s is empty" % path)
    if PAGE not in plain:
        raise SystemExit("panel has no %s" % PAGE)

    version = content_version(plain)
    names = asset_names(src_root)
    files = []
    for path in sorted(plain):
        raw = plain[path]
        if path.endswith(TEXT_EXTENSIONS):
            text = raw.decode("utf-8")
            # A module that was folded into the script is no file on the
            # machine, so a name for it that is left is one nothing answers.
            for name in sorted(folded):
                if asks_for(text, name):
                    raise SystemExit(
                        "panel file %s asks for %s, which is part of %s on "
                        "the machine and no file of its own" % (path, name, ENTRY)
                    )
            raw = stamp(text, version, names).encode("utf-8")
        body, zipped = stored(raw)
        etag = '"%s"' % hashlib.sha256(body).hexdigest()[:16]
        files.append(PanelFile(path, content_type(path), etag, body, zipped))
    return version, files


def c_string(text):
    return '"%s"' % text.replace("\\", "\\\\").replace('"', '\\"')


def header_text(version, files):
    """The header that defines the panel for the firmware. See PanelFiles.h
    for the types."""
    lines = [
        "// Made from data/ by scripts/embed_panel.py at each build. Not to be",
        "// edited: change data/ and build again.",
        "//",
        "// It defines the bytes of the panel, so one source file includes it.",
        "#pragma once",
        "",
        '#include "PanelFiles.h"',
        "",
    ]
    for number, file in enumerate(files):
        lines.append("// %s" % file.path)
        lines.append(
            "static const uint8_t EMBEDDED_PANEL_FILE_%d[] = {" % number
        )
        for start in range(0, len(file.body), 12):
            row = file.body[start : start + 12]
            lines.append("    %s," % ", ".join("0x%02x" % byte for byte in row))
        lines.append("};")
        lines.append("")
    lines.append("static const PanelFile EMBEDDED_PANEL_FILES[] = {")
    for number, file in enumerate(files):
        lines.append(
            "    {%s, %s, %s, EMBEDDED_PANEL_FILE_%d, %d, %s},"
            % (
                c_string(file.path),
                c_string(file.content_type),
                c_string(file.etag),
                number,
                len(file.body),
                "true" if file.gzip else "false",
            )
        )
    lines.append("};")
    lines.append("")
    lines.append(
        "static const PanelFiles EMBEDDED_PANEL = {%s, EMBEDDED_PANEL_FILES, %d};"
        % (c_string(version), len(files))
    )
    return "\n".join(lines) + "\n"


def write_if_changed(path, text):
    """Leaves a file that already says this alone, and says whether it
    wrote. The compiler builds again whatever includes a file that was
    written, and the build runs this every time."""
    try:
        with open(path, encoding="utf-8") as inp:
            if inp.read() == text:
                return False
    except FileNotFoundError:
        pass
    os.makedirs(os.path.dirname(path), exist_ok=True)
    # Whole or not at all, so a build that is stopped here leaves no half of
    # a header for the next one to compile.
    partial = path + ".partial"
    with open(partial, "w", encoding="utf-8") as out:
        out.write(text)
    os.replace(partial, path)
    return True


def embed(env):
    src_root = os.path.abspath(env.subst("$PROJECT_DATA_DIR"))
    made = os.path.abspath(os.path.join(env.subst("$BUILD_DIR"), "panel"))
    version, files = panel_files(src_root)
    write_if_changed(os.path.join(made, HEADER_NAME), header_text(version, files))
    env.Append(CPPPATH=[made])
    # As the firmware says it in its log when it starts, so the two can be
    # told to be the same panel by eye.
    print(
        "panel %s: %d files, %d bytes"
        % (version, len(files), sum(len(file.body) for file in files))
    )


def _run_from_platformio():
    # Import is SCons's. It puts env in this file's globals and returns
    # nothing, and it is absent when this is imported on its own.
    try:
        Import("env")  # noqa: F821
    except NameError:
        return
    embed(env)  # noqa: F821


_run_from_platformio()
