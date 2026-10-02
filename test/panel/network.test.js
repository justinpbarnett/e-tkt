import assert from "node:assert/strict";
import { test } from "node:test";

import {
  NetworkSearch,
  SLOW_LISTEN_MS,
  addingIntro,
  failureText,
  forgetChange,
  modeChange,
  modeNote,
  networkToAdd,
  ownSummary,
  reachSummary,
  readNetwork,
  rememberedRows,
  rememberedSummary,
  routerChange,
} from "../../data/network.js";
import { nearbyReply, networkReply } from "./device.js";

// The machine's own network, open, with this many phones on it.
function ownOpen(clients = 0) {
  return { name: "E-TKT-9C4F", open: true, clients: clients, address: "192.168.4.1" };
}

// What api/network says of a machine still trying the workshop's network.
function joining(changes = {}) {
  const reply = networkReply({ station: "joining", ...changes });
  delete reply.address;
  return reply;
}

// Of a machine never set up: it remembers no network, and its own is open.
function neverSetUp(changes = {}) {
  const reply = networkReply({ station: "off", remembered: [], own: ownOpen(), ...changes });
  delete reply.network;
  delete reply.address;
  return reply;
}

// Of a machine kept on its own network.
function onItsOwn(changes = {}) {
  const reply = networkReply({ mode: "own", station: "off", own: ownOpen(), ...changes });
  delete reply.network;
  delete reply.address;
  return reply;
}

// A failed try at the workshop's network, as the device tells one.
function failed(cause, reason = {}) {
  return joining({ failure: { network: "Workshop", cause: cause, ...reason } });
}

// A search that has heard what is in the simulator's air.
function searched() {
  const search = new NetworkSearch();
  search.asked(0);
  search.taken({ result: "listening", after: 0 });
  search.heard(nearbyReply());
  return search;
}

test("the card reads the reply the device gives", () => {
  // As it comes: every word on the card is made from the device's own.
  const reply = networkReply();
  assert.equal(readNetwork(reply), reply);
  assert.notEqual(readNetwork(joining()), null);
  assert.notEqual(readNetwork(neverSetUp()), null);
  assert.notEqual(readNetwork(onItsOwn()), null);
  assert.notEqual(readNetwork(failed("refused", { reason: 15, reason_name: "4WAY_HANDSHAKE_TIMEOUT" })), null);
});

test("a reply the card cannot read is not guessed at", () => {
  // A firmware that says something this page does not know of is one the
  // page would describe wrongly, and a change made on a guess can cut the
  // page off from the machine. The page shows no card for it.
  assert.equal(readNetwork(null), null);
  assert.equal(readNetwork([]), null);
  assert.equal(readNetwork("joined"), null);
  assert.equal(readNetwork(networkReply({ mode: "mesh" })), null);
  assert.equal(readNetwork(networkReply({ station: "roaming" })), null);
  assert.equal(readNetwork(networkReply({ network: undefined })), null);
  assert.equal(readNetwork(networkReply({ address: undefined })), null);
  assert.equal(readNetwork(networkReply({ host: undefined })), null);
  assert.equal(readNetwork(networkReply({ own: undefined })), null);
  assert.equal(readNetwork(networkReply({ own: { name: "E-TKT-9C4F" } })), null);
  assert.equal(readNetwork(networkReply({ own: { ...ownOpen(), clients: "2" } })), null);
  assert.equal(readNetwork(networkReply({ remembered: "Workshop" })), null);
  assert.equal(readNetwork(networkReply({ remembered: [7] })), null);
  assert.equal(readNetwork(networkReply({ max_remembered: "4" })), null);
  assert.equal(readNetwork(networkReply({ router_offered: "yes" })), null);
  assert.equal(readNetwork(joining({ failure: "refused" })), null);
  assert.equal(readNetwork(joining({ failure: { cause: "refused" } })), null);
});

test("a machine on a network says which, and where a phone on it finds the panel", () => {
  // By its address and by its name: the address works on every phone, and
  // the name stays the same when the network hands out another address.
  assert.deepEqual(reachSummary(networkReply()), {
    headline: "On Workshop",
    detail: [
      "A phone on that network opens the panel at ",
      { link: "http://192.168.1.50" },
      " or ",
      { link: "http://e-tkt-9c4f.local" },
      ".",
    ],
  });
});

