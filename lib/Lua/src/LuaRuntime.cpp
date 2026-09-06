#include "LuaRuntime.h"

#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>

#include <cstdio>
#include <cstring>

extern "C" {
#include "lauxlib.h"
#include "lualib.h"
}

// Callback used by Lua for all VM allocations. `ud` is the LuaRuntime*.
void *LuaRuntime::allocFn(void *ud, void *ptr, size_t osize, size_t nsize) {
  auto *self = static_cast<LuaRuntime *>(ud);
  if (nsize == 0) {
    free(ptr);
    if (self)
      self->trackAlloc(-static_cast<ptrdiff_t>(osize));
    return nullptr;
  }
  void *newPtr = realloc(ptr, nsize);
  if (newPtr && self) {
    self->trackAlloc(static_cast<ptrdiff_t>(nsize) -
                     static_cast<ptrdiff_t>(osize));
  }
  return newPtr;
}

void LuaRuntime::trackAlloc(ptrdiff_t delta) {
  if (delta < 0) {
    const size_t amount = static_cast<size_t>(-delta);
    currentAlloc_ = (currentAlloc_ > amount) ? currentAlloc_ - amount : 0;
  } else {
    currentAlloc_ += static_cast<size_t>(delta);
    if (currentAlloc_ > peakAlloc_)
      peakAlloc_ = currentAlloc_;
  }
}

// Custom panic handler: log instead of aborting so we can clean up.
namespace {
int panicHandler(lua_State *L) {
  const char *msg = lua_tostring(L, -1);
  LOG_ERR("LUA", "panic: %s", msg ? msg : "(unknown)");
  return 0; // Return 0; pcall unwinds without aborting the firmware.
}
} // namespace

LuaRuntime::LuaRuntime(size_t maxScriptBytes)
    : maxScriptBytes_(maxScriptBytes) {
  L_ = luaL_newstate();
  if (!L_) {
    LOG_ERR("LUA", "luaL_newstate failed (OOM)");
    return;
  }

  lua_setallocf(L_, &LuaRuntime::allocFn, this);
  lua_atpanic(L_, panicHandler);

  // Open only the stdlib modules mini-apps need. io/os/debug/package are not
  // compiled in (lib/Lua/library.json srcFilter), so luaL_openlibs would not
  // link; open the available libs individually.
  luaL_requiref(L_, "_G", luaopen_base, 1);
  lua_pop(L_, 1);
  luaL_requiref(L_, LUA_MATHLIBNAME, luaopen_math, 1);
  lua_pop(L_, 1);
  luaL_requiref(L_, LUA_STRLIBNAME, luaopen_string, 1);
  lua_pop(L_, 1);
  luaL_requiref(L_, LUA_TABLIBNAME, luaopen_table, 1);
  lua_pop(L_, 1);
  luaL_requiref(L_, LUA_UTF8LIBNAME, luaopen_utf8, 1);
  lua_pop(L_, 1);
  luaL_requiref(L_, LUA_COLIBNAME, luaopen_coroutine, 1);
  lua_pop(L_, 1);

  registerHostApis();
}

LuaRuntime::~LuaRuntime() {
  if (L_) {
    lua_close(L_);
    L_ = nullptr;
  }
}

bool LuaRuntime::runString(const char *script, size_t length,
                           std::string &error) {
  if (!L_) {
    error = "Lua state unavailable";
    return false;
  }
  if (length > maxScriptBytes_) {
    error = "Script too large for the memory budget";
    return false;
  }

  const int loadStatus = luaL_loadbufferx(L_, script, length, "=sdscript", "t");
  if (loadStatus != LUA_OK) {
    error = lua_tostring(L_, -1) ? lua_tostring(L_, -1) : "compile error";
    lua_pop(L_, 1);
    return false;
  }

  // Run the chunk now (caller was prepared to run immediately).
  const int callStatus = lua_pcall(L_, 0, LUA_MULTRET, 0);
  if (callStatus != LUA_OK) {
    const char *msg =
        lua_tostring(L_, -1) ? lua_tostring(L_, -1) : "(no error message)";
    error = msg;
    // Copy before popping (Lua allocator may re-use the string buffer).
    lua_pop(L_, 1);
    LOG_ERR("LUA", "runtime error: %s", msg);
    return false;
  }
  return true;
}

