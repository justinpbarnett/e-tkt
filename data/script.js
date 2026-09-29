// MIT License

// Copyright (c) 2022 Andrei Speridião

// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:

// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.

// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

//
// for more information, please visit https://github.com/andreisperid/E-TKT
//

"use strict";

// What the panel says while each command runs, keyed by the name the device
// answers to. That name is also the path this panel posts to, api/<name>,
// and the string /api/status reports back while the command runs.
//
// Wording only. Which commands exist is the device's to say, and it says so
// in api/capabilities; this table is checked against that list at startup
// and a disagreement is reported rather than guessed at.
//
// The wording used to be spread across the senders below and the status
// switch in handleData(), and home and move had already fallen out of both:
// a home or a move started from outside the panel left the button showing
// whatever it said last. No button posts either one today. They are here
// because the device can still be running one, and the panel has to be able
// to say so.
//
// A stopLabel is what the stop button says while that command runs, and
// only the commands with one are offered a stop. The device stops anything
// but a save, but a cut or a feed is over before a finger could get there.
//
// One entry to a line: src/simulator/test_firmware.py reads the names out of
// this table to hold them to the firmware's.
const COMMAND_LABELS = {
  cut: { busyLabel: "Cutting…" },
  feed: { busyLabel: "Feeding…" },
  reel: { busyLabel: "Loading the new roll…", stopLabel: "Stop loading" },
  testalign: { busyLabel: "Testing the alignment…", stopLabel: "Stop test" },
  testfull: { busyLabel: "Printing a test label…", stopLabel: "Stop test print" },
  save: { busyLabel: "Saving…" },
  tag: { busyLabel: "Printing…", stopLabel: "Stop printing" },
  home: { busyLabel: "Finding home…" },
  move: { busyLabel: "Moving the wheel…" },
};

// Shown when the device reports a command this copy of the panel has never
// heard of, which means a cached script.js is talking to newer firmware.
const UNKNOWN_BUSY_LABEL = "Working…";

// The commands setup starts, each of which says so on its own button while
// it runs. Anything else running while setup is open is named by the stop
// it gets, or if it cannot be stopped, by a line of its own.
const SETUP_COMMANDS = ["reel", "testalign", "testfull"];

// How often /api/status is asked, and how often while the page is in the
// background: still often enough that a run finishing is noticed on the way
// back, and rarely enough not to keep a phone's radio awake for nothing.
const POLL_MS = 1000;
const HIDDEN_POLL_MS = 5000;

// Polls in a row that can fail before the page says the device is gone. One
// is a dropped packet on a busy access point; two is worth saying out loud.
const OFFLINE_AFTER_MISSES = 2;

// How long a stop button, or what takes its place once the stop is done,
// ignores taps after it comes up. The stop comes up where the finger that
// started the command may still be tapping, and the page may scroll to it;
// a tap that soon was meant for what was there before.
const STOP_ARMING_MS = 700;

// What the page says when a stop reaches the device after the command it
// was meant for has finished.
const TOO_LATE_TO_STOP = "Too late to stop: the label maker had already finished.";

// How close to the top of the screen scrolling to the stops may take the
// button that started the command.
const SCROLL_ROOM = 16;

// How long the device takes to come back after a save, which restarts it.
const RESTART_SECONDS = 15;

// Where this browser keeps a theme picked with the button in the header.
// index.html reads the same key before the page is drawn.
const THEME_KEY = "e-tkt-theme";

// The smallest run the Multiple option offers. One label is the One option.
const MIN_MULTIPLE = 2;

// What the roll length's minus and plus move it by, in millimetres. Rolls
// come in whole and half metres; anything in between can still be typed.
const ROLL_STEP_MM = 500;

// How close the caret may come to either end of the track before the tape
// scrolls to follow it: clear of the fades that mark more tape past the edge.
const CARET_ROOM = 56;

// What the device will accept: the characters a label may contain, what the
// ones the wheel does not carry come out as instead, the range the align and
// force settings are offered in, how many labels one request may ask for,
// how long a roll may be declared at, and how much tape a feed pulls
// through. All of it arrives from api/capabilities at startup; until it does
// the panel refuses to validate or to send anything, the same way it refuses
// to save before align and force have loaded.
//
// None of it is guessed here on purpose. Each of these used to have a copy
// in this file that could drift from the firmware and did: the character
// set was a regex written twice, and the range was a literal here and two
// pairs of min/max attributes in index.html.
let device = null;

// Everything the page shows that is not the tape, drawn from here by
// render(). Handlers change this and call render() rather than writing to
// the page themselves, so the page cannot say two things at once.
const state = {
  // The last /api/status, or null until the first one lands.
  status: null,
  // Polls that have failed in a row.
  missedPolls: 0,
  // The command whose POST is on its way.
  posting: null,
  // A command the device has accepted that no poll has reported on yet.
  // Without it a quick command could come and go between two polls and the
  // page never show it running. With it the page is busy from the moment
  // the device says yes until a status asked for after that says what
  // became of it.
  pending: null,
  // The copies this page last asked the device for, so the stops are laid
  // out for a run of labels from the tap rather than from the first poll.
  sentCopies: null,
  // A stop this page has asked for: its kind, "now" or "after_label",
  // whether it has been sent, and when the device took it. A stop tapped
  // while the command is still on its way waits for the device to have
  // the command. The device says a stop is coming too, in status.stop, but
  // not until the next poll.
  stopRequest: null,
  // What the page has to say about a stop when the device has no record of
  // one to say it with: that it came too late to stop anything.
  stopNote: null,
  // The device's record of the last stop, as stopKey() has it, once it has
  // been dismissed here, so the next poll does not bring it straight back.
  dismissedStop: null,
  // From a tap on a stop until what it came to is brought into view: the
  // end of the command it stopped, or a problem stopping it.
  revealStop: false,
  view: "print",
  // align and force as the device has them, and as setup has them now.
  saved: null,
  draft: null,
  // The last thing that went wrong, and which view it belongs to.
  problem: null,
  // Set once the settings are saved and the device is restarting.
  restarting: false,
};

const $ = (id) => document.getElementById(id);

const el = {
  themeButton: $("theme-button"),
  viewName: $("view-name"),
  offline: $("offline"),
  printView: $("print-view"),
  setupView: $("setup-view"),
  form: $("label-form"),
  tapeTrack: $("tape-track"),
  tapeScroll: $("tape-scroll"),
  tape: $("tape"),
  input: $("label-input"),
  printingLabel: $("printing-label"),
  progressBar: $("progress-bar"),
  tipStart: $("tip-start"),
  tipEnd: $("tip-end"),
  hint: $("label-hint"),
  length: $("label-length"),
  keys: document.querySelectorAll("[data-insert]"),
  clearButton: $("clear-button"),
  copiesStepper: $("copies-stepper"),
  copiesInput: $("copies-input"),
  copiesLess: $("copies-less"),
  copiesMore: $("copies-more"),
  quantityNote: $("quantity-note"),
  printButton: $("print-button"),
  activity: $("activity"),
  activityFill: $("activity-fill"),
  activityText: $("activity-text"),
  activityPercent: $("activity-percent"),
  machineActions: $("machine-actions"),
  runActions: $("run-actions"),
  feedButton: $("feed-button"),
  cutButton: $("cut-button"),
  setupButton: $("setup-button"),
  stopNowButton: $("stop-now-button"),
  stopNowText: $("stop-now-text"),
  stopButton: $("stop-button"),
  rollRemaining: $("roll-remaining"),
  rollOf: $("roll-of"),
  rollMeter: $("roll-meter"),
  reelButton: $("reel-button"),
  alignValue: $("align-value"),
  forceValue: $("force-value"),
  stepButtons: document.querySelectorAll("[data-setting]"),
  testAlignButton: $("test-align-button"),
  testFullButton: $("test-full-button"),
  setupStatus: $("setup-status"),
  setupRunActions: $("setup-run-actions"),
  setupStopButton: $("setup-stop-button"),
  setupStopText: $("setup-stop-text"),
  cancelButton: $("cancel-button"),
  saveButton: $("save-button"),
  stopNotices: document.querySelectorAll("[data-stop-notice]"),
  problems: document.querySelectorAll("[data-problem]"),
  reelDialog: $("reel-dialog"),
  reelForm: $("reel-form"),
  reelLength: $("reel-length"),
  reelLess: $("reel-less"),
  reelMore: $("reel-more"),
  reelRange: $("reel-range"),
  reelConfirm: $("reel-confirm"),
  discardDialog: $("discard-dialog"),
  discardSummary: $("discard-summary"),
  saveDialog: $("save-dialog"),
  saveSummary: $("save-summary"),
  restartDialog: $("restart-dialog"),
  countdown: $("countdown"),
};

