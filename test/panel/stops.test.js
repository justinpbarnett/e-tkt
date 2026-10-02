import assert from "node:assert/strict";
import { test } from "node:test";

import { NEVER_STARTED, Sending, TAKEN, UNKNOWN } from "../../data/link.js";
import { readCapabilities } from "../../data/status.js";
import { Stops, stopPath } from "../../data/stops.js";
import { capabilitiesReply, labelMaker } from "./device.js";

// What the label maker says of its commands, which is what a stop's notice
// is told by.
const device = labelMaker();

// What api/status says with nothing running and no stop on record.
const IDLE = { busy: false, command: "idle" };
const ACCEPTED = { ok: true, status: 200 };
// When the stops here are posted, on the clock of performance.now(): before
// every answer and every status the tests go on to give them.
const SENT = 400;
// The id a stop here is posted under.
const STOP_ID = "5d7e21b6843f9a0c";

const NOT_SAID = Symbol("not said");

// What has been said of a command on its way, as its Sending has it, or
// NOT_SAID while nothing has.
function saidOf(command) {
  return Promise.race([command.outcome, new Promise((resolve) => setImmediate(() => resolve(NOT_SAID)))]);
}

// A page that has asked for a stop now, which the device answered after the
// command it was meant for had finished.
function tooLate() {
  const stops = new Stops();
  stops.answered(stops.ask("now", null, SENT), ACCEPTED, { result: "idle" }, 1000);
  return stops;
}

// What api/status says once a stop has cut something short and nothing is
// running: the record the device keeps until the next command.
function stoppedBy(stopped) {
  return { ...IDLE, stopped: { id: 7, cause: "operator", unfinished: false, ...stopped } };
}

test("there is nothing to say about a stop before the first status", () => {
  // The notices are drawn from the start, and the first poll can take a
  // while to come back.
  assert.equal(new Stops().notice(null), null);
  assert.equal(tooLate().notice(null).text, "Too late to stop: the label maker had already finished.");
});

test("a stop asked for here is pending from the tap", () => {
  // The device says a stop is coming too, but not until the next poll, and
  // the stop button says it is stopping from the moment it is tapped.
  const stops = new Stops();
  stops.ask("after_label", null, SENT);
  assert.equal(stops.pending(null), "after_label");
});

test("a stop another page asked for is pending while the device runs", () => {
  // A panel opened partway through a stop says so too. Once the device is
  // idle, there is nothing left for a stop to stop.
  const stops = new Stops();
  assert.equal(stops.pending({ busy: true, command: "tag", stop: "after_label" }), "after_label");
  assert.equal(stops.pending({ busy: false, command: "idle", stop: "after_label" }), null);
  assert.equal(stops.pending({ busy: true, command: "tag" }), null);
  assert.equal(stops.pending(null), null);
});

test("a stop now outranks one after the label, whoever asked for it", () => {
  // Stopping now is also the end of the label, and the stop button is the
  // one to say so.
  const afterHere = new Stops();
  afterHere.ask("after_label", null, SENT);
  assert.equal(afterHere.pending({ busy: true, command: "tag", stop: "now" }), "now");
  const nowHere = new Stops();
  nowHere.ask("now", null, SENT);
  assert.equal(nowHere.pending({ busy: true, command: "tag", stop: "after_label" }), "now");
});

test("a stop is posted from the tap, under an id of its own", () => {
  // Nothing holds it back, whatever else the page is waiting for: a stop is
  // no use late. What it is to wait for goes in the query, beside the id
  // that keeps one sent twice from landing twice.
  const stops = new Stops();
  assert.equal(stopPath(stops.ask("now", null, SENT), STOP_ID), "api/stop?id=5d7e21b6843f9a0c");
  assert.equal(stops.pending(null), "now");
  assert.equal(stopPath(stops.ask("after_label", null, SENT), STOP_ID), "api/stop?id=5d7e21b6843f9a0c&after=label");
  assert.equal(stops.pending(null), "after_label");
});

