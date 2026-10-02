// The Network card in Setup: how the label maker is reached, the networks it
// remembers and its own network, as api/network says them, with the words
// for each change before it is made and for a network before it is added.
// The page draws what this says and sends what it holds. Nothing in here
// touches the page; node tests it in test/panel/network.test.js.
//
// A network's name comes off the air, from whoever named that network. It
// leaves here only as text, for the page to set as text.

import { NO_ANSWER_YET } from "./status.js";

// How long a listen may take before the dialog says why it might: a listen
// itself takes a few seconds, and one asked for while the label maker is
// trying a network waits for the try to end.
export const SLOW_LISTEN_MS = 10000;

// What a network's name and its password may be, as the device holds them:
// what the radio takes for a name, and what WPA2 takes for a password.
const NAME_BYTES = 32;
const PASSWORD_BYTES = { fewest: 8, most: 63 };

// The weakest signal, in dBm, that still counts as strong, and as fair, and
// what each is called where the page cannot draw it.
const SIGNAL = { strong: -60, fair: -75 };
const SIGNAL_WORDS = { strong: "Strong signal", fair: "Fair signal", weak: "Weak signal" };

// What api/network said, if it is a reply this page knows how to describe,
// and null if it is not. The page shows no card then: a change made on a
// guess at what the label maker is doing can cut the page off from it.
export function readNetwork(reply) {
  const text = (value) => typeof value === "string";
  const own = reply === null || typeof reply !== "object" ? undefined : reply.own;
  const failure = own === undefined ? undefined : reply.failure;
  const known =
    own !== null &&
    typeof own === "object" &&
    ["join", "own"].includes(reply.mode) &&
    ["off", "joining", "joined"].includes(reply.station) &&
    (reply.station === "off" || text(reply.network)) &&
    (reply.station !== "joined" || text(reply.address)) &&
    text(reply.host) &&
    text(own.name) &&
    typeof own.open === "boolean" &&
    Number.isInteger(own.clients) &&
    text(own.address) &&
    Array.isArray(reply.remembered) &&
    reply.remembered.every(text) &&
    Number.isInteger(reply.max_remembered) &&
    typeof reply.router_offered === "boolean" &&
    (failure === undefined ||
      (failure !== null && typeof failure === "object" && text(failure.network) && text(failure.cause)));
  return known ? reply : null;
}

// Where the label maker is, and where a phone finds the panel: a headline,
// and a sentence under it as a list of parts, each one text or a link for
// the page to make.
export function reachSummary(network) {
  const own = network.own;
  // The reply to a change has the change in it at once, and the radio
  // follows a moment later. A page on the network the label maker leaves
  // then hears no more, so what it is left showing says where the panel
  // goes.
  if (network.mode === "own") {
    if (network.station === "joined") {
      return {
        headline: "Leaving " + network.network + "…",
        detail: panelOnOwn(own, own.open ? "" : "Its own network opens in a moment. "),
      };
    }
    return { headline: own.open ? "On its own network" : "Opening its own network…", detail: panelOnOwn(own) };
  }
  const [first] = network.remembered;
  if (first === undefined) {
    return {
      headline: "No network to join",
      detail: panelOnOwn(
        own,
        "The label maker remembers none, so " +
          (own.open ? "it is on its own. " : "its own network opens in a moment. "),
      ),
    };
  }
  if (staying(network)) {
    return {
      headline: "On " + network.network,
      detail: [
        "A phone on that network opens the panel at ",
        panelLink(network.address),
        " or ",
        panelLink(network.host),
        ".",
      ],
    };
  }
  if (network.station === "joined") {
    const next = "The label maker tries " + first + " next. ";
    return {
      headline: "Leaving " + network.network + "…",
      detail: own.open
        ? panelOnOwn(own, next + "Meanwhile its own network is open. ")
        : [next + "Its own network, " + own.name + ", opens if that takes more than a minute."],
    };
  }
  // The one it is trying, or the one it starts with once it has taken up a
  // change made a moment ago.
  const trying = network.station === "joining" && network.remembered.includes(network.network);
  return {
    headline: "Joining " + (trying ? network.network : first) + "…",
    detail: own.open
      ? panelOnOwn(own, "Meanwhile its own network is open. ")
      : ["Its own network, " + own.name + ", opens if this takes more than a minute."],
  };
}