async function startup() {
  document.body.dataset.printing = "false";
  applyTheme(document.documentElement.dataset.theme === "dark" ? "dark" : "light");
  wireEvents();
  drawTape();
  render();

  // Straight into typing where there is a keyboard to type on. Not on a
  // phone, where it would throw the keyboard up over the page unasked.
  if (matchMedia("(hover: hover) and (pointer: fine)").matches) {
    el.input.focus();
  }

  // The tape is measured in its own face, which may still be on its way
  // when the page first draws. Measured again once it is here.
  if (document.fonts) {
    document.fonts.load('25px "Impact Label Reversed"').then(drawTape, () => {});
  }

  // One after the other, not side by side: the device serves the page's own
  // files at the same time, and it has only a handful of sockets.
  await retrieveCapabilities();
  poll();
}

function wireEvents() {
  el.themeButton.addEventListener("click", toggleTheme);

  el.form.addEventListener("submit", (event) => {
    event.preventDefault();
    printLabels();
  });
  el.input.addEventListener("input", labelChanged);
  el.input.addEventListener("focus", followCaret);
  // In capture, because Firefox fires this at the input and does not let it
  // bubble, while Chrome and Safari fire it at the document.
  document.addEventListener("selectionchange", followCaret, true);
  el.tapeScroll.addEventListener("scroll", updateScrollTips, { passive: true });
  el.tipStart.addEventListener("click", () => jumpToScrollEnds(0));
  el.tipEnd.addEventListener("click", () => jumpToScrollEnds(1));
  new ResizeObserver(updateOverflow).observe(el.tapeTrack);

  for (const key of el.keys) {
    // Keeps the caret in the label, so the symbol goes where the caret was
    // and a phone's keyboard does not drop and come back up.
    key.addEventListener("mousedown", (event) => event.preventDefault());
    key.addEventListener("click", () => insertIntoField(key.dataset.insert));
  }
  el.clearButton.addEventListener("mousedown", (event) => event.preventDefault());
  el.clearButton.addEventListener("click", clearField);

  for (const radio of el.form.elements.margin) {
    radio.addEventListener("change", labelChanged);
  }
  for (const radio of el.form.elements.quantity) {
    radio.addEventListener("change", render);
  }
  el.copiesInput.addEventListener("input", render);
  el.copiesInput.addEventListener("change", commitCopies);
  el.copiesInput.addEventListener("keydown", (event) => {
    // Settles the number rather than printing it. A run of labels is too
    // much to start from a key pressed to finish typing a count.
    if (event.key === "Enter") {
      event.preventDefault();
      commitCopies();
    }
  });
  el.copiesLess.addEventListener("click", () => stepCopies(-1));
  el.copiesMore.addEventListener("click", () => stepCopies(1));

  el.feedButton.addEventListener("click", () => send("feed"));
  el.cutButton.addEventListener("click", () => send("cut"));
  el.setupButton.addEventListener("click", openSetup);
  el.stopNowButton.addEventListener("click", () => requestStop("now", el.runActions));
  el.stopButton.addEventListener("click", () => requestStop("after_label", el.runActions));
  el.setupStopButton.addEventListener("click", () => requestStop("now", el.setupRunActions));
  for (const box of el.stopNotices) {
    box.querySelector("[data-stop-cut]").addEventListener("click", () => {
      if (settled(box)) {
        send("cut");
      }
    });
    box.querySelector("[data-stop-dismiss]").addEventListener("click", () => {
      if (settled(box)) {
        dismissStopNotice();
      }
    });
  }
  // The stops are stuck to the bottom of the screen for as long as the page
  // is short of where they sit, and look it.
  window.addEventListener("scroll", watchStuck, { passive: true });
  window.addEventListener("resize", watchStuck);

  el.reelButton.addEventListener("click", openReelDialog);
  for (const button of el.stepButtons) {
    button.addEventListener("click", () => stepSetting(button.dataset.setting, Number(button.dataset.step)));
  }
  el.testAlignButton.addEventListener("click", testAlignCommand);
  el.testFullButton.addEventListener("click", testFullCommand);
  el.cancelButton.addEventListener("click", leaveSetup);
  el.saveButton.addEventListener("click", confirmSave);

  for (const button of document.querySelectorAll("[data-dismiss]")) {
    button.addEventListener("click", dismissProblem);
  }

  el.reelLength.addEventListener("input", renderReelDialog);
  el.reelLength.addEventListener("keydown", (event) => {
    // Enter in a form submits it with the first submit button in it, and
    // here that is Cancel. Enter after typing a length means use it.
    if (event.key === "Enter") {
      event.preventDefault();
      el.reelForm.requestSubmit(el.reelConfirm);
    }
  });
  el.reelLess.addEventListener("click", () => stepReel(-1));
  el.reelMore.addEventListener("click", () => stepReel(1));
  el.reelForm.addEventListener("submit", (event) => {
    if (event.submitter === el.reelConfirm && typedRollLength() === null) {
      event.preventDefault();
    }
  });
  el.reelDialog.addEventListener("close", async () => {
    const lengthMm = typedRollLength();
    if (el.reelDialog.returnValue === "reel" && lengthMm !== null && (await send("reel", { length_mm: lengthMm }))) {
      scrollStopsIntoPlace(el.setupRunActions, el.reelButton);
    }
  });
  el.discardDialog.addEventListener("close", () => {
    if (el.discardDialog.returnValue === "discard") {
      closeSetup();
    }
  });
  el.saveDialog.addEventListener("close", () => {
    if (el.saveDialog.returnValue === "save") {
      settingsCommand();
    }
  });
  // Nothing to go back to while the device restarts. Escape is refused, and
  // since a browser may close a dialog anyway on a second Escape, it is put
  // straight back up if it does.
  el.restartDialog.addEventListener("cancel", (event) => event.preventDefault());
  el.restartDialog.addEventListener("close", () => {
    if (state.restarting) {
      el.restartDialog.showModal();
    }
  });

  document.addEventListener("visibilitychange", () => {
    if (!document.hidden) {
      poll();
    }
  });
}

//-----------//
//   theme   //
//-----------//

const darkScheme = matchMedia("(prefers-color-scheme: dark)");

// Follows the phone's setting for as long as no choice has been made here.
darkScheme.addEventListener("change", (event) => {
  if (storedTheme() === null) {
    applyTheme(event.matches ? "dark" : "light");
  }
});

function storedTheme() {
  try {
    const theme = localStorage.getItem(THEME_KEY);
    return theme === "light" || theme === "dark" ? theme : null;
  } catch (error) {
    // Storage can be refused outright, as index.html says. No choice kept,
    // then, and the phone's setting decides.
    return null;
  }
}

function applyTheme(theme) {
  document.documentElement.dataset.theme = theme;
  // Named for what it does, not for what the page is now.
  el.themeButton.setAttribute("aria-label", theme === "dark" ? "Switch to light theme" : "Switch to dark theme");
  // The browser's own bars take the page's colour, in either theme. Read
  // back off the page so the colour is only ever written in style.css.
  const meta = document.querySelector('meta[name="theme-color"]');
  if (meta) {
    meta.content = getComputedStyle(document.body).backgroundColor;
  }
}

function toggleTheme() {
  const next = document.documentElement.dataset.theme === "dark" ? "light" : "dark";
  try {
    localStorage.setItem(THEME_KEY, next);
  } catch (error) {
    // Not kept, then. It still changes for as long as the page is open.
  }
  applyTheme(next);
}

//------------------//
//   capabilities   //
//------------------//

// Raised when api/capabilities answers but leaves out something this page
// needs, which means the device is running older firmware than this page.
class CapabilitiesMismatch extends Error {}

// Fetches what the device will accept. Retries on its own rather than
// leaving the panel unable to validate: the device answers this from its own
// constants, so there is no local fallback to fall back to.
async function retrieveCapabilities() {
  try {
    const response = await fetchWithTimeout("api/capabilities", { timeout: 5000 });
    if (response.status === 404) {
      throw new CapabilitiesMismatch("api/capabilities is not there");
    }
    if (!response.ok) {
      throw new Error("api/capabilities answered " + response.status);
    }
    device = readCapabilities(await response.json());
  } catch (error) {
    console.error("Unable to fetch what the device accepts, retrying");
    console.error(error);
    if (error instanceof CapabilitiesMismatch) {
      // Said on the page as well as in the console: nothing will print until
      // it is fixed, and the console is invisible from the bench.
      showProblem(
        "The label maker is running older firmware than this page. Upload the firmware and the page from the " +
          "same copy of the code.",
        "capabilities",
      );
      render();
    }
    setTimeout(retrieveCapabilities, 2000);
    return;
  }

  if (state.problem !== null && state.problem.source === "capabilities") {
    state.problem = null;
  }
  applyTypedLengthLimit();
  el.copiesInput.max = device.copies.maximum;
  warnAboutCommandList(device.commands);
  drawTape();
  render();
}