test("a stop the device took is over once a status asked for after that no longer says one is coming", () => {
  // A status asked for before the device took the stop still says the
  // command is running with no stop coming, and says nothing about the
  // stop. While the device runs on with a stop coming, it has not stopped.
  const stops = new Stops();
  const request = stops.ask("after_label", null, SENT);
  stops.statusArrived({ busy: true, command: "tag" }, 500);
  assert.equal(stops.pending(null), "after_label");
  assert.equal(stops.answered(request, ACCEPTED, { result: "stopping" }, 1000), null);
  stops.statusArrived({ busy: true, command: "tag" }, 999);
  assert.equal(stops.pending(null), "after_label");
  stops.statusArrived({ busy: true, command: "tag", stop: "after_label" }, 1000);
  assert.equal(stops.pending(null), "after_label");
  stops.statusArrived({ busy: false, command: "idle" }, 1001);
  assert.equal(stops.pending(null), null);
  // Idle, the device has nothing left for a stop to stop, whatever it says.
  // Running something else, it has moved on.
  for (const over of [
    { ...IDLE, stop: "after_label" },
    { busy: true, command: "feed" },
  ]) {
    const taken = new Stops();
    taken.answered(taken.ask("after_label", null, SENT), ACCEPTED, { result: "stopping" }, 1000);
    taken.statusArrived(over, 1000);
    assert.equal(taken.pending(null), null);
  }
});

test("a stop answered with no body is taken", () => {
  // The device sends its status line before the body, so a reply cut off on
  // the way back still came from a device that had the stop.
  const stops = new Stops();
  assert.equal(stops.answered(stops.ask("now", null, SENT), ACCEPTED, null, 1000), null);
  assert.equal(stops.pending(null), "now");
  stops.statusArrived({ busy: true, command: "tag", stop: "now" }, 1000);
  assert.equal(stops.pending(null), "now");
});

test("a stop that reaches the device after its command finished says it came too late", () => {
  // Not a failure: what it was doing finished while the tap was on its
  // way, and there is nothing to stop.
  const stops = new Stops();
  const request = stops.ask("after_label", null, SENT);
  assert.equal(stops.answered(request, ACCEPTED, { result: "idle" }, 1000), null);
  assert.equal(stops.pending(null), null);
  assert.deepEqual(stops.notice(IDLE), {
    text: "Too late to stop: the label maker had already finished.",
    unfinished: false,
  });
});

test("a stop the device refuses is not pending, and says why", () => {
  // In the device's own words when it gives any.
  const stops = new Stops();
  const reason = { error: "Only a run of labels can stop after a label" };
  assert.equal(
    stops.answered(stops.ask("after_label", null, SENT), { ok: false, status: 409 }, reason, 1000),
    "Only a run of labels can stop after a label",
  );
  assert.equal(stops.pending(null), null);
  const silent = "The label maker would not stop, and did not say why (HTTP 503).";
  assert.equal(stops.answered(stops.ask("now", null, SENT), { ok: false, status: 503 }, null, 2000), silent);
  assert.equal(stops.answered(stops.ask("now", null, SENT), { ok: false, status: 503 }, { error: 503 }, 3000), silent);
});

test("the answer to a stop asked for again since changes nothing", () => {
  // The page goes by the stop asked for last, whatever the device made of
  // the one before.
  const stops = new Stops();
  const first = stops.ask("after_label", null, SENT);
  stops.ask("now", null, SENT);
  assert.equal(stops.answered(first, { ok: false, status: 409 }, null, 1000), null);
  assert.equal(stops.answered(first, ACCEPTED, { result: "idle" }, 1000), null);
  assert.equal(stops.unreachable(first), null);
  assert.equal(stops.pending(null), "now");
  assert.equal(stops.notice(IDLE), null);
});

test("a stop that cannot reach the device is not pending, and says what to do instead", () => {
  // A stop now is for when something has gone wrong, and the power switch
  // still stops the machine. A run left to finish goes on printing.
  const stops = new Stops();
  assert.equal(
    stops.unreachable(stops.ask("now", null, SENT)),
    "Couldn’t reach the label maker to stop it. If it has to stop now, switch it off.",
  );
  assert.equal(stops.pending(null), null);
  assert.equal(
    stops.unreachable(stops.ask("after_label", null, SENT)),
    "Couldn’t reach the label maker to stop it. The rest of the labels will still print.",
  );
  assert.equal(stops.pending(null), null);
});

