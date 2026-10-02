"""Checks that the panel goes into the firmware as it is in data/.

embed_panel.py makes the files the firmware carries: the page's modules as
one script, each file gzipped when that is smaller, and the header that
holds them as constant data. A build runs it and the compiler takes what it
wrote, so a mistake in it is a panel that does not load on a machine. This
covers what it makes, from a folder of files to the text of the header. Run
it from the repository root with

    python3 -m unittest discover -s scripts

It needs node, as the build does, to check that the folded script parses.
"""

import gzip
import os
import re
import sys
import tempfile
import unittest
from unittest import mock

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import embed_panel  # noqa: E402  (needs the path)

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DATA = os.path.join(ROOT, "data")

PAGE = b'<script type="module" src="script.js"></script>\n' * 4
SCRIPT = b'console.log("the panel");\n' * 4


class Folder:
    """A folder of panel files for one test, which has a page and a script
    unless the test says otherwise."""

    def __init__(self, test, files=None):
        holder = tempfile.TemporaryDirectory()
        test.addCleanup(holder.cleanup)
        self.root = holder.name
        self.write("index.html", PAGE)
        self.write("script.js", SCRIPT)
        for name, content in (files or {}).items():
            self.write(name, content)

    def write(self, name, content):
        with open(os.path.join(self.root, name), "wb") as out:
            out.write(content)

    def remove(self, name):
        os.remove(os.path.join(self.root, name))

    def panel(self):
        return embed_panel.panel_files(self.root)

    def file(self, path):
        return named(self.panel()[1], path)


def named(files, path):
    for file in files:
        if file.path == path:
            return file
    raise AssertionError("the panel has no %s" % path)


def served(file):
    """What a browser has of a file once it has unpacked it."""
    return gzip.decompress(file.body) if file.gzip else file.body


def read_back(header):
    """The panel a header holds, as (version, {path: fields}), read out of
    its text the way the compiler would lay it out."""
    arrays = {}
    for name, body in re.findall(
            r"static const uint8_t (\w+)\[\] = \{(.*?)\};", header, re.S):
        arrays[name] = bytes(int(byte, 16)
                             for byte in re.findall(r"0x[0-9a-f]{2}", body))
    table = re.search(
        r"static const PanelFile EMBEDDED_PANEL_FILES\[\] = \{(.*?)\n\};",
        header, re.S).group(1)
    files = {}
    for path, kind, etag, array, length, zipped in re.findall(
            r'\{"([^"]*)", "([^"]*)", "((?:[^"\\]|\\.)*)", (\w+), (\d+), '
            r"(true|false)\}", table):
        files[path] = {
            "type": kind,
            "etag": etag.replace('\\"', '"'),
            "body": arrays[array],
            "length": int(length),
            "gzip": zipped == "true",
        }
    version, count = re.search(
        r'static const PanelFiles EMBEDDED_PANEL = \{"([^"]*)", '
        r"EMBEDDED_PANEL_FILES, (\d+)\};", header).groups()
    assert int(count) == len(files), "the header miscounts its files"
    return version, files