// Whether the label maker is on a network it joined, and staying: one it
// was told to forget a moment ago, or to leave for its own, it is on its way
// off.
function staying(network) {
  return network.mode === "join" && network.station === "joined" && network.remembered.includes(network.network);
}

// The sentence that says where the panel is on the label maker's own
// network, after whatever leads up to it.
function panelOnOwn(own, lead = "") {
  return [lead + "A phone on " + own.name + " opens the panel at ", panelLink(own.address), "."];
}

// An address of the panel, for the page to make a link of.
function panelLink(address) {
  return { link: "http://" + address };
}

// What became of the last try at a network, or null when there is none to
// tell: the device drops it as it joins a network or is kept on its own, and
// a reply from the moment in between has it still.
export function failureText(network) {
  const failure = network.failure;
  if (failure === undefined || network.mode !== "join" || network.station === "joined") {
    return null;
  }
  const name = "“" + failure.network + "”";
  if (failure.cause === "not_found") {
    return (
      "No network called " +
      name +
      " was heard. It may be out of reach, or its name may be typed differently. The label maker keeps trying."
    );
  }
  if (failure.cause === "refused") {
    // A handshake that never finished is all the radio knows, and a weak
    // signal ends one as a wrong password does.
    return (
      name +
      " turned the label maker away. That is a wrong password, or a signal too weak to finish joining: " +
      "the two look the same from here." +
      radioReason(failure) +
      " The label maker keeps trying."
    );
  }
  if (failure.cause === "no_address") {
    return (
      name +
      " let the label maker on and gave it no address, as a network does when it is full. " +
      "The label maker keeps trying."
    );
  }
  return "The label maker could not join " + name + "." + radioReason(failure) + " It keeps trying.";
}

// The radio's own reason for a failure, as a sentence to put after another,
// or nothing when it gave none.
function radioReason(failure) {
  if (!Number.isInteger(failure.reason)) {
    return "";
  }
  const named = typeof failure.reason_name === "string" ? ", " + failure.reason_name : "";
  return " The radio gave reason " + failure.reason + named + ".";
}

// The label maker's own network: whether it is open and who is on it, and
// a note on what it does next or how it is joined.
export function ownSummary(network) {
  const own = network.own;
  if (!own.open) {
    const soon = network.mode === "own" || network.remembered.length === 0;
    return {
      state: "Closed",
      note: soon ? "It opens in a moment." : "It opens when the label maker has been without a network for a minute.",
    };
  }
  const state = "Open, " + phonesOn(own.clients);
  if (staying(network)) {
    // The label maker's screen shows the network it joined by now.
    return {
      state: state,
      note: "It closes a minute after the last phone leaves it, now that the label maker is on a network.",
    };
  }
  return {
    state: state,
    note: "Its password, and a code a phone’s camera can join it with, are on the label maker’s screen.",
  };
}

// How many phones are on a network, in words.
function phonesOn(clients) {
  if (clients === 0) {
    return "nobody on it";
  }
  return clients === 1 ? "1 phone on it" : clients + " phones on it";
}

// The networks the label maker remembers, in the order it tries them, each
// with what it is doing with it now, if anything.
export function rememberedRows(network) {
  const tags = { joined: "Joined", joining: "Joining" };
  return network.remembered.map((ssid) => {
    const inUse = network.mode === "join" && network.station !== "off" && ssid === network.network;
    return { ssid: ssid, tag: inUse ? tags[network.station] : null };
  });
}

// How many networks the label maker remembers of those it can, and a note on
// what it does with them, or null when the list says it all.
export function rememberedSummary(network) {
  const count = network.remembered.length;
  let note = null;
  if (count === 0) {
    note = network.mode === "own" ? "None yet." : "None yet. Add the one the label maker is to join.";
  } else if (network.mode === "own") {
    note = "Not tried while the label maker keeps to its own network.";
  } else if (count > 1) {
    note = "Tried in this order. The one joined or added last comes first.";
  }
  return { count: count + " of " + network.max_remembered, note: note };
}

