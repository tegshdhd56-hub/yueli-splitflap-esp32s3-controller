#ifndef YUELI_HISTORY_BODY_H
#define YUELI_HISTORY_BODY_H
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

// A full 50-entry, 138-cell history fits below this bound, including UTF-8 colors.
static const size_t HISTORY_BODY_LIMIT = 65536;
struct HistoryBody {
  size_t received;
  size_t total;
  bool invalid;
  char text[1];
};

static HistoryBody* createHistoryBody(size_t total) {
  if (!total || total > HISTORY_BODY_LIMIT) return nullptr;
  auto* body = static_cast<HistoryBody*>(malloc(sizeof(HistoryBody) + total));
  if (!body) return nullptr;
  body->received = 0;
  body->total = total;
  body->invalid = false;
  body->text[0] = 0;
  return body;
}

static void appendHistoryBody(HistoryBody* body, const void* data, size_t length, size_t index, size_t total) {
  if (!body || body->invalid) return;
  if (total != body->total || index != body->received || length > body->total - body->received ||
      memchr(data, 0, length)) {
    body->invalid = true;
    return;
  }
  memcpy(body->text + index, data, length);
  body->received += length;
  body->text[body->received] = 0;
}
#endif