class TheFilesOfThePanel(unittest.TestCase):

    def test_the_modules_of_the_page_are_folded_into_one_script(self):
        folder = Folder(self, {
            "script.js": b'import { greet } from "./greeting.js";\ngreet();\n',
            "greeting.js": b'export function greet() {\n  return "hello";\n}\n',
        })

        version, files = folder.panel()

        self.assertEqual(["/index.html", "/script.js"],
                         [file.path for file in files])
        script = served(named(files, "/script.js")).decode("utf-8")
        self.assertIn("function greet()", script)
        self.assertIn("greet();", script)
        self.assertNotIn("import ", script)
        self.assertNotIn("export ", script)

    def test_a_file_is_gzipped_when_that_is_smaller(self):
        noise = os.urandom(512)
        folder = Folder(self, {
            "style.css": b"body { margin: 0; }\n" * 50,
            "icon.png": noise,
        })

        sheet = folder.file("/style.css")
        icon = folder.file("/icon.png")

        self.assertTrue(sheet.gzip)
        self.assertLess(len(sheet.body), 50 * 20)
        self.assertEqual(b"body { margin: 0; }\n" * 50, served(sheet))
        self.assertFalse(icon.gzip)
        self.assertEqual(noise, icon.body)

    def test_the_page_names_its_files_with_the_version_of_the_panel(self):
        folder = Folder(self, {
            "index.html": b'<link href="style.css"><script src="script.js">',
            "style.css": b"body { margin: 0; }\n",
        })

        version, files = folder.panel()

        self.assertRegex(version, r"^[0-9a-f]{8}$")
        self.assertEqual(
            '<link href="style.css?v=%s"><script src="script.js?v=%s">'
            % (version, version),
            served(named(files, "/index.html")).decode("utf-8"))

    def test_a_file_that_changes_changes_the_version_and_its_own_tag(self):
        folder = Folder(self, {"style.css": b"body { margin: 0; }\n"})
        version, files = folder.panel()

        folder.write("style.css", b"body { margin: 1px; }\n")
        later_version, later_files = folder.panel()

        self.assertNotEqual(version, later_version)
        self.assertNotEqual(named(files, "/style.css").etag,
                            named(later_files, "/style.css").etag)

    def test_the_same_files_make_the_same_panel_whenever_it_is_built(self):
        # A gzip stream carries the time it was made unless told not to. With
        # it, every build would be a new firmware and a new tag for a panel
        # that had not changed.
        folder = Folder(self, {"style.css": b"body { margin: 0; }\n" * 50})

        with mock.patch("time.time", return_value=1000000000.0):
            first = folder.panel()
        with mock.patch("time.time", return_value=2000000000.0):
            second = folder.panel()

        self.assertEqual(first, second)

    def test_each_file_is_tagged_and_typed_for_the_browser(self):
        folder = Folder(self, {
            "style.css": b"body { margin: 0; }\n",
            "manifest.json": b"{}\n",
            "icon.png": b"\x89PNG",
            "favicon.ico": b"\x00\x00\x01\x00",
            "drawing.svg": b"<svg/>",
            "fontwhite.ttf": b"\x00\x01\x00\x00",
        })

        version, files = folder.panel()

        self.assertEqual({
            "/drawing.svg": "image/svg+xml",
            "/favicon.ico": "image/x-icon",
            "/fontwhite.ttf": "font/ttf",
            "/icon.png": "image/png",
            "/index.html": "text/html",
            "/manifest.json": "application/json",
            "/script.js": "application/javascript",
            "/style.css": "text/css",
        }, {file.path: file.content_type for file in files})
        for file in files:
            self.assertRegex(file.etag, r'^"[0-9a-f]{16}"$')
        self.assertEqual(len(files), len({file.etag for file in files}))

    def test_a_file_of_a_type_the_firmware_cannot_name_stops_the_build(self):
        folder = Folder(self, {"notes.xyz": b"not for a browser"})

        with self.assertRaises(SystemExit) as stopped:
            folder.panel()

        self.assertIn("notes.xyz", str(stopped.exception))

    def test_a_file_name_an_address_cannot_carry_stops_the_build(self):
        for name in ("my style.css", 'quo"te.css', "café.css"):
            with self.subTest(name=name):
                folder = Folder(self, {name: b"body { margin: 0; }\n"})

                with self.assertRaises(SystemExit) as stopped:
                    folder.panel()

                self.assertIn(name, str(stopped.exception))

    def test_an_empty_file_stops_the_build(self):
        folder = Folder(self, {"style.css": b""})

        with self.assertRaises(SystemExit) as stopped:
            folder.panel()

        self.assertIn("style.css", str(stopped.exception))

    def test_a_panel_without_a_page_stops_the_build(self):
        folder = Folder(self)
        folder.remove("index.html")

        with self.assertRaises(SystemExit) as stopped:
            folder.panel()

        self.assertIn("index.html", str(stopped.exception))

    def test_a_file_asked_for_that_the_firmware_does_not_carry_stops_the_build(
            self):
        # A module folded into the script is no file of its own on the
        # machine. Asked for by its name all the same, it is a 404 there
        # that nothing on this side would have shown.
        folder = Folder(self, {
            "script.js": (b'import { greet } from "./greeting.js";\n'
                          b'greet(await import("./greeting.js"));\n'),
            "greeting.js": b'export function greet() {\n  return "hello";\n}\n',
        })

        with self.assertRaises(SystemExit) as stopped:
            folder.panel()

        self.assertIn("greeting.js", str(stopped.exception))

    def test_every_file_the_panel_asks_for_is_one_the_firmware_carries(self):
        version, files = embed_panel.panel_files(DATA)
        paths = {file.path for file in files}

        asked = set()
        for file in files:
            if file.path.endswith(embed_panel.TEXT_EXTENSIONS):
                for name, under in re.findall(
                        r"""["'](?:\./|/)?([A-Za-z0-9._-]+)\?v=(\w+)["']""",
                        served(file).decode("utf-8")):
                    self.assertEqual(version, under)
                    asked.add("/" + name)

        self.assertIn("/script.js", asked)
        self.assertIn("/style.css", asked)
        self.assertEqual(set(), asked - paths)

    def test_a_build_without_node_says_that_node_is_what_it_lacks(self):
        folder = Folder(self)

        with mock.patch("subprocess.run", side_effect=FileNotFoundError("node")):
            with self.assertRaises(SystemExit) as stopped:
                folder.panel()

        self.assertIn("node", str(stopped.exception))
        self.assertIn("install", str(stopped.exception))

    def test_the_panel_in_data_is_one_the_firmware_can_carry(self):
        version, files = embed_panel.panel_files(DATA)

        paths = [file.path for file in files]
        self.assertIn("/index.html", paths)
        self.assertEqual(["/script.js"],
                         [path for path in paths if path.endswith(".js")])
        page = served(named(files, "/index.html")).decode("utf-8")
        self.assertIn("script.js?v=%s" % version, page)
        self.assertIn("style.css?v=%s" % version, page)


