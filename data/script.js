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

// The page: what it shows, and what a tap on it does. What the page says
// and decides is worked out in the modules imported here, which never touch
// the page, so node tests them in test/panel/. This one reads the page,
// asks them, and draws what they answer.
import { CalibrationDraft, RESTART_SECONDS } from "./calibration.js";
import {
  codePoints,
  hintFor,
  isValidLabelText,
  paddedLabel,
  paddedLabelTarget,
  typedLengthLimit,
  unprintableCharacters,
} from "./label.js";
import { plural, quantity, settledCopies, steppedCopies } from "./quantity.js";
import {
  activity,
  busyText,
  CapabilitiesMismatch,
  commandListDisagreement,
  printingRun,
  printPercentage,
  readCapabilities,
  stopOffer,
} from "./status.js";
import { Stops } from "./stops.js";
import {
  labelLengthMm,
  offeredRollLength,
  rollGauge,
  rollRange,
  steppedRollLength,
  tapeLeftMm,
  typedRoll,
} from "./tape.js";
import { Estimates, timeText } from "./timing.js";

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

// How close to the top of the screen scrolling to the stops may take the
// button that started the command.
const SCROLL_ROOM = 16;

// Where this browser keeps a theme picked with the button in the header.
// index.html reads the same key before the page is drawn.
const THEME_KEY = "e-tkt-theme";

// Where this browser keeps whether labels are cut as they come out, so each
// run is cut or not as the last one was.
const CUT_KEY = "e-tkt-cut";

// How long the form stays on one run before the page asks the device how
// long it would take, so a count being typed is not asked about digit by
// digit.
const ESTIMATE_DELAY_MS = 300;

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
  // How long the device says the run on the form would take.
  estimates: new Estimates(),
  // The stops this page has asked for, and what the last one came to.
  stops: new Stops(),
  // From a tap on a stop until what it came to is brought into view: the
  // end of the command it stopped, or a problem stopping it.
  revealStop: false,
  view: "print",
  // The calibration Setup shows, against the one the device has saved.
  calibration: new CalibrationDraft(),
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
  cutInput: $("cut-input"),
  printButton: $("print-button"),
  activity: $("activity"),
  activityFill: $("activity-fill"),
  activityText: $("activity-text"),
  activityPercent: $("activity-percent"),
  timeNote: $("time-note"),
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
  el.cutInput.checked = storedCut();
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
  el.cutInput.addEventListener("change", cutChanged);

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
  for (const button of [el.testAlignButton, el.testFullButton]) {
    button.addEventListener("click", () => testCommand(button));
  }
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
    if (
      el.reelDialog.returnValue === "reel" &&
      lengthMm !== null &&
      (await send(el.reelButton.dataset.command, { length_mm: lengthMm }))
    ) {
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
  el.input.maxLength = typedLengthLimit(device);
  el.copiesInput.max = device.copies.maximum;
  const disagreement = commandListDisagreement(device);
  if (disagreement !== null) {
    console.warn(disagreement);
  }
  drawTape();
  render();
}

//-----------//
//   label   //
//-----------//

