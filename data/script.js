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

let busy = false;
let align;
let force;
let alignTemp;
let forceTemp;
let scrollbarHeight = 0;

// What the button says while each command runs, keyed by the name the device
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
const COMMAND_LABELS = {
  cut: { busyLabel: " cutting... " },
  feed: { busyLabel: " feeding... " },
  reel: { busyLabel: " reeling... " },
  testalign: { busyLabel: " testing... " },
  testfull: { busyLabel: " testing... " },
  save: { busyLabel: " saving... " },
  tag: { busyLabel: " printing... " },
  home: { busyLabel: " homing... " },
  move: { busyLabel: " moving... " },
};

// Shown when the device reports a command this copy of the panel has never
// heard of, which means a cached script.js is talking to newer firmware.
const UNKNOWN_BUSY_LABEL = " working... ";

// What the device will accept: the characters a label may contain, what the
// ones the wheel does not carry come out as instead, and the range the
// align and force fields are offered in. All three arrive from
// api/capabilities at startup; until they do the panel refuses to validate
// or to send anything, the same way it refuses to save before align and
// force have loaded.
//
// None of it is guessed here on purpose. Each of these used to have a copy
// in this file that could drift from the firmware and did: the character
// set was a regex written twice, and the range was a literal here and two
// pairs of min/max attributes in index.html.
let printableCharacters = null;
let characterAliases = null;
let calibrationRange = null;
let minLabelCharacters = null;

window.onload = startupRoutine;

async function startupRoutine() {
  checkOverlayScrollbars();
  let body = document.getElementsByTagName("body")[0];
  body.dataset.printing = "false";
  drawHelper();
  document.getElementById("text-input").focus();
  await retrieveCapabilities();
  await retrieveSettings();
  await getStatus();
}

function checkScrollbarWidth() {
  const outer = document.createElement("div");
  outer.style.overflow = "scroll";
  document.body.appendChild(outer);

  const scrollbarWidth = outer.offsetWidth - outer.clientWidth;
  document.body.removeChild(outer);

  return scrollbarWidth;
}

/**
 * Attempts to detect if overlay scrollbars are in use, and adds a css class we can
 * change layout behavior on.
 */
function checkOverlayScrollbars() {
  const scrollbarWidth = checkScrollbarWidth();
  if (scrollbarWidth === 0) {
    document.body.classList.add("overlay-scroll-enabled");
  }
}

// Fetches what the device will accept. Retries on its own rather than
// leaving the panel unable to validate: the device answers this from its own
// constants, so there is no local fallback to fall back to.
async function retrieveCapabilities() {
  try {
    const request = await fetchWithTimeout("api/capabilities", { timeout: 5000 });
    const response = await request.json();
    if (typeof response.printable !== "string" || response.printable.length === 0) {
      throw new Error("api/capabilities served no printable set");
    }
    const range = response.calibration;
    if (!range || !Number.isInteger(range.min) || !Number.isInteger(range.max)) {
      throw new Error("api/capabilities served no calibration range");
    }
    const label = response.label;
    if (!label || !Number.isInteger(label.minimum)) {
      throw new Error("api/capabilities served no minimum label length");
    }
    printableCharacters = response.printable;
    characterAliases = response.aliases || {};
    calibrationRange = range;
    minLabelCharacters = label.minimum;

    // Not fatal: an unknown command already falls back to UNKNOWN_BUSY_LABEL
    // and the panel keeps working. Worth saying out loud, though, because
    // the usual cause is a cached script.js talking to newer firmware, and
    // that is invisible from the bench.
    const offered = response.commands;
    if (Array.isArray(offered)) {
      const missing = offered.filter((name) => !(name in COMMAND_LABELS));
      const extra = Object.keys(COMMAND_LABELS).filter(
        (name) => !offered.includes(name),
      );
      if (missing.length > 0 || extra.length > 0) {
        console.warn(
          "This panel and the firmware disagree about the command list." +
            (missing.length ? " No wording here for: " + missing.join(", ") + "." : "") +
            (extra.length ? " Device does not offer: " + extra.join(", ") + "." : ""),
        );
      }
    }
  } catch (error) {
    console.error("Unable to fetch what the device accepts, retrying");
    console.error(error);
    setTimeout(retrieveCapabilities, 2000);
    return;
  }
  // changeField() reads the bounds back off the inputs, so this is where the
  // device's range reaches the + and - buttons.
  for (const field of ["align-field", "force-field"]) {
    document.getElementById(field).min = calibrationRange.min;
    document.getElementById(field).max = calibrationRange.max;
  }
  renderHint();
  validateField();
}