class TheHeader(unittest.TestCase):

    def test_it_carries_every_file_byte_for_byte(self):
        version, files = embed_panel.panel_files(DATA)

        held_version, held = read_back(embed_panel.header_text(version, files))

        self.assertEqual(version, held_version)
        self.assertEqual([file.path for file in files], list(held))
        for file in files:
            with self.subTest(path=file.path):
                self.assertEqual({
                    "type": file.content_type,
                    "etag": file.etag,
                    "body": file.body,
                    "length": len(file.body),
                    "gzip": file.gzip,
                }, held[file.path])

    def test_it_is_the_same_text_for_the_same_panel(self):
        folder = Folder(self)

        first = embed_panel.header_text(*folder.panel())
        second = embed_panel.header_text(*folder.panel())

        self.assertEqual(first, second)

    def test_it_is_written_only_when_it_has_changed(self):
        # The compiler builds again what includes a file that was written,
        # and the build writes this one every time it runs.
        holder = tempfile.TemporaryDirectory()
        self.addCleanup(holder.cleanup)
        path = os.path.join(holder.name, "made", "EmbeddedPanel.h")

        self.assertTrue(embed_panel.write_if_changed(path, "one\n"))
        os.utime(path, (1000000000, 1000000000))
        self.assertFalse(embed_panel.write_if_changed(path, "one\n"))
        self.assertEqual(1000000000, os.stat(path).st_mtime)

        self.assertTrue(embed_panel.write_if_changed(path, "two\n"))
        with open(path, encoding="utf-8") as written:
            self.assertEqual("two\n", written.read())


if __name__ == "__main__":
    unittest.main()