// The label as the device will be sent it, margin and all, or what the
// tape shows while nothing is typed.
function buildTreatedLabel() {
  return paddedLabel(el.input.value || "WRITE HERE", el.form.elements.margin.value, device);
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
  const target = paddedLabelTarget(device);
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

//--------------//
//   quantity   //
//--------------//

// What the quantity options come to as the page has them now, as quantity()
// in quantity.js works it out.
function quantityChosen() {
  const typed = el.input.value;
  return quantity(
    {
      mode: el.form.elements.quantity.value,
      copiesText: el.copiesInput.value,
      labelLength: isValidLabelText(typed, device) ? codePoints(buildTreatedLabel()) : null,
      tapeLeftMm: tapeLeftMm(state.status),
    },
    device,
  );
}

// Settles the Multiple field on a count the device will take.
function commitCopies() {
  if (device === null) {
    return;
  }
  el.copiesInput.value = settledCopies(el.copiesInput.value, device);
  render();
}

function stepCopies(step) {
  if (device === null) {
    return;
  }
  el.copiesInput.value = steppedCopies(el.copiesInput.value, step, device);
  render();
}

//---------//
//   cut   //
//---------//

// Whether this browser last had labels cut as they come out. Cut, as every
// label was before there was a choice, unless it was unticked here.
function storedCut() {
  try {
    return localStorage.getItem(CUT_KEY) !== "false";
  } catch (error) {
    // Storage can be refused outright, as index.html says. Cut, then.
    return true;
  }
}

function cutChanged() {
  try {
    localStorage.setItem(CUT_KEY, String(el.cutInput.checked));
  } catch (error) {
    // Not kept, then. It still counts for as long as the page is open.
  }
  render();
}

//---------------//
//   estimates   //
//---------------//

let estimateTimer = null;

// Asks the device how long the run on the form would take, once the form
// has stayed on it for ESTIMATE_DELAY_MS.
function scheduleEstimate() {
  clearTimeout(estimateTimer);
  estimateTimer = setTimeout(askEstimate, ESTIMATE_DELAY_MS);
}

// Only while the run could be printed: the time is under the print button,
// and says how long pressing it would take.
async function askEstimate() {
  const run = formRun();
  if (run === null || runningCommand() !== null || state.missedPolls >= OFFLINE_AFTER_MISSES) {
    return;
  }
  const request = state.estimates.ask(run);
  if (request === null) {
    return;
  }
  try {
    const response = await postJson("api/tag/estimate", run, { timeout: 5000 });
    const reply = await readJson(response);
    if (!response.ok) {
      console.warn("Unable to estimate the run");
      console.warn(reply !== null && typeof reply.error === "string" ? reply.error : response.status);
    }
    state.estimates.answered(request, reply);
  } catch (error) {
    console.warn("Unable to estimate the run");
    console.warn(error);
    state.estimates.unreachable(request);
  }
  render();
}

//--------------//
//   commands   //
//--------------//

// Sends one command to the device, by the name the device answers to, which
// is also the path it posts to. Returns whether the device accepted it; if
// it did not, the page says why.
async function send(name, data = {}) {
  state.problem = null;
  state.stops.commandStarting();
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
      state.stops.commandFailed();
      showProblem(reason ?? "The label maker refused that, and did not say why (HTTP " + response.status + ").");
    }
  } catch (error) {
    console.error("Unable to " + name);
    console.error(error);
    state.stops.commandFailed();
    showProblem("Couldn’t reach the label maker. Check that it’s switched on, then try again.");
  } finally {
    state.posting = null;
    render();
  }
  if (accepted) {
    // A stop tapped while the command was on its way, sent now that the
    // device has something to stop.
    const request = state.stops.takeUnsent();
    if (request !== null) {
      postStop(request);
    }
    // Now rather than on the next tick, so what the device is doing shows as
    // soon as it has started doing it.
    poll();
  }
  return accepted;
}

// The run of labels on the form, as it is posted to api/tag, or null while
// the form has none the device would take.
function formRun() {
  const copies = quantityChosen().copies;
  if (!isValidLabelText(el.input.value, device) || copies === null) {
    return null;
  }
  return { tag: buildTreatedLabel().toLowerCase(), copies: copies, cut: el.cutInput.checked };
}

function canPrint() {
  return runningCommand() === null && state.missedPolls < OFFLINE_AFTER_MISSES && formRun() !== null;
}

// sends the label to the device
async function printLabels() {
  if (!canPrint()) {
    return;
  }
  const run = formRun();
  // Puts a phone's keyboard away, so the label printing is what is on screen.
  el.input.blur();
  state.sentCopies = run.copies;
  if (await send("tag", run)) {
    scrollStopsIntoPlace(el.runActions, el.activity);
  }
}

// Asks the device to stop, "now" or "after_label", as stops.js has them.
//
// row is the stops the tap was on, which ignore it if they have only just
// come up.
function requestStop(kind, row) {
  if (!settled(row)) {
    return;
  }
  state.problem = null;
  state.stops.ask(kind);
  state.revealStop = true;
  render();
  // Still on its way, the command has nothing to stop yet. send() passes
  // this on once the device has it.
  if (state.posting === null) {
    postStop(state.stops.takeUnsent());
  }
}