bool LuaRuntime::loadFileForLateRun(const char *path, std::string &error) {
  HalFile file;
  if (!Storage.openFileForRead("LUA", path, file)) {
    error = "Cannot open script file";
    return false;
  }

  const uint32_t size = file.size();
  if (size > maxScriptBytes_) {
    error = "Script too large for the memory budget";
    file.close();
    return false;
  }

  auto buf = makeUniqueNoThrow<char[]>(size + 1);
  if (!buf) {
    error = "Out of memory loading script";
    file.close();
    return false;
  }

  if (file.read(buf.get(), size) != static_cast<int>(size)) {
    error = "Short read loading script";
    file.close();
    return false;
  }
  buf[size] = '\0';
  file.close();

  const int loadStatus =
      luaL_loadbufferx(L_, buf.get(), size, "=sdscript", "t");
  if (loadStatus != LUA_OK) {
    error = lua_tostring(L_, -1) ? lua_tostring(L_, -1) : "compile error";
    lua_pop(L_, 1);
    return false;
  }
  // Compiled chunk remains on the Lua stack for the caller to execute.
  return true;
}

bool LuaRuntime::runFile(const char *path, std::string &error) {
  if (!loadFileForLateRun(path, error))
    return false;
  if (!L_)
    return false;
  const int callStatus = lua_pcall(L_, 0, LUA_MULTRET, 0);
  if (callStatus != LUA_OK) {
    const char *msg =
        lua_tostring(L_, -1) ? lua_tostring(L_, -1) : "(no error message)";
    error = msg;
    lua_pop(L_, 1);
    LOG_ERR("LUA", "runtime error: %s", msg);
    return false;
  }
  return true;
}

