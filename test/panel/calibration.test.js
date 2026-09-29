import assert from "node:assert/strict";
import { test } from "node:test";

import { CalibrationDraft } from "../../data/calibration.js";
import { readCapabilities } from "../../data/status.js";
import { capabilitiesReply, labelMaker } from "./device.js";

// What api/status says of the calibration saved on the device.
function saved(align, force) {
  return { busy: false, command: "idle", align: align, force: force };
}

test("Setup opens on the calibration the device has now", () => {
  // Not what it had when the page loaded: another phone may have saved
  // since.
  const draft = new CalibrationDraft();
  draft.statusArrived(saved(4, 6), false);
  draft.statusArrived(saved(5, 3), false);
  draft.open();
  assert.deepEqual(draft.shown, { align: 5, force: 3 });
});

test("Setup opened before the device has said fills in once it does", () => {
  const draft = new CalibrationDraft();
  draft.open();
  assert.equal(draft.shown, null);
  draft.statusArrived(saved(5, 3), true);
  assert.deepEqual(draft.shown, { align: 5, force: 3 });
});

test("a value not changed in Setup follows the device, and a changed one stays as it was changed", () => {
  // Another phone can save while this one has Setup open, or the label maker
  // can come back up with numbers of its own.
  const device = labelMaker();
  const draft = new CalibrationDraft();
  draft.statusArrived(saved(5, 3), false);
  draft.open();
  draft.step("align", 1, device);
  draft.statusArrived(saved(2, 7), true);
  assert.deepEqual(draft.shown, { align: 6, force: 7 });
});

test("a status without a calibration in it leaves the one Setup shows as it was", () => {
  // Setup would otherwise show, and send, values the device never had.
  const draft = new CalibrationDraft();
  draft.statusArrived(saved(5, 3), false);
  draft.open();
  draft.statusArrived({ busy: false, command: "idle" }, true);
  draft.statusArrived({ busy: false, command: "idle", align: 6 }, true);
  draft.statusArrived({ busy: false, command: "idle", force: 7 }, true);
  assert.deepEqual(draft.shown, { align: 5, force: 3 });
});

test("closing Setup forgets the calibration it showed", () => {
  // The next time it opens, it opens on the device's again.
  const draft = new CalibrationDraft();
  draft.statusArrived(saved(5, 3), false);
  draft.open();
  draft.step("align", 1, labelMaker());
  draft.close();
  assert.equal(draft.shown, null);
  draft.statusArrived(saved(5, 3), false);
  assert.equal(draft.shown, null);
  draft.open();
  assert.deepEqual(draft.shown, { align: 5, force: 3 });
});

test("a value steps no further than the range the device takes", () => {
  // The device refuses a calibration outside its range, and says so with a
  // 400 for something the person did not do.
  const device = labelMaker();
  const draft = new CalibrationDraft();
  draft.statusArrived(saved(9, 1), false);
  draft.open();
  assert.equal(draft.step("align", 1, device), false);
  assert.equal(draft.step("force", -1, device), false);
  assert.deepEqual(draft.shown, { align: 9, force: 1 });
  assert.equal(draft.step("align", -1, device), true);
  assert.deepEqual(draft.shown, { align: 8, force: 1 });
});

test("a stepper is held back where its step would go outside the range, or before there is one", () => {
  const device = labelMaker();
  const draft = new CalibrationDraft();
  draft.statusArrived(saved(9, 5), false);
  assert.equal(draft.canStep("force", 1, device), false);
  draft.open();
  assert.equal(draft.canStep("align", 1, device), false);
  assert.equal(draft.canStep("align", -1, device), true);
  assert.equal(draft.canStep("force", 1, null), false);
  assert.equal(draft.step("force", 1, null), false);
  assert.equal(draft.canStep("force", 1, device), true);
});

test("a command goes out with the calibration fields the device says it takes, and only ones inside the range", () => {
  // The device refuses a value outside the range it served with a 400, for
  // something the person did not do. Which fields a command takes is the
  // device's to say: the align test presses at the minimum force whatever
  // it is sent.
  const device = labelMaker();
  const draft = new CalibrationDraft();
  assert.equal(draft.fieldsFor("testfull", device), null);
  draft.statusArrived(saved(5, 3), false);
  draft.open();
  assert.equal(draft.fieldsFor("testfull", null), null);
  assert.deepEqual(draft.fieldsFor("testfull", device), { align: 5, force: 3 });
  assert.deepEqual(draft.fieldsFor("save", device), { align: 5, force: 3 });
  assert.deepEqual(draft.fieldsFor("testalign", device), { align: 5 });

  const reply = capabilitiesReply();
  reply.commands.testalign.uses_force = true;
  reply.commands.save.uses_align = false;
  const changed = readCapabilities(reply);
  assert.deepEqual(draft.fieldsFor("testalign", changed), { align: 5, force: 3 });
  assert.deepEqual(draft.fieldsFor("save", changed), { force: 3 });

  const strongest = new CalibrationDraft();
  strongest.statusArrived(saved(5, 12), false);
  strongest.open();
  assert.equal(strongest.fieldsFor("save", device), null);
  assert.deepEqual(strongest.fieldsFor("testalign", device), { align: 5 });
});

test("Setup has unsaved changes only while it shows a calibration other than the one the device has saved", () => {
  // Saving restarts the label maker, which is not worth doing for the
  // numbers it already has, and leaving Setup asks first only when there is
  // something to lose.
  const device = labelMaker();
  const draft = new CalibrationDraft();
  assert.equal(draft.unsaved(), false);
  draft.statusArrived(saved(5, 3), true);
  assert.equal(draft.unsaved(), false);
  draft.step("force", 1, device);
  assert.equal(draft.unsaved(), true);
  draft.step("force", -1, device);
  assert.equal(draft.unsaved(), false);
  draft.step("align", -1, device);
  assert.equal(draft.unsaved(), true);
  // Saved from another phone.
  draft.statusArrived(saved(4, 3), true);
  assert.equal(draft.unsaved(), false);
});

test("leaving Setup with unsaved changes names the values that would be discarded", () => {
  // The dialog asks whether to discard them.
  const device = labelMaker();
  const draft = new CalibrationDraft();
  draft.statusArrived(saved(5, 3), true);
  assert.equal(draft.unsavedSummary(), null);
  draft.step("align", 1, device);
  assert.equal(draft.unsavedSummary(), "Your change to align has not been saved.");
  draft.step("force", 1, device);
  assert.equal(draft.unsavedSummary(), "Your changes to align and force have not been saved.");
  draft.step("align", -1, device);
  assert.equal(draft.unsavedSummary(), "Your change to force has not been saved.");
});

test("saving says which calibration is saved, and that the label maker restarts to use it", () => {
  // The device restarts once it has saved, and the dialog asks first.
  const device = labelMaker();
  const draft = new CalibrationDraft();
  assert.equal(draft.saveSummary(), null);
  draft.statusArrived(saved(5, 3), true);
  draft.step("align", 1, device);
  assert.equal(
    draft.saveSummary(),
    "Align 6 and force 3 are saved, then the label maker restarts to use them. It takes about 15 seconds.",
  );
});