test("a machine still joining says what its own network does meanwhile", () => {
  // Its own network opens a minute into the trying, and from then on that
  // is where the panel is.
  assert.deepEqual(reachSummary(joining()), {
    headline: "Joining Workshop…",
    detail: ["Its own network, E-TKT-9C4F, opens if this takes more than a minute."],
  });
  assert.deepEqual(reachSummary(joining({ own: ownOpen() })), {
    headline: "Joining Workshop…",
    detail: [
      "Meanwhile its own network is open. A phone on E-TKT-9C4F opens the panel at ",
      { link: "http://192.168.4.1" },
      ".",
    ],
  });
});

test("a machine that remembers no network says it is on its own", () => {
  // It has nothing to join, so its own network is open from the start.
  assert.deepEqual(reachSummary(neverSetUp()), {
    headline: "No network to join",
    detail: [
      "The label maker remembers none, so it is on its own. A phone on E-TKT-9C4F opens the panel at ",
      { link: "http://192.168.4.1" },
      ".",
    ],
  });
});

test("a machine kept on its own network says where the panel is on it", () => {
  assert.deepEqual(reachSummary(onItsOwn()), {
    headline: "On its own network",
    detail: ["A phone on E-TKT-9C4F opens the panel at ", { link: "http://192.168.4.1" }, "."],
  });
});

test("a change the machine has not followed yet reads as the change under way", () => {
  // The reply to a change has the new settings in it at once. What the radio
  // does follows a couple of seconds later, so that the reply gets away
  // first. In between, the page says where the machine is going and not
  // where it was.
  const onOwn = ["A phone on E-TKT-9C4F opens the panel at ", { link: "http://192.168.4.1" }, "."];
  assert.deepEqual(reachSummary(networkReply({ mode: "own" })), {
    headline: "Leaving Workshop…",
    detail: ["Its own network opens in a moment. " + onOwn[0], ...onOwn.slice(1)],
  });
  assert.deepEqual(reachSummary(networkReply({ mode: "own", own: ownOpen(1) })), {
    headline: "Leaving Workshop…",
    detail: onOwn,
  });
  assert.deepEqual(reachSummary(onItsOwn({ own: { ...ownOpen(), open: false } })), {
    headline: "Opening its own network…",
    detail: onOwn,
  });
  assert.equal(reachSummary(onItsOwn({ mode: "join" })).headline, "Joining Workshop…");
  assert.deepEqual(reachSummary(networkReply({ remembered: [] })), {
    headline: "No network to join",
    detail: ["The label maker remembers none, so its own network opens in a moment. " + onOwn[0], ...onOwn.slice(1)],
  });
});

test("a change that cuts the page off leaves it saying where the panel goes", () => {
  // Kept on its own network, or with its only network forgotten, the machine
  // leaves the network this page may be on, and these are the last words the
  // page has from it. They name the address to open once the phone is on the
  // machine's own network.
  const there = { link: "http://192.168.4.1" };
  assert.deepEqual(reachSummary(networkReply({ mode: "own" })).detail[1], there);
  assert.deepEqual(reachSummary(networkReply({ remembered: [] })).detail[1], there);
});

test("a machine that was only trying a network is not said to leave it", () => {
  // Told to keep to its own network while it tries one, it was on none.
  assert.equal(reachSummary(joining({ mode: "own" })).headline, "Opening its own network…");
  assert.equal(reachSummary(joining({ mode: "own", own: ownOpen() })).headline, "On its own network");
});

