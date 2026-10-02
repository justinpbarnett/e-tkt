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
import { Link, NEVER_STARTED, Sending, TAKEN, UNKNOWN, deliver, lastHeardText, newCommandId } from "./link.js";
import {
  NEARBY_POLL_MS,
  NETWORK_POLL_MS,
  NetworkCard,
  NetworkSearch,
  addingIntro,
  failureText,
  forgetChange,
  modeChange,
  modeNote,
  networkToAdd,
  ownSummary,
  reachSummary,
  rememberedRows,
  rememberedSummary,
  routerChange,
} from "./network.js";
import { plural, quantity, settledCopies, steppedCopies } from "./quantity.js";
import {
  activity,
  CapabilitiesMismatch,
  commandListDisagreement,
  NO_ANSWER_YET,
  printingRun,
  printPercentage,
  readCapabilities,
  refusalText,
  setupText,
  stopOffer,
} from "./status.js";
import { Stops, stopPath } from "./stops.js";
import {
  formatLength,
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

// How long the device is given to answer one try of what the page asks it or
// sends it, before the page takes that try for lost.
const ANSWER_MS = 5000;

// And one try of a command, or of a change to how the device is reached.
// Longer: the last try given up on is the page saying it could not reach the
// device, of something the device may have done.
const COMMAND_ANSWER_MS = 8000;

// How long the page waits before it asks again what the device accepts, when
// the device has not said.
const CAPABILITIES_RETRY_MS = 2000;

// How often the page brings the time since the device was last heard up to
// date, while it says it has lost touch. That time is told to the second.
const LAST_HEARD_TICK_MS = 1000;

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
  // Whether the device is answering the page, and when it last did.
  link: new Link(),
  // The command whose POST is on its way.
  posting: null,
  // Set while that command has had no answer, and the page is sending it
  // again.
  unanswered: false,
  // The command this page last sent, as a Sending: the id it went under, and
  // what became of it when a status, or a stop sent after it, says so before
  // its own answer does.
  sent: null,
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
  // What the Network card in Setup goes by: how the device is reached, as
  // api/network last said it, and the change to that which is on its way.
  networkCard: new NetworkCard(),
  // The Add dialog's search for the networks in reach of the device. Kept
  // from one opening to the next, so what it heard last time shows while it
  // listens again.
  search: new NetworkSearch(),
  // The change the confirm dialog is asking about: the path it posts to,
  // what it posts, and where on the page it was made, as one of the
  // data-unanswered notes in index.html names it.
  networkChange: null,
  // What went wrong adding a network, for the Add dialog to say.
  addProblem: null,
};

const $ = (id) => document.getElementById(id);

const el = {
  themeButton: $("theme-button"),
  viewName: $("view-name"),
  offline: $("offline"),
  lastHeard: $("last-heard"),
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
  unloadButton: $("unload-button"),
  reelButton: $("reel-button"),
  networkCard: $("network-card"),
  reachHeadline: $("reach-headline"),
  reachDetail: $("reach-detail"),
  reachFailure: $("reach-failure"),
  reachModes: document.querySelectorAll('[name="reach-mode"]'),
  reachModeNote: $("reach-mode-note"),
  rememberedCount: $("remembered-count"),
  rememberedList: $("remembered-list"),
  rememberedRow: $("remembered-row"),
  rememberedNote: $("remembered-note"),
  addNetworkButton: $("add-network-button"),
  ownName: $("own-name"),
  ownState: $("own-state"),
  ownNote: $("own-note"),
  routerInput: $("router-input"),
  unanswered: document.querySelectorAll("[data-unanswered]"),
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
  unloadDialog: $("unload-dialog"),
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
  networkConfirmDialog: $("network-confirm-dialog"),
  networkConfirmTitle: $("network-confirm-title"),
  networkConfirmSummary: $("network-confirm-summary"),
  networkConfirmButton: $("network-confirm-button"),
  networkDialog: $("network-dialog"),
  networkForm: $("network-form"),
  networkTitle: $("network-title"),
  networkIntro: $("network-intro"),
  networkFull: $("network-full"),
  listenButton: $("listen-button"),
  nearbyText: $("nearby-text"),
  nearbyList: $("nearby-list"),
  nearbyRow: $("nearby-row"),
  networkName: $("network-name"),
  networkNameNote: $("network-name-note"),
  networkPasswordEntry: $("network-password-entry"),
  networkPassword: $("network-password"),
  networkPasswordShow: $("network-password-show"),
  networkPasswordNote: $("network-password-note"),
  networkHeld: $("network-held"),
  networkError: $("network-error"),
  networkAdd: $("network-add"),
  restartDialog: $("restart-dialog"),
  countdown: $("countdown"),
};

