#include "Display.h"

#include <Arduino.h>
#include <U8g2lib.h>
#include <qrcode.h>

#include "Characters.h"
#include "Configuration.h"
#include "Progress.h"
#include "Sound.h"
#include "Utility.h"
#include "etktLogo.h"

Display::Display(Sound* sound, Characters* characters,
                 U8G2_SSD1306_128X64_NONAME_F_HW_I2C* u8g2) {
  this->u8g2 = u8g2;
  this->sound = sound;
  this->characters = characters;
}

Display::~Display() {
  // The screen is handed in, not built here, so it is not ours to delete.
  // qrcode is built in the member initialiser above, so it is.
  delete this->qrcode;
}

void Display::initialize() {
  // starts and sets up the display
  this->u8g2->begin();
  this->u8g2->clearBuffer();
  this->u8g2->setContrast(8);  // 0 > 255
  this->u8g2->setDrawColor(1);
  this->clear();
}

void Display::setConnectionInfo(String ip, String ssid) {
  this->ip = ip;
  this->ssid = ssid;
}

void Display::clear(int color) {
  // Paints every pixel the target colour. This used to be a nested loop
  // calling setDrawColor and drawPixel 8192 times per screen change, which is
  // 16384 calls into U8G2 to fill a buffer that drawBox fills in one.
  this->u8g2->setDrawColor(color);
  this->u8g2->drawBox(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
  // Kept from the original. It cannot matter to the buffer, which is in RAM
  // and is not sent until sendBuffer(), but the only machine that could show
  // otherwise is on a bench nobody is watching right now.
  delay(100);
  this->u8g2->setDrawColor(color == 0 ? 1 : 0);
  this->u8g2->setFont(u8g2_font_6x13_te);
}

void Display::playSplashScreen() {
  // initial start screen

  this->initialize();

  // invert colors
  this->clear(1);

  this->u8g2->setFont(u8g2_font_nine_by_five_nbp_t_all);
  this->u8g2->setDrawColor(0);
  this->u8g2->drawStr(40, 53, AUTHOR_SIGNATURE.c_str());
  this->u8g2->sendBuffer();

  this->u8g2->setDrawColor(1);

  int n = 1;

  // animated splash
  for (int i = 128; i > 7; i = i - 18) {
    for (int j = 0; j < 18; j += 9) {
      this->u8g2->drawXBM(i - j - 11, 8, 128, 32, etktLogo);
      this->u8g2->sendBuffer();
    }
    auto character = Utility::utf8CharAt(STARTUP_MELODY, n);
    if (character != " ") {
      this->sound->play(character, 200);
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

// No icon. 0 is not a drawable glyph in u8g2_font_open_iconic_all_1x_t.
#define NO_GLYPH 0

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

static const ScreenSpec SCREEN_SPECS[] = {
    {Screen::WIFI_SETUP, ScreenLayout::NOTICE, false, "WI-FI SETUP", 15,
     "Please, connect to", "the \"E-TKT\" network...", 0x011a, 0, 0},
    {Screen::WIFI_RESET, ScreenLayout::NOTICE, false, "WI-FI RESET", 15,
     "Connection cleared!", "Release the button.", 0x00cd, 0, 0},
    {Screen::CUTTING, ScreenLayout::BANNER, false, "CUTTING", 44, nullptr,
     nullptr, 0x00f2, 26, 90},
    {Screen::FEEDING, ScreenLayout::BANNER, false, "FEEDING", 44, nullptr,
     nullptr, 0x006e, 26, 90},
    {Screen::REELING, ScreenLayout::BANNER, false, "REELING", 44, nullptr,
     nullptr, 0x00d5, 26, 90},
    {Screen::TESTING, ScreenLayout::BANNER, false, "TESTING", 44, nullptr,
     nullptr, 0x0073, 26, 90},
    {Screen::FINISHED, ScreenLayout::BANNER, true, "FINISHED!", 42, nullptr,
     nullptr, 0x0073, 27, 90},
    {Screen::REBOOTING, ScreenLayout::BANNER, false, "REBOOTING...", 38,
     nullptr, nullptr, NO_GLYPH, 0, 0},
};

void Display::drawBanner(const ScreenSpec& spec) {
  // clear() leaves the draw colour set to the opposite of the background, so
  // the text reads either way round.
  this->clear(spec.inverted ? 1 : 0);
  this->u8g2->setFont(u8g2_font_nine_by_five_nbp_t_all);
  this->u8g2->drawStr(spec.titleX, 37, spec.title);

  if (spec.glyph != NO_GLYPH) {
    this->u8g2->setFont(u8g2_font_open_iconic_all_1x_t);
    this->u8g2->drawGlyph(spec.glyphLeftX, 37, spec.glyph);
    this->u8g2->drawGlyph(spec.glyphRightX, 37, spec.glyph);
  }

  this->u8g2->sendBuffer();
}

void Display::drawNotice(const ScreenSpec& spec) {
  this->clear(spec.inverted ? 1 : 0);
  this->u8g2->setFont(u8g2_font_nine_by_five_nbp_t_all);
  this->u8g2->drawStr(spec.titleX, 12, spec.title);
  this->u8g2->drawStr(3, 32, spec.line1);
  this->u8g2->drawStr(3, 47, spec.line2);

  this->u8g2->setFont(u8g2_font_open_iconic_all_1x_t);
  this->u8g2->drawGlyph(3, 12, spec.glyph);

  this->u8g2->sendBuffer();
}

// Catches the one mistake this table invites: adding an enumerator to Screen
// and forgetting its row. REBOOTING is last, so its value plus one is how
// many rows there have to be.
static_assert(sizeof(SCREEN_SPECS) / sizeof(SCREEN_SPECS[0]) ==
                  static_cast<int>(Screen::REBOOTING) + 1,
              "every Screen needs a row in SCREEN_SPECS");

void Display::render(Screen screen) {
  const int count = sizeof(SCREEN_SPECS) / sizeof(SCREEN_SPECS[0]);
  for (int i = 0; i < count; i++) {
    if (SCREEN_SPECS[i].screen != screen) {
      continue;
    }
    if (SCREEN_SPECS[i].layout == ScreenLayout::NOTICE) {
      this->drawNotice(SCREEN_SPECS[i]);
    } else {
      this->drawBanner(SCREEN_SPECS[i]);
    }
    return;
  }
  // Unreachable: the static_assert above counts the rows, and every row
  // names a distinct Screen. Leave whatever is on the glass rather than
  // blanking it, so a missing row shows up as a screen that did not change
  // instead of one that went dark.
}

void Display::renderIdle() {
  // main screen with qr code, network and attributed ip

  this->clear();
  this->u8g2->setFont(u8g2_font_nine_by_five_nbp_t_all);

  uint8_t qrcodeData[qrcode_getBufferSize(this->QRcode_Version)];

  if (this->ip != "") {
    this->u8g2->setDrawColor(1);

    this->u8g2->drawStr(14, 15, "E-TKT");
    this->u8g2->setDrawColor(2);
    this->u8g2->drawFrame(3, 3, 50, 15);
    this->u8g2->setDrawColor(1);

    this->u8g2->drawStr(14, 31, "ready");

    String resizeSSID;
    if (this->ssid.length() > 8) {
      resizeSSID = this->ssid.substring(0, 7) + "...";
    } else {
      resizeSSID = this->ssid;
    }
    const char *d = resizeSSID.c_str();
    this->u8g2->drawStr(14, 46, d);

    const char *b = this->ip.c_str();
    this->u8g2->drawStr(3, 61, b);

    this->u8g2->setFont(u8g2_font_open_iconic_all_1x_t);
    this->u8g2->drawGlyph(3, 46, 0x00f8);
    this->u8g2->drawGlyph(3, 31, 0x0073);

    String ipFull = "http://" + this->ip;
    qrcode_initText(qrcode, qrcodeData, QRcode_Version, QRcode_ECC,
                    ipFull.c_str());

    // qr code background
    for (uint8_t y = 0; y < 64; y++) {
      for (uint8_t x = 0; x < 64; x++) {
        this->u8g2->setDrawColor(0);
        this->u8g2->drawPixel(x + 128 - 64, y);
      }
    }

    // setup the top right corner of the QRcode
    uint8_t x0 = 128 - 64 + 6;
    uint8_t y0 = 3;

    // display QRcode
    for (uint8_t y = 0; y < qrcode->size; y++) {
      for (uint8_t x = 0; x < qrcode->size; x++) {
        int newX = x0 + (x * 2);
        int newY = y0 + (y * 2);

        if (qrcode_getModule(qrcode, x, y)) {
          this->u8g2->setDrawColor(1);
          this->u8g2->drawBox(newX, newY, 2, 2);
        } else {
          this->u8g2->setDrawColor(0);
          this->u8g2->drawBox(newX, newY, 2, 2);
        }
      }
    }
  }
  this->u8g2->sendBuffer();
  delay(1000);
}

void Display::renderProgress(int charactersDone, String label) {
  this->clear();

  // Show "⚙️ PRINTING" header.
  this->u8g2->setDrawColor(1);
  this->u8g2->setFont(u8g2_font_nine_by_five_nbp_t_all);
  this->u8g2->drawStr(15, 12, "PRINTING");
  this->u8g2->setFont(u8g2_font_open_iconic_all_1x_t);
  this->u8g2->drawGlyph(3, 12, 0x0081);

  auto labelLength = Utility::utf8Length(label);
  int progress_width = 0;
  int total_width = 0;

  // Do a pass thorugh the label characters to see how much horizontal
  // space is needed to render all the characters and how wide the completed
  // progress bar will be.
  for (int i = 0; i < labelLength; i++) {
    auto character = Utility::utf8CharAt(label, i);
    auto font = this->characters->getFont(character);
    total_width += font.width;
    if (i < charactersDone) {
      progress_width += font.width;
    }
  }

  // Calculate the render offset, which keeps the currently printing location
  // visible if the label doesn't fit all on screen.
  int render_offset = 0;
  if (total_width > SCREEN_WIDTH) {
    // If the "progress" location is off screen, offset the rendered label
    // so the progress indicator is centered.
    if (progress_width > SCREEN_WIDTH / 2) {
      render_offset = progress_width - SCREEN_WIDTH / 2;
    }

    // If centering the progress location woudl cause the right side of the
    // label to render before the right edge of the screen then realign so
    // it does, simulating a scrolling box's bounds.
    if (total_width - render_offset < SCREEN_WIDTH) {
      render_offset = total_width - SCREEN_WIDTH;
    }
  }

  // Iterate through the label again, this time drawing it on screen.  For
  // simplicity's sake always draw the entire label (even if its of screen)
  // and just let the screen buffer clip the edges.
  int x_position = 0;
  const int y_position = 36;
  for (int i = 0; i < labelLength; i++) {
    auto character = Utility::utf8CharAt(label, i);
    auto font = this->characters->getFont(character);
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

  // Print "XX%" at the bottom of the screen.
  String progressString =
      String(progressPercent(charactersDone, labelLength)) + "%";
  this->u8g2->setDrawColor(1);
  this->u8g2->drawStr(6, 60, progressString.c_str());

  this->u8g2->sendBuffer();
}

void Display::renderSaved(int align, int force) {
  this->clear(0);
  this->u8g2->setFont(u8g2_font_nine_by_five_nbp_t_all);
  this->u8g2->drawStr(47, 17, "SAVED!");

  String alignString = "ALIGN: ";
  alignString.concat(align);
  this->u8g2->drawStr(44, 37, alignString.c_str());

  String forceString = "FORCE: ";
  forceString.concat(force);
  this->u8g2->drawStr(42, 57, forceString.c_str());

  this->u8g2->setFont(u8g2_font_open_iconic_all_1x_t);
  this->u8g2->drawGlyph(33, 16, 0x0073);
  this->u8g2->drawGlyph(83, 16, 0x0073);

  this->u8g2->sendBuffer();
  // No delay here. The caller waits SAVED_SCREEN_MS.
}