test("a machine on a network it was told to forget says it leaves it, and what it tries next", () => {
  // It stays a moment longer, to get its answer out, and then goes for the
  // first of the networks it still remembers.
  assert.deepEqual(reachSummary(networkReply({ remembered: ["Church Guest", "Far Corner"] })), {
    headline: "Leaving Workshop…",
    detail: [
      "The label maker tries Church Guest next. Its own network, E-TKT-9C4F, opens if that takes more than a minute.",
    ],
  });
  assert.deepEqual(reachSummary(networkReply({ remembered: ["Church Guest"], own: ownOpen(1) })), {
    headline: "Leaving Workshop…",
    detail: [
      "The label maker tries Church Guest next. Meanwhile its own network is open. " +
        "A phone on E-TKT-9C4F opens the panel at ",
      { link: "http://192.168.4.1" },
      ".",
    ],
  });
  // One it was only trying is given up for the first one too.
  assert.equal(reachSummary(joining({ remembered: ["Church Guest"] })).headline, "Joining Church Guest…");
  // And nothing else on the card speaks of it as a network the machine is on.
  const leaving = networkReply({ remembered: ["Church Guest"], own: ownOpen(1) });
  assert.equal(addingIntro(leaving), addingIntro(joining()));
  assert.equal(ownSummary(leaving).note, ownSummary(joining({ own: ownOpen(1) })).note);
});

test("a network that was not heard is told as not heard", () => {
  // Out of reach and typed wrongly are the same thing to the radio, and the
  // page names both.
  assert.equal(
    failureText(failed("not_found", { reason: 201, reason_name: "NO_AP_FOUND" })),
    "No network called “Workshop” was heard. It may be out of reach, or its name may be typed differently. " +
      "The label maker keeps trying.",
  );
});

test("a network that turned the machine away is told as a wrong password or a weak signal", () => {
  // A handshake that never finished is all the radio knows, and a weak
  // signal ends one as a wrong password does. Told as a wrong password
  // alone, somebody in a basement types the right one in again and again.
  assert.equal(
    failureText(failed("refused", { reason: 15, reason_name: "4WAY_HANDSHAKE_TIMEOUT" })),
    "“Workshop” turned the label maker away. That is a wrong password, or a signal too weak to finish joining: " +
      "the two look the same from here. The radio gave reason 15, 4WAY_HANDSHAKE_TIMEOUT. " +
      "The label maker keeps trying.",
  );
});

test("a network that gave the machine no address is told as that", () => {
  // The radio has no reason to give for it: the network took the machine on,
  // and then said nothing.
  assert.equal(
    failureText(failed("no_address")),
    "“Workshop” let the label maker on and gave it no address, as a network does when it is full. " +
      "The label maker keeps trying.",
  );
});

test("any other failure is told with the radio's reason for it, when it gave one", () => {
  // A cause this page has no words for is told the same way: the number is
  // what somebody looking into it needs.
  assert.equal(
    failureText(failed("other", { reason: 200, reason_name: "BEACON_TIMEOUT" })),
    "The label maker could not join “Workshop”. The radio gave reason 200, BEACON_TIMEOUT. It keeps trying.",
  );
  assert.equal(failureText(failed("other")), "The label maker could not join “Workshop”. It keeps trying.");
  assert.equal(
    failureText(failed("jammed", { reason: 9 })),
    "The label maker could not join “Workshop”. The radio gave reason 9. It keeps trying.",
  );
});

test("a failure is told only while the machine is still trying", () => {
  // The device drops it as it joins a network or is kept on its own, and a
  // reply from the moment in between has it still.
  const failure = { network: "Workshop", cause: "not_found" };
  assert.equal(failureText(joining()), null);
  assert.equal(failureText(networkReply({ failure: failure })), null);
  assert.equal(failureText(onItsOwn({ failure: failure })), null);
});

test("the machine's own network says whether it is open, and how many phones are on it", () => {
  assert.equal(ownSummary(networkReply()).state, "Closed");
  assert.equal(ownSummary(neverSetUp()).state, "Open, nobody on it");
  assert.equal(ownSummary(neverSetUp({ own: ownOpen(1) })).state, "Open, 1 phone on it");
  assert.equal(ownSummary(onItsOwn({ own: ownOpen(3) })).state, "Open, 3 phones on it");
});

test("an open network of its own says where its password is", () => {
  // On the machine's screen, and nowhere else: the device serves it to
  // nobody.
  const note = "Its password, and a code a phone’s camera can join it with, are on the label maker’s screen.";
  assert.equal(ownSummary(onItsOwn()).note, note);
  assert.equal(ownSummary(joining({ own: ownOpen() })).note, note);
});