namespace {

// cp.file is the only file access scripts get. Originally io/os/loadfile were
// removed from the Lua build: the C `fopen` they use bypasses the HalStorage
// mutex that guards the (not thread-safe) SdFat SdSpiCard m_spiActive state.
// This module routes every operation through HalStorage, keeping the mutex
// intact while still giving scripts full-SD read/write/list access.
constexpr size_t MAX_FILE_IO_BYTES = 64 * 1024;

// Normalize a Lua filename argument to a rooted path. Returns the path in a
// caller buffer; rejects paths that would escape the SD root.
const char *checkPath(lua_State *L, int idx, char *buf, size_t bufLen) {
  const char *p = luaL_checkstring(L, idx);
  if (!p)
    return nullptr;
  if (p[0] != '/') {
    luaL_error(L, "path must be absolute (starts with /)");
    return nullptr;
  }
  // Reject ".." components so scripts cannot escape the root tree.
  const char *q = p;
  while ((q = strstr(q, ".."))) {
    const char prev = (q == p) ? '/' : q[-1];
    const char nxt = q[2];
    if ((prev == '/' || prev == 0) && (nxt == '/' || nxt == 0)) {
      luaL_error(L, "path must not contain '..'");
      return nullptr;
    }
    q += 2;
  }
  snprintf(buf, bufLen, "%s", p);
  return buf;
}

int l_file_read(lua_State *L) {
  char path[256];
  if (!checkPath(L, 1, path, sizeof(path)))
    return 0;

  HalFile file;
  if (!Storage.openFileForRead("LUA", path, file)) {
    lua_pushnil(L);
    lua_pushliteral(L, "cannot open file");
    return 2;
  }
  const size_t size = file.size();
  if (size > MAX_FILE_IO_BYTES) {
    file.close();
    lua_pushnil(L);
    lua_pushliteral(L, "file too large");
    return 2;
  }
  std::string content;
  content.resize(size);
  if (size > 0 && file.read(content.data(), size) != static_cast<int>(size)) {
    file.close();
    lua_pushnil(L);
    lua_pushliteral(L, "short read");
    return 2;
  }
  file.close();
  lua_pushlstring(L, content.data(), content.size());
  return 1;
}

// Read a Lua string argument (may contain embedded NULs) into an std::string.
bool readDataArg(lua_State *L, int idx, size_t maxBytes, std::string &out) {
  size_t len = 0;
  const char *data = luaL_checklstring(L, idx, &len);
  if (len > maxBytes) {
    luaL_error(L, "data too large");
    return false;
  }
  out.assign(data, len);
  return true;
}

int l_file_write_impl(lua_State *L, bool append) {
  char path[256];
  if (!checkPath(L, 1, path, sizeof(path)))
    return 0;
  std::string data;
  if (!readDataArg(L, 2, MAX_FILE_IO_BYTES, data))
    return 0;

  // Open reader first to preserve existing size when appending isn't needed.
  HalFile file;
  if (append) {
    // SdFat has no O_APPEND helper exposed here; read-modify-write is small
    // enough for the 64 KB budget and keeps the API honest.
    std::string existing;
    HalFile in;
    if (Storage.openFileForRead("LUA", path, in)) {
      const size_t sz = in.size();
      if (sz <= MAX_FILE_IO_BYTES) {
        existing.resize(sz);
        if (sz > 0 && in.read(existing.data(), sz) != static_cast<int>(sz))
          existing.clear();
      }
      in.close();
    }
    data = existing + data;
  }

  if (!Storage.openFileForWrite("LUA", path, file)) {
    lua_pushboolean(L, 0);
    lua_pushliteral(L, "cannot open for write");
    return 2;
  }
  const bool ok = file.write(data.data(), data.size()) == data.size();
  file.close();
  lua_pushboolean(L, ok ? 1 : 0);
  return 1;
}

int l_file_write(lua_State *L) { return l_file_write_impl(L, false); }
int l_file_append(lua_State *L) { return l_file_write_impl(L, true); }

int l_file_list(lua_State *L) {
  char path[256];
  if (!checkPath(L, 1, path, sizeof(path)))
    return 0;

  auto dir = Storage.open(path);
  if (!dir || !dir.isDirectory()) {
    lua_pushnil(L);
    lua_pushliteral(L, "not a directory");
    return 2;
  }

  lua_newtable(L);
  int n = 0;
  char name[256];
  dir.rewindDirectory();
  for (auto entry = dir.openNextFile(); entry; entry = dir.openNextFile()) {
    if (entry.getName(name, sizeof(name)) > 0) {
      if (name[0] != '.') {
        lua_pushinteger(L, ++n);
        lua_pushstring(L, name);
        lua_settable(L, -3);
      }
    }
    entry.close();
  }
  dir.close();
  return 1;
}

int l_file_exists(lua_State *L) {
  char path[256];
  if (!checkPath(L, 1, path, sizeof(path)))
    return 0;
  lua_pushboolean(L, Storage.exists(path) ? 1 : 0);
  return 1;
}

int l_file_remove(lua_State *L) {
  char path[256];
  if (!checkPath(L, 1, path, sizeof(path)))
    return 0;
  lua_pushboolean(L, Storage.remove(path) ? 1 : 0);
  return 1;
}

int l_file_mkdir(lua_State *L) {
  char path[256];
  if (!checkPath(L, 1, path, sizeof(path)))
    return 0;
  lua_pushboolean(L, Storage.mkdir(path) ? 1 : 0);
  return 1;
}

int l_file_isdir(lua_State *L) {
  char path[256];
  if (!checkPath(L, 1, path, sizeof(path)))
    return 0;
  auto dir = Storage.open(path, O_RDONLY);
  if (!dir) {
    lua_pushboolean(L, 0);
    return 1;
  }
  const bool isDir = dir.isDirectory();
  dir.close();
  lua_pushboolean(L, isDir ? 1 : 0);
  return 1;
}

// Register the cp.file table as a global "cp" table with sub-table "file".
void registerFileModule(lua_State *L) {
  static const luaL_Reg kFileLib[] = {
      {"read", l_file_read},     {"write", l_file_write},
      {"append", l_file_append}, {"list", l_file_list},
      {"exists", l_file_exists}, {"isdir", l_file_isdir},
      {"remove", l_file_remove}, {"mkdir", l_file_mkdir},
      {nullptr, nullptr},
  };

  lua_newtable(L);               // cp
  lua_newtable(L);               // cp file
  luaL_setfuncs(L, kFileLib, 0); // cp file = { funcs }
  lua_setfield(L, -2, "file");   // cp.file = file
  lua_setglobal(L, "cp");        // _G.cp = cp
}

} // namespace

void LuaRuntime::registerHostApis() {
  if (!L_)
    return;
  registerFileModule(L_);
}