// The tape's own face. Half the bytes of the panel, so it is fetched once
// the page is already on screen. A browser that kept it from last time
// finishes this without another trip, and the tape is measured again then.
function loadTapeFont() {
  if (typeof FontFace !== "function" || !document.fonts) {
    return;
  }
  const face = new FontFace("Impact Label Reversed", 'url("/fontwhite.ttf")', {
    display: "swap",
  });
  document.fonts.add(face);
  return face.load().then(drawTape, () => {});
}

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

  // One after the other, not side by side: the device serves the page's own
  // files at the same time, and it has only a handful of sockets. The tape
  // face comes after the two the page cannot work without, so it does not
  // take a socket from either of them.
  await retrieveCapabilities();
  await poll();
  await loadTapeFont();
  // Only Setup shows how the device is reached, and asks again as it opens.
  // Asked here first, so the card is in its place by then and does not come
  // up under a finger that was after what is below it.
  await askNetwork();
  render();
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

  el.unloadButton.addEventListener("click", () => openDialog(el.unloadDialog));
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
  el.unloadDialog.addEventListener("close", async () => {
    if (el.unloadDialog.returnValue === "unload" && (await send(el.unloadButton.dataset.command))) {
      scrollStopsIntoPlace(el.setupRunActions, el.unloadButton);
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

  for (const radio of el.reachModes) {
    radio.addEventListener("change", () => reachModeChosen(radio));
  }
  el.rememberedList.addEventListener("click", (event) => {
    const forget = event.target.closest("button");
    if (forget !== null) {
      forgetNetwork(forget.dataset.ssid);
    }
  });
  el.addNetworkButton.addEventListener("click", openNetworkDialog);
  el.routerInput.addEventListener("change", routerChosen);
  el.networkConfirmDialog.addEventListener("close", () => {
    if (el.networkConfirmDialog.returnValue === "confirm") {
      sendNetworkChange();
    } else {
      state.networkChange = null;
    }
  });

  el.listenButton.addEventListener("click", () => {
    listenForNetworks();
    // The button is held while the label maker listens, and a keyboard on it
    // would be left nowhere. From the title, the list is the next stop.
    el.networkTitle.focus();
  });
  el.nearbyList.addEventListener("click", (event) => {
    const pick = event.target.closest("button");
    if (pick !== null) {
      pickNetwork(pick.dataset.ssid);
    }
  });
  for (const field of [el.networkName, el.networkPassword]) {
    field.addEventListener("input", networkTyped);
  }
  el.networkPasswordShow.addEventListener("change", () => {
    el.networkPassword.type = el.networkPasswordShow.checked ? "text" : "password";
  });
  el.networkForm.addEventListener("keydown", (event) => {
    // Enter in a form submits it with the first submit button in it, and
    // here that is Cancel. Enter after a name goes on to its password, and
    // after that it means add the network. A button keeps its own Enter.
    if (event.key !== "Enter" || event.target.closest("button") !== null) {
      return;
    }
    event.preventDefault();
    if (event.target === el.networkName && !el.networkPasswordEntry.hidden) {
      el.networkPassword.focus();
    } else {
      el.networkForm.requestSubmit(el.networkAdd);
    }
  });
  el.networkForm.addEventListener("submit", (event) => {
    // Add leaves the dialog up until the device has the network, and
    // addNetwork() closes it then.
    if (event.submitter === el.networkAdd) {
      event.preventDefault();
      addNetwork();
    }
  });
  el.networkDialog.addEventListener("close", closeNetworkDialog);
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
      pollNetwork();
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
    const response = await fetchFromDevice("api/capabilities");
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
    setTimeout(retrieveCapabilities, CAPABILITIES_RETRY_MS);
    return;
  }

  takeDownProblem("capabilities");
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
  const run = printableRun();
  if (run === null) {
    return;
  }
  const request = state.estimates.ask(run);
  if (request === null) {
    return;
  }
  try {
    const response = await postJson("api/tag/estimate", run);
    const reply = await readJson(response);
    if (!response.ok) {
      console.warn("Unable to estimate the run");
      console.warn(refusalText(response, reply));
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
//
// A command that has had no answer is sent again, as deliver() in link.js
// has it, unless sendAgain is false. Sent again or not, a status that names
// the command is the device saying it took it, and the page waits for no
// other answer. Nor is it sent again once a stop has been sent after it: the
// stop then says what became of it.
async function send(name, data = {}, { sendAgain = true } = {}) {
  state.problem = null;
  state.stops.commandStarting();
  state.revealStop = false;
  state.posting = name;
  // One id for every try, so a device that took the command, and whose
  // answer was lost, says so to the next try and does not run it twice.
  const sending = new Sending();
  state.sent = sending;
  render();
  let accepted = false;
  const path = "api/" + name + "?id=" + sending.id;
  try {
    const answer = await sendUntilAnswered({
      attempt: () => postJson(path, data, { timeout: COMMAND_ANSWER_MS }),
      wanted: () => sendAgain,
      unanswered: () => {
        state.unanswered = true;
        render();
      },
      command: sending,
    });
    if (answer === NEVER_STARTED || answer === UNKNOWN) {
      // A stop was sent after the command, and what the page says of that
      // stop is all there is to say.
    } else if (answer === TAKEN || answer.ok) {
      accepted = true;
      // Unless a status has named the command: that status is on the page
      // already, with what has become of the command since.
      if (!sending.named()) {
        state.pending = { name: name, acceptedAt: performance.now() };
      }
    } else {
      const reply = await readJson(answer);
      // Refused because a stop sent after it got there first, the command
      // was stopped, and the page says that instead.
      if (!state.stops.commandRefused(reply)) {
        const problem = refusalText(answer, reply);
        console.error("Unable to " + name);
        console.error(problem);
        showProblem(problem);
      }
    }
  } catch (error) {
    console.error("Unable to " + name);
    console.error(error);
    showProblem("Couldn’t reach the label maker. Check that it’s switched on, then try again.", "command");
  } finally {
    state.posting = null;
    state.unanswered = false;
    render();
  }
  if (accepted) {
    // Now rather than on the next tick, so what the device is doing shows as
    // soon as it has started doing it.
    poll();
  }
  return accepted;
}

// Sends something until the device answers it, or something else says what
// became of it, as deliver() in link.js has it, on this page's clock.
function sendUntilAnswered(delivery) {
  return deliver({
    ...delivery,
    now: () => performance.now(),
    pause: (ms) => new Promise((resolve) => setTimeout(resolve, ms)),
  });
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

// The run on the form while pressing print would send it: the machine is
// idle and in touch, and the form has a run the device would take. null
// otherwise. The print button and the estimate under it both go by this.
function printableRun() {
  if (runningCommand() !== null || state.link.lost()) {
    return null;
  }
  return formRun();
}

// sends the label to the device
async function printLabels() {
  const run = printableRun();
  if (run === null) {
    return;
  }
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
  // A command still on its way is the one the stop is for, and the page
  // sends it no more: the stop goes now, and says what became of it.
  const request = state.stops.ask(kind, state.posting === null ? null : state.sent, performance.now());
  state.unanswered = false;
  state.revealStop = true;
  render();
  postStop(request);
}

async function postStop(request) {
  // One id for every try. The device keeps what it answered under it, so a
  // stop that got through, and whose answer was lost, cannot land a second
  // time on whatever the machine does next.
  const path = stopPath(request, newCommandId());
  try {
    // A stop is no use late, so the device is given less time to answer one
    // than to answer a command, before the page says it has not got through.
    // The page goes on trying after that, for as long as the stop is still
    // wanted.
    const response = await sendUntilAnswered({
      attempt: () => fetchFromDevice(path, { method: "POST" }),
      wanted: () => state.stops.wanted(request),
      unanswered: () => {
        const problem = state.stops.unanswered(request);
        if (problem !== null) {
          showProblem(problem, "stop");
          render();
        }
      },
    });
    takeDownProblem("stop");
    const problem = state.stops.answered(request, response, await readJson(response), performance.now());
    if (problem !== null) {
      console.error("Unable to stop");
      console.error(problem);
      showProblem(problem);
    }
  } catch (error) {
    takeDownProblem("stop");
    const problem = state.stops.unreachable(request);
    // Not of a stop the page no longer waits on: that one did its work, or
    // had none left to do, and nothing went wrong.
    if (problem !== null) {
      console.error("Unable to stop");
      console.error(error);
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

// Brings what a tap came to into view, once it has come to something: a
// stop tapped here, or a change on the Network card that could not be made.
// The stops stay in sight wherever the page is scrolled, and the card is
// where its change was asked for, but what either comes to has its own
// place on the page, which can be off the screen, or under the stops if
// they are still up. All of it, and clear of the stops, as far as that goes
// without taking its top off the screen.
function revealOutcome() {
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
  // Sent once. A save ends in a restart, and a restart is the end of the ids
  // the device kept: sent again after it, the save would run again and
  // restart the device again.
  if (!(await send("save", fields, { sendAgain: false }))) {
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
  pollNetwork();
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
//   network   //
//-------------//

let networkTimer = null;
let networkAsking = false;

// Whether Setup is up to show what the device says of its network.
function networkWanted() {
  return state.view === "setup" && !document.hidden && !state.restarting && !state.networkCard.absent;
}

// Whether the Add dialog is waiting to hear what a listen came to.
function listenAwaited() {
  return el.networkDialog.open && state.search.waiting;
}

// Asks how the device is reached for as long as Setup shows it, and what it
// has heard while the Add dialog waits for a listen to end. One request
// after the other, as at startup: the status poll goes on beside these, and
// the device has only a handful of sockets.
async function pollNetwork() {
  clearTimeout(networkTimer);
  if (networkAsking || !networkWanted()) {
    return;
  }
  networkAsking = true;
  try {
    if (listenAwaited()) {
      await askNearby();
    }
    if (state.networkCard.due(performance.now())) {
      await askNetwork();
    }
  } finally {
    networkAsking = false;
    render();
    if (networkWanted()) {
      networkTimer = setTimeout(pollNetwork, listenAwaited() ? NEARBY_POLL_MS : NETWORK_POLL_MS);
    }
  }
}

// Asks the device how it is reached, for the card to take. An answer that
// does not come, or comes cut short, leaves the card as it was until the
// next one.
async function askNetwork() {
  const asking = state.networkCard.asked(performance.now());
  try {
    const response = await fetchFromDevice("api/network");
    state.networkCard.answered(asking, response, response.ok ? await response.json() : null);
  } catch (error) {
    // The status poll is what says the device is out of reach.
  }
}

// Asks the device what it has heard, for the search to take.
async function askNearby() {
  try {
    const response = await fetchFromDevice("api/network/nearby");
    if (response.ok) {
      state.search.heard(await response.json());
    }
  } catch (error) {
    // Asked again in a moment, for as long as the dialog waits.
  }
}

// Asks the device to listen for the networks in its reach. What it hears is
// in api/network/nearby once it has listened, and pollNetwork() asks for
// that.
//
// Asked again under one id until the device answers, as deliver() in link.js
// has it, so a device that took the listen, and whose answer was lost, does
// not listen a second time. Not for a dialog that has been closed, or once
// the listen has been asked for again.
async function listenForNetworks() {
  const request = state.search.asked();
  const path = "api/network/listen?id=" + newCommandId();
  render();
  try {
    const response = await sendUntilAnswered({
      attempt: () => postJson(path, {}),
      wanted: () => el.networkDialog.open && state.search.wanted(request),
      unanswered: () => {
        state.search.unanswered(request);
        render();
      },
    });
    if (response.ok) {
      state.search.taken(request, await readJson(response), performance.now());
    } else {
      state.search.lost(request);
    }
  } catch (error) {
    state.search.lost(request);
  }
  render();
  pollNetwork();
}

// Sends the device one change to how it is reached, and takes what it says
// its network comes to after it. Returns what went wrong, in words for the
// page, or null. Nothing of it is logged: what is sent may hold a network's
// password.
//
// change is the path to post to, what to post, and where on the page the
// change was made. It is sent again under one id until the device answers,
// as deliver() in link.js has it, and the device makes it once: the answer
// can be lost over the very link the change is about. Whoever has left Setup
// by then asked for it all the same, so nothing but the time ends the tries.
async function changeNetwork(change) {
  const card = state.networkCard;
  card.changeSent(change.on);
  render();
  const path = change.path + "?id=" + newCommandId();
  try {
    const response = await sendUntilAnswered({
      attempt: () => postJson(path, change.body, { timeout: COMMAND_ANSWER_MS }),
      wanted: () => true,
      unanswered: () => {
        card.changeUnanswered();
        render();
      },
    });
    return card.changeAnswered(response, await readJson(response), performance.now());
  } catch (error) {
    return card.changeLost();
  } finally {
    render();
  }
}

// A way of reaching the label maker was picked. The radios go straight back
// to the one the device has, and move when it says the change is made.
function reachModeChosen(radio) {
  const network = state.networkCard.network;
  if (network !== null && radio.value !== network.mode) {
    confirmNetworkChange(modeChange(network, radio.value), {
      path: "api/network/mode",
      body: { mode: radio.value },
      on: "mode",
    });
  }
  render();
}

// The same for the tick that has its own network offer a way to the
// internet.
function routerChosen() {
  const network = state.networkCard.network;
  const offered = el.routerInput.checked;
  if (network !== null && offered !== network.router_offered) {
    confirmNetworkChange(routerChange(network), {
      path: "api/network/router",
      body: { offered: offered },
      on: "own",
    });
  }
  render();
}

function forgetNetwork(ssid) {
  const network = state.networkCard.network;
  if (network !== null) {
    confirmNetworkChange(forgetChange(network, ssid), {
      path: "api/network/forget",
      body: { ssid: ssid },
      on: "remembered",
    });
  }
}

// Makes a change on the Network card: once it is confirmed, when it comes
// with the words to ask by, and at once when it does not.
function confirmNetworkChange(words, change) {
  if (state.networkCard.busy) {
    return;
  }
  state.networkChange = change;
  if (words === null) {
    sendNetworkChange();
    return;
  }
  setText(el.networkConfirmTitle, words.title);
  setText(el.networkConfirmSummary, words.summary);
  setText(el.networkConfirmButton, words.confirm);
  openDialog(el.networkConfirmDialog);
}

// Sends the change that was asked for, and says so on the page if it could
// not be made. The card's controls are held while it is on its way, and one
// that is held loses the keyboard: it is given back to the one the change
// was made with, or if that was the button of a network now forgotten, to
// the button under the list.
async function sendNetworkChange() {
  const change = state.networkChange;
  state.networkChange = null;
  if (change === null) {
    return;
  }
  state.problem = null;
  const asker = document.activeElement;
  // Asked now: a network's button is off the card once it is forgotten.
  const onCard = el.networkCard.contains(asker);
  const problem = await changeNetwork(change);
  if (problem !== null) {
    showProblem(problem);
    render();
  }
  if (onCard && document.activeElement === document.body) {
    (asker.isConnected ? asker : el.addNetworkButton).focus({ preventScroll: true });
  }
  if (problem !== null) {
    revealOutcome();
  }
}

function openNetworkDialog() {
  if (state.networkCard.network === null) {
    return;
  }
  state.addProblem = null;
  openDialog(el.networkDialog);
  render();
  // Not left on the first thing in the dialog that can take it, where a
  // screen reader would start part of the way down.
  el.networkTitle.focus();
  listenForNetworks();
}

// A password is not left in the page once the dialog is done with it, and
// the next network starts from nothing. What the dialog said of a network
// still on its way is for the card to say from here on.
function closeNetworkDialog() {
  el.networkName.value = "";
  el.networkPassword.value = "";
  el.networkPassword.type = "password";
  el.networkPasswordShow.checked = false;
  state.addProblem = null;
  render();
}

// The network in the Add dialog's fields, checked against what the device
// would take.
function typedNetwork() {
  const typed = { ssid: el.networkName.value, password: el.networkPassword.value };
  return networkToAdd(typed, state.networkCard.network, state.search);
}

// What went wrong with the network as it was no longer applies to it.
function networkTyped() {
  state.addProblem = null;
  render();
}

// A network in reach was picked: its name goes in the field, and the
// keyboard goes on to what the network needs next.
function pickNetwork(ssid) {
  el.networkName.value = ssid;
  el.networkPassword.value = "";
  networkTyped();
  if (!el.networkPasswordEntry.hidden) {
    el.networkPassword.focus();
  } else if (!el.networkAdd.disabled) {
    el.networkAdd.focus();
  }
}

// Has the device remember the network in the dialog, and closes the dialog
// once it does. The dialog says what went wrong if it does not, or the page
// does, if the dialog was closed in the meantime.
async function addNetwork() {
  if (state.networkCard.network === null || state.networkCard.busy || runningCommand() !== null) {
    return;
  }
  const adding = typedNetwork();
  if (adding.body === null) {
    return;
  }
  state.addProblem = null;
  const asker = document.activeElement;
  const problem = await changeNetwork({ path: "api/network/remember", body: adding.body, on: "add" });
  if (problem === null) {
    el.networkDialog.close();
  } else if (el.networkDialog.open) {
    state.addProblem = problem;
  } else {
    showProblem(problem);
  }
  render();
  // Add is held while the network is on its way, and a keyboard on it is
  // left nowhere. It gets Add back to try again with.
  if (el.networkDialog.open && asker === el.networkAdd && document.activeElement !== asker) {
    el.networkAdd.focus();
  }
  // So is the button that opened the dialog, which a dialog closed in the
  // meantime could not hand the keyboard back to.
  if (!el.networkDialog.open && document.activeElement === document.body) {
    el.addNetworkButton.focus({ preventScroll: true });
  }
  if (problem !== null && !el.networkDialog.open) {
    revealOutcome();
  }
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
    const response = await fetchWithTimeout("api/status");
    if (!response.ok) {
      throw new Error("api/status answered " + response.status);
    }
    applyStatus(await response.json(), requestedAt);
    state.link.heard(performance.now());
  } catch (error) {
    // Once, when the page starts saying so, rather than every second after.
    if (state.link.missed()) {
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
  // A stop the page was still trying to get through is over once the device
  // has nothing left to stop, and so is what the page said about trying.
  if (state.stops.pending(null) === null) {
    takeDownProblem("stop");
  }
  // The device names the command it last took by the id it came under. When
  // that is the one this page last sent, the command arrived, whatever became
  // of its answer: the page stops waiting for one, and no longer says it
  // could not get the command through.
  if (state.sent !== null && state.sent.statusArrived(status)) {
    takeDownProblem("command");
  }
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
  const offline = state.link.lost();
  // What the page knows of the command running, as activity() and
  // stopOffer() in status.js take it, and the stops on offer while it runs.
  const running = {
    command: command,
    status: state.status,
    stop: state.stops.pending(state.status),
    sentCopies: state.sentCopies,
    offline: offline,
    unanswered: state.unanswered,
  };
  const offer = busy ? stopOffer(running, device) : null;
  // Read before anything is disabled or hidden: either can take focus away,
  // and then there is no telling where it was.
  const focused = document.activeElement;

  renderOffline(offline);
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
    revealOutcome();
  }
  markStuck();
}

// Brings the time since the device was last heard up to date, for as long as
// the page says it has lost touch.
let lastHeardTimer = null;

function renderOffline(offline) {
  clearTimeout(lastHeardTimer);
  const shown = offline && !state.restarting;
  el.offline.hidden = !shown;
  if (shown) {
    setText(el.lastHeard, lastHeardText(state.link.sinceHeard(performance.now())));
    lastHeardTimer = setTimeout(() => renderOffline(state.link.lost()), LAST_HEARD_TICK_MS);
  }
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
  setText(el.length, valid ? formatLength(labelLengthMm(codePoints(buildTreatedLabel()), device)) : "");
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
  const printable = printableRun();
  setText(el.printButton, chosen.printText);
  el.printButton.disabled = printable === null;
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
  if (printable !== null && !state.estimates.askedLast(printable)) {
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
  // Unloading needs nothing the device has to say first. Loading needs the
  // roll lengths it takes.
  el.unloadButton.disabled = busy || offline;
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
  for (const button of [el.unloadButton, el.reelButton, el.testAlignButton, el.testFullButton]) {
    showRunning(button, command === button.dataset.command);
  }
  // How the device is reached is left alone while it runs, and while one
  // change to it is still on its way.
  renderNetworkCard(busy || offline || state.restarting || state.networkCard.busy);
  renderNetworkDialog(busy);
  renderUnanswered();
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
  // A line of its own, for a command that has no stop to name it, and for
  // the wait until the device has said what it accepts.
  setText(el.setupStatus, setupText(running, device));
}

// A setup button says what its command is doing while it runs. The page
// has both of its labels, and style.css shows the one this picks.
function showRunning(button, running) {
  button.toggleAttribute("data-running", running);
}

// The Network card, from what api/network last said. held is set while
// nothing on it is to be changed.
function renderNetworkCard(held) {
  const network = state.networkCard.network;
  el.networkCard.hidden = network === null;
  if (network === null) {
    return;
  }
  const reach = reachSummary(network);
  setText(el.reachHeadline, reach.headline);
  setParts(el.reachDetail, reach.detail);
  const failure = failureText(network);
  el.reachFailure.hidden = failure === null;
  setText(el.reachFailure, failure ?? "");

  // Where the device has it, whatever was last tapped: a change shows here
  // once the device says it is made.
  for (const radio of el.reachModes) {
    radio.checked = radio.value === network.mode;
    radio.disabled = held;
  }
  setText(el.reachModeNote, modeNote(network));

  const remembered = rememberedSummary(network);
  setText(el.rememberedCount, remembered.count);
  el.rememberedNote.hidden = remembered.note === null;
  setText(el.rememberedNote, remembered.note ?? "");
  // Before the list is drawn, which hands the keyboard to this button when
  // the network it was on is gone.
  el.addNetworkButton.disabled = held;
  drawRows(el.rememberedList, el.rememberedRow, rememberedRows(network), fillRemembered, el.addNetworkButton);
  for (const forget of el.rememberedList.querySelectorAll("button")) {
    forget.disabled = held;
  }

  const own = ownSummary(network);
  setText(el.ownName, network.own.name);
  setText(el.ownState, own.state);
  setText(el.ownNote, own.note);
  el.routerInput.checked = network.router_offered;
  el.routerInput.disabled = held;
}

function fillRemembered(item, row) {
  item.querySelector(".network-name").textContent = row.ssid;
  const tag = item.querySelector(".network-tag");
  tag.hidden = row.tag === null;
  tag.textContent = row.tag ?? "";
  const forget = item.querySelector("button");
  forget.dataset.ssid = row.ssid;
  forget.setAttribute("aria-label", "Forget " + row.ssid);
}

// The Add dialog, while it is up: the networks in reach, and what is typed
// in held against what the device would take. The fields are the typist's,
// and nothing here writes to them.
function renderNetworkDialog(busy) {
  const network = state.networkCard.network;
  if (network === null || !el.networkDialog.open) {
    return;
  }
  const now = performance.now();
  setText(el.networkIntro, addingIntro(network));
  setText(el.nearbyText, state.search.text(now) ?? "");
  el.listenButton.disabled = busy || !state.search.askable(now);
  drawRows(el.nearbyList, el.nearbyRow, state.search.rows(network), fillNearby, el.networkTitle);
  for (const pick of el.nearbyList.querySelectorAll("button")) {
    if (pick.dataset.ssid === el.networkName.value) {
      pick.setAttribute("aria-current", "true");
    } else {
      pick.removeAttribute("aria-current");
    }
  }

  const adding = typedNetwork();
  el.networkFull.hidden = adding.full === null;
  setText(el.networkFull, adding.full ?? "");
  showCheck(el.networkName, el.networkNameNote, adding.name);
  // A network heard without a password is not asked for one.
  el.networkPasswordEntry.hidden = adding.password.wanted === "no";
  showCheck(el.networkPassword, el.networkPasswordNote, adding.password);
  el.networkHeld.hidden = !busy;
  el.networkError.hidden = state.addProblem === null;
  setText(el.networkError, state.addProblem ?? "");
  el.networkAdd.disabled = adding.body === null || state.networkCard.busy || busy;
  showRunning(el.networkAdd, state.networkCard.busy);
}

// The eye gets a lock and the bars of the signal, drawn by style.css from
// what is set here. The button is named with the same in words.
function fillNearby(item, row) {
  const pick = item.querySelector("button");
  pick.dataset.ssid = row.ssid;
  pick.setAttribute("aria-label", row.ssid + ". " + row.facts);
  item.querySelector(".network-name").textContent = row.ssid;
  item.querySelector(".network-tag").hidden = !row.remembered;
  item.querySelector(".icon-lock").toggleAttribute("hidden", !row.secured);
  item.querySelector(".icon-signal").dataset.signal = row.signal;
}

// What a check of a field in the Add dialog came to, under the field: a
// note, as a warning when what is typed is a mistake.
function showCheck(field, note, check) {
  field.setAttribute("aria-invalid", check.invalid ? "true" : "false");
  note.hidden = check.note === null;
  setText(note, check.note ?? "");
  setTone(note, check.invalid ? "warning" : null);
}

// Says that a change to how the device is reached has had no answer yet,
// beside what the change was made with, for as long as the page is sending
// it again.
function renderUnanswered() {
  const place = state.networkCard.unansweredAt(el.networkDialog.open);
  for (const note of el.unanswered) {
    const shown = note.dataset.unanswered === place;
    note.hidden = !shown;
    setText(note, shown ? NO_ANSWER_YET : "");
  }
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

// Takes down the problem that source raised, if it is the one showing.
function takeDownProblem(source) {
  if (state.problem !== null && state.problem.source === source) {
    state.problem = null;
  }
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

// What was last drawn in each element that holds more than text.
const drawn = new Map();

// Whether what is to be drawn in an element differs from what was drawn in
// it last, which it is noted as from here on. As with setText(), only what
// has changed is written: what is built again under a keyboard, or under a
// finger on its way down, is lost to it.
function outdated(element, content) {
  const now = JSON.stringify(content);
  if (drawn.get(element) === now) {
    return false;
  }
  drawn.set(element, now);
  return true;
}

// Draws a sentence given in parts, each one text or a link to make of an
// address.
function setParts(element, parts) {
  if (!outdated(element, parts)) {
    return;
  }
  element.replaceChildren(
    ...parts.map((part) => {
      if (typeof part === "string") {
        return part;
      }
      const link = document.createElement("a");
      link.href = part.link;
      link.textContent = part.link;
      // The page stays as it is, with what is typed into it.
      link.target = "_blank";
      link.rel = "noopener";
      return link;
    }),
  );
}

// Draws a list of networks: a copy of the template for each row, filled in
// by fill(). A keyboard that was in the list goes back to the button of the
// network it was on, or to fallback when that network is gone.
function drawRows(list, template, rows, fill, fallback) {
  if (!outdated(list, rows)) {
    return;
  }
  const focused = document.activeElement;
  const ssid = list.contains(focused) ? focused.dataset.ssid : null;
  list.replaceChildren(
    ...rows.map((row) => {
      const item = template.content.firstElementChild.cloneNode(true);
      fill(item, row);
      return item;
    }),
  );
  if (ssid !== null) {
    const buttons = [...list.querySelectorAll("button")];
    (buttons.find((button) => button.dataset.ssid === ssid) ?? fallback).focus({ preventScroll: true });
  }
}

//-----------//
//   utils   //
//-----------//

// Helper method to make fetch requests with a configurable timeout, which
// is ANSWER_MS unless the caller gives another.
// See: https://dmitripavlutin.com/timeout-fetch-request/
async function fetchWithTimeout(resource, options = {}) {
  const { timeout = ANSWER_MS } = options;

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

// Sends the device anything but a poll, as fetchWithTimeout() does, and tells
// the link what came back: the answer to a command is the device heard from
// as much as a status is. A poll says so for itself, once it has read a
// status out of its answer.
async function fetchFromDevice(resource, options = {}) {
  const response = await fetchWithTimeout(resource, options);
  state.link.answered(response, performance.now());
  return response;
}

// Helper method to post a json request, supports timeouts.
async function postJson(url, data, options = {}) {
  options.headers = {
    "Content-Type": "application/json",
    Accept: "application/json",
  };
  options.body = JSON.stringify(data);
  options.method = "POST";
  return await fetchFromDevice(url, options);
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