test("an own network left open after a join says when it closes", () => {
  // The screen shows the network joined by then, and not this one's
  // password.
  assert.equal(
    ownSummary(networkReply({ own: ownOpen(1) })).note,
    "It closes a minute after the last phone leaves it, now that the label maker is on a network.",
  );
});

test("a closed network of its own says when it opens", () => {
  assert.equal(
    ownSummary(networkReply()).note,
    "It opens when the label maker has been without a network for a minute.",
  );
  const closed = { ...ownOpen(), open: false };
  assert.equal(ownSummary(onItsOwn({ own: closed })).note, "It opens in a moment.");
  assert.equal(ownSummary(neverSetUp({ own: closed })).note, "It opens in a moment.");
});

test("the networks remembered are listed in the order they are tried, with the one in use marked", () => {
  const two = { remembered: ["Workshop", "Church Guest"] };
  assert.deepEqual(rememberedRows(networkReply(two)), [
    { ssid: "Workshop", tag: "Joined" },
    { ssid: "Church Guest", tag: null },
  ]);
  assert.deepEqual(rememberedRows(joining({ ...two, network: "Church Guest" })), [
    { ssid: "Workshop", tag: null },
    { ssid: "Church Guest", tag: "Joining" },
  ]);
  // A machine kept on its own network is on none of them, and neither is one
  // told to leave a moment ago.
  assert.deepEqual(rememberedRows(onItsOwn(two)), [
    { ssid: "Workshop", tag: null },
    { ssid: "Church Guest", tag: null },
  ]);
  assert.deepEqual(rememberedRows(networkReply({ ...two, mode: "own" })), [
    { ssid: "Workshop", tag: null },
    { ssid: "Church Guest", tag: null },
  ]);
});

test("the networks remembered are counted against how many the machine can hold", () => {
  assert.equal(rememberedSummary(networkReply()).count, "1 of 4");
  assert.equal(rememberedSummary(neverSetUp()).count, "0 of 4");
});

test("the list of networks remembered says what the machine does with it", () => {
  // One network that the machine joins needs nothing said of it.
  assert.equal(rememberedSummary(networkReply()).note, null);
  assert.equal(rememberedSummary(joining()).note, null);
  // The device puts a network first as it joins it, and as it is added.
  assert.equal(
    rememberedSummary(networkReply({ remembered: ["Workshop", "Church Guest"] })).note,
    "Tried in this order. The one joined or added last comes first.",
  );
  assert.equal(rememberedSummary(neverSetUp()).note, "None yet. Add the one the label maker is to join.");
  // Kept on its own network, the machine has no use for them until it is
  // told to join one again.
  assert.equal(rememberedSummary(onItsOwn()).note, "Not tried while the label maker keeps to its own network.");
  assert.equal(rememberedSummary(onItsOwn({ remembered: [] })).note, "None yet.");
});

test("each way of reaching the machine says what it comes to", () => {
  // Under the choice, for the one chosen. Its own network costs a phone the
  // other label makers, which is why it is not the only way.
  assert.equal(
    modeNote(networkReply()),
    "It joins a network it remembers, and opens its own when it has found none for a minute.",
  );
  assert.equal(
    modeNote(onItsOwn()),
    "It joins no network, and keeps its own open. Every label maker has its own, so a phone reaches one of them " +
      "at a time.",
  );
});

test("keeping the machine on its own network says how to reach it afterwards", () => {
  // The page that asks for this over the network the machine has joined is
  // cut off by it, and the way back is over a network the phone is not on
  // yet. It is told before, because it cannot be told after.
  assert.deepEqual(modeChange(networkReply(), "own"), {
    title: "Use only its own network?",
    summary:
      "The label maker leaves Workshop and is reached only over its own network, E-TKT-9C4F. " +
      "To reach it then, join E-TKT-9C4F with the password or the code on the label maker’s screen, " +
      "and open http://192.168.4.1.",
    confirm: "Use its own network",
  });
});

