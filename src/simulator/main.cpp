// The simulator's device: this label maker's own job runner and Api, built
// for the host on the machine in test/fakes, answering requests on stdin.
//
// src/simulator/server.py serves the panel, and relays each request under
// /api/ here as one line of JSON, and each reply back as one line of JSON:
//
//   in   {"method": "POST", "path": "/api/tag", "query": {},
//         "contentType": "application/json", "body": "{\"tag\": \"HI\"}"}
//   out  {"code": 200, "contentType": "application/json",
//         "body": "{\"result\":\"success\"}", "allow": null}
//
// A line is read the way ApiHandler in Network.cpp reads a request off the
// webserver on the device. So every route, refusal, status and log line the
// panel gets from the simulator is the firmware's own, and there is no second
// copy of any of it to drift.
//
// Time. The stubs' delay() does not sleep: it moves a virtual clock on, as
// yield() does by what a turn of a motor's loop costs, and the fake steppers
// step on AccelStepper's schedule against that clock. Here each millisecond
// the virtual clock reaches also waits for the wall clock, sped up --speed
// times, to catch up with it. So a label takes as long as it does on the
// machine, or a tenth of that at --speed 10, wheel and feed and all, and a
// request that arrives while it prints is answered in between, where the
// board's webserver task would answer it. A host that cannot keep up with
// --speed runs the machine slower than asked, and never bends its time.
//
// Reboots. A save ends in ESP.restart(), which the stubs count and return
// from. The simulator then builds the machine again, and keeps the flash,
// which is where the save put the calibration, and where the networks the
// machine remembers are.
//
// The network. The link supervisor and the network settings are the
// firmware's own too, on the radio in test/fakes, which has an air where the
// board has an antenna: a list of networks, and how each one answers a machine
// that tries it. So the panel's network card can be tried against whatever a
// network does, and the host joins and opens nothing. fillAir() below names
// the networks this machine hears, each with what it is there for, and
// AIR_PASSWORD is the password of the ones that have one. What the air cannot
// show is what only a radio does: a listen takes no time here, and nobody
// joins the machine's own network.
//
// server.py builds and runs it. By hand:
//   pio run -e simulator
//   .pio/build/simulator/program --speed 10

#include <ArduinoJson.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <unistd.h>

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include "Api.h"
#include "Arduino.h"
#include "Configuration.h"
#include "FakeRadio.h"
#include "HostMachine.h"
#include "LinkSupervisor.h"

// What the heap reports free. The panel does not read either number. They
// are what a machine reports between jobs, so the status looks like one.
static const size_t HEAP_FREE_BYTES = 200000;
static const size_t HEAP_LARGEST_FREE_BLOCK_BYTES = 110000;

// --- The air ---

// The password of every network in the air that has one.
static const char AIR_PASSWORD[] = "labelmaker";

static FakeNetwork* broadcast(FakeRadio* radio, const char* ssid,
                              const char* password, int channel, int rssi) {
  FakeNetwork* network = radio->add(ssid, password);
  network->channel = channel;
  network->rssi = rssi;
  return network;
}

/**
 * @brief Puts in the air the networks the machine hears, each one there for
 * something the panel's Network card has to deal with.
 */
static void fillAir(FakeRadio* radio) {
  // The board listens beside its job runner. Here a listen that took time
  // would take it out of a label.
  radio->surveyMs = 0;

  // The one to join: it lets the machine in at the first try.
  broadcast(radio, "Workshop", AIR_PASSWORD, 6, -48);
  // One that hides its name, which the firmware leaves out of what it lists.
  broadcast(radio, "", AIR_PASSWORD, 6, -55);
  // Another label maker's own network.
  broadcast(radio, "E-TKT-51B2", AIR_PASSWORD, 1, -58)->address = "192.168.4.2";
  // No password, and two access points under one name. The fake radio joins
  // the one put in the air last, so that is the louder one, as it is for a
  // real radio.
  broadcast(radio, "Church Guest", "", 11, -71)->address = "10.20.4.87";
  broadcast(radio, "Church Guest", "", 1, -63)->address = "10.20.4.87";
  // A name with markup in it, which a page has to show as it is.
  broadcast(radio, "<b>Cafe</b> & \"Friends\"", AIR_PASSWORD, 11, -67)
      ->address = "192.168.0.14";
  // No password, and no address to give.
  broadcast(radio, "Full House", "", 6, -70)->givesAddress = false;
  // As long as a name gets.
  broadcast(radio, "The Longest Network Name Allowed", AIR_PASSWORD, 1, -77)
      ->address = "172.16.30.5";
  // A name that is not all ASCII.
  broadcast(radio, "Jugendcafé 🎸", AIR_PASSWORD, 6, -81)->address =
      "192.168.178.61";
  // Weak, and it turns the machine away twice before it lets it in.
  FakeNetwork* farCorner =
      broadcast(radio, "Far Corner", AIR_PASSWORD, 11, -86);
  farCorner->address = "192.168.7.23";
  farCorner->refusals = 2;
}