// Everything api/capabilities has to say, checked before any of it is used,
// or a CapabilitiesMismatch naming what is missing.
function readCapabilities(response) {
  const need = (ok, what) => {
    if (!ok) {
      throw new CapabilitiesMismatch("api/capabilities served no " + what);
    }
  };
  need(typeof response.printable === "string" && response.printable.length > 0, "printable set");
  const calibration = response.calibration;
  need(calibration && Number.isInteger(calibration.min) && Number.isInteger(calibration.max), "calibration range");
  const label = response.label;
  need(label && Number.isInteger(label.minimum), "minimum label length");
  need(Number.isInteger(label.maximum), "maximum label length");
  const copies = response.copies;
  need(copies && Number.isInteger(copies.minimum) && Number.isInteger(copies.maximum), "copies range");
  const roll = response.roll;
  need(
    roll && Number.isInteger(roll.minimum_mm) && Number.isInteger(roll.maximum_mm) && Number.isInteger(roll.default_mm),
    "roll lengths",
  );
  const feed = response.feed;
  need(feed && Number.isInteger(feed.length_um) && feed.length_um > 0 && Number.isInteger(feed.lead), "feed length");

  return {
    printable: response.printable,
    aliases: response.aliases || {},
    calibration: calibration,
    label: label,
    copies: copies,
    roll: roll,
    feed: feed,
    commands: Array.isArray(response.commands) ? response.commands : null,
  };
}

// Not fatal: an unknown command already falls back to UNKNOWN_BUSY_LABEL and
// the panel keeps working. Worth saying out loud, though, because the usual
// cause is a cached script.js talking to newer firmware, and that is
// invisible from the bench.
function warnAboutCommandList(offered) {
  if (offered === null) {
    return;
  }
  const missing = offered.filter((name) => !(name in COMMAND_LABELS));
  const extra = Object.keys(COMMAND_LABELS).filter((name) => !offered.includes(name));
  if (missing.length > 0 || extra.length > 0) {
    console.warn(
      "This panel and the firmware disagree about the command list." +
        (missing.length ? " No wording here for: " + missing.join(", ") + "." : "") +
        (extra.length ? " Device does not offer: " + extra.join(", ") + "." : ""),
    );
  }
}

//-----------//
//   label   //
//-----------//

// The widest margin buildTreatedLabel() adds to a label that is already long
// enough on its own, per side. Short labels get more, to reach the minimum,
// but a label near the maximum never does.
const WIDEST_MARGIN = 1;

// Caps the input at what the device will actually take, less the margin this
// panel is about to add to it. The number used to be maxlength="247" written
// into data/index.html, which is the panel deciding for itself what the
// device accepts -- and it decided wrong, because the margin pushed a full
// 247 characters to 249 and the device refused the label on arrival.
function applyTypedLengthLimit() {
  el.input.maxLength = device.label.maximum - WIDEST_MARGIN * 2;
}

// How many characters the panel pads a label up to, or null while the device
// has not said yet.
//
// One past the device's minimum. A label that only just reaches the minimum
// leaves the device topping the tape up with trailing feeds, which pushes the
// text off centre; padding one further does not. The number comes from
// api/capabilities -- three places here used to write it as a bare 7 while
// the device called it 6, and two of them kept saying 7 after the third
// started asking.
function paddedLabelTarget() {
  return device === null ? null : device.label.minimum + 1;
}

function buildTreatedLabel() {
  let fieldValue = el.input.value;
  if (fieldValue.length === 0) {
    fieldValue = "WRITE HERE";
  }
  let multiplier = el.form.elements.margin.value === "tight" ? 0 : 1;

  // Spaces go on both sides so the text stays centred. No fallback when the
  // device has not said yet: a guessed minimum is the same drift in a
  // different place, so only the mode's own padding is applied. That shows
  // for as long as the first api/capabilities call takes -- drawTape() runs
  // again when it lands, and the one caller that sends is behind
  // isValidLabelText(), which refuses until then.
  const target = paddedLabelTarget();
  if (target !== null) {
    const printLength = codePoints(fieldValue) + multiplier * 2;
    if (printLength < target) {
      // Added to the margin the mode already asked for, not put in its place.
      // Assigning here discarded the loose mode's own space on each side, so
      // every short label in that mode went out two characters under the
      // minimum the device had just asked for.
      multiplier += Math.ceil((target - printLength) / 2);
    }
  }
  return " ".repeat(multiplier) + fieldValue + " ".repeat(multiplier);
}

// Characters as the device counts them. Four of the wheel's are more than
// one byte, and a count in bytes or in UTF-16 would disagree with it.
function codePoints(text) {
  return Array.from(text).length;
}

// Each character of the label the wheel does not carry, once, in the order
// typed. Case does not matter: the label is sent lowercase and the firmware
// upper-cases it again.
function unprintableCharacters(text) {
  if (device === null) {
    return [];
  }
  const found = [];
  for (const character of text.toUpperCase()) {
    if (device.printable.indexOf(character) < 0 && !found.includes(character)) {
      found.push(character);
    }
  }
  return found;
}

// Whether the label in the input is something the device would accept. Every
// character is checked against the set the device served, so this answer and
// the device's answer cannot drift apart.
function isValidLabelText() {
  return device !== null && el.input.value.length > 0 && unprintableCharacters(el.input.value).length === 0;
}

// The line under the tape. Normally it lists what may be typed. While the
// label holds a character the wheel does not carry it names it, and while it
// holds one the wheel prints as something else it says what that will come
// out as, which is the only warning before the tape is spent.
function hintFor(typed) {
  if (device === null) {
    return { text: "", tone: null };
  }
  const unprintable = unprintableCharacters(typed);
  if (unprintable.length > 0) {
    return { text: "Not on the wheel: " + unprintable.join(" "), tone: "danger" };
  }
  const upper = typed.toUpperCase();
  const surprises = Object.keys(device.aliases)
    .filter((character) => upper.indexOf(character) >= 0)
    .map((character) => character + " prints " + device.aliases[character]);
  if (surprises.length > 0) {
    return { text: surprises.join(", "), tone: "warning" };
  }
  return { text: summariseCharacters(device.printable), tone: null };
}

// Turns the served character set into something short enough to sit under
// the input. A run of three or more consecutive letters or digits collapses
// to a range; everything else is listed as itself. Nothing is left out, so
// the line cannot quietly stop matching what the device accepts.
function summariseCharacters(characters) {
  const sameKind = (a, b) => (/[0-9]/.test(a) && /[0-9]/.test(b)) || (/[A-Z]/.test(a) && /[A-Z]/.test(b));

  const parts = [];
  let run = [];
  const flush = () => {
    if (run.length === 0) {
      return;
    }
    parts.push(run.length >= 3 ? run[0] + "-" + run[run.length - 1] : run.join(" "));
    run = [];
  };

  for (const glyph of characters) {
    if (glyph === " ") {
      continue;
    }
    const previous = run[run.length - 1];
    const follows = previous !== undefined && glyph.codePointAt(0) === previous.codePointAt(0) + 1;
    if (previous !== undefined && !(follows && sameKind(previous, glyph))) {
      flush();
    }
    run.push(glyph);
  }
  flush();

  if (characters.indexOf(" ") >= 0) {
    parts.push("space");
  }
  return parts.join(" ");
}

function labelChanged() {
  drawTape();
  render();
}

function clearField() {
  el.input.value = "";
  labelChanged();
  el.input.focus();
}

// Puts a symbol where the caret is, or over the selection.
function insertIntoField(symbol) {
  const input = el.input;
  const start = input.selectionStart ?? input.value.length;
  const end = input.selectionEnd ?? start;
  // maxLength only stops typing and pasting. A value set from here has to
  // keep to it by hand.
  if (input.maxLength >= 0 && input.value.length - (end - start) + symbol.length > input.maxLength) {
    return;
  }
  input.setRangeText(symbol, start, end, "end");
  input.focus();
  labelChanged();
}

//----------//
//   tape   //
//----------//

// The same sums as labelFeeds() and labelsThatFit() in src/Tape.h, over the
// numbers api/capabilities serves, so the page can say how many labels fit
// before the roll runs out. The machine cannot see the tape: every length
// here is a count of feeds, and an estimate.
//
// A label shorter than the minimum is topped up to it with blank feeds, so
// there is something to take hold of when the tape is cut -- except a
// one-character label, which the machine has always printed short.
function topUpFeeds(length) {
  if (length >= device.label.minimum || length === 1) {
    return 0;
  }
  return device.label.minimum - Math.max(length, 0);
}