test("a phone on the machine's own network is told when the change puts it off for a moment", () => {
  // Beside a network the machine joins or tries, its own is on that
  // network's channel. Kept alone, it is opened again on a quiet one.
  const tail =
    "To reach it then, join E-TKT-9C4F with the password or the code on the label maker’s screen, " +
    "and open http://192.168.4.1.";
  assert.equal(
    modeChange(joining({ own: ownOpen(1) }), "own").summary,
    "The label maker stops looking for a network to join and is reached only over its own network, E-TKT-9C4F. " +
      tail +
      " A phone already on E-TKT-9C4F drops off it for a moment.",
  );
  assert.equal(
    modeChange(networkReply({ own: ownOpen(1) }), "own").summary,
    "The label maker leaves Workshop and is reached only over its own network, E-TKT-9C4F. " +
      tail +
      " A phone already on E-TKT-9C4F drops off it for a moment.",
  );
  // With no network remembered it is alone already, and stays as it is.
  assert.equal(
    modeChange(neverSetUp(), "own").summary,
    "The label maker stops looking for a network to join and is reached only over its own network, E-TKT-9C4F. " + tail,
  );
});

test("going back to joining says what the machine tries, and that its own network stays open", () => {
  // Whoever asks for this is on the machine's own network, and has to see
  // how it went.
  const stays =
    " Its own network stays open while a phone is on it, and this card says where the label maker is " +
    "once it has joined.";
  assert.deepEqual(modeChange(onItsOwn({ remembered: ["Workshop", "Church Guest"] }), "join"), {
    title: "Join a network?",
    summary: "The label maker tries the networks it remembers, Workshop first." + stays,
    confirm: "Join a network",
  });
  assert.equal(modeChange(onItsOwn(), "join").summary, "The label maker tries Workshop." + stays);
  assert.equal(
    modeChange(onItsOwn({ remembered: [] }), "join").summary,
    "The label maker remembers no network yet, so it stays on its own until one is added.",
  );
});

test("forgetting the network the machine is on says that it leaves it, and what it does next", () => {
  assert.deepEqual(forgetChange(networkReply({ remembered: ["Workshop", "Church Guest"] }), "Workshop"), {
    title: "Forget this network?",
    summary:
      "The label maker leaves Workshop now, and tries Church Guest instead. " +
      "A phone on Workshop loses touch with this page.",
    confirm: "Forget network",
  });
  assert.equal(
    forgetChange(networkReply(), "Workshop").summary,
    "The label maker leaves Workshop now, and opens its own network, E-TKT-9C4F. " +
      "A phone on Workshop loses touch with this page.",
  );
});

test("forgetting any other network says what it takes to add it back", () => {
  // The device keeps no password it could be asked for again, and serves
  // none.
  const summary =
    "The label maker forgets Church Guest. A network with a password needs it typed in again to be added back.";
  const two = { remembered: ["Workshop", "Church Guest"] };
  assert.equal(forgetChange(networkReply(two), "Church Guest").summary, summary);
  assert.equal(forgetChange(joining({ ...two, network: "Church Guest" }), "Church Guest").summary, summary);
  assert.equal(forgetChange(onItsOwn(two), "Church Guest").summary, summary);
});

test("changing what its own network offers asks first only while that network is open", () => {
  // The radio takes the change as the network opens, so an open one is
  // closed and opened again, and every phone on it is put off it.
  assert.equal(routerChange(networkReply()), null);
  assert.deepEqual(routerChange(onItsOwn()), {
    title: "Restart its own network?",
    summary:
      "E-TKT-9C4F has to restart to change what it tells a phone that joins it. " +
      "A phone on it drops off for a moment, and may have to be put back on it.",
    confirm: "Restart network",
  });
});

test("a search waits until the device has listened after it was asked", () => {
  // The list the device has from an earlier listen is not what this one
  // heard. Its count of listens tells the two apart.
  const search = new NetworkSearch();
  assert.equal(search.waiting, false);
  search.asked(1000);
  assert.equal(search.waiting, true);
  search.taken({ result: "listening", after: 3 });
  search.heard(nearbyReply({ listening: true, listens: 3 }));
  assert.equal(search.waiting, true);
  search.heard(nearbyReply({ listens: 4 }));
  assert.equal(search.waiting, false);
});

