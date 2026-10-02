#pragma once

// The panel as the firmware carries it, and what a browser is answered when
// it asks for one of its files.
//
// The files are part of the firmware. scripts/embed_panel.py makes them from
// data/ at each build, as constant data, and the webserver sends them from
// where the flash is mapped: they take none of the memory the connections
// are made of. They were files on a SPIFFS partition before, copied into RAM
// at each start so that a send did not have to read flash. The copy took
// most of the memory the chip has free, and the firmware and its panel were
// two uploads that could be out of step.
//
// Pure: no Arduino, no webserver, so the host test suite can reach it. The
// webserver's side of it is PanelHandler in Network.cpp, which has no rules
// of its own.

#include <stddef.h>
#include <stdint.h>
#include <string.h>

// One file of the panel.
struct PanelFile {
  // The address the page asks for it under, without the query: "/script.js".
  const char* path;
  // Its Content-Type.
  const char* type;
  // Names these bytes, quotes and all, as the ETag header carries it.
  const char* etag;
  const uint8_t* bytes;
  size_t length;
  // The bytes are the file gzipped, which the answer has to say.
  bool gzip;
};

// The whole panel. The version names its contents: the page gives it to
// every file it asks for, as ?v=, so a panel that changes asks under new
// addresses.
struct PanelFiles {
  const char* version;
  const PanelFile* files;
  size_t count;
};

// The answer to a GET for one of the panel's files.
struct PanelReply {
  // NULL when the panel has no file at that address, and then the rest says
  // nothing.
  const PanelFile* file;
  // The browser holds these bytes already, so the answer is a 304 without
  // them.
  bool unchanged;
  // How long the browser may keep the file, as the Cache-Control header
  // carries it.
  const char* cacheControl;
};

/**
 * @brief The file a GET for `path` answers with, or NULL for none.
 *
 * The address of the machine itself is the page. An address is matched
 * whole.
 */
inline const PanelFile* panelFile(const PanelFiles& panel, const char* path) {
  if (strcmp(path, "/") == 0) {
    path = "/index.html";
  }
  for (size_t i = 0; i < panel.count; i++) {
    if (strcmp(panel.files[i].path, path) == 0) {
      return &panel.files[i];
    }
  }
  return NULL;
}

/**
 * @brief What a GET for `path` is answered with.
 *
 * @param version the v of the request's query, empty for none.
 * @param held the request's If-None-Match, empty for none: the tag of the
 *        copy the browser holds.
 *
 * A file may be kept for good only under an address that changes when the
 * file does, which is one with this panel's version in it. The page never
 * has such an address: it is what names the others. Under any other address
 * the browser is to ask again each time, which costs one short answer while
 * its copy is still the one.
 */
inline PanelReply panelReply(const PanelFiles& panel, const char* path,
                             const char* version, const char* held) {
  PanelReply reply;
  reply.file = panelFile(panel, path);
  reply.unchanged = false;
  reply.cacheControl = "no-cache";
  if (reply.file == NULL) {
    return reply;
  }

  if (strcmp(reply.file->path, "/index.html") != 0 &&
      strcmp(version, panel.version) == 0) {
    reply.cacheControl = "public, max-age=31536000, immutable";
  }

  // The tag as it was given out, with any space a browser put around it.
  while (*held == ' ' || *held == '\t') {
    held++;
  }
  const size_t tagLength = strlen(reply.file->etag);
  if (strncmp(held, reply.file->etag, tagLength) == 0) {
    held += tagLength;
    while (*held == ' ' || *held == '\t') {
      held++;
    }
    reply.unchanged = *held == '\0';
  }
  return reply;
}

/**
 * @brief How many bytes of the firmware the panel's files are.
 */
inline size_t panelBytes(const PanelFiles& panel) {
  size_t total = 0;
  for (size_t i = 0; i < panel.count; i++) {
    total += panel.files[i].length;
  }
  return total;
}
