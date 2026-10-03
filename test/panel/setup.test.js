import assert from "node:assert/strict";
import { test } from "node:test";

import { Sending } from "../../data/link.js";
import { RollLoad } from "../../data/setup.js";

// The id of a command some other phone sent.
const ANOTHER_ID = "0f1e2d3c4b5a6978";

// What api/status says while the device loads the roll it took under id.
function loading(id) {
  return { busy: true, command: "reel", last_command_id: id };
}

// What api/status says once the device is idle again, of the command it took
// last, and anything more says of how that went.
function idleAfter(id, more = {}) {
  return { busy: false, command: "idle", last_command_id: id, ...more };
}

// What the page knows as a status comes in, as script.js hands it to
// RollLoad.statusArrived(): Setup open and nothing to keep the page in it,
// but for what changes says.
function page(status, changes = {}) {
  return {
    status: status,
    running: status.busy ? status.command : null,
    setup: true,
    problem: false,
    unsaved: false,
    ...changes,
  };
}

// A load this page sent and the device took, and the Sending it went as.
function loadTaken() {
  const sent = new Sending("reel");
  const load = new RollLoad();
  load.started(sent);
  return { load, sent };
}

test("the page goes back to the print view once a roll it loaded is in", () => {
  // Nothing is left to do in Setup then, and the way out of it is at the
  // bottom of the page. Not before: the stop is in Setup while the tape goes
  // through.
  const { load, sent } = loadTaken();
  assert.equal(load.statusArrived(page(loading(sent.id))), false);
  assert.equal(load.statusArrived(page(idleAfter(sent.id))), true);
});

test("it goes back once, and not again for a status that still names the load", () => {
  // Every status names the load until the device takes another command, and
  // Setup opened again in the meantime is not to close by itself.
  const { load, sent } = loadTaken();
  assert.equal(load.statusArrived(page(idleAfter(sent.id))), true);
  assert.equal(load.statusArrived(page(idleAfter(sent.id))), false);
});

test("a status asked for before the device took the load decides nothing", () => {
  // It says the device is idle and names no load, and the page goes on
  // taking the load to be running until a status asked for since says how
  // it is.
  const { load, sent } = loadTaken();
  assert.equal(load.statusArrived(page({ busy: false, command: "idle" }, { running: "reel" })), false);
  assert.equal(load.statusArrived(page(idleAfter(sent.id))), true);
});

test("a load that was stopped leaves the page in Setup", () => {
  // Whoever stopped it. The tape is part of the way in, and what comes next
  // is in Setup: unload it, or load it again.
  const { load, sent } = loadTaken();
  const stopped = { id: 3, command: "reel", cause: "operator", unfinished: false };
  assert.equal(load.statusArrived(page(idleAfter(sent.id, { stopped: stopped }))), false);
});

test("a load this page asked to stop leaves the page in Setup, even when the stop came too late", () => {
  // Too late, the load ran to its end and the device has no record of a
  // stop. But whoever tapped stop saw something go wrong, a jam the label
  // maker cannot see, and what they want next is Unload.
  const { load, sent } = loadTaken();
  load.stopAsked();
  assert.equal(load.statusArrived(page(idleAfter(sent.id))), false);
});

test("a stop tapped while the load was still on its way leaves the page in Setup", () => {
  // The stop went before the device had answered the load, so the load is
  // only then known to have been taken.
  const sent = new Sending("reel");
  sent.stopSent();
  const load = new RollLoad();
  load.started(sent);
  assert.equal(load.statusArrived(page(idleAfter(sent.id))), false);
});

test("a label maker that restarted during the load leaves the page in Setup", () => {
  // It names no command after a restart, and the roll is not in.
  const { load } = loadTaken();
  assert.equal(load.statusArrived(page({ busy: false, command: "idle" })), false);
});

test("another command taken since the load leaves the page in Setup", () => {
  // Sent from another phone. The status names that command now, and no
  // longer says how the load went.
  const { load, sent } = loadTaken();
  assert.equal(load.statusArrived(page({ busy: true, command: "feed", last_command_id: ANOTHER_ID })), false);
  assert.equal(load.statusArrived(page(idleAfter(ANOTHER_ID))), false);
  assert.equal(load.statusArrived(page(idleAfter(sent.id))), false);
});

test("calibration that has not been saved keeps the page in Setup", () => {
  // Leaving Setup discards it. Back asks first, and the page going back by
  // itself would not.
  const { load, sent } = loadTaken();
  assert.equal(load.statusArrived(page(idleAfter(sent.id), { unsaved: true })), false);
});

test("a problem on the page keeps the page in Setup", () => {
  // Leaving Setup takes it down, and this one may not have been read: a
  // change to the network sent before the load, say, that could not be made.
  const { load, sent } = loadTaken();
  assert.equal(load.statusArrived(page(idleAfter(sent.id), { problem: true })), false);
});

test("Setup left while the roll went in is not left again", () => {
  // The print view shows the load from there on, and the person is where
  // the page would have taken them. Setup opened again after that stays
  // open.
  const { load, sent } = loadTaken();
  assert.equal(load.statusArrived(page(loading(sent.id), { setup: false })), false);
  assert.equal(load.statusArrived(page(idleAfter(sent.id), { setup: false })), false);
  assert.equal(load.statusArrived(page(idleAfter(sent.id))), false);
});

test("a roll loaded from somewhere else leaves the page where it is", () => {
  // Another phone's load. Whoever has Setup open here did not ask for it,
  // and may be in the middle of something.
  const load = new RollLoad();
  assert.equal(load.statusArrived(page(loading(ANOTHER_ID))), false);
  assert.equal(load.statusArrived(page(idleAfter(ANOTHER_ID))), false);
});

test("each load is waited on for itself", () => {
  // One that was stopped, loaded again: the page goes back for the second.
  const { load, sent } = loadTaken();
  load.stopAsked();
  assert.equal(load.statusArrived(page(idleAfter(sent.id))), false);

  const again = new Sending("reel");
  load.started(again);
  assert.equal(load.statusArrived(page(loading(again.id))), false);
  assert.equal(load.statusArrived(page(idleAfter(again.id))), true);
});
