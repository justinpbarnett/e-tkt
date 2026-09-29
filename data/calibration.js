// The calibration Setup shows: an align and a force, taken from the device's
// when Setup opens and stepped from there, against the pair the device has
// saved. The page draws what this says and sends what it holds; node tests
// it in test/panel/calibration.test.js.
//
// The values are held as numbers here and never read back off the page: an
// input's value and its min and max are all strings, "9" + 1 is "91", and
// the steppers that used to read them compared strings and only stayed in
// range because "91" happens to sort after "9".

// How long the device takes to come back after a save, which restarts it.
export const RESTART_SECONDS = 15;

export class CalibrationDraft {
  // The pair the device has saved, from the last status that said.
  #saved = null;
  // The pair Setup shows, or null while Setup is closed or the device has not
  // said what it has.
  #draft = null;

  // A status arrived. setupOpen is whether Setup is open.
  statusArrived(status, setupOpen) {
    if (!(Number.isInteger(status.align) && Number.isInteger(status.force))) {
      return;
    }
    const before = this.#saved;
    this.#saved = { align: status.align, force: status.force };
    if (setupOpen && this.#draft === null) {
      this.#draft = { ...this.#saved };
    } else if (this.#draft !== null && before !== null) {
      // A value not changed here follows the device, when another phone
      // saves one or the label maker comes back up with its own.
      for (const name of ["align", "force"]) {
        if (this.#draft[name] === before[name]) {
          this.#draft[name] = this.#saved[name];
        }
      }
    }
  }

  // Setup opened.
  open() {
    this.#draft = this.#saved === null ? null : { ...this.#saved };
  }

  // Setup closed.
  close() {
    this.#draft = null;
  }

  // The pair Setup shows, or null.
  get shown() {
    return this.#draft === null ? null : { ...this.#draft };
  }

  // Whether a step of align or force, by -1 or 1, lands on a value the
  // device takes.
  canStep(name, step, device) {
    return this.#draft !== null && calibrationValuesReady(device, this.#draft[name] + step);
  }

  // Steps align or force, by -1 or 1, and says whether it moved.
  step(name, step, device) {
    if (!this.canStep(name, step, device)) {
      return false;
    }
    this.#draft[name] += step;
    return true;
  }

  // The fields of the calibration Setup shows that the command carries in
  // its body, as the device says in api/capabilities, or null while they are
  // not ones the device would take.
  fieldsFor(command, device) {
    const facts = device === null ? undefined : device.commands.get(command);
    if (this.#draft === null || facts === undefined) {
      return null;
    }
    const fields = {};
    if (facts.uses_align) {
      fields.align = this.#draft.align;
    }
    if (facts.uses_force) {
      fields.force = this.#draft.force;
    }
    return calibrationValuesReady(device, ...Object.values(fields)) ? fields : null;
  }

  // What the dialog before a save says, or null while Setup has nothing to
  // save.
  saveSummary() {
    if (this.#draft === null) {
      return null;
    }
    return (
      "Align " +
      this.#draft.align +
      " and force " +
      this.#draft.force +
      " are saved, then the label maker restarts to use them. It takes about " +
      RESTART_SECONDS +
      " seconds."
    );
  }

  // Whether Setup shows a calibration other than the one the device has
  // saved.
  unsaved() {
    return this.#changed().length > 0;
  }

  // What leaving Setup would discard, or null when it would discard nothing.
  unsavedSummary() {
    const changed = this.#changed();
    if (changed.length === 0) {
      return null;
    }
    const [change, have] = changed.length > 1 ? ["changes", "have"] : ["change", "has"];
    return `Your ${change} to ${changed.join(" and ")} ${have} not been saved.`;
  }

  // The names of the values Setup shows changed from the ones the device has
  // saved.
  #changed() {
    if (this.#draft === null) {
      return [];
    }
    return ["align", "force"].filter((name) => this.#draft[name] !== this.#saved[name]);
  }
}

// The device refuses an align or a force outside the range it served, with
// a 400 for something the person did not do. The values come only from
// /api/status, which reports them clamped to that range, and from the
// steppers, which keep to it, so what this holds back in practice is a send
// before api/capabilities has said what the range is.
function calibrationValuesReady(device, ...values) {
  if (device === null) {
    return false;
  }
  return values.every(
    (value) => Number.isInteger(value) && value >= device.calibration.min && value <= device.calibration.max,
  );
}