// The lead, one per character -- a space is a feed with no press -- and the
// top-up. The cut takes none.
function labelFeeds(length) {
  return device.feed.lead + Math.max(length, 0) + topUpFeeds(length);
}

function labelLengthMm(length) {
  return (labelFeeds(length) * device.feed.length_um) / 1000;
}

// Rounded down: a label that would run off the end of the tape is not one
// that fits.
function labelsThatFit(remainingMm, length) {
  const perLabelUm = labelFeeds(length) * device.feed.length_um;
  if (remainingMm <= 0 || perLabelUm <= 0) {
    return 0;
  }
  return Math.floor((remainingMm * 1000) / perLabelUm);
}

// A length of tape in the unit a person would say it in. Rounded down, so
// what is left is never more than the device's own estimate.
function formatLength(mm) {
  if (mm < 10) {
    return Math.max(Math.floor(mm), 0) + " mm";
  }
  if (mm < 1000) {
    return Math.floor(mm / 10) + " cm";
  }
  return Math.floor(mm / 100) / 10 + " m";
}

let measuringContext = null;

function measureText(element, text) {
  if (measuringContext === null) {
    measuringContext = document.createElement("canvas").getContext("2d");
  }
  const style = getComputedStyle(element);
  measuringContext.font = `${style.fontWeight} ${style.fontSize} ${style.fontFamily}`;
  return measuringContext.measureText(text).width;
}

function getLabelWidth(element, label) {
  // A floor, so a short label does not collapse the box it sits in. The floor
  // is the padded length the panel aims at, not a number of its own.
  const target = paddedLabelTarget();
  const floor = target === null ? 0 : measureText(element, " ".repeat(target));
  return Math.ceil(Math.max(measureText(element, label), floor)) + 4;
}

// Sizes the strip to the label the device will be sent, margins and all, so
// what is on the screen is the piece of tape that comes out.
function drawTape() {
  el.input.style.width = getLabelWidth(el.input, buildTreatedLabel()) + "px";
  updateOverflow();
  followCaret();
}

// Lets the track scroll once the strip is longer than it, and only then.
// Measured rather than worked out, so the spacers at either end, the card's
// padding and the width of the phone are all already in the answer.
function updateOverflow() {
  const scroll = el.tapeScroll;
  const overflowing = scroll.scrollWidth > scroll.clientWidth;
  el.tapeTrack.classList.toggle("scrolling", overflowing);
  if (!overflowing) {
    scroll.scrollLeft = 0;
  }
  updateScrollTips();
}

// Keeps the caret in view on a label longer than the track. At either end of
// the text the track goes all the way to that end, so the margin shows too.
function followCaret() {
  const input = el.input;
  const scroll = el.tapeScroll;
  if (document.activeElement !== input || !el.tapeTrack.classList.contains("scrolling")) {
    return;
  }
  const value = input.value;
  const caret = input.selectionDirection === "backward" ? input.selectionStart : input.selectionEnd;
  if (caret === null) {
    return;
  }
  if (caret === 0) {
    scroll.scrollLeft = 0;
  } else if (caret === value.length) {
    scroll.scrollLeft = scroll.scrollWidth - scroll.clientWidth;
  } else {
    // The text is centred in the strip, so it starts part way in.
    const textStart = (input.offsetWidth - measureText(input, value)) / 2;
    const x = el.tape.offsetLeft + textStart + measureText(input, value.slice(0, caret));
    if (x < scroll.scrollLeft + CARET_ROOM) {
      scroll.scrollLeft = x - CARET_ROOM;
    } else if (x > scroll.scrollLeft + scroll.clientWidth - CARET_ROOM) {
      scroll.scrollLeft = x - scroll.clientWidth + CARET_ROOM;
    }
  }
  updateScrollTips();
}

// Shows the "…" at whichever ends of the track have more tape past them.
function updateScrollTips() {
  const scroll = el.tapeScroll;
  const overflowing = el.tapeTrack.classList.contains("scrolling");
  // "1" is margin of error, for scroll positions that come out fractional.
  el.tipStart.classList.toggle("visible", overflowing && scroll.scrollLeft > 1);
  el.tipEnd.classList.toggle(
    "visible",
    overflowing && Math.ceil(scroll.scrollLeft + scroll.clientWidth) < scroll.scrollWidth - 1,
  );
}

// jumps to the scroll target where 0 is the start and 1 the end
function jumpToScrollEnds(target) {
  const position = target * el.input.value.length;
  el.input.focus();
  el.input.setSelectionRange(position, position);
  followCaret();
}

// While a label prints: the label the device is working on, with the part
// already embossed under the progress bar, kept in the middle of the track.
function drawPrinting(status) {
  const label = typeof status.current_label === "string" ? status.current_label : "";
  if (el.printingLabel.textContent !== label) {
    // textContent, not innerHTML: this is current_label off api/status, which
    // is whatever was posted to api/tag, and a label is text.
    el.printingLabel.textContent = label;
  }
  const width = getLabelWidth(el.printingLabel, label);
  el.printingLabel.style.width = width + "px";
  updateOverflow();

  const characters = Array.from(label);
  const done = Math.round((characters.length * printPercentage(status)) / 100);
  const printed = characters.slice(0, done).join("");
  // From the strip's left edge, past the centring, to just after the last
  // character down.
  const textStart = Math.max((width - measureText(el.printingLabel, label)) / 2, 0);
  const progress = printed === "" ? 0 : textStart + measureText(el.printingLabel, printed) + 1;
  el.progressBar.style.width = progress + "px";

  const scroll = el.tapeScroll;
  scroll.scrollLeft = Math.max(el.tape.offsetLeft + progress - scroll.clientWidth / 2, 0);
}

// How far through the current label the device is. The device already holds
// the last point back while it finishes feeding and cutting (see Progress.h).
// Subtracting another one here is what made the browser read a point below
// the OLED beside it.
function printPercentage(status) {
  const percentage = parseInt(status.progress, 10);
  return Number.isNaN(percentage) ? 0 : Math.min(Math.max(percentage, 0), 100);
}

//--------------//
//   quantity   //
//--------------//

function quantityMode() {
  return el.form.elements.quantity.value;
}

function remainingRollMm() {
  const roll = state.status && state.status.roll;
  return roll && Number.isFinite(roll.remaining_mm) ? roll.remaining_mm : null;
}

// How many labels of the one typed fit on what is left of the roll, or null
// while there is no label to measure or no roll to measure it against.
function labelsFitting() {
  const remaining = remainingRollMm();
  if (remaining === null || !isValidLabelText()) {
    return null;
  }
  return labelsThatFit(remaining, codePoints(buildTreatedLabel()));
}

// The whole number in the Multiple field, in range or not, or null while it
// is not one. A number field that holds something it cannot read as a
// number reports an empty value, so anything else typed is null here too.
function rawCopies() {
  const text = el.copiesInput.value.trim();
  return /^\d+$/.test(text) ? Number(text) : null;
}

// The count in the Multiple field, or null while it is not one the device
// would take.
function typedCopies() {
  const copies = rawCopies();
  if (device === null || copies === null) {
    return null;
  }
  return copies >= MIN_MULTIPLE && copies <= device.copies.maximum ? copies : null;
}

// How many labels the print button asks for, or null while the options as
// they stand do not come to a number.
function requestedCopies() {
  switch (quantityMode()) {
    case "multiple":
      return typedCopies();
    case "max": {
      const fit = labelsFitting();
      return fit === null || fit < 1 ? null : Math.min(fit, device.copies.maximum);
    }
    default:
      return 1;
  }
}

// Settles the Multiple field on a count the device will take: the nearest
// one to what was typed, or the smallest when nothing readable was.
function commitCopies() {
  if (device === null) {
    return;
  }
  const typed = Number(el.copiesInput.value.trim());
  const copies = el.copiesInput.value.trim() === "" || !Number.isFinite(typed) ? MIN_MULTIPLE : Math.round(typed);
  el.copiesInput.value = clamp(copies, MIN_MULTIPLE, device.copies.maximum);
  render();
}

// From an out-of-range count, the first step lands back inside the range.
function stepCopies(step) {
  if (device === null) {
    return;
  }
  const copies = rawCopies();
  const next = copies === null ? MIN_MULTIPLE : copies + step;
  el.copiesInput.value = clamp(next, MIN_MULTIPLE, device.copies.maximum);
  render();
}

function plural(count, one, many) {
  return count + " " + (count === 1 ? one : many);
}

// The way out of a roll that is spent, on a line of its own under what is
// wrong with it.
const LOAD_NEW_ROLL = "\nLoad a new roll in Setup.";

