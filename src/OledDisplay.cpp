#include "OledDisplay.h"

#include <Arduino.h>
#include <U8g2lib.h>
#include <qrcode.h>

#include <map>
#include <vector>

#include "Configuration.h"
#include "Progress.h"
#include "Sound.h"
#include "Utility.h"
#include "etktLogo.h"

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

namespace {

const String STARTUP_MELODY = "  E.TKT ";
const String AUTHOR_SIGNATURE = "andrei.cc";

// How one character of a label is drawn on the progress screen: the font,
// the code of the glyph in it, how wide it is, and how far it sits off the
// line the rest of the label is drawn on.
struct FontInfo {
  const uint8_t* font;
  int code;
  int width;
  int width_offset;
  int height_offset;
  bool isSymbol() const { return font != u8g2_font_6x13_te; }

  FontInfo(const uint8_t* font, int code, int width, int width_offset,
           int height_offset) {
    this->font = font;
    this->code = code;
    this->width = width;
    this->width_offset = width_offset;
    this->height_offset = height_offset;
  }
};

// For each non-ascii "glyph" character, the font, symbol code, width, x
// offset and y offset it is drawn with. The offsets align the glyph with the
// rest of the label text, which comes from a font that spaces differently.
//
// Only this screen draws a label, so the table is this file's own. It used
// to sit in Characters.cpp, which put U8g2 fonts in the way of everything
// that only wanted to know where a character sits on the wheel.
const std::map<String, FontInfo> GLYPHS = {
    {"♡", FontInfo(u8g2_font_6x12_t_symbols, 0x2664, 5, -1, -1)},
    {"☆", FontInfo(u8g2_font_6x12_t_symbols, 0x2605, 5, -1, -1)},
    {"♪", FontInfo(u8g2_font_siji_t_6x10, 0xE271, 5, -3, 0)},
    {"€", FontInfo(u8g2_font_6x12_t_symbols, 0x20AC, 6, -1, -1)}};

// How to draw `character`: its row in GLYPHS, or the label font for
// everything else.
FontInfo fontFor(const String& character) {
  const std::map<String, FontInfo>::const_iterator found =
      GLYPHS.find(character);
  if (found == GLYPHS.end()) {
    return FontInfo(u8g2_font_6x13_te, 0, 7, 0, 0);
  }
  return found->second;
}

}  // namespace

OledDisplay::OledDisplay(Sound* sound,
                         U8G2_SSD1306_128X64_NONAME_F_HW_I2C* u8g2) {
  this->u8g2 = u8g2;
  this->sound = sound;
}

OledDisplay::~OledDisplay() {
  // The screen is handed in, not built here, so it is not ours to delete.
  // qrcode is built in the member initialiser above, so it is.
  delete this->qrcode;
}

void OledDisplay::initialize() {
  // starts and sets up the display
  this->u8g2->begin();
  this->u8g2->clearBuffer();
  this->u8g2->setContrast(8);  // 0 > 255
  this->u8g2->setDrawColor(1);
  this->clear();
}

void OledDisplay::setConnectionInfo(const ConnectionInfo& info) {
  this->lock.lock();
  this->info = info;
  this->changed = true;
  this->lock.unlock();
}

bool OledDisplay::takeConnectionChange() {
  this->lock.lock();
  const bool was = this->changed;
  this->changed = false;
  this->lock.unlock();
  return was;
}