// What the way the label maker is reached comes to, for under the choice of
// one.
export function modeNote(network) {
  return network.mode === "own"
    ? "It joins no network, and keeps its own open. Every label maker has its own, so a phone reaches one of them " +
        "at a time."
    : "It joins a network it remembers, and opens its own when it has found none for a minute.";
}

// What the dialog says before the label maker is told to join a network, or
// to keep to its own: "join" or "own".
export function modeChange(network, mode) {
  const own = network.own;
  if (mode === "own") {
    const leaves = network.station === "joined" ? "leaves " + network.network : "stops looking for a network to join";
    // Beside a network it joins or tries, its own network is on that
    // network's channel, and is opened again on a quiet one of its own.
    const reopened = own.open && network.remembered.length > 0;
    return {
      title: "Use only its own network?",
      summary:
        "The label maker " +
        leaves +
        " and is reached only over its own network, " +
        own.name +
        ". To reach it then, join " +
        own.name +
        " with the password or the code on the label maker’s screen, and open http://" +
        own.address +
        "." +
        (reopened ? " A phone already on " + own.name + " drops off it for a moment." : ""),
      confirm: "Use its own network",
    };
  }
  const [first, ...others] = network.remembered;
  let summary = "The label maker remembers no network yet, so it stays on its own until one is added.";
  if (first !== undefined) {
    summary =
      "The label maker tries " +
      (others.length > 0 ? "the networks it remembers, " + first + " first" : first) +
      ". Its own network stays open while a phone is on it, and this card says where the label maker is " +
      "once it has joined.";
  }
  return { title: "Join a network?", summary: summary, confirm: "Join a network" };
}

// What the dialog says before a network is forgotten.
export function forgetChange(network, ssid) {
  const onIt = network.mode === "join" && network.station === "joined" && network.network === ssid;
  let summary =
    "The label maker forgets " + ssid + ". A network with a password needs it typed in again to be added back.";
  if (onIt) {
    const others = network.remembered.filter((name) => name !== ssid);
    const next = others.length > 0 ? "tries " + others[0] + " instead" : "opens its own network, " + network.own.name;
    summary =
      "The label maker leaves " + ssid + " now, and " + next + ". A phone on " + ssid + " loses touch with this page.";
  }
  return { title: "Forget this network?", summary: summary, confirm: "Forget network" };
}

// What the dialog says before a change to whether the label maker's own
// network offers itself as the way to the internet, or null when the change
// needs no asking: the radio takes it as the network opens, so only an open
// one has to be closed and opened again.
export function routerChange(network) {
  if (!network.own.open) {
    return null;
  }
  return {
    title: "Restart its own network?",
    summary:
      network.own.name +
      " has to restart to change what it tells a phone that joins it. " +
      "A phone on it drops off for a moment, and may have to be put back on it.",
    confirm: "Restart network",
  };
}

// What the page says of a change the label maker never answered, though it
// was sent again and again. It may have been made all the same, and the
// answers lost. adding is set for a network to add, which the Add dialog
// still holds.
export function unansweredChange(adding) {
  return adding
    ? "Couldn’t reach the label maker, so the network may or may not have been added. Adding it again does no harm."
    : "Couldn’t reach the label maker, so the change may or may not have been made. " +
        "Its screen shows how it is reached now.";
}

// The search the Add dialog runs for the networks in reach of the label
// maker: a listen asked for, and the list the device has once it has
// listened.
export class NetworkSearch {
  // The listen under way, or null while none is: whether a try at asking
  // for it has had no answer, and once the device has answered, when it did
  // and how many listens it had done by then. What this one hears is in its
  // list once it has done more.
  #request = null;
  // What api/network/nearby last said, or null.
  #nearby = null;
  // Whether the last listen asked for never reached the device.
  #lost = false;

  // The page is asking the device to listen. Returns the listen, for the
  // page to say what becomes of the asking. One under way is over with.
  asked() {
    this.#request = { unanswered: false, takenAt: null, after: null };
    this.#lost = false;
    return this.#request;
  }

  // Whether the page still waits on this listen, and not on one asked for
  // since. The page stops sending one it does not.
  wanted(request) {
    return this.#request === request;
  }

  // A try at asking for the listen had no answer, and the page is sending
  // it again.
  unanswered(request) {
    if (this.#request === request) {
      request.unanswered = true;
    }
  }