// The line under the quantity: what the choice will take out of the roll.
// A warning, not a refusal, when the choice is more than the roll is
// estimated to hold -- the estimate is a count of feeds against a length
// somebody typed in, and the tape on the spool is the better judge.
function quantityNote(mode) {
  const remaining = remainingRollMm();
  if (remaining === null || device === null) {
    return { text: "", tone: null };
  }
  if (remaining <= 0) {
    return { text: "The roll is estimated to be empty." + LOAD_NEW_ROLL, tone: "warning" };
  }
  const left = formatLength(remaining);
  if (!isValidLabelText()) {
    return { text: left + " of tape left on the roll.", tone: null };
  }

  const length = codePoints(buildTreatedLabel());
  const fit = labelsThatFit(remaining, length);
  if (fit === 0) {
    return {
      text: "Only " + left + " left, not enough for a label this long." + LOAD_NEW_ROLL,
      tone: "warning",
    };
  }

  if (mode === "multiple") {
    const copies = typedCopies();
    if (copies === null) {
      return { text: "Enter a number from " + MIN_MULTIPLE + " to " + device.copies.maximum + ".", tone: "warning" };
    }
    if (copies > fit) {
      return { text: "Only about " + plural(fit, "fits", "fit") + " on the " + left + " left.", tone: "warning" };
    }
    return {
      text: "Uses about " + formatLength(copies * labelLengthMm(length)) + " of the " + left + " left.",
      tone: null,
    };
  }
  if (mode === "max") {
    if (fit > device.copies.maximum) {
      return {
        text: plural(device.copies.maximum, "label", "labels") + ", the most one run prints. About " + fit + " fit.",
        tone: null,
      };
    }
    return { text: plural(fit, "label", "labels") + ", to the end of the roll.", tone: null };
  }
  return {
    text: "About " + plural(fit, "label this long fits", "labels this long fit") + " on the " + left + " left.",
    tone: null,
  };
}

function printButtonText(copies) {
  if (quantityMode() === "one" || copies === 1) {
    return "Print label";
  }
  return copies === null ? "Print labels" : "Print " + copies + " labels";
}

//--------------//
//   commands   //
//--------------//

// Sends one command to the device. The name is a key in COMMAND_LABELS,
// which is also the path it posts to. Returns whether the device accepted
// it; if it did not, the page says why.
async function send(name, data = {}) {
  state.problem = null;
  // Whatever the last stop had to say, a new command is the end of it.
  state.stopRequest = null;
  state.stopNote = null;
  state.revealStop = false;
  state.posting = name;
  render();
  let accepted = false;
  try {
    const response = await postJson("api/" + name, data);
    if (response.ok) {
      accepted = true;
      state.pending = { name: name, acceptedAt: performance.now() };
    } else {
      const reply = await readJson(response);
      const reason = reply && typeof reply.error === "string" ? reply.error : null;
      console.error("Unable to " + name);
      console.error(reason ?? response.status);
      // Nor is there anything for a stop tapped meanwhile to stop.
      state.stopRequest = null;
      showProblem(reason ?? "The label maker refused that, and did not say why (HTTP " + response.status + ").");
    }
  } catch (error) {
    console.error("Unable to " + name);
    console.error(error);
    state.stopRequest = null;
    showProblem("Couldn’t reach the label maker. Check that it’s switched on, then try again.");
  } finally {
    state.posting = null;
    render();
  }
  if (accepted) {
    // A stop tapped while the command was on its way, sent now that the
    // device has something to stop.
    const request = state.stopRequest;
    if (request !== null && !request.sent) {
      postStop(request);
    }
    // Now rather than on the next tick, so what the device is doing shows as
    // soon as it has started doing it.
    poll();
  }
  return accepted;
}

function canPrint() {
  return (
    runningCommand() === null &&
    state.missedPolls < OFFLINE_AFTER_MISSES &&
    isValidLabelText() &&
    requestedCopies() !== null
  );
}

// sends the label to the device
async function printLabels() {
  if (!canPrint()) {
    return;
  }
  const copies = requestedCopies();
  // Puts a phone's keyboard away, so the label printing is what is on screen.
  el.input.blur();
  state.sentCopies = copies;
  if (await send("tag", { tag: buildTreatedLabel().toLowerCase(), copies: copies })) {
    scrollStopsIntoPlace(el.runActions, el.activity);
  }
}

// Stops what the device is doing, "now" or "after_label". Now is at the
// next press or turn of a motor: a press already on its way down always
// finishes, so the wheel is never left jammed in the tape. After the label
// is once the label being pressed has been cut, so nothing is cut short.
//
// row is the stops the tap was on, which ignore it if they have only just
// come up.
function requestStop(kind, row) {
  if (!settled(row)) {
    return;
  }
  const request = { kind: kind, sent: false, acceptedAt: null };
  state.problem = null;
  state.stopNote = null;
  state.stopRequest = request;
  state.revealStop = true;
  render();
  // Still on its way, the command has nothing to stop yet. send() passes
  // this on once the device has it.
  if (state.posting === null) {
    postStop(request);
  }
}

async function postStop(request) {
  request.sent = true;
  const now = request.kind === "now";
  try {
    // A stop is no use late, so the device is given less time than usual
    // to answer one before the page says it cannot get through.
    const response = await fetchWithTimeout(now ? "api/stop" : "api/stop?after=label", {
      method: "POST",
      timeout: 5000,
    });
    const reply = await readJson(response);
    if (state.stopRequest !== request) {
      // Overtaken by a command sent since, which is the end of this one.
    } else if (!response.ok) {
      const reason = reply && typeof reply.error === "string" ? reply.error : null;
      console.error("Unable to stop");
      console.error(reason ?? response.status);
      state.stopRequest = null;
      showProblem(reason ?? "The label maker would not stop, and did not say why (HTTP " + response.status + ").");
    } else if (reply !== null && reply.result === "idle") {
      // Not a failure: what it was doing finished while the tap was on its
      // way.
      state.stopRequest = null;
      state.stopNote = TOO_LATE_TO_STOP;
    } else {
      request.acceptedAt = performance.now();
    }
  } catch (error) {
    console.error("Unable to stop");
    console.error(error);
    if (state.stopRequest === request) {
      state.stopRequest = null;
      showProblem(
        now
          ? "Couldn’t reach the label maker to stop it. If it has to stop now, switch it off."
          : "Couldn’t reach the label maker to stop it. The rest of the labels will still print.",
      );
    }
  }
  render();
  poll();
}

// The stop the device has been asked for, by this page or any other, or
// null. A stop now outranks one after the label.
function pendingStop() {
  const status = state.status;
  const kinds = [
    state.stopRequest === null ? null : state.stopRequest.kind,
    status !== null && status.busy ? status.stop : null,
  ];
  if (kinds.includes("now")) {
    return "now";
  }
  return kinds.includes("after_label") ? "after_label" : null;
}

// The device's record of what the last stop cut short, or null. It keeps
// one until the next command, and has none when nothing was cut short.
function lastStop() {
  const stopped = state.status === null ? null : state.status.stopped;
  return stopped !== null && typeof stopped === "object" ? stopped : null;
}

function stopKey(stopped) {
  return [stopped.command, stopped.printed, stopped.copies, stopped.unfinished].join();
}

// What to say once a stop has ended a command, or null: the device's
// record, or without one a note of this page's own.
function stopNotice() {
  const stopped = lastStop();
  if (stopped !== null) {
    if (stopKey(stopped) === state.dismissedStop) {
      return null;
    }
    return { text: stoppedText(stopped), unfinished: stopped.unfinished === true };
  }
  return state.stopNote === null ? null : { text: state.stopNote, unfinished: false };
}

// Tape fed for a label that was then not finished is still in the machine,
// and comes out on the front of the next label unless it is cut off first.
function stoppedText(stopped) {
  const unfinished = stopped.unfinished === true;
  const cutFirst = " Cut it off before printing again.";
  switch (stopped.command) {
    case "tag": {
      const { printed, copies } = stopped;
      if (!Number.isInteger(printed) || !Number.isInteger(copies) || copies <= 1) {
        return unfinished ? "Stopped partway through the label." + cutFirst : "Stopped before the label was started.";
      }
      if (unfinished) {
        return "Stopped partway through label " + (printed + 1) + " of " + copies + "." + cutFirst;
      }
      if (printed === 0) {
        return "Stopped before label 1 of " + copies + " was started.";
      }
      return "Stopped after " + printed + " of " + copies + " labels.";
    }
    case "testfull":
      return unfinished
        ? "Stopped partway through the test label." + cutFirst
        : "Stopped before the test label was started.";
    case "testalign":
      return "Stopped the alignment test.";
    case "reel":
      return "Stopped loading the new roll before the tape was all the way through. Load it again to finish.";
    default:
      return "The label maker was stopped.";
  }
}

function dismissStopNotice() {
  const stopped = lastStop();
  if (stopped !== null) {
    state.dismissedStop = stopKey(stopped);
  }
  state.stopNote = null;
  render();
  (state.view === "setup" ? el.setupView : el.printView).focus({ preventScroll: true });
}

