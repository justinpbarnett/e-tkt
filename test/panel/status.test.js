import assert from "node:assert/strict";
import { test } from "node:test";

import {
  activity,
  busyText,
  commandListDisagreement,
  printPercentage,
  printingRun,
  readCapabilities,
  stopOffer,
} from "../../data/status.js";
import { capabilitiesReply, labelMaker, running } from "./device.js";

test("the label maker's capabilities read as the numbers the panel works from", () => {
  // Everything the panel validates and counts with arrives in this one reply,
  // and none of it has a copy in the panel to fall back on.
  const device = labelMaker();
  assert.equal(device.label.minimum, 6);
  assert.equal(device.label.maximum, 249);
  assert.equal(device.copies.maximum, 500);
  assert.equal(device.feed.length_um, 4000);
  assert.deepEqual(device.aliases, { 0: "O", 1: "I" });
  assert.equal(device.commands.get("tag").prints_run, true);
  assert.equal(device.commands.get("save").stoppable, false);
});

test("capabilities from older firmware are refused, naming what is missing", () => {
  // The panel would otherwise count tape with an undefined feed and print
  // NaN. The name is what tells someone at the bench which half to upload.
  const reply = capabilitiesReply();
  delete reply.feed;
  assert.throws(() => readCapabilities(reply), {
    name: "CapabilitiesMismatch",
    message: "api/capabilities served no feed length",
  });

  const stale = capabilitiesReply();
  delete stale.commands.tag.stoppable;
  assert.throws(() => readCapabilities(stale), {
    name: "CapabilitiesMismatch",
    message: "api/capabilities served no command facts",
  });

  for (const fact of ["uses_align", "uses_force", "presses_label"]) {
    const unsaid = capabilitiesReply();
    delete unsaid.commands.save[fact];
    assert.throws(() => readCapabilities(unsaid), {
      name: "CapabilitiesMismatch",
      message: "api/capabilities served no command facts",
    });
  }
});

test("the panel has words for every command the firmware offers", () => {
  // The panel warns in the console when the two disagree, where nobody at
  // the bench looks. This is the same check, where a disagreement fails.
  assert.equal(commandListDisagreement(labelMaker()), null);
});

test("a disagreement about the command list names what each side has that the other lacks", () => {
  // Which half is behind is the first thing someone at the bench needs to
  // know, and the names say which.
  const reply = capabilitiesReply();
  reply.commands.dance = { ...reply.commands.cut };
  delete reply.commands.move;
  assert.equal(
    commandListDisagreement(readCapabilities(reply)),
    "This panel and the firmware disagree about the command list. No wording here for: dance. Device does not offer: move.",
  );

  const newer = capabilitiesReply();
  newer.commands.dance = { ...newer.commands.cut };
  newer.commands.spin = { ...newer.commands.cut };
  assert.equal(
    commandListDisagreement(readCapabilities(newer)),
    "This panel and the firmware disagree about the command list. No wording here for: dance, spin.",
  );

  const older = capabilitiesReply();
  delete older.commands.home;
  assert.equal(
    commandListDisagreement(readCapabilities(older)),
    "This panel and the firmware disagree about the command list. Device does not offer: home.",
  );
});

test("a command the panel has no words for is still said to be working", () => {
  // Newer firmware can run a command this copy of the panel has never heard
  // of, and the page still has to say something while it runs.
  assert.equal(busyText("dance"), "Working…");
  assert.equal(busyText("reel"), "Loading the new roll…");
});

test("a status is a run being printed only while the device prints one", () => {
  // The tape drawing follows the run, and a cut or a test has no label of
  // the panel's to draw.
  const device = labelMaker();
  const printing = { busy: true, command: "tag", copy: 2, copies: 5, progress: 40 };
  assert.equal(printingRun(printing, device), printing);
  assert.equal(printingRun({ busy: true, command: "testfull", progress: 40 }, device), null);
  assert.equal(printingRun({ ...printing, busy: false }, device), null);
  assert.equal(printingRun(null, device), null);
  // Which commands print one is the device's to say, and until it has, none
  // does.
  assert.equal(printingRun(printing, null), null);
});

test("a command that prints no labels has no number to show while it runs", () => {
  // The device counts progress only through the labels it presses, so
  // anything else has no number the page could show.
  assert.deepEqual(activity(running(), labelMaker()), { text: "Cutting…", percentage: null });
});

test("a single label shows how far through it the device is", () => {
  const printing = { busy: true, command: "tag", copy: 1, copies: 1, progress: 42 };
  assert.deepEqual(activity(running({ command: "tag", status: printing }), labelMaker()), {
    text: "Printing…",
    percentage: 42,
  });
});

test("progress out of range, or not a number, stays inside the bar", () => {
  // The bar and the drawing of the tape are both sized from this number.
  assert.equal(printPercentage({ progress: 150 }), 100);
  assert.equal(printPercentage({ progress: -5 }), 0);
  assert.equal(printPercentage({}), 0);
  assert.equal(printPercentage({ progress: "37" }), 37);
});

