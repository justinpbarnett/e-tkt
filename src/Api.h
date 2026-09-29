#pragma once

#include <Arduino.h>

#include <map>

#include "ETKT.h"
#include "Logger.h"

/**
 * @brief The HTTP methods a route can answer to. An adapter reads any other
 * method as OTHER, which no route answers to.
 */
enum class Method { GET, POST, OTHER };

/**
 * @brief One request to a path under /api/, as the webserver received it.
 */
struct Request {
  Method method = Method::GET;

  // The path alone, "/api/status", without the query string.
  String path;

  // The query string's parameters, decoded. Only /api/stop reads one.
  std::map<String, String> query;

  // The Content-Type header, or empty if the request had none.
  String contentType;

  // The body. An adapter can cut a long body off after Api::MAX_BODY_BYTES
  // + 1 bytes rather than hold all of it, because a body that long is
  // refused either way.
  String body;
};

/**
 * @brief The answer to a Request, for the webserver to send as it is.
 */
struct Reply {
  int code;

  // "application/json" for every reply but the log's, which is plain text.
  const char* contentType;

  String body;

  // For a 405, the method the path does answer to, which HTTP asks the
  // reply to name in an Allow header. NULL for every other reply.
  const char* allow;
};

/**
 * @brief Everything the device answers under /api/: the routes, the checks
 * on what a request sends, the words of every refusal, and the JSON of
 * every reply.
 *
 * The webserver is an adapter in front of this. It turns each request under
 * /api/ into a Request, and sends back the Reply as it comes. So a reply can
 * be tested on a host, and the webserver has no rules of its own to drift
 * from the ones tested here.
 */
class Api {
 private:
  ETKT* etkt;
  Logger* logger;

  // Which route a request is for, and that route's answer to it.
  Reply route(const Request& request);

  // One per route, each answering a request the route's method and path have
  // already matched.
  Reply command(const CommandSpec* spec, const Request& request);
  Reply status();
  Reply stop(const Request& request);
  Reply capabilities();
  Reply log();

 public:
  // The longest body a command may send. Several times what the longest label
  // takes, even written as escapes, and small enough to read into RAM whole.
  static const size_t MAX_BODY_BYTES = 2048;

  Api(ETKT* etkt, Logger* logger);

  /**
   * @brief Answers one request.
   *
   * Safe to call from the webserver's task while the command loop runs:
   * everything it asks the job runner goes through the job runner's lock.
   */
  Reply handle(const Request& request);
};
