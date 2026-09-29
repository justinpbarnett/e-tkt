#pragma once

// The second adapter for the Display interface in src/Display.h.
//
// OledDisplay is the first. This one draws nothing: it records what the job
// runner asked the screen to show, stamped with the virtual clock from
// test/stubs/Arduino.h, so a test can say what an operator standing at the
// machine would have seen, and when.

#include <functional>
#include <vector>

#include "Arduino.h"
#include "Display.h"

/**
 * @brief Every Display call, in order, with the time it happened.
 *
 * Only the fields the call carries are filled in; the rest keep their
 * defaults.
 */
struct DisplayCall {
  enum Kind {
    INITIALIZE,
    RENDER,
    RENDER_IDLE,
    CONNECTION_INFO,
    RENDER_PROGRESS,
    RENDER_SAVED
  };
  explicit DisplayCall(Kind kind) : kind(kind) {}
  Kind kind;
  Screen screen = Screen::WIFI_SETUP;  // RENDER
  bool stopped = false;                // RENDER_IDLE
  int charactersDone = 0;              // RENDER_PROGRESS
  String label = "";                   // RENDER_PROGRESS
  int copy = 0;                        // RENDER_PROGRESS
  int copies = 0;                      // RENDER_PROGRESS
  int align = 0;                       // RENDER_SAVED
  int force = 0;                       // RENDER_SAVED
  String ip = "";                      // CONNECTION_INFO
  String ssid = "";                    // CONNECTION_INFO
  unsigned long atMs = 0;
};

class FakeDisplay : public Display {
 private:
  void record(DisplayCall call) {
    call.atMs = millis();
    this->calls.push_back(call);
    // The copy, not the entry in calls: a hook that makes the job runner draw
    // again grows the vector underneath any reference into it.
    if (this->onCall) {
      this->onCall(call);
    }
  }

 public:
  std::vector<DisplayCall> calls;

  /**
   * @brief Called with each call as it is recorded, for a test that has to
   * act at the moment the screen changes -- to post a job while the idle
   * screen draws, say.
   */
  std::function<void(const DisplayCall&)> onCall;

  void initialize() override {
    this->record(DisplayCall(DisplayCall::INITIALIZE));
  }

  void render(Screen screen) override {
    DisplayCall c(DisplayCall::RENDER);
    c.screen = screen;
    this->record(c);
  }

  void renderIdle(bool stopped) override {
    DisplayCall c(DisplayCall::RENDER_IDLE);
    c.stopped = stopped;
    this->record(c);
  }

  void setConnectionInfo(const String& ip, const String& ssid) override {
    DisplayCall c(DisplayCall::CONNECTION_INFO);
    c.ip = ip;
    c.ssid = ssid;
    this->record(c);
  }

  void renderProgress(int charactersDone, const String& label, int copy,
                      int copies) override {
    DisplayCall c(DisplayCall::RENDER_PROGRESS);
    c.charactersDone = charactersDone;
    c.label = label;
    c.copy = copy;
    c.copies = copies;
    this->record(c);
  }

  void renderSaved(int align, int force) override {
    DisplayCall c(DisplayCall::RENDER_SAVED);
    c.align = align;
    c.force = force;
    this->record(c);
  }

  // --- helpers the tests read the recording through ------------------------

  int countOf(DisplayCall::Kind kind) const {
    int n = 0;
    for (size_t i = 0; i < this->calls.size(); i++) {
      if (this->calls[i].kind == kind) {
        n++;
      }
    }
    return n;
  }

  /** @brief The last call of that kind, or NULL if there was none. */
  const DisplayCall* last(DisplayCall::Kind kind) const {
    for (size_t i = this->calls.size(); i > 0; i--) {
      if (this->calls[i - 1].kind == kind) {
        return &this->calls[i - 1];
      }
    }
    return NULL;
  }

  /** @brief Just the fixed screens shown, in order. */
  std::vector<Screen> screens() const {
    std::vector<Screen> out;
    for (size_t i = 0; i < this->calls.size(); i++) {
      if (this->calls[i].kind == DisplayCall::RENDER) {
        out.push_back(this->calls[i].screen);
      }
    }
    return out;
  }

  void clear() { this->calls.clear(); }
};
