#include "WriteDebugLog.h"

#include <HalStorage.h>
#include <WString.h>

#include <cstring>

// Cap log file size so an unbricked device can't grow it forever through a
// long chain of boots. Each boot appends a couple of lines.
constexpr size_t kDebugLogMaxBytes = 4096;

void writeDebugLog(const char* line) {
  if (!Storage.ready() || !line || *line == '\0') return;

  // Read existing content (bounded) so we append rather than overwrite.
  std::string existing;
  HalFile head = Storage.open("/debug.log");
  if (head) {
    const size_t sz = head.size();
    if (sz <= kDebugLogMaxBytes) {
      existing.resize(sz);
      if (sz > 0 && head.read(existing.data(), sz) == static_cast<int>(sz)) {
        // keep as-is
      } else {
        existing.clear();
      }
    }
    head.close();
  }

  if (existing.size() >= kDebugLogMaxBytes - 256) {
    // Rotate: drop the first half.
    existing = existing.substr(existing.size() / 2);
  }

  existing += line;
  existing += '\n';

  HalFile out;
  if (!Storage.openFileForWrite("DBG", "/debug.log", out)) return;
  out.write(existing.data(), existing.size());
  out.close();
}