test("a command on its way is not shown with the progress of a run the device reports", () => {
  // A status can be a second old, and from another phone's run. The number
  // belongs to the command the words are about.
  const printing = { busy: true, command: "tag", copy: 1, copies: 1, progress: 42 };
  assert.deepEqual(activity(running({ command: "feed", status: printing }), labelMaker()), {
    text: "Feeding…",
    percentage: null,
  });
});

test("a run of labels is counted label by label, and its bar fills once over the whole run", () => {
  // The tape above the bar already shows how far into this label it is,
  // and a bar that emptied at every cut would say nothing about when the
  // run ends. The count is held together by no-break spaces, so a narrow
  // screen does not split it over two lines.
  const printing = { busy: true, command: "tag", copy: 3, copies: 5, progress: 43 };
  assert.deepEqual(activity(running({ command: "tag", status: printing }), labelMaker()), {
    text: "Printing label 3 of 5",
    percentage: 48,
  });
});

test("a run the device has taken but not yet started shows an empty bar", () => {
  // The device counts the labels from 1 once it starts the first, and
  // reports 0 in the moment before.
  const taken = { busy: true, command: "tag", copy: 0, copies: 5, progress: 0 };
  assert.equal(activity(running({ command: "tag", status: taken }), labelMaker()).percentage, 0);
});

test("a count past the end of the run is taken for its last label", () => {
  // Or the bar would fill past its end.
  const overrun = { busy: true, command: "tag", copy: 6, copies: 5, progress: 43 };
  assert.equal(activity(running({ command: "tag", status: overrun }), labelMaker()).percentage, 88);
});

test("a run whose count the page cannot read is shown as a single label", () => {
  const halfCounted = { busy: true, command: "tag", copies: 5, progress: 42 };
  assert.deepEqual(activity(running({ command: "tag", status: halfCounted }), labelMaker()), {
    text: "Printing…",
    percentage: 42,
  });
  const unreadable = { busy: true, command: "tag", copy: 2, copies: "5", progress: 42 };
  assert.deepEqual(activity(running({ command: "tag", status: unreadable }), labelMaker()), {
    text: "Printing…",
    percentage: 42,
  });
});

test("the activity bar says a stop is coming, once one has been asked for", () => {
  const printing = { busy: true, command: "tag", copy: 3, copies: 5, progress: 43 };
  const afterLabel = running({ command: "tag", status: printing, stop: "after_label" });
  assert.deepEqual(activity(afterLabel, labelMaker()), {
    text: "Stopping after label 3 of 5",
    percentage: 48,
  });
});

test("a stop now says only that it is stopping, whatever was running", () => {
  // A stop now ends the command at its next press or turn of a motor, so
  // there is none of the run left to count.
  const printing = { busy: true, command: "tag", copy: 3, copies: 5, progress: 43 };
  assert.deepEqual(activity(running({ command: "tag", status: printing, stop: "now" }), labelMaker()), {
    text: "Stopping…",
    percentage: 48,
  });
  const loading = { busy: true, command: "reel" };
  assert.deepEqual(activity(running({ command: "reel", status: loading, stop: "now" }), labelMaker()), {
    text: "Stopping…",
    percentage: null,
  });
});

test("a save is offered no stop", () => {
  // The device cannot stop one: a save moves nothing and ends in a restart,
  // and one cut off partway would leave half a calibration behind.
  const saving = running({ command: "save", status: { busy: true, command: "save" } });
  assert.equal(stopOffer(saving, labelMaker()), null);
});

test("anything the device can stop is offered the red stop, however quick it is", () => {
  // What can be stopped is the device's to say, and it can stop everything
  // that moves. A cut turns the wheel to the cut mark and presses the blade
  // in, which takes long enough to want it stopped.
  const device = labelMaker();
  assert.deepEqual(stopOffer(running(), device), { now: { text: "Stop cutting", stopping: false }, afterLabel: null });
  const feeding = running({ command: "feed", status: { busy: true, command: "feed" } });
  assert.deepEqual(stopOffer(feeding, device), { now: { text: "Stop feeding", stopping: false }, afterLabel: null });
});

test("every command the device can stop has words of its own for the red stop", () => {
  // The red stop says what it stops. A plain "Stop" is for a command this
  // copy of the panel has never heard of.
  const device = labelMaker();
  for (const [command, facts] of device.commands) {
    if (facts.stoppable) {
      const offer = stopOffer(running({ command: command, status: { busy: true, command: command } }), device);
      assert.notEqual(offer.now.text, "Stop", command);
    }
  }
});

test("a command the panel has no words for is still offered a stop", () => {
  // Newer firmware can run a command this copy of the panel has never heard
  // of, and if the device says it can stop it, the page offers the stop.
  const reply = capabilitiesReply();
  reply.commands.dance = { ...reply.commands.cut };
  const dancing = running({ command: "dance", status: { busy: true, command: "dance" } });
  assert.deepEqual(stopOffer(dancing, readCapabilities(reply)), {
    now: { text: "Stop", stopping: false },
    afterLabel: null,
  });
});