async function postStop(request) {
  try {
    // A stop is no use late, so the device is given less time than usual
    // to answer one before the page says it cannot get through.
    const response = await fetchWithTimeout(request.kind === "now" ? "api/stop" : "api/stop?after=label", {
      method: "POST",
      timeout: 5000,
    });
    const problem = state.stops.answered(request, response, await readJson(response), performance.now());
    if (problem !== null) {
      console.error("Unable to stop");
      console.error(problem);
      showProblem(problem);
    }
  } catch (error) {
    console.error("Unable to stop");
    console.error(error);
    const problem = state.stops.unreachable(request);
    if (problem !== null) {
      showProblem(problem);
    }
  }
  render();
  poll();
}

function dismissStopNotice() {
  state.stops.dismiss(state.status);
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

// Runs the test a setup button names in data-command. Each test is sent the
// calibration fields the device says it uses, in api/capabilities. The
// alignment test uses align alone: it always presses at the minimum force,
// slowly and lightly, so the alignment can be checked without embossing
// anything.
async function testCommand(button) {
  const command = button.dataset.command;
  const fields = state.calibration.fieldsFor(command, device);
  if (fields === null) {
    console.error("Cannot run " + command + ": the calibration it uses is not loaded from the device yet");
    return;
  }
  if (await send(command, fields)) {
    scrollStopsIntoPlace(el.setupRunActions, button);
  }
}

// sends settings save command to the device, and reloads once it has
// restarted
async function settingsCommand() {
  const fields = state.calibration.fieldsFor("save", device);
  if (fields === null) {
    console.error("Cannot save: align/force not loaded from the device yet");
    return;
  }
  if (!(await send("save", fields))) {
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
  state.calibration.open();
  render();
  window.scrollTo(0, 0);
  el.setupView.focus({ preventScroll: true });
}

function leaveSetup() {
  const lost = state.calibration.unsavedSummary();
  if (lost !== null) {
    el.discardSummary.textContent = lost;
    openDialog(el.discardDialog);
    return;
  }
  closeSetup();
}

function closeSetup() {
  state.view = "print";
  state.calibration.close();
  state.problem = null;
  render();
  // Measured again now it is on screen: hidden, the track had no width.
  drawTape();
  window.scrollTo(0, 0);
  el.setupButton.focus({ preventScroll: true });
}

// The steppers for align and force.
function stepSetting(name, step) {
  if (state.calibration.step(name, step, device)) {
    render();
  }
}

function confirmSave() {
  const summary = state.calibration.saveSummary();
  if (summary === null || state.calibration.fieldsFor("save", device) === null) {
    return;
  }
  el.saveSummary.textContent = summary;
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
  el.reelLength.min = roll.minimum_mm / 1000;
  el.reelLength.max = roll.maximum_mm / 1000;
  el.reelLength.value = offeredRollLength(state.status, device) / 1000;
  renderReelDialog();
  openDialog(el.reelDialog);
}

// The roll length typed into the dialog, in millimetres, or null while it
// is not one the device would take.
function typedRollLength() {
  return device === null ? null : typedRoll(el.reelLength.value, device).lengthMm;
}

function stepReel(direction) {
  if (device === null) {
    return;
  }
  el.reelLength.value = steppedRollLength(el.reelLength.value, direction, device) / 1000;
  renderReelDialog();
}

function renderReelDialog() {
  if (device === null) {
    return;
  }
  const typed = typedRoll(el.reelLength.value, device);
  el.reelLength.setAttribute("aria-invalid", typed.lengthMm === null ? "true" : "false");
  el.reelConfirm.disabled = typed.lengthMm === null;
  el.reelLess.disabled = !typed.lessAvailable;
  el.reelMore.disabled = !typed.moreAvailable;
  setText(el.reelRange, rollRange(device));
  setTone(el.reelRange, typed.lengthMm === null ? "warning" : null);
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
  state.stops.statusArrived(status, requestedAt);
  state.calibration.statusArrived(status, state.view === "setup");
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

//------------//
//   render   //
//------------//

function render() {
  const command = runningCommand();
  const busy = command !== null;
  const offline = state.missedPolls >= OFFLINE_AFTER_MISSES;
  // What the page knows of the command running, as activity() and
  // stopOffer() in status.js take it, and the stops on offer while it runs.
  const running = {
    command: command,
    status: state.status,
    stop: state.stops.pending(state.status),
    sentCopies: state.sentCopies,
    offline: offline,
  };
  const offer = busy ? stopOffer(running, device) : null;
  // Read before anything is disabled or hidden: either can take focus away,
  // and then there is no telling where it was.
  const focused = document.activeElement;

  el.offline.hidden = !offline || state.restarting;
  el.printView.hidden = state.view !== "print";
  el.setupView.hidden = state.view !== "setup";
  setText(el.viewName, state.view === "setup" ? "Setup" : "Label maker");

  renderPrintView(running, offer, focused);
  renderSetupView(running, offer, focused);
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

function renderPrintView(running, offer, focused) {
  const { command, offline } = running;
  const busy = command !== null;
  const printing = printingRun(state.status, device);

  const wasPrinting = document.body.dataset.printing === "true";
  document.body.dataset.printing = printing !== null ? "true" : "false";
  if (printing !== null) {
    drawPrinting(printing);
  } else if (wasPrinting) {
    // The input is back, and it was not measured while it was hidden.
    drawTape();
  }

  // the label
  const typed = el.input.value;
  const valid = isValidLabelText(typed, device);
  el.input.disabled = busy;
  el.input.setAttribute("aria-invalid", unprintableCharacters(typed, device).length > 0 ? "true" : "false");
  const hint = hintFor(typed, device);
  setText(el.hint, hint.text);
  setTone(el.hint, hint.tone);
  setText(el.length, valid ? Math.round(labelLengthMm(codePoints(buildTreatedLabel()), device)) + " mm" : "");
  for (const key of el.keys) {
    key.disabled = busy;
  }
  el.clearButton.disabled = busy || typed === "";
  for (const radio of el.form.elements.margin) {
    radio.disabled = busy;
  }

  // how many
  const chosen = quantityChosen();
  // quantity() falls back to One once Max has nothing left to print to the
  // end of, rather than leave a choice selected that cannot be made.
  if (el.form.elements.quantity.value !== chosen.mode) {
    el.form.elements.quantity.value = chosen.mode;
  }
  for (const radio of el.form.elements.quantity) {
    radio.disabled =
      busy ||
      (radio.value === "max" && !chosen.maxAvailable) ||
      (radio.value === "multiple" && !chosen.multipleAvailable);
  }
  el.copiesStepper.hidden = chosen.mode !== "multiple";
  el.copiesInput.disabled = busy;
  el.copiesInput.setAttribute("aria-invalid", chosen.copiesInvalid ? "true" : "false");
  el.copiesLess.disabled = busy || !chosen.fewerAvailable;
  el.copiesMore.disabled = busy || !chosen.moreAvailable;
  setText(el.quantityNote, chosen.note.text);
  setTone(el.quantityNote, chosen.note.tone);
  el.cutInput.disabled = busy;

  // print, or what the machine is doing instead
  const run = formRun();
  setText(el.printButton, chosen.printText);
  el.printButton.disabled = busy || offline || run === null;
  el.printButton.hidden = busy;
  el.activity.hidden = !busy;
  if (busy) {
    renderActivity(running);
  }
  // The button a keyboard was on goes away under it while the machine runs.
  // Focus goes to what took its place, and back again after.
  if (busy && focused === el.printButton) {
    el.activity.focus({ preventScroll: true });
  } else if (!busy && focused === el.activity) {
    el.printButton.focus({ preventScroll: true });
  }

  // and how long it takes
  if (!busy && !offline && run !== null && !state.estimates.askedLast(run)) {
    scheduleEstimate();
  }
  setText(el.timeNote, timeText(running, device, state.estimates.text(run)));
  // Holds its line while it has something to say or soon will, so what is
  // under it does not move each time it has nothing to say for a moment.
  el.timeNote.toggleAttribute("data-held", busy || run !== null);

  // under the card, or while something that can be stopped runs, the stops
  el.machineActions.hidden = offer !== null;
  showArmed(el.runActions, offer !== null && state.view === "print");
  el.feedButton.disabled = busy || offline;
  el.cutButton.disabled = busy || offline;
  el.setupButton.disabled = busy;
  if (offer !== null) {
    renderStops(offer);
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

function renderStops(offer) {
  setText(el.stopNowText, offer.now.text);
  el.stopNowButton.disabled = offer.now.stopping;
  el.stopNowButton.toggleAttribute("data-running", offer.now.stopping);

  const afterLabel = offer.afterLabel;
  el.stopButton.hidden = afterLabel === null;
  if (afterLabel !== null) {
    setText(el.stopButton, afterLabel.text);
    el.stopButton.disabled = afterLabel.disabled;
    el.stopButton.toggleAttribute("data-running", afterLabel.stopping);
  }
}

function renderActivity(running) {
  const { text, percentage } = activity(running, device);
  setText(el.activityText, text);
  setText(el.activityPercent, percentage === null ? "" : percentage + "%");
  el.activityFill.style.width = (percentage ?? 0) + "%";
  // Breathes while there is no number to watch go up instead.
  el.activity.toggleAttribute("data-breathing", percentage === null);
}

function renderSetupView(running, offer, focused) {
  const { command, offline } = running;
  const busy = command !== null;
  const gauge = rollGauge(state.status);
  if (gauge !== null) {
    setText(el.rollRemaining, gauge.left);
    setText(el.rollOf, gauge.of);
    el.rollMeter.style.width = gauge.share * 100 + "%";
    setTone(el.rollMeter, gauge.low ? "warning" : null);
  } else {
    setText(el.rollRemaining, "–");
    setText(el.rollOf, "");
    el.rollMeter.style.width = "0";
  }
  el.reelButton.disabled = busy || offline || device === null;

  const calibration = state.calibration;
  const shown = calibration.shown;
  setText(el.alignValue, shown === null ? "–" : String(shown.align));
  setText(el.forceValue, shown === null ? "–" : String(shown.force));
  for (const button of el.stepButtons) {
    // Held while anything runs, so the number beside a test is the one it
    // is testing.
    button.disabled = busy || !calibration.canStep(button.dataset.setting, Number(button.dataset.step), device);
  }
  for (const button of [el.testAlignButton, el.testFullButton]) {
    button.disabled = busy || offline || calibration.fieldsFor(button.dataset.command, device) === null;
  }
  // Whoever started it: the command a button names in data-command is the
  // one it says is running.
  for (const button of [el.reelButton, el.testAlignButton, el.testFullButton]) {
    showRunning(button, command === button.dataset.command);
  }
  // Saving restarts the label maker, which is not worth doing for the
  // numbers it already has.
  el.saveButton.disabled =
    busy || offline || calibration.fieldsFor("save", device) === null || !calibration.unsaved() || state.restarting;
  el.cancelButton.disabled = state.restarting;

  // the stop, for anything that can be stopped, whoever started it
  showArmed(el.setupRunActions, offer !== null && state.view === "setup");
  if (offer !== null) {
    setText(el.setupStopText, offer.now.text);
    el.setupStopButton.disabled = offer.now.stopping;
    el.setupStopButton.toggleAttribute("data-running", offer.now.stopping);
  }
  if (focused === el.setupStopButton && (el.setupRunActions.hidden || el.setupStopButton.disabled)) {
    el.setupView.focus({ preventScroll: true });
  }
  // Named by a line of its own when it has no stop to name it, which leaves
  // a save: the device can stop everything else, and setup's own tests and
  // the new roll say so on their buttons as well.
  setText(el.setupStatus, busy && offer === null ? busyText(command) : "");
}

// A setup button says what its command is doing while it runs. The page
// has both of its labels, and style.css shows the one this picks.
function showRunning(button, running) {
  button.toggleAttribute("data-running", running);
}

// What the last stop left behind, in whichever view is open, once the
// machine is done with it.
function renderStopNotices(busy, offline, focused) {
  const notice = busy ? null : state.stops.notice(state.status, device);
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