test("a stop that has had no answer yet says what to do meanwhile, and stays pending", () => {
  // The page goes on trying, which can take a while. A stop now is for when
  // something has gone wrong, so the power switch is named at the first try
  // to come to nothing, not kept for when the page gives up.
  const stops = new Stops();
  const request = stops.ask("now", null, SENT);
  assert.equal(
    stops.unanswered(request),
    "No answer from the label maker yet. Still trying to stop it. If it has to stop now, switch it off.",
  );
  assert.equal(stops.pending(null), "now");
  assert.equal(stops.wanted(request), true);
  assert.equal(
    stops.unanswered(stops.ask("after_label", null, SENT)),
    "No answer from the label maker yet. Still trying to stop it after this label.",
  );
  assert.equal(stops.pending(null), "after_label");
});

test("a stop still on its way is over once the device is idle", () => {
  // Whether the stop got there and its answer was lost, or the command ended
  // on its own, nothing is left to stop. Another try could only reach
  // whatever the machine does next.
  const stops = new Stops();
  const request = stops.ask("now", null, SENT);
  // A status asked for before the stop was sent can be from before the
  // device had the command, and says nothing about the stop.
  stops.statusArrived(IDLE, SENT - 1);
  assert.equal(stops.wanted(request), true);
  stops.statusArrived({ busy: true, command: "tag" }, SENT);
  assert.equal(stops.wanted(request), true);
  stops.statusArrived(IDLE, SENT);
  assert.equal(stops.wanted(request), false);
  assert.equal(stops.pending(null), null);
  assert.equal(stops.unanswered(request), null);
  assert.equal(stops.unreachable(request), null);
  assert.equal(stops.notice(IDLE).text, "Too late to stop: the label maker had already finished.");
  // One that did stop something left a record, and the record says so.
  const lost = new Stops();
  const lostRequest = lost.ask("now", null, SENT);
  const record = stoppedBy({ command: "testalign" });
  lost.statusArrived(record, SENT);
  assert.equal(lost.wanted(lostRequest), false);
  assert.equal(lost.notice(record, device).text, "Stopped the alignment test.");
});

test("a stop overtaken by a command, or by another stop, is no longer wanted", () => {
  // The page stops trying to get it through: it was for a command that is
  // over, and must not reach the one that came after.
  const stops = new Stops();
  const first = stops.ask("after_label", null, SENT);
  const second = stops.ask("now", null, SENT);
  assert.equal(stops.wanted(first), false);
  assert.equal(stops.unanswered(first), null);
  assert.equal(stops.wanted(second), true);
  stops.commandStarting();
  assert.equal(stops.wanted(second), false);
});

test("a stop now that the device took, and that left no record, came too late", () => {
  // One that lands on the last cut, or in the celebration after it, cuts
  // nothing short. A stop after the label never leaves a record: it ends
  // the run the usual way.
  const stops = new Stops();
  stops.answered(stops.ask("now", null, SENT), ACCEPTED, { result: "stopping" }, 1000);
  stops.statusArrived(IDLE, 1000);
  assert.deepEqual(stops.notice(IDLE), {
    text: "Too late to stop: the label maker had already finished.",
    unfinished: false,
  });
  const gentle = new Stops();
  gentle.answered(gentle.ask("after_label", null, SENT), ACCEPTED, { result: "stopping" }, 1000);
  gentle.statusArrived(IDLE, 1000);
  assert.equal(gentle.notice(IDLE), null);
  // One that did leave a record stopped something, and the record says so.
  // It is gone after a restart, and the stop was still not too late.
  const recorded = new Stops();
  recorded.answered(recorded.ask("now", null, SENT), ACCEPTED, { result: "stopping" }, 1000);
  recorded.statusArrived(stoppedBy({ command: "testalign" }), 1000);
  recorded.statusArrived(IDLE, 2000);
  assert.equal(recorded.notice(IDLE), null);
});

