#pragma once

#include <Arduino.h>

#include <map>

// Which slot on the daisy wheel each character sits in. This is the
// firmware's one statement of what the machine can emboss; everything that
// needs to know -- the wheel, the tune the sounder plays, the validator in
// the webapp -- reads it from here or from what this header derives.
//
// Two entries share a slot with a letter on purpose. The wheel carries no 0
// and no 1, so CHARACTERS sends them to the O and the I the way a
// typewriter does. CHARACTER_ALIASES below says so out loud, because until
// it did the only way to find out was to print a label and read it.
extern const std::map<String, int> CHARACTERS;

// The wheel drives here to cut. It is a position on the wheel rather than
// anything a label says, so it is the one CHARACTERS entry a label may not
// contain.
constexpr const char* CUT_CHARACTER = "*";

// What a typed character actually comes out as, for the characters the wheel
// does not carry. Served to the webapp so it can warn before the tape is
// spent rather than after.
extern const std::map<String, String> CHARACTER_ALIASES;

/**
 * @brief Returns every character a label is allowed to contain.
 *
 * That is a space, then every CHARACTERS entry except the cut mark, in the
 * order the map holds them. The space has no wheel slot -- a space is the
 * feeder advancing the tape with nothing pressed into it -- so it is added
 * here rather than living in CHARACTERS and having to be excluded from
 * every lookup.
 *
 * Characters outside the ASCII range are multi-byte UTF-8 and the returned
 * String carries their bytes, so callers must step through it by character
 * rather than by index. Utility::utf8CharAt does that; so does JavaScript's
 * for...of.
 *
 * The result is the whole answer to "may a label say this?". The webapp
 * fetches it at startup rather than keeping its own list, which is what
 * four separate and disagreeing copies of this set used to be.
 */
String printableCharacters();

/**
 * @brief The first character of `label` that a label may not contain, or ""
 * when every one of them is printable.
 *
 * The rule is printableCharacters(): a space, or a character with a slot on
 * the wheel that is not the cut mark. Checked on an upper-cased copy, because
 * that is what the machine prints -- the webapp sends what was typed and
 * ETKT::tagCommandInternal upper-cases it on the way to the press.
 *
 * Steps the label by UTF-8 code point, so the character it hands back is a
 * whole one and can be quoted straight into an error message.
 *
 * This is the device's own answer, not the webapp's. Until it existed only
 * the webapp asked, so a label POSTed straight at /api/tag walked characters
 * the wheel does not carry: DaisyWheel::move() refused each one and cut the
 * coil current, and the press came down regardless, on whichever slot the
 * wheel had stopped at.
 */
String unprintableCharacter(const String& label);