void OledDisplay::clear(int color) {
  // Paints every pixel the target colour. This used to be a nested loop
  // calling setDrawColor and drawPixel 8192 times per screen change, which is
  // 16384 calls into U8G2 to fill a buffer that drawBox fills in one.
  this->u8g2->setDrawColor(color);
  this->u8g2->drawBox(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
  this->u8g2->setDrawColor(color == 0 ? 1 : 0);
  this->u8g2->setFont(u8g2_font_6x13_te);
}

void OledDisplay::playSplashScreen() {
  // initial start screen. The screen is already running: ETKT::initialize()
  // started it, and a second begin() only blanks it again.

  // invert colors
  this->clear(1);

  this->u8g2->setFont(u8g2_font_nine_by_five_nbp_t_all);
  this->u8g2->setDrawColor(0);
  this->u8g2->drawStr(40, 53, AUTHOR_SIGNATURE.c_str());
  this->u8g2->sendBuffer();

  this->u8g2->setDrawColor(1);

  const std::vector<String> melody = Utility::characters(STARTUP_MELODY);
  size_t n = 1;

  // animated splash
  for (int i = 128; i > 7; i = i - 18) {
    for (int j = 0; j < 18; j += 9) {
      this->u8g2->drawXBM(i - j - 11, 8, 128, 32, etktLogo);
      this->u8g2->sendBuffer();
    }
    // A frame past the end of the tune is a silent one, not a read past the
    // end of it.
    if (n < melody.size() && melody[n] != " ") {
      this->sound->play(melody[n], 200);
    }
    n++;
  }

  // draw a box with subtractive color
  this->u8g2->setDrawColor(2);
  this->u8g2->drawBox(0, 0, 128, 64);
  this->u8g2->sendBuffer();

  sound->play(3000, 150);
}

// ---------------------------------------------------------------------------
// The fixed screens.
//
// Nine renderers used to sit here, each one nine lines long, each one
// differing from its neighbours in a word, an icon and a couple of pixel
// offsets. Those differences are the table below; the drawing is drawBanner
// and drawNotice. The pixel positions are carried over exactly, so nothing on
// the glass moves.
// ---------------------------------------------------------------------------

// Everything from here to the end of the table is this file's own. None of
// it is named in a header: Display.h used to declare the two draw helpers
// as members taking `const struct ScreenSpec&`, which gave a purely internal
// type external linkage and put a type the header cannot see into its
// interface.
namespace {

// No icon. 0 is not a drawable glyph in u8g2_font_open_iconic_all_1x_t.
constexpr int NO_GLYPH = 0;

enum class ScreenLayout {
  // One word centred at y=37, an icon either side of it.
  BANNER,
  // A title at y=12 with an icon beside it, then two lines of body text.
  NOTICE,
};

struct ScreenSpec {
  Screen screen;
  ScreenLayout layout;
  // True paints the screen white and the text black. Only FINISHED does.
  bool inverted;
  const char* title;
  int titleX;
  // NOTICE only.
  const char* line1;
  const char* line2;
  uint16_t glyph;
  // BANNER only: where the two copies of the icon sit.
  int glyphLeftX;
  int glyphRightX;
};

const ScreenSpec SCREEN_SPECS[] = {
    {Screen::WIFI_RESET, ScreenLayout::NOTICE, false, "WI-FI RESET", 15,
     "Connection cleared!", "Release the button.", 0x00cd, 0, 0},
    {Screen::CUTTING, ScreenLayout::BANNER, false, "CUTTING", 44, nullptr,
     nullptr, 0x00f2, 26, 90},
    {Screen::FEEDING, ScreenLayout::BANNER, false, "FEEDING", 44, nullptr,
     nullptr, 0x006e, 26, 90},
    {Screen::REELING, ScreenLayout::BANNER, false, "REELING", 44, nullptr,
     nullptr, 0x00d5, 26, 90},
    // REELING the other way round: the same gaps either side of the word,
    // and the arrows turned to point back.
    {Screen::UNLOADING, ScreenLayout::BANNER, false, "UNLOADING", 38, nullptr,
     nullptr, 0x00d6, 21, 97},
    {Screen::TESTING, ScreenLayout::BANNER, false, "TESTING", 44, nullptr,
     nullptr, 0x0073, 26, 90},
    {Screen::FINISHED, ScreenLayout::BANNER, true, "FINISHED!", 42, nullptr,
     nullptr, 0x0073, 27, 90},
    // Where the idle screen would be while the roll is out: see
    // ETKT::showIdle(). REELING's arrows, for the load the button then does.
    {Screen::NEW_ROLL, ScreenLayout::NOTICE, false, "NEW ROLL", 15,
     "Put the new roll in,", "then press the button.", 0x00d5, 0, 0},
    {Screen::REBOOTING, ScreenLayout::BANNER, false, "REBOOTING...", 38,
     nullptr, nullptr, NO_GLYPH, 0, 0},
};

// Draws one word centred between two icons. The shape behind CUTTING,
// FEEDING, REELING, UNLOADING, TESTING, FINISHED and REBOOTING. The caller
// has already cleared the glass, which leaves the draw colour set to the
// opposite of the background so the text reads either way round.
void drawBanner(U8G2_SSD1306_128X64_NONAME_F_HW_I2C* u8g2,
                const ScreenSpec& spec) {
  u8g2->setFont(u8g2_font_nine_by_five_nbp_t_all);
  u8g2->drawStr(spec.titleX, 37, spec.title);

  if (spec.glyph != NO_GLYPH) {
    u8g2->setFont(u8g2_font_open_iconic_all_1x_t);
    u8g2->drawGlyph(spec.glyphLeftX, 37, spec.glyph);
    u8g2->drawGlyph(spec.glyphRightX, 37, spec.glyph);
  }

  u8g2->sendBuffer();
}

// Draws a titled notice with two lines of body text and one icon beside the
// title. The shape behind WIFI_RESET and NEW_ROLL.
void drawNotice(U8G2_SSD1306_128X64_NONAME_F_HW_I2C* u8g2,
                const ScreenSpec& spec) {
  u8g2->setFont(u8g2_font_nine_by_five_nbp_t_all);
  u8g2->drawStr(spec.titleX, 12, spec.title);
  u8g2->drawStr(3, 32, spec.line1);
  u8g2->drawStr(3, 47, spec.line2);

  u8g2->setFont(u8g2_font_open_iconic_all_1x_t);
  u8g2->drawGlyph(3, 12, spec.glyph);

  u8g2->sendBuffer();
}

}  // namespace

// Catches the one mistake this table invites: adding an enumerator to Screen
// and forgetting its row. REBOOTING is last, so its value plus one is how
// many rows there have to be.
static_assert(sizeof(SCREEN_SPECS) / sizeof(SCREEN_SPECS[0]) ==
                  static_cast<int>(Screen::REBOOTING) + 1,
              "every Screen needs a row in SCREEN_SPECS");

void OledDisplay::render(Screen screen) {
  const int count = sizeof(SCREEN_SPECS) / sizeof(SCREEN_SPECS[0]);
  for (int i = 0; i < count; i++) {
    if (SCREEN_SPECS[i].screen != screen) {
      continue;
    }
    const ScreenSpec& spec = SCREEN_SPECS[i];
    // Both layouts started with this identical call, so it belongs here
    // rather than at the top of each of them.
    this->clear(spec.inverted ? 1 : 0);
    if (spec.layout == ScreenLayout::NOTICE) {
      drawNotice(this->u8g2, spec);
    } else {
      drawBanner(this->u8g2, spec);
    }
    return;
  }
  // Unreachable: the static_assert above counts the rows, and every row
  // names a distinct Screen. Leave whatever is on the glass rather than
  // blanking it, so a missing row shows up as a screen that did not change
  // instead of one that went dark.
}

// ---------------------------------------------------------------------------
// The idle screen.
// ---------------------------------------------------------------------------

namespace {

// The QR code has the right half of the glass to itself. Everything else
// has to end before it.
constexpr int QR_LEFT = SCREEN_WIDTH - 64;

// With no code to draw, a line runs to as far from the right edge as it
// starts from the left one.
constexpr int TEXT_RIGHT = SCREEN_WIDTH - 3;

// How many bytes of text a version 3 code holds at each level of error
// correction, from the level that reads through the most damage down. The
// library does not check: a text too long for its level is written over the
// correction codewords, and one longer still past the end of the buffer.
struct CodeLevel {
  uint8_t ecc;
  unsigned int bytes;
};

const CodeLevel CODE_LEVELS[] = {
    {ECC_QUARTILE, 32}, {ECC_MEDIUM, 42}, {ECC_LOW, 53}};

// The level a text is drawn at: the highest it leaves room for. NULL for no
// text, and for one too long for any, which gets no code.
const CodeLevel* codeLevelFor(const String& text) {
  if (text.length() == 0) {
    return NULL;
  }
  for (const CodeLevel& level : CODE_LEVELS) {
    if (text.length() <= level.bytes) {
      return &level;
    }
  }
  return NULL;
}

// The fonts a line steps down through until it fits, the house font first.
//
// The name of the machine's own network is what a phone's list of networks
// is searched for, so it has to show whole, and the second of these is where
// its ten characters fit beside the code. The line under it holds the
// address, or the password of that network, which is typed in from here when
// the code will not scan. An address is figures and dots, which have
// narrower fonts than letters do: the last of them fits any address there
// is.
const uint8_t* const NAME_FONTS[] = {u8g2_font_nine_by_five_nbp_t_all,
                                     u8g2_font_5x7_tr};
const uint8_t* const WORD_FONTS[] = {u8g2_font_nine_by_five_nbp_t_all,
                                     u8g2_font_5x7_tr, u8g2_font_4x6_tr};
const uint8_t* const FIGURE_FONTS[] = {u8g2_font_nine_by_five_nbp_t_all,
                                       u8g2_font_miranda_nbp_tn,
                                       u8g2_font_squeezed_r7_tn};

bool isFigures(const String& text) {
  for (unsigned int i = 0; i < text.length(); i++) {
    const char c = text.charAt(i);
    if ((c < '0' || c > '9') && c != '.') {
      return false;
    }
  }
  return true;
}

// Selects the first of `fonts` that `text` is no wider than `width` in, and
// says whether there was one. With none, the last of them is left selected.
template <size_t N>
bool fitFont(U8G2_SSD1306_128X64_NONAME_F_HW_I2C* u8g2,
             const uint8_t* const (&fonts)[N], const String& text, int width) {
  for (const uint8_t* font : fonts) {
    u8g2->setFont(font);
    if (u8g2->getStrWidth(text.c_str()) <= width) {
      return true;
    }
  }
  return false;
}

// As much of `text` as fits `width` in the selected font, ending in dots.
String cutToFit(U8G2_SSD1306_128X64_NONAME_F_HW_I2C* u8g2, const String& text,
                int width) {
  String kept = text;
  while (kept.length() > 0 &&
         u8g2->getStrWidth((kept + "...").c_str()) > width) {
    kept = kept.substring(0, kept.length() - 1);
  }
  return kept + "...";
}

}  // namespace

void OledDisplay::renderIdle(bool stopped) {
  // main screen with qr code, network and attributed ip

  // The link's task may say something new while this draws. That is for the
  // next time: this one draws what had been said as it began.
  this->lock.lock();
  const ConnectionInfo info = this->info;
  this->changed = false;
  this->lock.unlock();

  this->clear();
  this->u8g2->setFont(u8g2_font_nine_by_five_nbp_t_all);
  this->u8g2->setDrawColor(1);

  this->u8g2->drawStr(14, 15, "E-TKT");
  this->u8g2->setDrawColor(2);
  this->u8g2->drawFrame(3, 3, 50, 15);
  this->u8g2->setDrawColor(1);

  this->u8g2->drawStr(14, 31, stopped ? "stopped" : "ready");

  const CodeLevel* level = codeLevelFor(info.qr);
  const int right = level != NULL ? QR_LEFT : TEXT_RIGHT;

  if (info.name != "") {
    // A name too long for either font is cut short in the house one.
    String name = info.name;
    if (!fitFont(this->u8g2, NAME_FONTS, name, right - 14)) {
      this->u8g2->setFont(NAME_FONTS[0]);
      name = cutToFit(this->u8g2, name, right - 14);
    }
    this->u8g2->drawStr(14, 46, name.c_str());
  }

  if (isFigures(info.detail)) {
    fitFont(this->u8g2, FIGURE_FONTS, info.detail, right - 3);
  } else {
    fitFont(this->u8g2, WORD_FONTS, info.detail, right - 3);
  }
  this->u8g2->drawStr(3, 61, info.detail.c_str());

  this->u8g2->setFont(u8g2_font_open_iconic_all_1x_t);
  if (info.name != "") {
    this->u8g2->drawGlyph(3, 46, 0x00f8);
  }
  // A tick when the last job finished, the media stop square when it was
  // stopped partway.
  this->u8g2->drawGlyph(3, 31, stopped ? 0x00d9 : 0x0073);

  if (level != NULL) {
    uint8_t qrcodeData[qrcode_getBufferSize(this->QRcode_Version)];
    qrcode_initText(qrcode, qrcodeData, QRcode_Version, level->ecc,
                    info.qr.c_str());

    // qr code background
    this->u8g2->setDrawColor(0);
    this->u8g2->drawBox(QR_LEFT, 0, SCREEN_WIDTH - QR_LEFT, SCREEN_HEIGHT);

    // setup the top right corner of the QRcode
    const int x0 = QR_LEFT + 6;
    const int y0 = 3;

    // display QRcode
    this->u8g2->setDrawColor(1);
    for (uint8_t y = 0; y < qrcode->size; y++) {
      for (uint8_t x = 0; x < qrcode->size; x++) {
        if (qrcode_getModule(qrcode, x, y)) {
          this->u8g2->drawBox(x0 + (x * 2), y0 + (y * 2), 2, 2);
        }
      }
    }
  }
  this->u8g2->sendBuffer();
}

void OledDisplay::renderProgress(int charactersDone, const String& label,
                                 int copy, int copies) {
  this->clear();

  // Show "⚙️ PRINTING" header.
  this->u8g2->setDrawColor(1);
  this->u8g2->setFont(u8g2_font_nine_by_five_nbp_t_all);
  this->u8g2->drawStr(15, 12, "PRINTING");
  this->u8g2->setFont(u8g2_font_open_iconic_all_1x_t);
  this->u8g2->drawGlyph(3, 12, 0x0081);

  const std::vector<String> characters = Utility::characters(label);
  int progress_width = 0;
  int total_width = 0;

  // Do a pass thorugh the label characters to see how much horizontal
  // space is needed to render all the characters and how wide the completed
  // progress bar will be.
  for (size_t i = 0; i < characters.size(); i++) {
    auto font = fontFor(characters[i]);
    total_width += font.width;
    if ((int)i < charactersDone) {
      progress_width += font.width;
    }
  }

  // How far the label has slid to the left to keep the character being
  // pressed on the glass. Arithmetic only, and tested as such in
  // test_progress; see scrollOffset in Progress.h for the two rules.
  const int render_offset =
      scrollOffset(progress_width, total_width, SCREEN_WIDTH);

  // Iterate through the label again, this time drawing it on screen.  For
  // simplicity's sake always draw the entire label (even if its of screen)
  // and just let the screen buffer clip the edges.
  int x_position = 0;
  const int y_position = 36;
  for (const String& character : characters) {
    auto font = fontFor(character);
    this->u8g2->setFont(font.font);
    auto characterX = x_position + font.width_offset - render_offset;
    auto characterY = y_position + font.height_offset;
    if (font.isSymbol()) {
      this->u8g2->drawGlyph(characterX, characterY, font.code);
    } else {
      this->u8g2->drawStr(characterX, characterY, character.c_str());
    }
    x_position += font.width;
  }

  if (progress_width > 0) {
    // Render an inverted-color rectangle over the completed characters.
    this->u8g2->setDrawColor(2);
    this->u8g2->drawBox(0 - render_offset, 21, progress_width - 1, 21);
  }

  // Draw a box around the text, which looks like the edges of a label.
  this->u8g2->setDrawColor(1);
  this->u8g2->drawFrame(0 - render_offset, 21, total_width, 22);

  // If needed, draw ellipses on the right side of the screen to indicate the
  // label continues.
  if (total_width - render_offset > SCREEN_WIDTH) {
    this->u8g2->setDrawColor(0);

    // Clear 14 pixels of space on the right side of the screen.
    this->u8g2->drawBox(SCREEN_WIDTH - 14, 21, 14, 22);

    // Draw a triplet of 2x2 pixel dots, "...", in the middle of the
    // text line.
    this->u8g2->setDrawColor(1);
    for (int i = 1; i <= 3; i++) {
      this->u8g2->drawBox(SCREEN_WIDTH - (i * 4) + 2, 31, 2, 2);
    }
  }

  // Print "XX%" at the bottom of the screen. The font is set here rather
  // than left over from the label: a label ending in a symbol leaves an icon
  // font selected, and the percentage came out in icons.
  String progressString =
      String(progressPercent(charactersDone, (int)characters.size())) + "%";
  this->u8g2->setDrawColor(1);
  this->u8g2->setFont(u8g2_font_6x13_te);
  this->u8g2->drawStr(6, 60, progressString.c_str());

  // And which label of a run this is, "3/10", as far in from the right edge
  // as the percentage is from the left.
  if (copies > 1) {
    const String countString = String(copy) + "/" + copies;
    const int countWidth = this->u8g2->getStrWidth(countString.c_str());
    this->u8g2->drawStr(SCREEN_WIDTH - 6 - countWidth, 60, countString.c_str());
  }

  this->u8g2->sendBuffer();
}

void OledDisplay::renderSaved(const Calibration& saved) {
  this->clear(0);
  this->u8g2->setFont(u8g2_font_nine_by_five_nbp_t_all);
  this->u8g2->drawStr(47, 17, "SAVED!");

  String alignString = "ALIGN: ";
  alignString.concat(saved.align);
  this->u8g2->drawStr(44, 37, alignString.c_str());

  String forceString = "FORCE: ";
  forceString.concat(saved.force);
  this->u8g2->drawStr(42, 57, forceString.c_str());

  this->u8g2->setFont(u8g2_font_open_iconic_all_1x_t);
  this->u8g2->drawGlyph(33, 16, 0x0073);
  this->u8g2->drawGlyph(83, 16, 0x0073);

  this->u8g2->sendBuffer();
  // No delay here. The caller waits SAVED_SCREEN_MS.
}