test("a stop that cut a run of labels short says how far it got", () => {
  // A label left unfinished is tape fed for it, still in the machine, and
  // it comes out on the front of the next label unless it is cut off first.
  const notice = (stopped) => new Stops().notice(stoppedBy({ command: "tag", ...stopped }), device);
  assert.deepEqual(notice({ printed: 2, copies: 5, unfinished: true }), {
    text: "Stopped partway through label 3 of 5. Cut it off before printing again.",
    unfinished: true,
  });
  assert.deepEqual(notice({ printed: 2, copies: 5 }), { text: "Stopped after 2 of 5 labels.", unfinished: false });
  assert.equal(notice({ printed: 0, copies: 5 }).text, "Stopped before label 1 of 5 was started.");
  assert.equal(notice({ printed: 1, copies: 5 }).text, "Stopped after 1 of 5 labels.");
  const partway = "Stopped partway through the label. Cut it off before printing again.";
  assert.equal(notice({ printed: 0, copies: 1, unfinished: true }).text, partway);
  assert.equal(notice({ printed: 0, copies: 1 }).text, "Stopped before the label was started.");
  // A record without the count, or with half of it, is taken for a single
  // label.
  assert.equal(notice({ unfinished: true }).text, partway);
  assert.equal(notice({ copies: 5, unfinished: true }).text, partway);
  assert.equal(notice({ printed: 2, unfinished: true }).text, partway);
});

test("a stop that cut a test or a new roll short says what it left", () => {
  // The test label is tape fed like any other. A roll stopped partway is
  // not through to the cutter yet.
  const notice = (stopped) => new Stops().notice(stoppedBy(stopped), device);
  assert.deepEqual(notice({ command: "testfull", unfinished: true }), {
    text: "Stopped partway through the test label. Cut it off before printing again.",
    unfinished: true,
  });
  assert.equal(notice({ command: "testfull" }).text, "Stopped before the test label was started.");
  assert.equal(notice({ command: "testalign" }).text, "Stopped the alignment test.");
  assert.equal(
    notice({ command: "reel" }).text,
    "Stopped loading the new roll before the tape was all the way through. Load it again to finish.",
  );
  // A record that does not say whether it left a label unfinished left
  // none, rather than one the page cannot say yes or no to.
  assert.equal(notice({ command: "testfull", unfinished: undefined }).unfinished, false);
});

test("a stop that cut an unload short says how to finish it", () => {
  // The stop can come before the end of the tape is out of the feed cog or
  // after it, and the device cannot tell which: once the end is out, the cog
  // turns without moving it.
  const notice = (stopped) => new Stops().notice(stoppedBy(stopped), device);
  assert.deepEqual(notice({ command: "unload" }), {
    text: "Stopped unloading the roll. If the tape is still in the feed cog, unload it again.",
    unfinished: false,
  });
});

test("a stop that cut a cut, a feed or a turn of the wheel short says so", () => {
  // A cut stopped partway can leave the tape half cut through, and the way
  // to finish it is to cut again. The others leave nothing to do.
  const notice = (stopped) => new Stops().notice(stoppedBy(stopped), device);
  assert.equal(notice({ command: "cut" }).text, "Stopped the cut before it was through. Cut again to finish.");
  assert.equal(notice({ command: "feed" }).text, "Stopped the feed.");
  assert.equal(notice({ command: "home" }).text, "Stopped the wheel.");
  assert.equal(notice({ command: "move" }).text, "Stopped the wheel.");
});