// When each set of stops, and each stop notice, last came up.
const shownAt = new Map();

// Shows or hides a set of stops or a stop notice, noting when it comes up.
// Each is hidden while its view is, so coming back to the view counts.
function showArmed(element, shown) {
  if (shown && element.hidden) {
    shownAt.set(element, performance.now());
  }
  element.hidden = !shown;
}

// Whether one has been up long enough for a tap on it to have been meant
// for it. See STOP_ARMING_MS.
function settled(element) {
  return !element.hidden && performance.now() - (shownAt.get(element) ?? -Infinity) >= STOP_ARMING_MS;
}

// Scrolls stops that have come up for a command this page started down to
// their own place on the page, so they are not stuck over the view of what
// the command is doing -- as far as that goes without scrolling what
// started it off the top of the screen.
function scrollStopsIntoPlace(row, origin) {
  if (row.hidden) {
    return;
  }
  scrollPage(Math.max(0, Math.min(stuckBy(row), origin.getBoundingClientRect().top - SCROLL_ROOM)));
}

// Brings what a stop tapped here came to into view, once it has come to
// something. The stops stay in sight wherever the page is scrolled, but
// what they come to has its own place on the page, which can be off the
// screen, or under the stops if they are still up. All of it, and clear of
// the stops, as far as that goes without taking its top off the screen.
function revealStopOutcome() {
  const view = state.view === "setup" ? el.setupView : el.printView;
  const box =
    view.querySelector("[data-problem]:not([hidden])") ?? view.querySelector("[data-stop-notice]:not([hidden])");
  if (box === null) {
    return;
  }
  const row = state.view === "setup" ? el.setupRunActions : el.runActions;
  const { top, bottom } = box.getBoundingClientRect();
  const down = Math.max(0, bottom + SCROLL_ROOM - innerHeight, row.hidden ? 0 : stuckBy(row));
  scrollPage(Math.min(down, top - SCROLL_ROOM));
}

// How far down the page has to scroll for a set of stops stuck at the
// bottom of the screen to be back in their own place. Nothing stuck is in
// the way of anything above that place.
function stuckBy(row) {
  return naturalBottom(row) - (innerHeight - parseFloat(getComputedStyle(row).bottom));
}

function scrollPage(by) {
  if (Math.abs(by) >= 1) {
    const reduce = matchMedia("(prefers-reduced-motion: reduce)").matches;
    window.scrollBy({ top: by, behavior: reduce ? "auto" : "smooth" });
  }
}

// Where the bottom of a set of stops is in the flow of the page, however
// far up from there it is stuck: the top of what follows it less the gap
// between them, or the bottom of what it is in.
function naturalBottom(row) {
  let next = row.nextElementSibling;
  while (next !== null && next.hidden) {
    next = next.nextElementSibling;
  }
  if (next === null) {
    return row.parentElement.getBoundingClientRect().bottom;
  }
  return next.getBoundingClientRect().top - parseFloat(getComputedStyle(next).marginTop);
}

let stuckFrame = 0;

function watchStuck() {
  if (stuckFrame === 0) {
    stuckFrame = requestAnimationFrame(() => {
      stuckFrame = 0;
      markStuck();
    });
  }
}

// Marks a set of stops stuck while it is held at the bottom of the screen
// over the page, up from its own place. style.css puts a scrim behind it.
function markStuck() {
  for (const row of [el.runActions, el.setupRunActions]) {
    const stuck = !row.hidden && row.getBoundingClientRect().bottom < naturalBottom(row) - 0.5;
    row.toggleAttribute("data-stuck", stuck);
  }
}

// The device rejects any align or force outside the range it served. The
// values are only ever set from /api/status or by the steppers, which keep
// to the range, so an out-of-range value means a fetch has not landed yet --
// sending it anyway would draw a 400 for something the person did not do.
function calibrationValuesReady(...values) {
  if (device === null) {
    return false;
  }
  return values.every(
    (value) => Number.isInteger(value) && value >= device.calibration.min && value <= device.calibration.max,
  );
}

// no force: this test always presses at the minimum, slowly and lightly, so
// the alignment can be checked without embossing anything
async function testAlignCommand() {
  const draft = state.draft;
  if (draft === null || !calibrationValuesReady(draft.align)) {
    console.error("Cannot run the alignment test: align not loaded from the device yet");
    return;
  }
  if (await send("testalign", { align: draft.align })) {
    scrollStopsIntoPlace(el.setupRunActions, el.testAlignButton);
  }
}

async function testFullCommand() {
  const draft = state.draft;
  if (draft === null || !calibrationValuesReady(draft.align, draft.force)) {
    console.error("Cannot run the full test: align/force not loaded from the device yet");
    return;
  }
  if (await send("testfull", { align: draft.align, force: draft.force })) {
    scrollStopsIntoPlace(el.setupRunActions, el.testFullButton);
  }
}

// sends settings save command to the device, and reloads once it has
// restarted
async function settingsCommand() {
  const draft = state.draft;
  if (draft === null || !calibrationValuesReady(draft.align, draft.force)) {
    console.error("Cannot save: align/force not loaded from the device yet");
    return;
  }
  if (!(await send("save", { align: draft.align, force: draft.force }))) {
    return;
  }
  state.restarting = true;
  render();

  let count = RESTART_SECONDS;
  const showCount = () => {
    el.countdown.textContent = plural(Math.max(count, 0), "second", "seconds");
  };
  showCount();
  el.restartDialog.showModal();
  setInterval(() => {
    count -= 1;
    showCount();
    if (count <= 0) {
      window.location.reload();
    }
  }, 1000);
}

//-----------//
//   setup   //
//-----------//

function openSetup() {
  state.view = "setup";
  state.problem = null;
  // What the device has now, not what it had when the page loaded: another
  // phone may have saved since.
  state.draft = state.saved === null ? null : { ...state.saved };
  render();
  window.scrollTo(0, 0);
  el.setupView.focus({ preventScroll: true });
}

function leaveSetup() {
  if (hasUnsavedChanges()) {
    const { draft, saved } = state;
    const changed = ["align", "force"].filter((name) => draft[name] !== saved[name]);
    const [change, have] = changed.length > 1 ? ["changes", "have"] : ["change", "has"];
    el.discardSummary.textContent = `Your ${change} to ${changed.join(" and ")} ${have} not been saved.`;
    openDialog(el.discardDialog);
    return;
  }
  closeSetup();
}

function closeSetup() {
  state.view = "print";
  state.draft = null;
  state.problem = null;
  render();
  // Measured again now it is on screen: hidden, the track had no width.
  drawTape();
  window.scrollTo(0, 0);
  el.setupButton.focus({ preventScroll: true });
}

function hasUnsavedChanges() {
  const { draft, saved } = state;
  return draft !== null && saved !== null && (draft.align !== saved.align || draft.force !== saved.force);
}

// The steppers for align and force. The values are held as numbers here
// and never read back off the page: an input's value and its min and max
// are all strings, "9" + 1 is "91", and the steppers that used to read
// them compared strings and only stayed in range because "91" happens to
// sort after "9".
function stepSetting(name, step) {
  if (state.draft === null || device === null) {
    return;
  }
  const next = state.draft[name] + step;
  if (next < device.calibration.min || next > device.calibration.max) {
    return;
  }
  state.draft[name] = next;
  render();
}

function confirmSave() {
  const draft = state.draft;
  if (draft === null || !calibrationValuesReady(draft.align, draft.force)) {
    return;
  }
  el.saveSummary.textContent =
    "Align " +
    draft.align +
    " and force " +
    draft.force +
    " are saved, then the label maker restarts to use them. It takes about " +
    RESTART_SECONDS +
    " seconds.";
  openDialog(el.saveDialog);
}

function openDialog(dialog) {
  // A dialog keeps the last answer it was closed with. Cleared, so Escape
  // this time is not read as whatever was pressed last time.
  dialog.returnValue = "";
  dialog.showModal();
}

function openReelDialog() {
  if (device === null) {
    return;
  }
  const roll = device.roll;
  const current = state.status && state.status.roll ? state.status.roll.length_mm : roll.default_mm;
  el.reelLength.min = roll.minimum_mm / 1000;
  el.reelLength.max = roll.maximum_mm / 1000;
  el.reelLength.value = clamp(current, roll.minimum_mm, roll.maximum_mm) / 1000;
  renderReelDialog();
  openDialog(el.reelDialog);
}

// The roll length typed into the dialog, in millimetres, or null while it
// is not one the device would take.
function typedRollLength() {
  const lengthMm = typedRollLengthUnchecked();
  if (lengthMm === null || device === null) {
    return null;
  }
  return lengthMm >= device.roll.minimum_mm && lengthMm <= device.roll.maximum_mm ? lengthMm : null;
}