  // The device answered the request, and this is its reply, at a time in
  // milliseconds. One this page cannot read is no answer: without the count
  // in it, what this listen hears cannot be told from what the one before
  // did.
  taken(request, reply, now) {
    if (this.#request !== request) {
      return;
    }
    if (reply === null || typeof reply !== "object" || !Number.isInteger(reply.after)) {
      this.lost(request);
      return;
    }
    request.unanswered = false;
    request.takenAt = now;
    request.after = reply.after;
  }

  // The request did not get through, and the page has given up sending it.
  lost(request) {
    if (this.#request !== request) {
      return;
    }
    this.#request = null;
    this.#lost = true;
  }

  // A reply from api/network/nearby. One this page cannot read leaves the
  // list as it was.
  heard(reply) {
    const known =
      reply !== null &&
      typeof reply === "object" &&
      Number.isInteger(reply.listens) &&
      Array.isArray(reply.networks) &&
      reply.networks.every(
        (network) =>
          network !== null &&
          typeof network === "object" &&
          typeof network.ssid === "string" &&
          Number.isFinite(network.rssi) &&
          typeof network.secured === "boolean",
      );
    if (!known) {
      return;
    }
    this.#nearby = reply;
    if (this.waiting && reply.listens > this.#request.after) {
      this.#request = null;
    }
  }

  // Whether the device has a listen of this page's that it has yet to be
  // heard from, so that its list is worth asking for. Not while the listen
  // is still on its way: no list can end the wait before the device has
  // said how many listens it had done.
  get waiting() {
    return this.#request !== null && this.#request.after !== null;
  }

  // Whether a listen can be asked for: not while one is under way, until
  // that one is long in coming. A listen the device lost to a restart never
  // ends, and asked for twice, the device listens once.
  askable(now) {
    return this.#request === null || this.#slow(now);
  }

  // Whether the device has had the listen under way for long. The time it
  // took to reach the device does not count: that is the link's doing, and
  // the words for a slow listen give the device's reason for one.
  #slow(now) {
    return this.waiting && now - this.#request.takenAt >= SLOW_LISTEN_MS;
  }

  // What the dialog says above the list, or null before the first listen
  // is asked for. A list that is there is said too, in a line that keeps
  // its place when the next listen starts.
  text(now) {
    if (this.#lost) {
      return "Couldn’t ask the label maker to listen.";
    }
    if (this.#request !== null) {
      // The device has not said it has the listen, so the page does not say
      // it is listening.
      if (this.#request.unanswered) {
        return NO_ANSWER_YET;
      }
      return this.#slow(now)
        ? "Still waiting to listen. The label maker can’t while it is trying to join a network, " +
            "so the name may be quicker to type in below."
        : "Listening for networks…";
    }
    if (this.#nearby === null) {
      return null;
    }
    const count = this.#nearby.networks.length;
    if (count === 0) {
      return "No network heard. One that hides its name can be typed in below.";
    }
    return count === 1 ? "1 network." : count + " networks, the strongest first.";
  }

  // The networks in reach, the loudest first as the device lists them: each
  // one's name, how strong its signal is, whether it has a password, and
  // whether the label maker remembers it, and all of that but the name in
  // words.
  rows(network) {
    if (this.#nearby === null) {
      return [];
    }
    return this.#nearby.networks.map((heard) => {
      const signal = signalOf(heard.rssi);
      const remembered = network.remembered.includes(heard.ssid);
      return {
        ssid: heard.ssid,
        signal: signal,
        secured: heard.secured,
        remembered: remembered,
        facts:
          SIGNAL_WORDS[signal] +
          (heard.secured ? ", has a password" : ", no password") +
          (remembered ? ", remembered" : ""),
      };
    });
  }

  // The network of this name as it was heard, or null if none was.
  named(ssid) {
    const heard = this.#nearby === null ? undefined : this.#nearby.networks.find((network) => network.ssid === ssid);
    return heard === undefined ? null : heard;
  }
}

// A signal's strength in dBm as one of three words.
function signalOf(rssi) {
  if (rssi >= SIGNAL.strong) {
    return "strong";
  }
  return rssi >= SIGNAL.fair ? "fair" : "weak";
}