test("what a stop left is told by what the device says of the command it stopped", () => {
  // Not by its name: newer firmware can stop a command this copy of the
  // panel has never heard of, and what the device says of it is all the page
  // knows. One that prints a run is counted, one that presses a label says
  // whether it got that far, and one that does neither only that it stopped.
  const reply = capabilitiesReply();
  reply.commands.dance = { ...reply.commands.tag };
  reply.commands.twirl = { ...reply.commands.testfull };
  reply.commands.spin = { ...reply.commands.cut };
  const newer = readCapabilities(reply);
  const notice = (stopped) => new Stops().notice(stoppedBy(stopped), newer);
  assert.equal(notice({ command: "dance", printed: 2, copies: 5 }).text, "Stopped after 2 of 5 labels.");
  assert.equal(notice({ command: "twirl" }).text, "Stopped before the label was started.");
  assert.equal(
    notice({ command: "twirl", unfinished: true }).text,
    "Stopped partway through the label. Cut it off before printing again.",
  );
  assert.equal(notice({ command: "spin" }).text, "The label maker was stopped.");
});

test("a stop by a lost wheel says what to check", () => {
  // Whoever pressed the stop button knows why they did. A job that found
  // the wheel lost stopped itself, and the notice is all there is to say so.
  const notice = new Stops().notice(stoppedBy({ command: "testalign", cause: "lost_wheel" }), device);
  assert.equal(
    notice.text,
    "Stopped the alignment test. The daisy wheel could not find its home. Check the magnet on the wheel and the hall sensor.",
  );
  // A record that does not say what stopped it is taken for the stop button.
  const unsaid = new Stops().notice(stoppedBy({ command: "testalign", cause: undefined }), device);
  assert.equal(unsaid.text, "Stopped the alignment test.");
});

test("a dismissed notice stays dismissed, and the next stop's still shows", () => {
  // The device keeps its record until the next command, so every poll
  // brings it back. Every stop has an id of its own, so two that say the
  // same thing are still two stops.
  const stops = new Stops();
  const first = stoppedBy({ command: "testalign", id: 7 });
  stops.dismiss(first);
  assert.equal(stops.notice(first, device), null);
  assert.equal(stops.notice(stoppedBy({ command: "testalign", id: 8 }), device).text, "Stopped the alignment test.");
});

test("stops from a device that numbers none are told apart by what they say", () => {
  // Firmware older than this page sends no id.
  const unnumbered = (stopped) => ({ ...IDLE, stopped: { command: "tag", unfinished: false, ...stopped } });
  const stops = new Stops();
  stops.dismiss(unnumbered({ printed: 2, copies: 5 }));
  assert.equal(stops.notice(unnumbered({ printed: 2, copies: 5 }), device), null);
  assert.equal(stops.notice(unnumbered({ printed: 3, copies: 5 }), device).text, "Stopped after 3 of 5 labels.");
  assert.equal(stops.notice(unnumbered({ printed: 2, copies: 6 }), device).text, "Stopped after 2 of 6 labels.");
  const otherCommand = unnumbered({ command: "testfull", printed: 2, copies: 5 });
  assert.equal(stops.notice(otherCommand, device).text, "Stopped before the test label was started.");
  assert.equal(stops.notice(unnumbered({ printed: 2, copies: 5, unfinished: true }), device).unfinished, true);
});

test("a dismissal lasts as long as the device keeps that record", () => {
  // The record goes with the next command. A stop after that which says
  // the same thing, from a device that numbers none, is another stop.
  const stops = new Stops();
  const record = { ...IDLE, stopped: { command: "testalign", unfinished: false } };
  stops.dismiss(record);
  stops.statusArrived(record, 1000);
  assert.equal(stops.notice(record, device), null);
  stops.statusArrived({ busy: true, command: "testalign" }, 2000);
  assert.equal(stops.notice(record, device).text, "Stopped the alignment test.");
});

test("the too-late note stays until it is dismissed", () => {
  // Polls go on while the device is idle, and none of them has a record of
  // the stop to say anything else with.
  const stops = tooLate();
  stops.statusArrived(IDLE, 2000);
  assert.equal(stops.notice(IDLE).text, "Too late to stop: the label maker had already finished.");
  stops.dismiss(IDLE);
  assert.equal(stops.notice(IDLE), null);
});

test("the too-late note goes once the device runs something", () => {
  // Started from this page or any other, a command is the end of what the
  // last stop had to say.
  const stops = tooLate();
  stops.statusArrived({ busy: true, command: "feed" }, 2000);
  stops.statusArrived(IDLE, 3000);
  assert.equal(stops.notice(IDLE), null);
});