// --- Requests ---

/**
 * @brief The request lines on stdin, read as they arrive.
 *
 * With read() and select() rather than std::cin, whose buffer can hold a
 * line that select() cannot see, so a wait for the next line could sleep
 * past one already read.
 */
class Requests {
 private:
  std::string buffer;
  bool ended = false;

 public:
  /**
   * @brief Takes the next whole line into `line`, waiting at most
   * `timeoutUs` for it, or for as long as it takes when `timeoutUs` is
   * negative. Returns false if no line came in time, or at the end of input.
   */
  bool next(std::string& line, long timeoutUs);

  /** @brief Whether the relay has closed its end, with no line left. */
  bool over() const { return this->ended; }
};

bool Requests::next(std::string& line, long timeoutUs) {
  const std::chrono::steady_clock::time_point deadline =
      std::chrono::steady_clock::now() + std::chrono::microseconds(timeoutUs);
  while (true) {
    const size_t newline = this->buffer.find('\n');
    if (newline != std::string::npos) {
      line.assign(this->buffer, 0, newline);
      this->buffer.erase(0, newline + 1);
      return true;
    }
    if (this->ended) {
      return false;
    }

    fd_set readable;
    FD_ZERO(&readable);
    FD_SET(STDIN_FILENO, &readable);
    timeval wait;
    timeval* waitFor = NULL;
    if (timeoutUs >= 0) {
      const long left = std::max(
          0L, (long)std::chrono::duration_cast<std::chrono::microseconds>(
                  deadline - std::chrono::steady_clock::now())
                  .count());
      wait.tv_sec = left / 1000000;
      wait.tv_usec = (int)(left % 1000000);
      waitFor = &wait;
    }
    const int ready = select(STDIN_FILENO + 1, &readable, NULL, NULL, waitFor);
    if (ready < 0 && errno == EINTR) {
      continue;
    }
    if (ready == 0) {
      return false;
    }

    char chunk[4096];
    const ssize_t got =
        ready < 0 ? -1 : read(STDIN_FILENO, chunk, sizeof(chunk));
    if (got < 0 && errno == EINTR) {
      continue;
    }
    if (got <= 0) {
      // A last line without its newline is not one the relay sent whole, so
      // it goes unanswered.
      this->ended = true;
      return false;
    }
    this->buffer.append(chunk, (size_t)got);
  }
}

/**
 * @brief Reads one request line into `request`, as ApiHandler reads a
 * request off the webserver. False if the line is not a request, which only
 * a broken relay sends.
 *
 * The line is parsed where it is, so the strings in the document point into
 * it rather than being copied, and it is changed in the process.
 */
static bool readRequest(std::string& line, Request& request) {
  // Every member of the query takes at least six characters of the line,
  // "a":"", so this many slots hold any query a line can carry.
  DynamicJsonDocument doc(JSON_OBJECT_SIZE(5) +
                          JSON_OBJECT_SIZE(line.size() / 6 + 1));
  if (deserializeJson(doc, &line[0]) != DeserializationError::Ok) {
    return false;
  }
  JsonObjectConst asked = doc.as<JsonObjectConst>();
  if (asked.isNull()) {
    return false;
  }

  const char* method = asked["method"] | "";
  if (strcmp(method, "GET") == 0) {
    request.method = Method::GET;
  } else if (strcmp(method, "POST") == 0) {
    request.method = Method::POST;
  } else {
    request.method = Method::OTHER;
  }
  request.path = asked["path"] | "";
  for (JsonPairConst param : asked["query"].as<JsonObjectConst>()) {
    request.query[String(param.key().c_str())] =
        String(param.value().as<const char*>());
  }
  request.contentType = asked["contentType"] | "";

  // Cut where the device cuts it: past Api::MAX_BODY_BYTES + 1 bytes, the
  // most ApiHandler keeps, or at a NUL, where the device's copy ends as a
  // string.
  const char* body = asked["body"] | "";
  request.body =
      String(std::string(body, strnlen(body, Api::MAX_BODY_BYTES + 1)));
  return true;
}