// How long a text is in UTF-8, which is how the device and the radio count
// a name and a password.
function utf8Length(text) {
  return new TextEncoder().encode(text).length;
}

// What the label maker does with a network once it is added, for the top of
// the Add dialog. It tries one only while it has no network.
export function addingIntro(network) {
  if (network.mode === "own") {
    return "The label maker keeps to its own network, and tries a network added here once it is told to join one.";
  }
  if (staying(network)) {
    return (
      "The label maker stays on " + network.network + ", and tries a network added here when it is next without one."
    );
  }
  return "The label maker tries a network as soon as it is added, and the Network card says how that went.";
}

// A network as the Add dialog has it, a name and a password typed in or a
// name picked from the search's list, checked against what the device will
// take:
//
//   full      the words for a label maker that remembers all it can, or null
//   name      a note for under the name, and whether the device would
//             refuse the name
//   password  whether the network wants one ("yes", "no", or "maybe" for a
//             network that was not heard), a note, and whether what is typed
//             is a mistake
//   body      what to send to api/network/remember, or null while there is
//             nothing the device would take
export function networkToAdd(typed, network, search) {
  const heard = search.named(typed.ssid);
  const replaces = network.remembered.includes(typed.ssid);
  const isFull = network.remembered.length >= network.max_remembered;
  const name = nameCheck(typed.ssid, heard, replaces, isFull);
  const password = passwordCheck(typed.password, heard);

  let body = null;
  const nameReady = typed.ssid !== "" && !name.invalid;
  const passwordReady = !password.invalid && !(password.wanted === "yes" && typed.password === "");
  if (nameReady && passwordReady) {
    body = { ssid: typed.ssid };
    if (password.wanted !== "no" && typed.password !== "") {
      body.password = typed.password;
    }
  }
  return {
    full: isFull
      ? "The label maker remembers " +
        network.remembered.length +
        " networks, which is all it can. Forget one first, or add one of them again to change its password."
      : null,
    name: name,
    password: password,
    body: body,
  };
}

// The note for under a network's name, and whether the name is one the
// device would refuse: one too long for a network, or one more than the
// device has room for.
function nameCheck(ssid, heard, replaces, isFull) {
  const length = utf8Length(ssid);
  if (length > NAME_BYTES) {
    return {
      note: "A network’s name is at most " + NAME_BYTES + " bytes long, and this is " + length + ".",
      invalid: true,
    };
  }
  if (isFull && !replaces && ssid !== "") {
    return { note: "The label maker has no room for another network. Forget one first.", invalid: true };
  }
  let note = null;
  if (replaces) {
    note = "The label maker remembers this one. Adding it again replaces its password, and has it tried first.";
  } else if (heard !== null && signalOf(heard.rssi) === "weak") {
    note = "Its signal is weak where the label maker stands, so the link may not hold.";
  }
  return { note: note, invalid: false };
}

// Whether a network wants a password, the note for under the one typed, and
// whether that one is of a length the device would refuse.
function passwordCheck(password, heard) {
  if (heard !== null && !heard.secured) {
    return { wanted: "no", note: "This network has no password.", invalid: false };
  }
  const wanted = heard === null ? "maybe" : "yes";
  const length = utf8Length(password);
  if (password === "") {
    const note = wanted === "maybe" ? "Leave it empty for a network without one." : null;
    return { wanted: wanted, note: note, invalid: false };
  }
  // A byte is a character in a password of plain letters, and the note says
  // characters then. With a letter that takes more than one, the count is
  // not the one in the field, and is said as what it is.
  const plain = length === password.length;
  const limit = (which, bytes) =>
    plain
      ? "A password has " + which + " " + bytes + " characters, and this has " + length + "."
      : "A password is " + which + " " + bytes + " bytes long, and this is " + length + ".";
  if (length < PASSWORD_BYTES.fewest) {
    return { wanted: wanted, note: limit("at least", PASSWORD_BYTES.fewest), invalid: true };
  }
  if (length > PASSWORD_BYTES.most) {
    return { wanted: wanted, note: limit("at most", PASSWORD_BYTES.most), invalid: true };
  }
  return { wanted: wanted, note: null, invalid: false };
}