test("a stop asked for again is the end of what the last one had to say", () => {
  // Until the next status comes in, the page still shows the run the stop
  // came too late for, stops and all.
  const stops = tooLate();
  stops.ask("now", null, SENT);
  assert.equal(stops.notice(IDLE), null);
});

test("a command sent from here is the end of what the last stop had to say", () => {
  // Before the device has even answered it. A stop still on its way is
  // overtaken, and what the device makes of it changes nothing.
  const noted = tooLate();
  noted.commandStarting();
  assert.equal(noted.notice(IDLE), null);
  const stops = new Stops();
  const request = stops.ask("after_label", null, SENT);
  stops.commandStarting();
  assert.equal(stops.pending(null), null);
  assert.equal(stops.unreachable(request), null);
});

test("a stop tapped while its command is still on its way names that command, and holds it", () => {
  // The device may not have the command yet, and the stop can get there
  // first. Named by the id it was sent under, the command is then kept from
  // starting when it does arrive. The page sends it no more: the operator has
  // said stop, and another try could only start what they stopped.
  const stops = new Stops();
  const command = new Sending();
  stops.commandStarting();
  const request = stops.ask("after_label", command, SENT);
  assert.equal(stopPath(request, STOP_ID), "api/stop?id=5d7e21b6843f9a0c&after=label&for=" + command.id);
  assert.equal(command.held(), true);
  assert.equal(stops.pending(null), "after_label");
  // A stop asked for again names it too, and nothing has been said of the
  // command yet.
  const again = stops.ask("now", command, SENT);
  assert.equal(stopPath(again, STOP_ID), "api/stop?id=5d7e21b6843f9a0c&for=" + command.id);
});

test("a stop that got there before its command says it was in time", async () => {
  // The device had not started the command, and refuses it when it comes.
  // That is the stop having worked, and the page says so.
  const stops = new Stops();
  const command = new Sending();
  const request = stops.ask("now", command, SENT);
  assert.equal(await saidOf(command), NOT_SAID);
  assert.equal(stops.answered(request, ACCEPTED, { result: "not_started" }, 1000), null);
  assert.equal(await saidOf(command), NEVER_STARTED);
  assert.equal(stops.pending(null), null);
  assert.deepEqual(stops.notice(IDLE), {
    text: "Stopped in time: the label maker had not started.",
    unfinished: false,
  });
});

test("a stop that finds its command running, or over, says the device took it", async () => {
  // Either way the command got there, which is all its own answer would have
  // said. What became of the stop is told as for any other.
  const running = new Stops();
  const command = new Sending();
  running.answered(running.ask("now", command, SENT), ACCEPTED, { result: "stopping" }, 1000);
  assert.equal(await saidOf(command), TAKEN);
  assert.equal(running.pending(null), "now");
  const over = new Stops();
  const ended = new Sending();
  over.answered(over.ask("now", ended, SENT), ACCEPTED, { result: "idle" }, 1000);
  assert.equal(await saidOf(ended), TAKEN);
  assert.equal(over.notice(IDLE).text, "Too late to stop: the label maker had already finished.");
  // The device refuses a stop only of what it is running.
  const refused = new Stops();
  const cutting = new Sending();
  const reason = { error: "Only a run of labels can stop after a label" };
  assert.equal(
    refused.answered(refused.ask("after_label", cutting, SENT), { ok: false, status: 409 }, reason, 1000),
    "Only a run of labels can stop after a label",
  );
  assert.equal(await saidOf(cutting), TAKEN);
});

test("a stop that could not be got through leaves what became of its command unknown", async () => {
  // The try of the command that went out may have got there, or not. The
  // page has said it could not stop the machine, and has no more to say.
  const stops = new Stops();
  const command = new Sending();
  assert.equal(
    stops.unreachable(stops.ask("now", command, SENT)),
    "Couldn’t reach the label maker to stop it. If it has to stop now, switch it off.",
  );
  assert.equal(await saidOf(command), UNKNOWN);
  // Nor does a refusal that is not the device's word on what it is running
  // say anything of the command.
  const garbled = new Stops();
  const unheard = new Sending();
  garbled.answered(garbled.ask("now", unheard, SENT), { ok: false, status: 503 }, null, 1000);
  assert.equal(await saidOf(unheard), UNKNOWN);
});