test("what the device heard before shows while it listens again", () => {
  // Most of it is still there, and a network can be picked from it without
  // waiting.
  const search = new NetworkSearch();
  assert.deepEqual(search.rows(networkReply()), []);
  search.asked(1000);
  search.taken({ result: "listening", after: 3 });
  search.heard(nearbyReply({ listening: true, listens: 3 }));
  assert.equal(search.rows(networkReply()).length, 8);
  assert.equal(search.text(1500), "Listening for networks…");
});

test("a listen that is long in coming says why, and what to do meanwhile", () => {
  // The radio cannot listen while it is trying a network, and a network that
  // gives no address holds it for two minutes.
  const search = new NetworkSearch();
  search.asked(1000);
  search.taken({ result: "listening", after: 0 });
  assert.equal(search.text(1000 + SLOW_LISTEN_MS - 1), "Listening for networks…");
  assert.equal(
    search.text(1000 + SLOW_LISTEN_MS),
    "Still waiting to listen. The label maker can’t while it is trying to join a network, " +
      "so the name may be quicker to type in below.",
  );
});

test("a listen is not asked for again while one is under way, until that one is long in coming", () => {
  // A listen the device lost to a restart never ends, and asking again is
  // the way out of waiting for it. Asked twice, the device listens once.
  const search = new NetworkSearch();
  assert.equal(search.askable(0), true);
  search.asked(1000);
  assert.equal(search.askable(1500), false);
  search.taken({ result: "listening", after: 0 });
  assert.equal(search.askable(1000 + SLOW_LISTEN_MS - 1), false);
  assert.equal(search.askable(1000 + SLOW_LISTEN_MS), true);
  search.heard(nearbyReply({ listens: 1 }));
  assert.equal(search.askable(1000 + SLOW_LISTEN_MS + 1), true);
});

test("a listen that could not be asked for says so, until it is asked for again", () => {
  const search = new NetworkSearch();
  search.asked(0);
  search.lost();
  assert.equal(search.waiting, false);
  assert.equal(search.text(50), "Couldn’t ask the label maker to listen.");
  search.asked(100);
  assert.equal(search.waiting, true);
  assert.equal(search.text(150), "Listening for networks…");
});

test("an answer to the listen that the page cannot read counts as no answer", () => {
  // Without the device's count of listens there is no telling what this
  // listen heard from what the one before did, and the search would wait
  // for ever.
  const search = new NetworkSearch();
  search.asked(0);
  search.taken(null);
  assert.equal(search.waiting, false);
  assert.equal(search.text(50), "Couldn’t ask the label maker to listen.");
  search.asked(100);
  search.taken({ result: "listening" });
  assert.equal(search.waiting, false);
});

test("a listen that heard nothing says a name can still be typed in", () => {
  const search = new NetworkSearch();
  assert.equal(search.text(0), null);
  search.asked(0);
  search.taken({ result: "listening", after: 0 });
  search.heard(nearbyReply({ networks: [] }));
  assert.equal(search.text(900), "No network heard. One that hides its name can be typed in below.");
});

test("a listen that heard networks says how many it lists", () => {
  // The line that said the label maker was listening stays, so the list
  // under it keeps its place when a listen starts and when it ends, and a
  // screen reader hears that there is a list now. The device lists the
  // strongest of what it heard, so the count is of the list.
  assert.equal(searched().text(900), "8 networks, the strongest first.");
  const search = new NetworkSearch();
  search.asked(0);
  search.taken({ result: "listening", after: 0 });
  search.heard(nearbyReply({ networks: [{ ssid: "Workshop", rssi: -48, secured: true }] }));
  assert.equal(search.text(900), "1 network.");
});

test("each network in reach says how strong it is, whether it has a password, and whether it is remembered", () => {
  const rows = searched().rows(networkReply());
  assert.equal(rows.length, 8);
  // In words as well, for a screen reader: the page draws a lock and the
  // bars of the signal.
  assert.deepEqual(rows[0], {
    ssid: "Workshop",
    signal: "strong",
    secured: true,
    remembered: true,
    facts: "Strong signal, has a password, remembered",
  });
  assert.deepEqual(rows[2], {
    ssid: "Church Guest",
    signal: "fair",
    secured: false,
    remembered: false,
    facts: "Fair signal, no password",
  });
  assert.deepEqual(rows[7], {
    ssid: "Far Corner",
    signal: "weak",
    secured: true,
    remembered: false,
    facts: "Weak signal, has a password",
  });
  // A name is handed on as it was heard, markup and all. The page sets it as
  // text.
  assert.equal(rows[3].ssid, '<b>Cafe</b> & "Friends"');
});

