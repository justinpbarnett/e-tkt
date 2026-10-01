import { readFileSync } from "node:fs";

import { readCapabilities } from "../../data/status.js";

// What the label maker says it will accept, read the way the panel reads it.
// capabilities.json is the reply the firmware serves, word for word, and
// src/simulator/test_server.py holds it to that, so these tests cannot go on
// passing against a device that no longer exists.
export function labelMaker() {
  return readCapabilities(capabilitiesReply());
}

// The reply itself, a fresh copy each time, for a test to take something
// out of.
export function capabilitiesReply() {
  return JSON.parse(readFileSync(new URL("capabilities.json", import.meta.url), "utf8"));
}

// What the page knows of the command running, as script.js hands it to
// activity() and stopOffer() in status.js and to timeLeftText() in
// timing.js: a cut the device has reported, but for what overrides says.
export function running(overrides) {
  return {
    command: "cut",
    status: { busy: true, command: "cut" },
    stop: null,
    sentCopies: null,
    offline: false,
    unanswered: false,
    ...overrides,
  };
}
