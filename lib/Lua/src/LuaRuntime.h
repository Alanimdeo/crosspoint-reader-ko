#pragma once

#include <cstddef>
#include <string>

struct lua_State;

// Low-level RAII wrapper around a single Lua 5.4 state, embedded in the
// firmware. The Lua VM exists only for the lifetime of this object: construct
// it when an activity enters, destroy it on activity exit so all VM memory is
// returned to the heap.
//
// Only the stdlib modules actually needed by mini-apps are opened (base,
// math, string, table, utf8, coroutine). io/os/debug/package are not compiled
// into the image at all (see lib/Lua/library.json srcFilter), which keeps
// flash and RAM footprint small and prevents scripts from touching the FS.
class LuaRuntime {
 public:
  explicit LuaRuntime(size_t maxScriptBytes = 32768);
  ~LuaRuntime();

  LuaRuntime(const LuaRuntime&) = delete;
  LuaRuntime& operator=(const LuaRuntime&) = delete;

  // Load and run a script file from SD. Returns true on success. On failure
  // returns false and fills error with a human-readable message.
  bool runFile(const char* path, std::string& error);

  // Load a script file from SD and compile it WITHOUT running it. The compiled
  // function is left on the Lua stack (top). Returns true on success. Caller
  // must call lua_pcall() to actually execute it (e.g. inside a render task so
  // drawing happens under the render lock). On failure returns false and fills
  // error; the stack is unchanged on error.
  bool loadFileForLateRun(const char* path, std::string& error);

  // Run a script given in memory (UTF-8). Returns true on success.
  bool runString(const char* script, size_t length, std::string& error);

  // Maximum size in bytes for a single script file (loaded fully at once).
  size_t maxScriptBytes() const { return maxScriptBytes_; }

  // Exposed so host API registration can push CFunctions.
  lua_State* state() { return L_; }

  // Registers the host libraries on the state: cp.file (SD read/write/list)
  // and cp.misc (peek heap, platform info). Called automatically after the
  // stdlib set is opened in the constructor.
  void registerHostApis();

  // Returns the peak VM heap allocation while this VM ran (bytes).
  size_t peakAlloc() const { return peakAlloc_; }

  // Owner object for use with the allocator callback (public so the
  // allocator, which is a static member of the class, can call trackAlloc).
  struct LuaRuntimeAlloc;

 private:
  static void* allocFn(void* ud, void* ptr, size_t osize, size_t nsize);
  void trackAlloc(ptrdiff_t delta);

  lua_State* L_ = nullptr;
  size_t maxScriptBytes_;
  size_t peakAlloc_ = 0;
  size_t currentAlloc_ = 0;
};