/**
 * @brief Writes one reply line to stdout, and sends it on its way.
 */
static void writeReply(const Reply& reply) {
  // Every string is stored as a pointer, so four members are all the
  // document holds, however long the body.
  StaticJsonDocument<JSON_OBJECT_SIZE(4)> doc;
  doc["code"] = reply.code;
  doc["contentType"] = reply.contentType;
  doc["body"] = reply.body.c_str();
  // NULL, for every reply but a 405, is null.
  doc["allow"] = reply.allow;
  std::string line;
  serializeJson(doc, line);
  line += '\n';
  fwrite(line.data(), 1, line.size(), stdout);
  fflush(stdout);
}

// --- Simulator ---

/**
 * @brief One machine, kept in time with the wall clock, answering requests.
 */
class Simulator {
 private:
  // How many times faster than the machine this one runs.
  const double speed;

  // The wall-clock time the machine last booted at. The virtual clock counts
  // from 0 there.
  std::chrono::steady_clock::time_point wallStart;

  Requests requests;

  // The machine, with how it is reached and the Api in front of both. NULL
  // while it boots: requests that arrive meanwhile wait on stdin, as on the
  // board, where no webserver is up yet to take them.
  HostMachine* machine = NULL;

  // When the link was last looked at, by the machine's clock.
  unsigned long linkSteppedMs = 0;

  std::mt19937 generator;

  /**
   * @brief How far the virtual clock ought to have got by now, in the
   * machine's milliseconds since it booted.
   */
  double dueMs() const {
    const std::chrono::duration<double, std::milli> real =
        std::chrono::steady_clock::now() - this->wallStart;
    return real.count() * this->speed;
  }

  /**
   * @brief Moves the virtual clock up to the wall clock, when it is behind.
   *
   * Between jobs only. Nothing waits while the machine is idle, so the
   * virtual clock stands still while the device's would run on. In a job
   * the clock is the machine's own, and pace() only ever holds it back.
   */
  void keepTime() {
    const unsigned long due = (unsigned long)this->dueMs();
    if (due > stubClockMs()) {
      stubClockMs() = due;
    }
  }

  /**
   * @brief Looks at the link, once WIFI_STEP_MS of the machine's time have
   * gone by since the last look.
   *
   * On the board the link supervisor has a task of its own, which looks
   * every WIFI_STEP_MS whatever the job runner is doing. Here there is one
   * thread, so the look is taken between two of the machine's milliseconds
   * while it works, and between two requests while it is idle. A look that
   * comes late is not made up for: the supervisor goes by the time that has
   * passed, and not by how often it was asked.
   */
  void keepLink() {
    if (this->machine == NULL ||
        stubClockMs() - this->linkSteppedMs < WIFI_STEP_MS) {
      return;
    }
    this->linkSteppedMs = stubClockMs();
    this->machine->linkSupervisor.step();
  }

  /**
   * @brief How long the wall clock takes to bring the machine to its next
   * look at the link, in microseconds.
   */
  long untilLinkStepUs() const {
    const double leftMs =
        (double)(this->linkSteppedMs + WIFI_STEP_MS) - this->dueMs();
    return leftMs > 0 ? (long)std::ceil(leftMs * 1000.0 / this->speed) : 0;
  }

  /**
   * @brief Called each time the virtual clock reaches a new millisecond,
   * waiting or moving a motor. Holds the machine until the wall clock
   * catches up, and answers requests meanwhile.
   *
   * A host that wakes late, or cannot keep up with --speed, leaves the
   * machine behind the wall clock, and it waits again once it is ahead. Its
   * clock is never moved on to make up the difference, which would bend
   * every move under way, so a label takes the machine's own time. A
   * request is answered all the same, as the board's webserver task
   * answers one whatever the job runner is doing, and the link is looked
   * at, as its task looks at it.
   */
  void pace() {
    if (stubRestarts() > 0) {
      // Past the reboot the save asked for. The board never runs this far,
      // so it takes no time, and what it says goes unheard.
      stubSerialLines().clear();
      this->forget();
      return;
    }
    while (true) {
      const double aheadMs = (double)stubClockMs() - this->dueMs();
      const long waitUs =
          aheadMs > 0 ? (long)std::ceil(aheadMs * 1000.0 / this->speed) : 0;
      if (this->machine == NULL) {
        if (waitUs == 0) {
          break;
        }
        std::this_thread::sleep_for(std::chrono::microseconds(waitUs));
        continue;
      }
      std::string line;
      if (this->requests.next(line, waitUs)) {
        this->answer(line);
      } else if (this->requests.over()) {
        // The relay has gone, and there is nobody left to answer. Mid-job,
        // so nothing is put away: this is the machine's plug coming out.
        std::_Exit(0);
      } else if (waitUs == 0) {
        break;
      }
    }
    this->keepLink();
    this->forget();
    this->drainSerial();
  }

