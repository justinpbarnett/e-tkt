#pragma once

#include <Arduino.h>

#include <map>
#include <mutex>

#include "ArduinoJson.h"
#include "ETKT.h"
#include "LinkSupervisor.h"
#include "Logger.h"
#include "NetworkSettings.h"

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

  // The query string's parameters, decoded: the id a command or a stop is
  // sent under, what a stop is to wait for, and the command it is for.
  std::map<String, String> query;

  // The Content-Type header, or empty if the request had none.
  String contentType;

  // The body. An adapter can cut a long body off after Api::MAX_BODY_BYTES
  // + 1 bytes rather than hold all of it, because a body that long is
  // refused either way. It can hold a network's password, so no adapter
  // logs it.
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
  LinkSupervisor* linkSupervisor;
  NetworkSettings* networkSettings;
  Logger* logger;

  // How many ids the device remembers: of commands, of stops, and of what
  // changes the network. A panel sends one of those at a time and gives up
  // on it within seconds, so only the newest few can come again; eight
  // leaves room for several panels at once.
  static const size_t REMEMBERED_IDS = 8;

  // A request sent under an id, and what the device answered it.
  struct Answer {
    String id;
    Reply reply;
  };

  // The newest answers, the oldest written over first. Kept in RAM alone, so
  // a restart forgets them: a command sent again across one runs again,
  // which takes a reply lost and a restart within the few seconds a panel
  // keeps trying.
  Answer answers[REMEMBERED_IDS] = {};
  size_t nextAnswer = 0;
  std::mutex answersLock;

  // What the request sent under this id was answered, written through.
  // False if the device remembers none: the id is new, or older than the
  // ones it keeps, or the request was sent under none.
  bool recall(const String& id, Reply* reply);

  // Keeps the answer to the request sent under this id, and nothing for a
  // request sent under none.
  void remember(const String& id, const Reply& reply);

  // Refuses, from here on, the command sent under this id that has not
  // arrived, and says whether there was one to refuse.
  bool keepFromStarting(const String& commandId);

  // Which answers to a request sent under an id are kept: the ones that
  // took it, or every one, a refusal too.
  enum class Keep { ACCEPTED, EVERY_ANSWER };

  // What a request sent under an id was answered the first time, or what
  // answerNow() says to it, kept for when it is sent again. answerNow() is
  // handed the id, which is empty for a request sent under none.
  template <typename AnswerNow>
  Reply once(const Request& request, Keep keep, AnswerNow answerNow);

  // What changes how the machine is reached. Each is handed the body of its
  // request, and makes the change, or writes the reply that refuses it and
  // returns false.
  typedef bool (Api::*NetworkChange)(const JsonObjectConst& body,
                                     Reply* refused);
  bool setNetworkMode(const JsonObjectConst& body, Reply* refused);
  bool rememberNetwork(const JsonObjectConst& body, Reply* refused);
  bool forgetNetwork(const JsonObjectConst& body, Reply* refused);
  bool offerRouter(const JsonObjectConst& body, Reply* refused);

  // Makes one of them once however often it is sent under its id, and
  // answers with the network as it is afterwards.
  Reply changeNetwork(const Request& request, NetworkChange change);

  // Which route a request is for, and that route's answer to it.
  Reply route(const Request& request);

  // One per route, each answering a request the route's method and path have
  // already matched. The routes that are not commands are a table in route(),
  // so each takes the request whether it reads it or not.
  Reply command(const CommandSpec* spec, const Request& request);
  Reply submit(const CommandSpec* spec, const Request& request,
               const String& id);
  Reply estimate(const CommandSpec* spec, const Request& request);
  Reply status(const Request& request);
  Reply stop(const Request& request);
  Reply stopRunning(const Request& request);
  Reply capabilities(const Request& request);
  Reply log(const Request& request);
  Reply network(const Request& request);
  Reply networksNearby(const Request& request);
  Reply listenForNetworks(const Request& request);

 public:
  // The longest body a request may send. Several times what the longest label
  // takes, even written as escapes, and small enough to read into RAM whole.
  static const size_t MAX_BODY_BYTES = 2048;

  Api(ETKT* etkt, LinkSupervisor* linkSupervisor,
      NetworkSettings* networkSettings, Logger* logger);

  /**
   * @brief Answers one request.
   *
   * Safe to call from the webserver's task while the command loop runs and
   * the link is kept up: everything it asks the job runner goes through the
   * job runner's lock, and the link and the network settings each have
   * their own.
   */
  Reply handle(const Request& request);
};