test("a list of networks the page cannot read leaves the one it has", () => {
  const search = searched();
  search.heard(null);
  search.heard({ listens: 2, networks: "none" });
  search.heard({ listens: 2, networks: [{ ssid: 7 }] });
  assert.equal(search.rows(networkReply()).length, 8);
});

test("the dialog says what the machine does with a network once it is added", () => {
  // It tries it only when it has no network: one got ready for another place
  // stays on the network it is on, and one kept on its own tries none.
  assert.equal(
    addingIntro(neverSetUp()),
    "The label maker tries a network as soon as it is added, and the Network card says how that went.",
  );
  assert.equal(
    addingIntro(joining()),
    "The label maker tries a network as soon as it is added, and the Network card says how that went.",
  );
  assert.equal(
    addingIntro(networkReply()),
    "The label maker stays on Workshop, and tries a network added here when it is next without one.",
  );
  assert.equal(
    addingIntro(onItsOwn()),
    "The label maker keeps to its own network, and tries a network added here once it is told to join one.",
  );
});

test("a network heard with a password on it needs the password typed in", () => {
  // The Add button waits for it, as a phone's own Join button does.
  const search = searched();
  let check = networkToAdd({ ssid: "Far Corner", password: "" }, neverSetUp(), search);
  assert.equal(check.password.wanted, "yes");
  assert.equal(check.password.invalid, false);
  assert.equal(check.body, null);
  check = networkToAdd({ ssid: "Far Corner", password: "labelmaker" }, neverSetUp(), search);
  assert.equal(check.password.note, null);
  assert.deepEqual(check.body, { ssid: "Far Corner", password: "labelmaker" });
});

test("a network heard without a password takes none, whatever is typed", () => {
  // The page shows no password field for it. One typed before the network
  // was picked is not sent: the radio would hold out for a password the
  // network never asks for.
  const check = networkToAdd({ ssid: "Church Guest", password: "left over" }, neverSetUp(), searched());
  assert.equal(check.password.wanted, "no");
  assert.equal(check.password.note, "This network has no password.");
  assert.deepEqual(check.body, { ssid: "Church Guest" });
});

test("a name typed in that was not heard may have a password or none", () => {
  // A network that hides its name, or the one at the place the machine goes
  // to next.
  const search = searched();
  let check = networkToAdd({ ssid: "Sanctuary", password: "" }, neverSetUp(), search);
  assert.equal(check.password.wanted, "maybe");
  assert.equal(check.password.note, "Leave it empty for a network without one.");
  assert.deepEqual(check.body, { ssid: "Sanctuary" });
  check = networkToAdd({ ssid: "Sanctuary", password: "labelmaker" }, neverSetUp(), search);
  assert.deepEqual(check.body, { ssid: "Sanctuary", password: "labelmaker" });
  // Before anything has been heard, every name is one of these.
  check = networkToAdd({ ssid: "Workshop", password: "" }, neverSetUp(), new NetworkSearch());
  assert.equal(check.password.wanted, "maybe");
});

test("a password is held to the length WPA2 takes", () => {
  // 8 to 63 bytes, which is how the device counts them. It refuses any other
  // with a 400, for something the page could have said first.
  const add = (password) => networkToAdd({ ssid: "Sanctuary", password: password }, neverSetUp(), searched());
  assert.deepEqual(add("short").password, {
    wanted: "maybe",
    note: "A password has at least 8 characters, and this has 5.",
    invalid: true,
  });
  assert.equal(add("short").body, null);
  assert.deepEqual(add("x".repeat(64)).password, {
    wanted: "maybe",
    note: "A password has at most 63 characters, and this has 64.",
    invalid: true,
  });
  assert.equal(add("x".repeat(64)).body, null);
  assert.notEqual(add("x".repeat(8)).body, null);
  assert.notEqual(add("x".repeat(63)).body, null);
  // Seven letters, two of them two bytes long.
  assert.notEqual(add("pässwör").body, null);
  // Said in bytes where a letter takes more than one: a count of characters
  // would not be the one in the field.
  assert.equal(add("über").password.note, "A password is at least 8 bytes long, and this is 5.");
  assert.equal(add("ж".repeat(40)).password.note, "A password is at most 63 bytes long, and this is 80.");
});