test("a command that can be stopped is offered the red stop, in words of its own", () => {
  const loading = running({ command: "reel", status: { busy: true, command: "reel" } });
  assert.deepEqual(stopOffer(loading, labelMaker()), {
    now: { text: "Stop loading", stopping: false },
    afterLabel: null,
  });
});

test("a command the device says it cannot stop is offered no stop", () => {
  // Which commands the device can stop is its own to say, and the page
  // offers no stop the device would refuse.
  const reply = capabilitiesReply();
  reply.commands.reel.stoppable = false;
  const loading = running({ command: "reel", status: { busy: true, command: "reel" } });
  assert.equal(stopOffer(loading, readCapabilities(reply)), null);
});

test("before the device has said what it can stop, the stop is offered anyway", () => {
  // A stop must never wait on a fetch.
  const printing = running({ command: "tag", status: { busy: true, command: "tag", copy: 1, copies: 1 } });
  assert.deepEqual(stopOffer(printing, null), {
    now: { text: "Stop printing", stopping: false },
    afterLabel: null,
  });
});

test("a stop now says it is stopping, and cannot be asked for twice", () => {
  const loading = running({ command: "reel", status: { busy: true, command: "reel" }, stop: "now" });
  assert.deepEqual(stopOffer(loading, labelMaker()), {
    now: { text: "Stopping…", stopping: true },
    afterLabel: null,
  });
});

test("a run of labels can instead be let finish the label it is on", () => {
  const printing = { busy: true, command: "tag", copy: 2, copies: 5, progress: 10 };
  assert.deepEqual(stopOffer(running({ command: "tag", status: printing }), labelMaker()), {
    now: { text: "Stop printing", stopping: false },
    afterLabel: { text: "Stop after this label", stopping: false, disabled: false },
  });
});

test("the last label of a run has no label after it to stop at", () => {
  // Stopping after the last label is where the run ends anyway.
  const last = { busy: true, command: "tag", copy: 5, copies: 5, progress: 10 };
  assert.deepEqual(stopOffer(running({ command: "tag", status: last }), labelMaker()).afterLabel, {
    text: "Stop after this label",
    stopping: false,
    disabled: true,
  });
});

test("a stop after the label says it is coming, and leaves the red stop free to overtake it", () => {
  const printing = { busy: true, command: "tag", copy: 2, copies: 5, progress: 10 };
  const gentle = running({ command: "tag", status: printing, stop: "after_label" });
  assert.deepEqual(stopOffer(gentle, labelMaker()), {
    now: { text: "Stop printing", stopping: false },
    afterLabel: { text: "Stopping after this label…", stopping: true, disabled: true },
  });
});

test("a stop now takes the stop after the label out of reach", () => {
  const printing = { busy: true, command: "tag", copy: 2, copies: 5, progress: 10 };
  const offer = stopOffer(running({ command: "tag", status: printing, stop: "now" }), labelMaker());
  assert.deepEqual(offer.afterLabel, { text: "Stop after this label", stopping: false, disabled: true });
});

test("the stop after the label is held back while the page is out of touch, and the red stop is not", () => {
  // The red stop may still get through, and if it does not, the page says
  // what else to do.
  const printing = { busy: true, command: "tag", copy: 2, copies: 5, progress: 10 };
  assert.deepEqual(stopOffer(running({ command: "tag", status: printing, offline: true }), labelMaker()), {
    now: { text: "Stop printing", stopping: false },
    afterLabel: { text: "Stop after this label", stopping: false, disabled: true },
  });
});

test("before a status reports on a run, its stops come up the size the page asked for", () => {
  // The device takes a moment to report the run, and until it does, the
  // page's own count is the only one there is.
  const device = labelMaker();
  const sending = running({ command: "tag", status: { busy: false, command: "idle" }, sentCopies: 5 });
  assert.deepEqual(stopOffer(sending, device).afterLabel, {
    text: "Stop after this label",
    stopping: false,
    disabled: false,
  });
  assert.equal(stopOffer({ ...sending, sentCopies: 1 }, device).afterLabel, null);
  assert.equal(stopOffer({ ...sending, status: null }, device).afterLabel.disabled, false);
});

test("only a run of labels is offered a stop after the label", () => {
  // The page's own count is of the last labels it asked for, whatever has
  // been started since.
  const loading = running({ command: "reel", status: { busy: true, command: "reel" }, sentCopies: 5 });
  assert.equal(stopOffer(loading, labelMaker()).afterLabel, null);
});

test("a run whose count the page cannot read is offered only the red stop", () => {
  const unreadable = { busy: true, command: "tag", copy: 2, copies: "5", progress: 10 };
  assert.equal(stopOffer(running({ command: "tag", status: unreadable }), labelMaker()).afterLabel, null);
});