  /**
   * @brief Answers one request line.
   */
  void answer(std::string& line) {
    Request request;
    if (!readRequest(line, request)) {
      Reply refused = {500, "application/json",
                       "{\"error\":\"The simulator could not read the "
                       "request\"}",
                       NULL};
      writeReply(refused);
      return;
    }
    writeReply(this->machine->api.handle(request));
  }

  /**
   * @brief Whether the machine is busy: a command is waiting for the job
   * runner to take it, or running.
   */
  bool busy() {
    return this->machine->etkt.createStatus().currentCommand != Command::IDLE;
  }

  /**
   * @brief Drops what the fakes and the stubs recorded, which the simulator
   * never reads. See HostMachine::forget().
   */
  void forget() {
    if (this->machine != NULL) {
      this->machine->forget();
    }
    stubAnalogWrites().clear();
    stubDigitalWrites().clear();
    stubTones().clear();
  }

  /**
   * @brief Prints what the machine sent down its serial port, to stderr.
   */
  void drainSerial() {
    for (size_t i = 0; i < stubSerialLines().size(); i++) {
      fprintf(stderr, "%s\n", stubSerialLines()[i].c_str());
    }
    stubSerialLines().clear();
  }

  /**
   * @brief Builds the machine, and boots it, from what the flash holds.
   *
   * At start, and again after every ESP.restart(). What a reboot clears is
   * cleared: the clock, the count of reboots asked for, and everything that
   * lived in RAM. The flash stays.
   */
  void boot() {
    // The old magnet goes with the old machine, and the new one installs its
    // own.
    stubAnalogRead() = nullptr;
    delete this->machine;
    this->machine = NULL;
    stubClockMs() = 0;
    stubClockUs() = 0;
    stubRestarts() = 0;
    this->wallStart = std::chrono::steady_clock::now();

    this->machine = new HostMachine();
    fillAir(&this->machine->radio);
    // The first look is the one the board takes as its webserver starts.
    this->machine->linkSupervisor.step();
    this->linkSteppedMs = stubClockMs();
    this->forget();
    this->drainSerial();
  }

  /**
   * @brief After each job, reboots the machine if the job asked it to.
   */
  void afterJob() {
    if (stubRestarts() > 0) {
      // What the old machine said past its restart, the board never says.
      stubSerialLines().clear();
      this->boot();
      return;
    }
    this->forget();
    this->drainSerial();
  }

 public:
  explicit Simulator(double speed)
      : speed(speed), generator(std::random_device()()) {
    stubReset();
    stubHeap().freeBytes = HEAP_FREE_BYTES;
    stubHeap().largestFreeBlockBytes = HEAP_LARGEST_FREE_BLOCK_BYTES;
    stubRandom() = [this](long low, long high) {
      if (high <= low) {
        return low;
      }
      std::uniform_int_distribution<long> between(low, high - 1);
      return between(this->generator);
    };
    stubAfterTick() = [this] { this->pace(); };
  }

  /**
   * @brief Boots the machine, and runs it until stdin closes.
   */
  int run() {
    this->boot();
    std::string line;
    while (true) {
      // loop() waits on the wall clock for a command when none is submitted,
      // so it is only called with one.
      while (this->busy()) {
        this->machine->etkt.loop();
        this->afterJob();
      }
      // Idle, it waits for a request, and for no longer than the link can
      // go without a look.
      const bool asked = this->requests.next(line, this->untilLinkStepUs());
      if (!asked && this->requests.over()) {
        return 0;
      }
      this->keepTime();
      if (asked) {
        this->answer(line);
      }
      this->keepLink();
      this->forget();
      this->drainSerial();
    }
  }
};

static int usage() {
  fprintf(stderr,
          "usage: program [--speed N]\n"
          "  --speed N  how many times faster than the machine to run, above "
          "0 (default 1)\n");
  return 2;
}

int main(int argc, char** argv) {
  double speed = 1;
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--speed") != 0 || i + 1 >= argc) {
      return usage();
    }
    char* end = NULL;
    speed = strtod(argv[++i], &end);
    if (end == argv[i] || *end != '\0' || !std::isfinite(speed) ||
        !(speed > 0)) {
      return usage();
    }
  }
  Simulator simulator(speed);
  return simulator.run();
}