function typedRollLengthUnchecked() {
  const text = el.reelLength.value.trim();
  const metres = Number(text);
  return text === "" || !Number.isFinite(metres) ? null : Math.round(metres * 1000);
}

// Moves to the next half metre up or down, rather than by half a metre from
// wherever the field is, so a typed 2.7 steps to 3 and not to 3.2.
function stepReel(direction) {
  if (device === null) {
    return;
  }
  const roll = device.roll;
  const current = typedRollLengthUnchecked() ?? roll.default_mm;
  const next =
    direction > 0
      ? (Math.floor(current / ROLL_STEP_MM) + 1) * ROLL_STEP_MM
      : (Math.ceil(current / ROLL_STEP_MM) - 1) * ROLL_STEP_MM;
  el.reelLength.value = clamp(next, roll.minimum_mm, roll.maximum_mm) / 1000;
  renderReelDialog();
}

function renderReelDialog() {
  if (device === null) {
    return;
  }
  const roll = device.roll;
  const lengthMm = typedRollLength();
  const typed = typedRollLengthUnchecked();
  el.reelLength.setAttribute("aria-invalid", lengthMm === null ? "true" : "false");
  el.reelConfirm.disabled = lengthMm === null;
  el.reelLess.disabled = typed !== null && typed <= roll.minimum_mm;
  el.reelMore.disabled = typed !== null && typed >= roll.maximum_mm;
  setText(
    el.reelRange,
    "From " +
      roll.minimum_mm / 1000 +
      " to " +
      roll.maximum_mm / 1000 +
      " m. A new roll is usually " +
      formatLength(roll.default_mm) +
      ".",
  );
  setTone(el.reelRange, lengthMm === null ? "warning" : null);
}

//-------------//
//   polling   //
//-------------//

let pollTimer = null;
let polling = false;

async function poll() {
  if (polling || state.restarting) {
    return;
  }
  polling = true;
  clearTimeout(pollTimer);
  const requestedAt = performance.now();
  try {
    const response = await fetchWithTimeout("api/status", { timeout: 5000 });
    if (!response.ok) {
      throw new Error("api/status answered " + response.status);
    }
    applyStatus(await response.json(), requestedAt);
    state.missedPolls = 0;
  } catch (error) {
    state.missedPolls += 1;
    // Once, when the page starts saying so, rather than every second after.
    if (state.missedPolls === OFFLINE_AFTER_MISSES) {
      console.error("Lost touch with the label maker");
      console.error(error);
    }
  } finally {
    polling = false;
    render();
    if (!state.restarting) {
      pollTimer = setTimeout(poll, document.hidden ? HIDDEN_POLL_MS : POLL_MS);
    }
  }
}

function applyStatus(status, requestedAt) {
  state.status = status;
  if (state.pending !== null && requestedAt >= state.pending.acceptedAt) {
    state.pending = null;
  }
  // A stop the device took is over once a status asked for after that no
  // longer says one is coming. One to stop now that left no record behind
  // had nothing left to stop.
  const request = state.stopRequest;
  if (request !== null && request.acceptedAt !== null && requestedAt >= request.acceptedAt) {
    if (!(status.busy && status.stop)) {
      if (request.kind === "now" && !status.busy && lastStop() === null) {
        state.stopNote = TOO_LATE_TO_STOP;
      }
      state.stopRequest = null;
    }
  }
  if (status.busy) {
    state.stopNote = null;
  }
  if (lastStop() === null) {
    state.dismissedStop = null;
  }
  if (Number.isInteger(status.align) && Number.isInteger(status.force)) {
    const before = state.saved;
    state.saved = { align: status.align, force: status.force };
    if (state.view === "setup" && state.draft === null) {
      state.draft = { ...state.saved };
    } else if (state.draft !== null && before !== null) {
      // A value not changed here follows the device, when another phone
      // saves one or the label maker comes back up with its own.
      for (const name of ["align", "force"]) {
        if (state.draft[name] === before[name]) {
          state.draft[name] = state.saved[name];
        }
      }
    }
  }
}

// The command the device is running, or about to, or null when it is idle.
function runningCommand() {
  if (state.posting !== null) {
    return state.posting;
  }
  if (state.pending !== null) {
    return state.pending.name;
  }
  if (state.status !== null && state.status.busy) {
    return state.status.command;
  }
  return null;
}

function busyLabel(command) {
  const spec = COMMAND_LABELS[command];
  return spec ? spec.busyLabel : UNKNOWN_BUSY_LABEL;
}

// What the stop says while the command runs, or null if it is not offered
// one.
function stopLabel(command) {
  const spec = COMMAND_LABELS[command];
  return spec && spec.stopLabel ? spec.stopLabel : null;
}

//------------//
//   render   //
//------------//

function render() {
  const command = runningCommand();
  const busy = command !== null;
  const offline = state.missedPolls >= OFFLINE_AFTER_MISSES;
  // Read before anything is disabled or hidden: either can take focus away,
  // and then there is no telling where it was.
  const focused = document.activeElement;

  el.offline.hidden = !offline || state.restarting;
  el.printView.hidden = state.view !== "print";
  el.setupView.hidden = state.view !== "setup";
  setText(el.viewName, state.view === "setup" ? "Setup" : "Label maker");

  renderPrintView(command, busy, offline, focused);
  renderSetupView(command, busy, offline, focused);
  renderStopNotices(busy, offline, focused);
  renderProblems();
  // A stop comes to something when the command it stopped is over, or if
  // it could not be stopped, when the page says so.
  if (state.revealStop && (!busy || state.problem !== null)) {
    state.revealStop = false;
    revealStopOutcome();
  }
  markStuck();
}

function renderPrintView(command, busy, offline, focused) {
  const status = state.status;
  const run = status !== null && status.busy && status.command === "tag" ? status : null;

  const wasPrinting = document.body.dataset.printing === "true";
  document.body.dataset.printing = run !== null ? "true" : "false";
  if (run !== null) {
    drawPrinting(run);
  } else if (wasPrinting) {
    // The input is back, and it was not measured while it was hidden.
    drawTape();
  }

  // the label
  const typed = el.input.value;
  const valid = isValidLabelText();
  el.input.disabled = busy;
  el.input.setAttribute("aria-invalid", unprintableCharacters(typed).length > 0 ? "true" : "false");
  const hint = hintFor(typed);
  setText(el.hint, hint.text);
  setTone(el.hint, hint.tone);
  setText(el.length, valid ? Math.round(labelLengthMm(codePoints(buildTreatedLabel()))) + " mm" : "");
  for (const key of el.keys) {
    key.disabled = busy;
  }
  el.clearButton.disabled = busy || typed === "";
  for (const radio of el.form.elements.margin) {
    radio.disabled = busy;
  }

  // how many
  const remaining = remainingRollMm();
  const fit = labelsFitting();
  const maxAvailable = !(remaining !== null && remaining <= 0) && fit !== 0;
  const multipleAvailable = device === null || device.copies.maximum >= MIN_MULTIPLE;
  if (!maxAvailable && quantityMode() === "max") {
    // Nothing left to print to the end of. Back to one, rather than leave a
    // choice selected that cannot be made.
    el.form.elements.quantity.value = "one";
  }
  for (const radio of el.form.elements.quantity) {
    radio.disabled =
      busy || (radio.value === "max" && !maxAvailable) || (radio.value === "multiple" && !multipleAvailable);
  }
  const mode = quantityMode();
  const copiesRaw = rawCopies();
  el.copiesStepper.hidden = mode !== "multiple";
  el.copiesInput.disabled = busy;
  el.copiesInput.setAttribute("aria-invalid", mode === "multiple" && typedCopies() === null ? "true" : "false");
  el.copiesLess.disabled = busy || (copiesRaw !== null && copiesRaw <= MIN_MULTIPLE);
  el.copiesMore.disabled = busy || device === null || (copiesRaw !== null && copiesRaw >= device.copies.maximum);
  const note = quantityNote(mode);
  setText(el.quantityNote, note.text);
  setTone(el.quantityNote, note.tone);

  // print, or what the machine is doing instead
  const copies = requestedCopies();
  setText(el.printButton, printButtonText(copies));
  el.printButton.disabled = busy || offline || !valid || copies === null;
  el.printButton.hidden = busy;
  el.activity.hidden = !busy;
  if (busy) {
    renderActivity(command, run);
  }
  // The button a keyboard was on goes away under it while the machine runs.
  // Focus goes to what took its place, and back again after.
  if (busy && focused === el.printButton) {
    el.activity.focus({ preventScroll: true });
  } else if (!busy && focused === el.activity) {
    el.printButton.focus({ preventScroll: true });
  }

  // under the card, or while something that can be stopped runs, the stops
  const stopText = busy ? stopLabel(command) : null;
  el.machineActions.hidden = stopText !== null;
  showArmed(el.runActions, stopText !== null && state.view === "print");
  el.feedButton.disabled = busy || offline;
  el.cutButton.disabled = busy || offline;
  el.setupButton.disabled = busy;
  if (stopText !== null) {
    renderStops(command, run, stopText, offline);
  }
  // A stop a keyboard was on goes out of reach once it is pressed, and
  // away once the command is over. Focus goes to what says how it went.
  if (
    (focused === el.stopNowButton || focused === el.stopButton) &&
    (el.runActions.hidden || focused.hidden || focused.disabled)
  ) {
    (busy ? el.activity : el.printButton).focus({ preventScroll: true });
  }
}

