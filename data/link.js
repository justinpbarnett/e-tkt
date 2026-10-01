// The link to the device, which can be slow or lose what is sent over it: a
// basement, or a hall with a thousand phones on the same Wi-Fi. What the page
// does about that is decided here, where node tests it in
// test/panel/link.test.js. Nothing here touches the page.
//
// A reply can be lost after the device has done what it was asked. So every
// command and every stop goes out under an id, which the device remembers
// with what it answered: one sent again under its id is told again what it
// was told, and nothing runs twice.

// How long the page waits, once a try has had no answer, before it sends the
// same thing again.
export const RETRY_WAIT_MS = 1000;

// How long after the first try the page stops starting new ones, and says it
// cannot reach the device. Someone is standing at the machine waiting, and
// past this they are better told than kept.
export const GIVE_UP_AFTER_MS = 20000;

// Random bytes in an id. The device keeps its newest few, and among those
// eight bytes do not come up twice.
const ID_BYTES = 8;

// The id a command or a stop is sent under, and sent again under. The device
// keeps one of up to 36 bytes.
export function newCommandId() {
  // Not crypto.randomUUID(): the device serves the page over plain HTTP, and
  // a browser offers that only to a page served securely.
  const bytes = crypto.getRandomValues(new Uint8Array(ID_BYTES));
  return Array.from(bytes, (byte) => byte.toString(16).padStart(2, "0")).join("");
}

// Sends something to the device until the device answers it, and returns the
// answer, whatever it says: a refusal is an answer too, and is not sent
// again.
//
//   attempt     sends it once, under the same id every time. Resolves with
//               the device's answer, and rejects when none came
//   wanted      whether it is still worth sending. A try that has gone out
//               is not called back, but no other follows it
//   unanswered  told each time a try has had no answer and another is to
//               follow, so the page can say so
//   now         the time, in milliseconds
//   pause       resolves once that many milliseconds have passed
//
// Rejects with what the last try came to once it is no longer wanted, or the
// device has not answered for GIVE_UP_AFTER_MS.
export async function deliver({ attempt, wanted, unanswered, now, pause }) {
  const startedAt = now();
  for (;;) {
    try {
      return await attempt();
    } catch (error) {
      if (!wanted() || now() - startedAt >= GIVE_UP_AFTER_MS) {
        throw error;
      }
      unanswered();
      await pause(RETRY_WAIT_MS);
      if (!wanted()) {
        throw error;
      }
    }
  }
}