test("a stop for a command still on its way is not over because the device is idle", () => {
  // The command may be yet to arrive, and the stop has still to get there
  // before it does. Once a status names the command the device has had it,
  // and idle then means it is over.
  const stops = new Stops();
  const command = new Sending();
  const request = stops.ask("now", command, SENT);
  stops.statusArrived(IDLE, SENT);
  assert.equal(stops.wanted(request), true);
  stops.statusArrived({ ...IDLE, last_command_id: "9a0c5d7e21b6843f" }, SENT);
  assert.equal(stops.wanted(request), true);
  assert.equal(stops.pending(null), "now");
  stops.statusArrived({ ...IDLE, last_command_id: command.id }, SENT);
  assert.equal(stops.wanted(request), false);
  assert.equal(stops.pending(null), null);
  assert.equal(stops.notice(IDLE).text, "Too late to stop: the label maker had already finished.");
});

test("a stop answered with no body says nothing of a command the device never names", async () => {
  // The device has the stop, and what it found was in the part that was
  // lost. The page takes it that the command is running until a status says:
  // it shows the stop on its way and offers nothing else meanwhile. Idle, and
  // naming some other command or none, the device never had this one or has
  // had another since, and the page cannot say which.
  const stops = new Stops();
  const command = new Sending();
  assert.equal(stops.answered(stops.ask("now", command, SENT), ACCEPTED, null, 1000), null);
  assert.equal(await saidOf(command), TAKEN);
  assert.equal(stops.pending(null), "now");
  stops.statusArrived(IDLE, 1000);
  assert.equal(stops.pending(null), null);
  assert.equal(stops.notice(IDLE), null);
});

test("a command refused because its stop got there first is the stop having worked", () => {
  // The answer to the command can come back before the answer to the stop,
  // and says the same: stopped before it started. Nothing went wrong, and the
  // page says what it says of the stop.
  const stops = new Stops();
  const request = stops.ask("now", new Sending(), SENT);
  assert.equal(stops.commandRefused({ error: "Stopped before it started", result: "not_started" }), true);
  assert.equal(stops.wanted(request), false);
  assert.equal(stops.pending(null), null);
  assert.equal(stops.notice(IDLE).text, "Stopped in time: the label maker had not started.");
});

test("a stop tapped while a command was on its way goes with the command, if the device refuses it", () => {
  // The command left nothing running for the stop to stop. Why the device
  // refused it is for the page to show.
  const stops = new Stops();
  stops.commandStarting();
  const request = stops.ask("now", new Sending(), SENT);
  assert.equal(stops.commandRefused({ error: "The printer is already busy executing a command." }), false);
  assert.equal(stops.wanted(request), false);
  assert.equal(stops.pending(null), null);
  assert.equal(stops.notice(IDLE), null);
  // A refusal that says nothing is not the stop's doing either.
  assert.equal(new Stops().commandRefused(null), false);
});

test("a stop that was in time says so over the record of an older stop", () => {
  // The device keeps its record of the last stop until it takes another
  // command, and it never took this one. What the record says is old news,
  // and one dismissal takes both down.
  const stops = new Stops();
  const record = stoppedBy({ command: "tag", printed: 2, copies: 5 });
  stops.statusArrived(record, 100);
  assert.equal(stops.notice(record, device).text, "Stopped after 2 of 5 labels.");
  stops.commandStarting();
  stops.answered(stops.ask("now", new Sending(), SENT), ACCEPTED, { result: "not_started" }, 1000);
  stops.statusArrived(record, 1000);
  assert.equal(stops.notice(record, device).text, "Stopped in time: the label maker had not started.");
  stops.dismiss(record);
  assert.equal(stops.notice(record, device), null);
});