// Stopping is never held back for the page being out of touch: the tap
// may still get through, and if it does not, the page says what else to do.
function renderStops(command, run, stopText, offline) {
  const stop = pendingStop();
  const now = stop === "now";
  setText(el.stopNowText, now ? "Stopping…" : stopText);
  el.stopNowButton.disabled = now;
  el.stopNowButton.toggleAttribute("data-running", now);

  // A run of labels can instead be let finish the label it is on. How many
  // labels it has comes from the device once a poll has it, and until then
  // from what this page asked for, so the stops come up the right size.
  const copies = run !== null ? run.copies : command === "tag" ? state.sentCopies : null;
  const gentle = stop === "after_label";
  el.stopButton.hidden = !(command === "tag" && Number.isInteger(copies) && copies > 1);
  setText(el.stopButton, gentle ? "Stopping after this label…" : "Stop after this label");
  el.stopButton.disabled = stop !== null || offline || (run !== null && run.copy >= run.copies);
  el.stopButton.toggleAttribute("data-running", gentle);
}

function renderActivity(command, run) {
  const stop = pendingStop();
  let text = busyLabel(command);
  let percentage = null;
  if (command === "tag" && run !== null) {
    percentage = printPercentage(run);
    if (Number.isInteger(run.copies) && Number.isInteger(run.copy) && run.copies > 1) {
      // Kept together when a narrow screen puts the words over two lines.
      const count = ["label", run.copy, "of", run.copies].join(" ");
      text = (stop === "after_label" ? "Stopping after " : "Printing ") + count;
      // The whole run, not the label it is on: the tape above already shows
      // how far into this label it is, and a bar that emptied at every cut
      // would say nothing about when the run ends.
      percentage = Math.floor(((clamp(run.copy, 1, run.copies) - 1) * 100 + percentage) / run.copies);
    }
  }
  if (stop === "now") {
    text = "Stopping…";
  }
  setText(el.activityText, text);
  setText(el.activityPercent, percentage === null ? "" : percentage + "%");
  el.activityFill.style.width = (percentage ?? 0) + "%";
  // Breathes while there is no number to watch go up instead.
  el.activity.toggleAttribute("data-breathing", percentage === null);
}

function renderSetupView(command, busy, offline, focused) {
  const roll = state.status !== null ? state.status.roll : null;
  if (roll) {
    setText(el.rollRemaining, formatLength(roll.remaining_mm));
    setText(el.rollOf, "left of " + formatLength(roll.length_mm));
    const share = roll.length_mm > 0 ? clamp(roll.remaining_mm / roll.length_mm, 0, 1) : 0;
    el.rollMeter.style.width = share * 100 + "%";
    setTone(el.rollMeter, share < 0.1 ? "warning" : null);
  } else {
    setText(el.rollRemaining, "–");
    setText(el.rollOf, "");
    el.rollMeter.style.width = "0";
  }
  el.reelButton.disabled = busy || offline || device === null;
  showRunning(el.reelButton, command === "reel");

  const draft = state.draft;
  setText(el.alignValue, draft === null ? "–" : String(draft.align));
  setText(el.forceValue, draft === null ? "–" : String(draft.force));
  for (const button of el.stepButtons) {
    const next = draft === null ? null : draft[button.dataset.setting] + Number(button.dataset.step);
    // Held while anything runs, so the number beside a test is the one it
    // is testing.
    button.disabled = busy || !calibrationValuesReady(next);
  }
  const alignReady = draft !== null && calibrationValuesReady(draft.align);
  const bothReady = draft !== null && calibrationValuesReady(draft.align, draft.force);
  el.testAlignButton.disabled = busy || offline || !alignReady;
  showRunning(el.testAlignButton, command === "testalign");
  el.testFullButton.disabled = busy || offline || !bothReady;
  showRunning(el.testFullButton, command === "testfull");
  // Saving restarts the label maker, which is not worth doing for the
  // numbers it already has.
  el.saveButton.disabled = busy || offline || !bothReady || !hasUnsavedChanges() || state.restarting;
  el.cancelButton.disabled = state.restarting;

  // the stop, for anything that can be stopped, whoever started it
  const stopText = busy ? stopLabel(command) : null;
  showArmed(el.setupRunActions, stopText !== null && state.view === "setup");
  if (stopText !== null) {
    const now = pendingStop() === "now";
    setText(el.setupStopText, now ? "Stopping…" : stopText);
    el.setupStopButton.disabled = now;
    el.setupStopButton.toggleAttribute("data-running", now);
  }
  if (focused === el.setupStopButton && (el.setupRunActions.hidden || el.setupStopButton.disabled)) {
    el.setupView.focus({ preventScroll: true });
  }
  setText(el.setupStatus, busy && stopText === null && !SETUP_COMMANDS.includes(command) ? busyLabel(command) : "");
}

// A setup button says what its command is doing while it runs. The page
// has both of its labels, and style.css shows the one this picks.
function showRunning(button, running) {
  button.toggleAttribute("data-running", running);
}

// What the last stop left behind, in whichever view is open, once the
// machine is done with it.
function renderStopNotices(busy, offline, focused) {
  const notice = busy ? null : stopNotice();
  for (const box of el.stopNotices) {
    const view = box.closest(".view") === el.setupView ? "setup" : "print";
    if (notice !== null) {
      setText(box.querySelector(".notice-text"), notice.text);
      box.toggleAttribute("data-unfinished", notice.unfinished);
      const cut = box.querySelector("[data-stop-cut]");
      cut.hidden = !notice.unfinished;
      cut.disabled = offline;
    }
    showArmed(box, notice !== null && view === state.view);
    // Cut is the one that goes away under a keyboard, while it cuts.
    if (box.hidden && box.contains(focused)) {
      (state.view === "setup" ? el.setupView : busy ? el.activity : el.printButton).focus({ preventScroll: true });
    }
  }
}

function renderProblems() {
  for (const box of el.problems) {
    const view = box.closest(".view") === el.setupView ? "setup" : "print";
    const message = state.problem !== null && state.problem.view === view ? state.problem.message : "";
    box.hidden = message === "";
    setText(box.querySelector(".problem-text"), message);
  }
}

// source names what raised it, for whatever has to take it down again when
// that same thing goes right.
function showProblem(message, source = null) {
  state.problem = { view: state.view, message: message, source: source };
}

function dismissProblem() {
  state.problem = null;
  render();
  (state.view === "setup" ? el.setupView : el.printView).focus({ preventScroll: true });
}

// Writes only what has changed. render() runs on every poll, and a live
// region given the same words again is read out again.
function setText(element, text) {
  if (element.textContent !== text) {
    element.textContent = text;
  }
}

function setTone(element, tone) {
  if (tone === null) {
    delete element.dataset.tone;
  } else {
    element.dataset.tone = tone;
  }
}

//-----------//
//   utils   //
//-----------//

function clamp(value, min, max) {
  return Math.min(Math.max(value, min), max);
}

// Helper method to make fetch requests with a configurable timeout.
// See: https://dmitripavlutin.com/timeout-fetch-request/
async function fetchWithTimeout(resource, options = {}) {
  const { timeout = 8000 } = options;

  const controller = new AbortController();
  const id = setTimeout(() => controller.abort(), timeout);
  try {
    return await fetch(resource, {
      ...options,
      signal: controller.signal,
    });
  } finally {
    clearTimeout(id);
  }
}

// Helper method to post a json request, supports timeouts.
async function postJson(url, data, options = {}) {
  options.headers = {
    "Content-Type": "application/json",
    Accept: "application/json",
  };
  options.body = JSON.stringify(data);
  options.method = "POST";
  return await fetchWithTimeout(url, options);
}

// The body of a reply as JSON, or null when it is not any: a 404 from older
// firmware is plain text, and a reply cut off by a timeout is nothing.
async function readJson(response) {
  try {
    return await response.json();
  } catch (error) {
    return null;
  }
}

// Last, once everything above it exists: startup() draws the tape straight
// away, and that reaches for bindings further down this file.
startup();