test("a name is held to the 32 bytes the radio takes", () => {
  // Bytes and not letters: a name with a guitar in it runs out sooner.
  const add = (ssid) => networkToAdd({ ssid: ssid, password: "" }, neverSetUp(), searched());
  assert.notEqual(add("x".repeat(32)).body, null);
  assert.deepEqual(add("x".repeat(33)).name, {
    note: "A network’s name is at most 32 bytes long, and this is 33.",
    invalid: true,
  });
  assert.equal(add("x".repeat(33)).body, null);
  assert.notEqual(add("🎸".repeat(8)).body, null);
  assert.equal(add("🎸".repeat(9)).name.note, "A network’s name is at most 32 bytes long, and this is 36.");
});

test("an empty name is nothing to add, and no mistake either", () => {
  // The dialog opens with it empty.
  const check = networkToAdd({ ssid: "", password: "" }, neverSetUp(), searched());
  assert.deepEqual(check.name, { note: null, invalid: false });
  assert.equal(check.body, null);
});

test("a machine that remembers all it can adds no more until one is forgotten", () => {
  // The device would refuse it with a 409. One of the four again is no
  // fifth: it takes the place of the one it has.
  const full = networkReply({ remembered: ["Workshop", "Church Guest", "Far Corner", "Sanctuary"] });
  const words =
    "The label maker remembers 4 networks, which is all it can. " +
    "Forget one first, or add one of them again to change its password.";
  let check = networkToAdd({ ssid: "Full House", password: "" }, full, searched());
  assert.equal(check.full, words);
  assert.equal(check.body, null);
  check = networkToAdd({ ssid: "Far Corner", password: "labelmaker" }, full, searched());
  assert.equal(check.full, words);
  assert.deepEqual(check.body, { ssid: "Far Corner", password: "labelmaker" });
  assert.equal(networkToAdd({ ssid: "Full House", password: "" }, networkReply(), searched()).full, null);
});

test("a name there is no room for says so where it is typed", () => {
  // The words at the top of the dialog are out of sight by the time a name
  // is picked from a long list, and Add is held with nothing near it to say
  // why. A name it remembers has room, and an empty field is no mistake yet.
  const full = networkReply({ remembered: ["Workshop", "Church Guest", "Far Corner", "Sanctuary"] });
  assert.deepEqual(networkToAdd({ ssid: "Full House", password: "" }, full, searched()).name, {
    note: "The label maker has no room for another network. Forget one first.",
    invalid: true,
  });
  assert.equal(networkToAdd({ ssid: "Far Corner", password: "labelmaker" }, full, searched()).name.invalid, false);
  assert.deepEqual(networkToAdd({ ssid: "", password: "" }, full, searched()).name, { note: null, invalid: false });
});

test("a network already remembered is added again to put its password right", () => {
  // The device replaces the one it has under that name, and tries it first.
  const check = networkToAdd({ ssid: "Workshop", password: "the right one" }, networkReply(), searched());
  assert.deepEqual(check.name, {
    note: "The label maker remembers this one. Adding it again replaces its password, and has it tried first.",
    invalid: false,
  });
  assert.deepEqual(check.body, { ssid: "Workshop", password: "the right one" });
});

test("a network with a weak signal says so as it is picked", () => {
  // A machine standing where its network hardly reaches is what its own
  // network is for, and this is the moment to say so.
  const check = networkToAdd({ ssid: "Far Corner", password: "" }, neverSetUp(), searched());
  assert.deepEqual(check.name, {
    note: "Its signal is weak where the label maker stands, so the link may not hold.",
    invalid: false,
  });
  assert.equal(networkToAdd({ ssid: "Workshop", password: "" }, neverSetUp(), searched()).name.note, null);
});