// Fills the hint line under the input. Normally it lists what may be typed.
// While the label holds a character the wheel does not carry it says what
// that character will come out as instead, which is the only warning before
// the tape is spent.
function renderHint() {
  const hint = document.getElementById("hint");
  if (printableCharacters === null) {
    return;
  }

  const typed = document.getElementById("text-input").value.toUpperCase();
  const surprises = Object.keys(characterAliases)
    .filter((character) => typed.indexOf(character) >= 0)
    .map((character) => character + " prints " + characterAliases[character]);

  hint.textContent =
    surprises.length > 0 ? surprises.join("   ") : summariseCharacters(printableCharacters);
}

// Turns the served character set into something short enough to sit under
// the input. A run of three or more consecutive letters or digits collapses
// to a range; everything else is listed as itself. Nothing is left out, so
// the line cannot quietly stop matching what the device accepts.
function summariseCharacters(characters) {
  const sameKind = (a, b) =>
    (/[0-9]/.test(a) && /[0-9]/.test(b)) || (/[A-Z]/.test(a) && /[A-Z]/.test(b));

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

async function retrieveSettings() {
  // TODO: consolidate this method with getStatus(), since they both now use the same API method.

  // retrieve settings from the device
  let request = await fetchWithTimeout("api/status", { timeout: 5000 });
  let response = await request.json();

  align = response.align;
  force = response.force;
  alignTemp = align;
  forceTemp = force;

  document.getElementById("align-field").value = align;
  document.getElementById("force-field").value = force;
}

function measureText(element, text) {
  // Create a temporary canvas element
  const canvas = document.createElement("canvas");
  const context = canvas.getContext("2d");

  // Apply the styles (height and font) from the element to the context
  const style = getComputedStyle(element);
  context.font = `${style.fontSize} ${style.fontFamily}`;

  // Measure the text
  const metrics = context.measureText(text);

  // Return the width
  return metrics.width;
}

function calculateLength() {
  // calculates the label length based on the number of characters (including spaces)

  let label = document.getElementById("length-label");
  let treatedLabel = buildTreatedLabel();

  if (!isValidLabelText()) {
    label.innerHTML = "??mm";
    label.style.opacity = 0.2;
  } else {
    label.innerHTML = (treatedLabel.length < 7 ? 7 : treatedLabel.length) * 4 + "mm";
    label.style.opacity = 1;
  }
}

async function labelCommand() {
  // sends the label to the device
  if (isValidLabelText()) {
    document.getElementById("text-input").blur();
    setUiBusy(true);
    await sendCommand("tag", { tag: buildTreatedLabel().toLowerCase() });
  }
}

function buildTreatedLabel() {
  const LabelInput = document.getElementById("text-input");
  let fieldValue = LabelInput.value;
  if (fieldValue.length == 0) {
    fieldValue = "WRITE HERE";
  }
  switch (document.getElementById("mode-dropdown").value) {
    case "tight":
      multiplier = 0;
      break;
    default:
      multiplier = 1;
      break;
  }

  // Pad to one past the device's minimum. Spaces go on both sides so the
  // text stays centred; stopping exactly at the minimum would leave the
  // device topping the tape up with trailing feeds instead, which does not.
  //
  // The number comes from api/capabilities. It used to be written here as a
  // bare 7 while the device called it 6, and no fallback is written here
  // now: a guessed minimum is the same drift in a different place. Until
  // the device has said, only the mode's own padding is applied. That
  // shows for as long as the first api/capabilities call takes: the two
  // callers that draw the preview run again when it lands, and the one
  // that sends is behind isValidLabelText(), which refuses until then.
  if (minLabelCharacters !== null) {
    const target = minLabelCharacters + 1;
    const printLength = fieldValue.length + multiplier * 2;
    if (printLength < target) {
      multiplier = Math.ceil((target - printLength) / 2);
    }
  }
  return " ".repeat(multiplier) + fieldValue + " ".repeat(multiplier);
}

function getScrollbarHeight(element) {
  if (element.scrollHeight > element.clientHeight) {
    return element.offsetHeight - element.clientHeight;
  } else {
    return 0;
  }
}

function getLabelWidth(element, label) {
  return Math.max(measureText(element, label), measureText(element, " ".repeat(7))) + 4;
}

function drawHelper() {
  // draws visual helper with label length taking options into account
  const labelInput = document.getElementById("text-input");
  const labelText = labelInput.value;
  const scroll = document.getElementById("text-form-scroll"); // picks up the parent scroll element
  const border = document.getElementById("text-form-border");

  document.getElementById("clear-button").disabled = labelText === "";
  document.getElementById("submit-button").disabled = labelText === "";
  document.getElementById("reel-button").disabled = !(labelText === "");
  document.getElementById("feed-button").disabled = !(labelText === "");
  document.getElementById("cut-button").disabled = !(labelText === "");
  document.getElementById("setup-button").disabled = !(labelText === "");

  labelInput.style.width = getLabelWidth(labelInput, buildTreatedLabel()) + "px";
  const neededWidth = labelInput.clientWidth + 4;
  if (neededWidth > scroll.clientWidth) {
    // If modifying the text near the beginning or end of the scrollable area, then
    // move the scroll area to keep the border visible while editing for better context.
    if (labelInput.selectionEnd && labelInput.selectionEnd >= labelText.length - 20) {
      scroll.scrollLeft = scroll.scrollWidth - scroll.clientWidth;
    } else if (labelInput.selectionStart && labelInput.selectionStart < 20) {
      scroll.scrollLeft = 0;
    }
    // Avoid leaving the scroll position past the end of the "needed" scrollable area.
    // Dunno why browsers let you do this.
    if (scroll.scrollLeft + neededWidth > scroll.scrollWidth) {
      scroll.scrollLeft = neededWidth - scroll.clientWidth;
    }
    border.classList.add("scrolling");
  } else {
    border.classList.remove("scrolling");
    scroll.scrollLeft = 0;
  }

  onTextInputSelectionchange();
}

function onTextInputSelectionchange() {
  const labelInput = document.getElementById("text-input");
  const scroll = document.getElementById("text-form-scroll");

  // If the selection is at the beginning or end of the text, then move the scroll area to
  // the beginning or end to include the label margins.
  if (labelInput.selectionStart == labelInput.selectionEnd && labelInput.selectionEnd == 0) {
    scroll.scrollLeft = 0;
  } else if (
    labelInput.selectionStart == labelInput.selectionEnd &&
    labelInput.selectionEnd == labelInput.value.length
  ) {
    scroll.scrollLeft = scroll.scrollWidth - scroll.clientWidth;
  }
}

// Whether the label in the input is something the device would accept. Every
// character is checked against the set the device served, so this answer and
// the device's answer cannot drift apart. Case does not matter: the label is
// sent lowercase and the firmware upper-cases it again.
function isValidLabelText() {
  const value = document.getElementById("text-input").value;
  if (value.length === 0 || printableCharacters === null) {
    return false;
  }
  for (const character of value.toUpperCase()) {
    if (printableCharacters.indexOf(character) < 0) {
      return false;
    }
  }
  return true;
}

function updateScrollHelper() {
  // shows "..." helper if text is overflowed to that side

  let scroll = document.getElementById("text-form-scroll");
  let leftHelper = document.getElementById("tip-left");
  let rightHelper = document.getElementById("tip-right");

  // console.log(Math.round(scroll.scrollLeft + scroll.offsetWidth), scroll.scrollWidth);

  if (scroll.scrollWidth > scroll.offsetWidth) {
    if (
      Math.round(scroll.scrollLeft + scroll.offsetWidth) >=
      scroll.scrollWidth - 1 // "1" is margin of error
    ) {
      rightHelper.classList.remove("visible");
    } else {
      rightHelper.classList.add("visible");
    }
    if (scroll.scrollLeft == 0) {
      leftHelper.classList.remove("visible");
    } else {
      leftHelper.classList.add("visible");
    }
  } else {
    leftHelper.classList.remove("visible");
    rightHelper.classList.remove("visible");
  }
}

function jumpToScrollEnds(target) {
  // jumps to the scroll target where 0 is the start and 1 the end

  let labelInput = document.getElementById("text-input");
  labelInput.focus();
  labelInput.setSelectionRange(target * labelInput.value.length, target * labelInput.value.length );

  // let scroll = document.getElementById("text-form-scroll");
  // scroll.scrollTo({ left: target * scroll.scrollWidth, behavior: "smooth" });
}

function lerp(start, end, amt) {
  return (1 - amt) * start + amt * end;
}

function validateField() {
  // instantly validates label field by blocking buttons and giving visual feedback
  let labelInput = document.getElementById("text-input");
  drawHelper();
  renderHint();

  if (!isValidLabelText() && labelInput.value != "") {
    document.getElementById("hint").style.color = "red";
    document.getElementById("text-input").style.color = "red";
    document.getElementById("submit-button").disabled = true;
    document.getElementById("submit-button").value = " invalid entry ";
    document.getElementById("submit-button").style.color = "red";
  } else {
    document.getElementById("hint").style.color = "#e7dac960";
    document.getElementById("text-input").style.color = "#e7dac9ff";
    document.getElementById("submit-button").value = labelInput.value != "" ? " Print label! " : " ... ";
    document.getElementById("submit-button").style.color = "#e7dac9ff";
  }
}

function labelTextChanged() {
  validateField();
  calculateLength();
  drawHelper();
}

function labelTextKeyDown(e) {
  if (e.key === "Enter" && isValidLabelText()) {
    document.getElementById("submit-button").click();
  }
  onTextInputSelectionchange();
}

function marginDropdownChanged(e) {
  validateField();
  calculateLength();
  drawHelper();
}

function clearField() {
  // clears the label field and restore default button and form states
  const labelInput = document.getElementById("text-input");

  document.getElementById("clear-button").disabled = true;
  document.getElementById("submit-button").disabled = true;
  document.getElementById("reel-button").disabled = false;
  document.getElementById("feed-button").disabled = false;
  document.getElementById("cut-button").disabled = false;
  document.getElementById("hint").style.color = "#777777";
  document.getElementById("text-input").style.color = "#ffffff";
  document.getElementById("submit-button").value = " ... ";

  labelInput.value = "";

  drawHelper();
  calculateLength();

  labelInput.focus();
}

// The device rejects any align or force outside the range it served. The
// fields are disabled and only ever written by changeField() or
// retrieveSettings(), so an out-of-range value means a fetch has not landed
// yet -- sending it anyway would draw a 400 that nothing surfaces to the
// user.
function calibrationValuesReady(...values) {
  if (calibrationRange === null) {
    return false;
  }
  return values.every(
    (value) =>
      Number.isInteger(value) && value >= calibrationRange.min && value <= calibrationRange.max
  );
}

function updateTempValues() {
  // updates the temporary setting values

  // Number() here so the values POST as JSON numbers rather than strings. The
  // device rejects anything outside the range it served, and a stray string
  // would be parsed as 0 and refused.
  align = Number(document.getElementById("align-field").value);
  force = Number(document.getElementById("force-field").value);
}

function changeField(action, fieldName) {
  // incremental / decremental buttons for the align and force settings

  const field = document.getElementById(fieldName);
  // field.value and the min/max attributes are all strings. "9" + 1 is "91",
  // not 10, so the add branch was doing a lexicographic string comparison
  // while the remove branch coerced to numbers -- it only stayed in range
  // because "91" happens to sort after "9". Parse everything up front.
  //
  // The bounds come from api/capabilities, written onto the inputs by
  // retrieveCapabilities(). Before that lands both read as 0 and neither
  // button moves, which is the right answer: nothing here knows yet what
  // the device would accept.
  const min = Number(field.min);
  const max = Number(field.max);
  let currentValue = Number(field.value);

  if (action == "add" && currentValue + 1 <= max) {
    currentValue++;
    field.value = currentValue;
  } else if (action == "remove" && currentValue - 1 >= min) {
    currentValue--;
    field.value = currentValue;
  }
  updateTempValues();
}

function insertIntoField(specialChar) {
  // inserts special emoji character in the label form

  const labelInput = document.getElementById("text-input");
  labelInput.focus();

  let insertStartPoint;
  let insertEndPoint;
  let value = labelInput.value;

  if (labelInput.selectionStart == labelInput.selectionEnd) {
    insertStartPoint = labelInput.selectionStart;
    insertEndPoint = insertStartPoint;
  } else {
    insertStartPoint = labelInput.selectionStart;
    insertEndPoint = labelInput.selectionEnd;
  }

  // text before cursor/highlighted text + special character + text after cursor/highlighted text
  value = value.slice(0, insertStartPoint) + specialChar + value.slice(insertEndPoint);
  labelInput.value = value;

  labelInput.setSelectionRange(insertStartPoint + 1, insertStartPoint + 1);
  validateField();
  labelInput.focus();
}

async function toggleSettings(safe = true) {
  // shows/hide settings page

  let state = document.getElementById("settings-frame").style.visibility;

  // console.log(state);
  // console.log(align + " / " + alignTemp + " / / " + force + " / " + forceTemp);

  if (state === "hidden") {
    // Must be awaited: alignTemp/forceTemp below are the snapshot the "discard
    // unsaved changes?" check compares against, so taking it before the fetch
    // lands captures the previous values and reports a spurious edit.
    await retrieveSettings();
    alignTemp = align;
    forceTemp = force;
    document.getElementById("settings-frame").style.visibility = "visible";
    document.getElementById("main-frame").style.visibility = "hidden";
  } else {
    if (!safe || (align == alignTemp && force == forceTemp) || confirm("Discard unsaved changes?")) {
      document.getElementById("settings-frame").style.visibility = "hidden";
      document.getElementById("main-frame").style.visibility = "visible";
      alignTemp = align;
      forceTemp = force;
    }
  }
}

async function reelCommand() {
  // sends reel command to the device
  let prompt = confirm(
    "Confirm loading a new reel?\n\nPlease make sure the tape is touching the cog.\n\nImportant: unsaved align and force settings will be lost."
  );
  if (prompt) {
    toggleSettings(false);
    setUiBusy(true);
    document.getElementById("submit-button").value = COMMAND_LABELS.reel.busyLabel;
    await sendCommand("reel");
  }
}

async function feedCommand() {
  // sends feed command to the device
  setUiBusy(true);
  document.getElementById("submit-button").value = COMMAND_LABELS.feed.busyLabel;
  await sendCommand("feed");
}

async function cutCommand() {
  // sends cut command to the device
  setUiBusy(true);
  document.getElementById("submit-button").value = COMMAND_LABELS.cut.busyLabel;
  await sendCommand("cut");
}

async function testAlignCommand() {
  // sends test command to the device
  updateTempValues();
  if (!calibrationValuesReady(align)) {
    console.error("Cannot run the alignment test: align not loaded from the device yet");
    return;
  }
  // no force: this test always presses at the minimum, slowly and lightly, so
  // the alignment can be checked without embossing anything
  let data = {
    align: align,
  };
  setUiBusy(true);
  await sendCommand("testalign", data);
}

async function testFullCommand() {
  // sends test command to the device
  updateTempValues();
  if (!calibrationValuesReady(align, force)) {
    console.error("Cannot run the full test: align/force not loaded from the device yet");
    return;
  }
  let data = {
    align: align,
    force: force,
  };
  setUiBusy(true);
  await sendCommand("testfull", data);
}

async function settingsCommand() {
  // sends settings save command to the device, and triggers self restart in 15 seconds

  updateTempValues();

  // console.log("settings / align (" + align + ") force (" + force + ")");

  if (!calibrationValuesReady(align, force)) {
    console.error("Cannot save: align/force not loaded from the device yet");
    return;
  }

  if (confirm("Confirm saving align [" + align + "] and force [" + force + "] settings?")) {
    setUiBusy(true);
    document.getElementById("submit-button").value = COMMAND_LABELS.save.busyLabel;
    if (!(await sendCommand("save", { align: align, force: force }))) {
      return;
    }

    document.getElementById("settings-frame").style.visibility = "hidden";
    document.getElementById("refresh-frame").style.visibility = "visible";

    let count = 15;
    document.getElementById("countdown").textContent = count;

    setInterval(function () {
      count = count - 1;
      document.getElementById("countdown").textContent = count;

      // console.log(count);

      if (count == 0) {
        window.location.reload();
      }
    }, 1000);
  }
}

// Helper method to amke fetch requests with a configurable timeout.
// See: https://dmitripavlutin.com/timeout-fetch-request/
async function fetchWithTimeout(resource, options = {}) {
  const { timeout = 8000 } = options;

  const controller = new AbortController();
  const id = setTimeout(() => controller.abort(), timeout);
  const response = await fetch(resource, {
    ...options,
    signal: controller.signal,
  });
  clearTimeout(id);
  return response;
}

// Sends one command to the device and reports a refusal to the console. The
// name is a key in COMMAND_LABELS, which is also the path it posts to. Returns
// whether the device accepted it.
async function sendCommand(name, data = {}) {
  const response = await postJson("api/" + name, data);
  if (!response.ok) {
    console.error("Unable to " + name);
    console.error((await response.json())["error"]);
  }
  return response.ok;
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

async function getStatus() {
  try {
    let request = await fetchWithTimeout("api/status", { timeout: 5000 });
    handleData(await request.json());
  } catch (error) {
    // TODO: Add some UI treatment for when there are communication errors, eg
    // a "Reconnecting..." toast message or something.
    console.error("Problem ");
    console.error(error);
  } finally {
    setTimeout(getStatus, 1000);
  }
}

wasBusy = false;

// Enables or disables UI elements to prevent intercations while the printer is printing,
// reeling, cutting, etc.
function setUiBusy(busy) {
  if (busy && !wasBusy) {
    // Disable UI elements
    wasBusy = true;
    Array.from(document.querySelectorAll("input")).forEach((element) => {
      element.disabled = true;
    });
    document.getElementById("mode-dropdown").disabled = true;
  } else if (!busy && wasBusy) {
    // Enable UI elements
    wasBusy = false;
    let body = document.getElementsByTagName("body")[0];
    body.dataset.printing = "false";
    const labelInput = document.getElementById("text-input");
    Array.from(document.querySelectorAll("input")).forEach((element) => {
      element.disabled = false;
    });
    document.getElementById("mode-dropdown").disabled = false;
    document.getElementById("submit-button").disabled = labelInput.value == "";
    document.getElementById("clear-button").disabled = labelInput.value == "";
    validateField();
  }
}

function handleData(data_json) {
  setUiBusy(data_json.busy);

  if (!data_json.busy) {
    return;
  }
  // The device already holds the last point back while it finishes feeding
  // and cutting (see Progress.h). Subtracting another one here is what made
  // the browser read a point below the OLED beside it.
  let percentage = parseInt(data_json.progress);

  const submitButton = document.getElementById("submit-button");
  const spec = COMMAND_LABELS[data_json.command];
  submitButton.value = spec ? spec.busyLabel : UNKNOWN_BUSY_LABEL;

  // tag is the only command with more to show than its own name: it scrolls
  // the label past a progress bar as the characters go down, and it counts
  // the percentage into the button. Everything else has said its piece.
  if (data_json.command !== "tag") {
    return;
  }

  let scroll = document.getElementById("text-form-scroll"); // picks up the parent scroll element

  submitButton.value = " printing " + percentage + "% ";
  let body = document.getElementsByTagName("body")[0];
  body.dataset.printing = "true";
  const label = data_json.current_label || "unknown";
  const printingLabel = document.getElementById("printing-label");
  printingLabel.style.width = getLabelWidth(printingLabel, label) + "px";
  printingLabel.innerHTML = label;
  const printed = label.substring(0, Math.round(label.length * (percentage / 100)));
  const progressLength = measureText(printingLabel, printed) + 3;
  document.getElementById("progress-bar").style.width = progressLength + "px";

  if (progressLength < scroll.clientWidth / 2) {
    scroll.scrollLeft = 0;
  } else if (progressLength > scroll.scrollWidth - scroll.clientWidth / 2) {
    scroll.scrollLeft = scroll.scrollWidth;
  } else {
    scroll.scrollLeft = progressLength - scroll.clientWidth / 2;
  }
}
