// The way out of Setup once a roll has been loaded from it. A new roll is
// the one thing done in Setup that leaves nothing more to do there: what is
// wanted next is a label, and that is on the print view. So the page goes
// back there by itself once the roll is in, and stays in Setup when it is
// not, where Unload and Load are. Which of the two it is, is decided here,
// where node tests it in test/panel/setup.test.js. Nothing here touches the
// page.
//
// The label maker cannot see the tape. A load that ran to its end is a roll
// that is in, as far as it can tell, and a jam is for whoever stands at the
// machine to see: they stop the load, or open Setup again and unload.
//
// What the page knows as a status comes in, as RollLoad.statusArrived()
// takes it:
//
//   status   the status
//   running  the command the page takes the device to be running, or about
//            to, or null when it takes it to be idle
//   setup    whether Setup is the view on screen
//   problem  whether the page is showing a problem
//   unsaved  whether Setup holds calibration that has not been saved

import { lastStop } from "./stops.js";

// The load of a new roll this page asked for from Setup, from when the
// device takes it until the device is idle again.
export class RollLoad {
  // The id the load went under, or null while there is none to wait on.
  #id = null;

  // The device has taken a load this page sent, as the Sending sent. Not
  // waited on when a stop was sent after it, which was tapped before the
  // device had answered the load: see stopAsked().
  started(sent) {
    this.#id = sent.followedByStop() ? null : sent.id;
  }

  // This page has asked for a stop. Whoever tapped it saw something go
  // wrong, and what they want next is in Setup, even when the stop comes
  // too late and the load runs to its end.
  stopAsked() {
    this.#id = null;
  }

  // A status has come in. Returns whether the page is to go back to the
  // print view now.
  //
  // That is decided once, at the first status with nothing running, and the
  // load is waited on no longer whichever way it went. The roll is in when
  // that status still names the load as the last command the device took,
  // and holds no record of a stop. A device that restarted partway names no
  // command, and one that has taken another since says nothing of how the
  // load went.
  //
  // And the page goes back only when that loses nothing. Leaving Setup
  // discards calibration that has not been saved, and takes down a problem
  // that may not have been read, so with either the page stays. So it does
  // when Setup has been left already.
  statusArrived(page) {
    if (this.#id === null || page.running !== null) {
      return false;
    }
    const id = this.#id;
    this.#id = null;
    return (
      page.status.last_command_id === id &&
      lastStop(page.status) === null &&
      page.setup &&
      !page.problem &&
      !page.unsaved
    );
  }
}
