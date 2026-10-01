# Stages the panel for the SPIFFS image.
#
# The page's modules are one script on the device. Each of them was its own
# connection, and on the weak link a connection costs more than the bytes.
# data/ in the repo stays as separate modules so the node tests can import
# them. A content hash is written into the page's own addresses, so a new
# image is a new address and a browser can keep the previous one for good.
#
# Each file is stored gzipped when that is smaller. ESPAsyncWebServer
# serves "<path>.gz" with Content-Encoding: gzip and the content type of
# the original path, when the plain file is absent. A file that does not
# shrink is copied as it is. The image is built from the staging
# directory, so data/ stays readable.
#
# A SPIFFS object name on this core is 32 bytes including the leading
# slash and the trailing NUL, so the stored path may be 31 characters.

import gzip
import hashlib
import os
import re
import shutil
import subprocess
import tempfile

NAME_LIMIT = 31
ENTRY = "script.js"
TEXT_EXTENSIONS = (".html", ".css", ".js", ".json", ".svg")

# import { a, b } from "./tape.js"; including the form that spans lines.
IMPORT_RE = re.compile(
    r"import\s+[\s\S]*?\sfrom\s+['\"](\./[^'\"]+)['\"]\s*;"
)
EXPORT_RE = re.compile(
    r"(?m)^export (?=(?:async )?function |class |const |let |var )"
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
        result = subprocess.run(
            ["node", "--check", path],
            capture_output=True,
            text=True,
        )
        if result.returncode != 0:
            raise SystemExit(
                "panel bundle failed node --check:\n%s" % (result.stderr or result.stdout)
            )
    finally:
        os.remove(path)


def content_version(src_root):
    digest = hashlib.sha256()
    for dirpath, _dirnames, filenames in os.walk(src_root):
        for name in sorted(filenames):
            path = os.path.join(dirpath, name)
            rel = os.path.relpath(path, src_root).replace(os.sep, "/")
            digest.update(rel.encode("utf-8"))
            digest.update(b"\0")
            with open(path, "rb") as inp:
                digest.update(inp.read())
    return digest.hexdigest()[:8]


def asset_names(src_root):
    names = []
    for _dirpath, _dirnames, filenames in os.walk(src_root):
        names.extend(filenames)
    return names


def stamp(text, version, names):
    """Point the page at this image. The server ignores the query; the
    browser treats a new one as a different file."""
    for name in names:
        for prefix in ("/", "./", ""):
            for quote in ('"', "'"):
                old = "%s%s%s%s" % (quote, prefix, name, quote)
                new = "%s%s%s?v=%s%s" % (quote, prefix, name, version, quote)
                text = text.replace(old, new)
    return text


def write_stored(dest, rel, raw):
    packed = gzip.compress(raw, 9)
    if len(packed) < len(raw):
        rel = rel + ".gz"
        body = packed
    else:
        body = raw
    out = os.path.join(dest, rel)
    os.makedirs(os.path.dirname(out) or dest, exist_ok=True)
    with open(out, "wb") as outp:
        outp.write(body)
    stored = "/" + rel.replace(os.sep, "/")
    if len(stored) > NAME_LIMIT:
        raise SystemExit("SPIFFS name too long: %s" % stored)
    return len(raw), len(body)


def stage(env):
    src_root = os.path.abspath(env.subst("$PROJECT_DATA_DIR"))
    dest = os.path.abspath(os.path.join(env.subst("$BUILD_DIR"), "data-gzip"))
    if src_root == dest:
        return
    if os.path.isdir(dest):
        shutil.rmtree(dest)
    os.makedirs(dest)

    sources = javascript_sources(src_root)
    bundled, folded = bundle_modules(sources)
    version = content_version(src_root)
    names = asset_names(src_root)
    folded = set(folded)
    plain_total = 0
    stored_total = 0

    for dirpath, _dirnames, filenames in os.walk(src_root):
        rel_dir = os.path.relpath(dirpath, src_root)
        if rel_dir == ".":
            rel_dir = ""
        for name in filenames:
            if name in folded:
                continue
            rel = os.path.join(rel_dir, name) if rel_dir else name
            if name == ENTRY:
                text = stamp(bundled, version, names)
                raw = text.encode("utf-8")
            else:
                src = os.path.join(dirpath, name)
                with open(src, "rb") as inp:
                    raw = inp.read()
                if name.endswith(TEXT_EXTENSIONS):
                    text = stamp(raw.decode("utf-8"), version, names)
                    raw = text.encode("utf-8")
            plain, stored = write_stored(dest, rel, raw)
            plain_total += plain
            stored_total += stored
    print(
        "panel %s: %d bytes staged as %d"
        % (version, plain_total, stored_total)
    )
    env.Replace(PROJECT_DATA_DIR=dest)


def _run_from_platformio():
    # Import is SCons's. It puts env in this file's globals and returns
    # nothing, and it is absent when the bundler is imported on its own.
    try:
        Import("env")  # noqa: F821
    except NameError:
        return
    stage(env)  # noqa: F821


_run_from_platformio()
