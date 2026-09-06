#include "LuaRunnerActivity.h"

#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalClock.h>
#include <I18n.h>
#include <Logging.h>

#include <cstring>
#include <string>

#include "MappedInputManager.h"
#include "activities/Activity.h"
#include "components/UITheme.h"
#include "fontIds.h"

extern "C" {
#include "lauxlib.h"
#include "lua.h"
}

namespace {
// Resolve the cp.gfx `font` option string to a firmware font id.
// font ids come from fontIds.h; the font objects are registered at boot in
// src/main.cpp (Pretendard under UI_FONT_ID, KoPub under KOPUB_14_FONT_ID,
// upstream Noto faces under the NOTOSANS/NSERIF ids).
int resolveFontId(lua_State* L, int idx, int fallback) {
  if (lua_type(L, idx) != LUA_TSTRING) return fallback;
  const char* f = lua_tostring(L, idx);
  if (strcmp(f, "ui") == 0 || strcmp(f, "pretendard") == 0) return UI_FONT_ID;
  if (strcmp(f, "kopub") == 0 || strcmp(f, "reader") == 0) return KOPUB_14_FONT_ID;
  if (strcmp(f, "custom") == 0) return CUSTOM_FONT_ID;
  if (strcmp(f, "system") == 0) return SYSTEM_FONT_ID;
  if (strcmp(f, "sans12") == 0) return NOTOSANS_12_FONT_ID;
  if (strcmp(f, "sans14") == 0) return NOTOSANS_14_FONT_ID;
  if (strcmp(f, "sans16") == 0) return NOTOSANS_16_FONT_ID;
  if (strcmp(f, "sans18") == 0) return NOTOSANS_18_FONT_ID;
  if (strcmp(f, "serif12") == 0) return NOTOSERIF_12_FONT_ID;
  if (strcmp(f, "serif14") == 0) return NOTOSERIF_14_FONT_ID;
  if (strcmp(f, "serif16") == 0) return NOTOSERIF_16_FONT_ID;
  if (strcmp(f, "serif18") == 0) return NOTOSERIF_18_FONT_ID;
  return fallback;
}

// Resolve the cp.gfx `style` option string to EpdFontFamily::Style.
EpdFontFamily::Style resolveFontStyle(lua_State* L, int idx) {
  if (lua_type(L, idx) != LUA_TSTRING) return EpdFontFamily::REGULAR;
  const char* s = lua_tostring(L, idx);
  if (strcmp(s, "bold") == 0) return EpdFontFamily::BOLD;
  if (strcmp(s, "italic") == 0) return EpdFontFamily::ITALIC;
  if (strcmp(s, "bold_italic") == 0) return EpdFontFamily::BOLD_ITALIC;
  return EpdFontFamily::REGULAR;
}

// Retrieve the GfxRenderer bound as lightuserdata upvalue #1 of a cp.gfx
// function. The renderer reference is set up once in onEnter() and stays
// valid for the lifetime of this activity (the VM dies in onExit()).
GfxRenderer& gfxUpvalue(lua_State* L) {
  void* ptr = lua_touserdata(L, lua_upvalueindex(1));
  return *static_cast<GfxRenderer*>(ptr);
}

int l_gfx_clear(lua_State* L) {
  gfxUpvalue(L).clearScreen();
  return 0;
}

// Parse cp.gfx.text option table at index `idx` (nil/absent = defaults).
// Returns the resolved font id, style and ink color.
void parseTextOpts(lua_State* L, int idx, int& fontId, EpdFontFamily::Style& style, bool& black) {
  fontId = UI_FONT_ID;
  style = EpdFontFamily::REGULAR;
  black = true;
  if (!lua_istable(L, idx)) return;

  lua_getfield(L, idx, "font");
  fontId = resolveFontId(L, -1, fontId);
  lua_pop(L, 1);

  lua_getfield(L, idx, "style");
  style = resolveFontStyle(L, -1);
  lua_pop(L, 1);

  lua_getfield(L, idx, "color");
  if (lua_isstring(L, -1)) {
    const char* c = lua_tostring(L, -1);
    if (strcmp(c, "white") == 0) black = false;
  }
  lua_pop(L, 1);
}

// cp.gfx.text(text, x, y [, opts])
//   opts.font  = "ui" (default) | "kopub" | "system" | "custom" |
//                "sans12..18" | "serif12..18"
//   opts.style = "regular" (default) | "bold" | "italic" | "bold_italic"
//   opts.color = "black" (default) | "white"
int l_gfx_text(lua_State* L) {
  GfxRenderer& r = gfxUpvalue(L);
  const char* text = luaL_checkstring(L, 1);
  const int x = luaL_checkinteger(L, 2);
  const int y = luaL_checkinteger(L, 3);

  int fontId;
  EpdFontFamily::Style style;
  bool black;
  parseTextOpts(L, 4, fontId, style, black);

  r.drawText(fontId, x, y, text, black, style);
  return 0;
}

// cp.gfx.textwidth(text [, opts]) -> pixel width of the text
int l_gfx_textwidth(lua_State* L) {
  GfxRenderer& r = gfxUpvalue(L);
  const char* text = luaL_checkstring(L, 1);

  int fontId;
  EpdFontFamily::Style style;
  bool black;
  parseTextOpts(L, 2, fontId, style, black);

  lua_pushinteger(L, r.getTextWidth(fontId, text, style));
  return 1;
}

int l_gfx_rect(lua_State* L) {
  GfxRenderer& r = gfxUpvalue(L);
  const int x = luaL_checkinteger(L, 1);
  const int y = luaL_checkinteger(L, 2);
  const int w = luaL_checkinteger(L, 3);
  const int h = luaL_checkinteger(L, 4);
  const bool filled = lua_isnoneornil(L, 5) || lua_toboolean(L, 5);
  if (filled) {
    r.fillRect(x, y, w, h);
  } else {
    r.drawRect(x, y, w, h);
  }
  return 0;
}

// cp.gfx.fill(x, y, w, h) — solid rectangle (convenience for rect(..., true)).
int l_gfx_fill(lua_State* L) {
  GfxRenderer& r = gfxUpvalue(L);
  const int x = luaL_checkinteger(L, 1);
  const int y = luaL_checkinteger(L, 2);
  const int w = luaL_checkinteger(L, 3);
  const int h = luaL_checkinteger(L, 4);
  r.fillRect(x, y, w, h);
  return 0;
}

int l_gfx_line(lua_State* L) {
  GfxRenderer& r = gfxUpvalue(L);
  const int x1 = luaL_checkinteger(L, 1);
  const int y1 = luaL_checkinteger(L, 2);
  const int x2 = luaL_checkinteger(L, 3);
  const int y2 = luaL_checkinteger(L, 4);
  r.drawLine(x1, y1, x2, y2);
  return 0;
}

int l_gfx_size(lua_State* L) {
  GfxRenderer& r = gfxUpvalue(L);
  lua_pushinteger(L, r.getScreenWidth());
  lua_pushinteger(L, r.getScreenHeight());
  return 2;
}

int l_gfx_display(lua_State* L) {
  // Runs inside the render task (render() holds the RenderLock), so the
  // framebuffer is committed immediately. No-op arguments are tolerated for a
  // future refresh-mode parameter.
  GfxRenderer& r = gfxUpvalue(L);
  r.displayBuffer();
  return 0;
}

namespace {
// 5x7 dot-matrix glyphs for cp.gfx.digits. Rows top→bottom, each row is a
// 5-bit mask (bit 4 = leftmost). Digit shapes were specified by hand and
// verified visually; unsupported chars are skipped.
constexpr uint8_t kDigitGlyphs[] = {
    // 0
    0x0E,
    0x11,
    0x13,
    0x15,
    0x19,
    0x11,
    0x0E,
    // 1
    0x02,
    0x06,
    0x0A,
    0x02,
    0x02,
    0x02,
    0x02,
    // 2
    0x0E,
    0x11,
    0x01,
    0x02,
    0x04,
    0x08,
    0x1F,
    // 3
    0x0E,
    0x11,
    0x01,
    0x06,
    0x01,
    0x11,
    0x0E,
    // 4
    0x02,
    0x06,
    0x0A,
    0x12,
    0x1F,
    0x02,
    0x02,
    // 5
    0x1F,
    0x10,
    0x1E,
    0x01,
    0x01,
    0x11,
    0x0E,
    // 6
    0x0E,
    0x11,
    0x10,
    0x1E,
    0x11,
    0x11,
    0x0E,
    // 7
    0x1F,
    0x11,
    0x02,
    0x04,
    0x04,
    0x04,
    0x04,
    // 8
    0x0E,
    0x11,
    0x11,
    0x0E,
    0x11,
    0x11,
    0x0E,
    // 9
    0x0E,
    0x11,
    0x11,
    0x0F,
    0x01,
    0x11,
    0x0E,
    // : (colon)
    0x00,
    0x02,
    0x02,
    0x00,
    0x02,
    0x02,
    0x00,
    // . (dot)
    0x00,
    0x00,
    0x00,
    0x00,
    0x01,
    0x01,
    0x00,
    // - (minus)
    0x00,
    0x00,
    0x0E,
    0x00,
    0x00,
    0x00,
    0x00,
    // (space)
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
};

constexpr int kDigitGlyphH = 7;
constexpr int kDigitGlyphW = 5;  // 5-bit wide rows

// Map a char to a glyph index in kDigitGlyphs (returns -1 if unsupported).
int digitIndex(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  switch (c) {
    case ':':
      return 10;
    case '.':
      return 11;
    case '-':
      return 12;
    case ' ':
      return 13;
    default:
      return -1;
  }
}

int l_gfx_digits(lua_State* L) {
  GfxRenderer& r = gfxUpvalue(L);
  const char* text = luaL_checkstring(L, 1);
  const int x = luaL_checkinteger(L, 2);
  const int y = luaL_checkinteger(L, 3);
  const int scale = luaL_optinteger(L, 4, 2);

  // opts = { color = "white" } optionally.
  bool ink = true;
  if (lua_istable(L, 5)) {
    lua_getfield(L, 5, "color");
    if (lua_isstring(L, -1) && strcmp(lua_tostring(L, -1), "white") == 0) ink = false;
    lua_pop(L, 1);
  }

  int cx = x;
  for (size_t i = 0; i < strlen(text); ++i) {
    const int gi = digitIndex(text[i]);
    if (gi < 0) {
      cx += 2 * scale;
      continue;  // unsupported char: small gap
    }
    for (int row = 0; row < kDigitGlyphH; ++row) {
      const uint8_t bits = kDigitGlyphs[gi * kDigitGlyphH + row];
      for (int col = 0; col < kDigitGlyphW; ++col) {
        if (bits & (1 << (4 - col))) {
          r.fillRect(cx + col * scale, y + row * scale, scale, scale, ink);
        }
      }
    }
    cx += (kDigitGlyphW + 1) * scale;  // +1 cell spacing between glyphs
  }
  return 0;
}

int l_gfx_digits_width(lua_State* L) {
  const char* text = luaL_checkstring(L, 1);
  const int scale = luaL_optinteger(L, 2, 2);
  int w = 0;
  for (size_t i = 0; i < strlen(text); ++i) {
    const int gi = digitIndex(text[i]);
    if (gi < 0) {
      w += 2 * scale;
      continue;
    }
    w += (kDigitGlyphW + 1) * scale;
  }
  // Remove the trailing spacing cell for exact width.
  if (w > 0) w -= scale;
  lua_pushinteger(L, w);
  return 1;
}
}  // namespace

// cp.gfx table, functions bound to the activity's renderer (upvalue 1).
const luaL_Reg kGfxLib[] = {
    {"clear", l_gfx_clear},
    {"text", l_gfx_text},
    {"textwidth", l_gfx_textwidth},
    {"digits", l_gfx_digits},
    {"digits_width", l_gfx_digits_width},
    {"rect", l_gfx_rect},
    {"fill", l_gfx_fill},
    {"line", l_gfx_line},
    {"size", l_gfx_size},
    {"display", l_gfx_display},
    {nullptr, nullptr},
};

void registerGfxModule(lua_State* L, GfxRenderer& renderer) {
  // luaL_setfuncs(L, regs, nup) expects, at entry, the table at the bottom
  // with the nup upvalues on TOP of it (it reads the upvalue at -nup and the
  // table at -(nup+2)). So push the table first, then the renderer upvalue.
  lua_newtable(L);                      // [gfx]
  lua_pushlightuserdata(L, &renderer);  // [gfx, renderer]
  luaL_setfuncs(L, kGfxLib, 1);         // [gfx] — funcs bound to renderer

  // Attach gfx onto the existing cp table (created by the file module).
  lua_getglobal(L, "cp");  // [gfx, cp]
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    lua_newtable(L);  // [gfx, cp]
  }
  lua_pushvalue(L, -2);        // [gfx, cp, gfx]
  lua_setfield(L, -2, "gfx");  // [gfx, cp] ; cp.gfx = gfx
  lua_pop(L, 2);               // []
}

// --- cp.input ------------------------------------------------------------
//
// Handlers live in the global _cp_input table so the script controls their
// lifetime naturally (the VM owns them; lua_close frees them). LuaRunner
// looks them up and calls them from loop() when the matching button edge or
// swipe fires.
//
//   cp.input.on("confirm", function(key) ... end)       -- on release
//   cp.input.on_press("confirm", function(key) ... end) -- on press
//   cp.input.on("any", function(key, edge) ... end)     -- every edge
//   cp.input.swipe(function(dir) ... end)               -- "left/right/up/down"
//   cp.input.pressed("confirm")                         -- poll (bool)

// _cp_input table sub-object accessor used by the module functions.
void ensureInputTable(lua_State* L, const char* field) {
  lua_getglobal(L, "_cp_input");  // t
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    lua_newtable(L);                // t
    lua_pushvalue(L, -1);           // t t
    lua_setglobal(L, "_cp_input");  // t ; _G._cp_input = t
  }
  lua_getfield(L, -1, field);  // t f
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    lua_newtable(L);             // t f
    lua_pushvalue(L, -1);        // t f f
    lua_setfield(L, -3, field);  // t f ; t.field = f
  }
  lua_remove(L, -2);  // f
}

// Register a handler table field: on/on_press both land in _cp_input.edge.<key>
// with the edge name stored so "any" handlers can pass it through.
int l_input_on(lua_State* L) {
  const char* key = luaL_checkstring(L, 1);
  luaL_checktype(L, 2, LUA_TFUNCTION);
  const char* edge = "released";
  if (lua_gettop(L) >= 3) {
    const char* e = luaL_optstring(L, 3, nullptr);
    if (e) edge = e;
  }

  ensureInputTable(L, edge);  // f (edge table)
  lua_pushvalue(L, 2);        // f handler
  lua_setfield(L, -2, key);   // f[key] = handler
  lua_pop(L, 1);              // []
  return 0;
}

// cp.input.on_press(key, handler) == cp.input.on(key, handler, "pressed").
int l_input_on_press(lua_State* L) {
  lua_pushliteral(L, "pressed");
  return l_input_on(L);
}

int l_input_swipe(lua_State* L) {
  luaL_checktype(L, 1, LUA_TFUNCTION);
  lua_getglobal(L, "_cp_input");  // t
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    lua_newtable(L);  // t
    lua_pushvalue(L, -1);
    lua_setglobal(L, "_cp_input");  // _G._cp_input = t
  }
  lua_pushvalue(L, 1);           // t handler
  lua_setfield(L, -2, "swipe");  // t.swipe = handler
  lua_pop(L, 1);
  return 0;
}

int l_input_pressed(lua_State* L) {
  // Poll the mapped logical button by name. MappedInputManager is upvalue 1.
  MappedInputManager& mi = *static_cast<MappedInputManager*>(lua_touserdata(L, lua_upvalueindex(1)));
  MappedInputManager::Button b = MappedInputManager::Button::Confirm;
  const char* key = luaL_checkstring(L, 1);
  bool known = true;
  if (strcmp(key, "confirm") == 0) {
    b = MappedInputManager::Button::Confirm;
  } else if (strcmp(key, "back") == 0) {
    b = MappedInputManager::Button::Back;
  } else if (strcmp(key, "up") == 0) {
    b = MappedInputManager::Button::Up;
  } else if (strcmp(key, "down") == 0) {
    b = MappedInputManager::Button::Down;
  } else if (strcmp(key, "left") == 0) {
    b = MappedInputManager::Button::Left;
  } else if (strcmp(key, "right") == 0) {
    b = MappedInputManager::Button::Right;
  } else if (strcmp(key, "pageback") == 0) {
    b = MappedInputManager::Button::PageBack;
  } else if (strcmp(key, "pageforward") == 0) {
    b = MappedInputManager::Button::PageForward;
  } else {
    known = false;
  }
  lua_pushboolean(L, known && mi.isPressed(b));
  return 1;
}

const luaL_Reg kInputLib[] = {
    {"on", l_input_on}, {"on_press", l_input_on_press}, {"swipe", l_input_swipe}, {"pressed", l_input_pressed},
    {nullptr, nullptr},
};

void registerInputModule(lua_State* L, MappedInputManager& mappedInput) {
  lua_newtable(L);                         // [input]
  lua_pushlightuserdata(L, &mappedInput);  // [input, mappedInput]
  luaL_setfuncs(L, kInputLib, 1);          // [input] — funcs bound to mappedInput

  lua_getglobal(L, "cp");  // [input, cp]
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    lua_newtable(L);  // [input, cp]
  }
  lua_pushvalue(L, -2);          // [input, cp, input]
  lua_setfield(L, -2, "input");  // [input, cp] ; cp.input = input
  lua_pop(L, 2);                 // []
}

// --- cp.time --------------------------------------------------------------
// Read-only wall-clock / uptime access. There is deliberately no blocking
// sleep here: scripts run on the render task, so sleeping would freeze the UI.
// Use cp.frame(ms, handler) for periodic redraws instead.

int l_time_available(lua_State* L) {
  lua_pushboolean(L, halClock.isAvailable() ? 1 : 0);
  return 1;
}

// Returns the cached hour/minute (as HalClock provides). nil, nil if no RTC.
int l_time_gettime(lua_State* L) {
  uint8_t h, m;
  if (!halClock.getTime(h, m)) {
    lua_pushnil(L);
    lua_pushnil(L);
    return 2;
  }
  lua_pushinteger(L, h);
  lua_pushinteger(L, m);
  return 2;
}

// Full date/time table, or nil if the RTC is absent/unreadable.
int l_time_now(lua_State* L) {
  uint16_t year;
  uint8_t month, day, hour, minute, second, weekday;
  if (!halClock.getDateTime(year, month, day, hour, minute, second, weekday)) {
    lua_pushnil(L);
    return 1;
  }
  lua_newtable(L);
  lua_pushinteger(L, year);
  lua_setfield(L, -2, "year");
  lua_pushinteger(L, month);
  lua_setfield(L, -2, "month");
  lua_pushinteger(L, day);
  lua_setfield(L, -2, "day");
  lua_pushinteger(L, hour);
  lua_setfield(L, -2, "hour");
  lua_pushinteger(L, minute);
  lua_setfield(L, -2, "minute");
  lua_pushinteger(L, second);
  lua_setfield(L, -2, "second");
  lua_pushinteger(L, weekday);
  lua_setfield(L, -2, "weekday");
  return 1;
}

int l_time_millis(lua_State* L) {
  lua_pushinteger(L, static_cast<lua_Integer>(millis()));
  return 1;
}

const luaL_Reg kTimeLib[] = {
    {"available", l_time_available}, {"gettime", l_time_gettime}, {"now", l_time_now},
    {"millis", l_time_millis},       {nullptr, nullptr},
};

void registerTimeModule(lua_State* L) {
  lua_newtable(L);                // [time]
  luaL_setfuncs(L, kTimeLib, 0);  // [time]

  lua_getglobal(L, "cp");  // [time, cp]
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    lua_newtable(L);  // [time, cp]
  }
  lua_pushvalue(L, -2);         // [time, cp, time]
  lua_setfield(L, -2, "time");  // [time, cp] ; cp.time = time
  lua_pop(L, 2);                // []
}

// --- cp.frame -------------------------------------------------------------
// Periodic redraw scheduler. Scripts call
//   cp.frame(intervalMs, handler)    -- start periodic redraws
//   cp.frame(nil)                    -- stop
// The handler is stored in the _cp_frame global table together with the
// interval and the next-due time. LuaRunnerActivity::loop() checks it every
// frame; when due it triggers a render and render() invokes only the handler
// (the main chunk runs just once at startup). The handler runs on the render
// task, so it may use cp.gfx freely; globals persist because the VM lives for
// the whole activity.

int l_frame(lua_State* L) {
  if (lua_isnoneornil(L, 1)) {
    // Stop: clear the handler.
    lua_getglobal(L, "_cp_frame");  // f
    if (lua_istable(L, -1)) {
      lua_pushnil(L);
      lua_setfield(L, -2, "handler");
    }
    lua_pop(L, 1);
    return 0;
  }

  luaL_checktype(L, 2, LUA_TFUNCTION);
  lua_Integer intervalMs = luaL_checkinteger(L, 1);
  if (intervalMs < 50) intervalMs = 50;  // floor: don't pound the e-ink.

  lua_getglobal(L, "_cp_frame");  // f
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    lua_newtable(L);  // f
    lua_pushvalue(L, -1);
    lua_setglobal(L, "_cp_frame");
  }
  lua_pushinteger(L, intervalMs);
  lua_setfield(L, -2, "interval_ms");
  lua_pushinteger(L, static_cast<lua_Integer>(millis()) + intervalMs);
  lua_setfield(L, -2, "next_due");
  lua_pushvalue(L, 2);
  lua_setfield(L, -2, "handler");
  lua_pop(L, 1);
  return 0;
}

void registerFrameModule(lua_State* L) {
  // Seed _cp_frame so loop()/render() can read it without nil checks.
  lua_newtable(L);
  lua_setglobal(L, "_cp_frame");

  // cp table already exists from cp.time/cp.input.
  lua_getglobal(L, "cp");  // cp
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    lua_newtable(L);  // cp
    lua_pushvalue(L, -1);
    lua_setglobal(L, "cp");
  }
  lua_pushcfunction(L, l_frame);
  lua_setfield(L, -2, "frame");
  lua_pop(L, 1);
}

}  // namespace

LuaRunnerActivity::LuaRunnerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string scriptPath,
                                     int scriptVersion)
    : Activity("LuaRunner", renderer, mappedInput), scriptPath(std::move(scriptPath)), scriptVersion(scriptVersion) {}

void LuaRunnerActivity::onEnter() {
  Activity::onEnter();

  lua = makeUniqueNoThrow<LuaRuntime>();
  if (!lua) {
    LOG_ERR("LUA", "OOM creating Lua runtime");
    startError = tr(STR_FILE_OPEN_FAILED);
    requestUpdate();
    return;
  }

  // Compile the script now. The compiled chunk stays on the Lua stack and is
  // executed in render() under the render lock, so any cp.gfx drawing the
  // script does happens safely on the render task.
  std::string error;
  if (!lua->loadFileForLateRun(scriptPath.c_str(), error)) {
    LOG_ERR("LUA", "Script failed: %s", error.c_str());
    startError = error.empty() ? tr(STR_FILE_OPEN_FAILED) : std::move(error);
  }

  registerGfxModule(lua->state(), renderer);
  registerInputModule(lua->state(), mappedInput);
  registerTimeModule(lua->state());
  registerFrameModule(lua->state());
  requestUpdate();
}

void LuaRunnerActivity::onExit() {
  Activity::onExit();
  // Destroy the VM so every byte it allocated is returned to the heap.
  lua.reset();
}

void LuaRunnerActivity::loop() {
  Activity::loop();

  // Back+Power pressed together = go home. This is the universal escape hatch
  // for every script regardless of what the script binds to back/power, so a
  // misbehaving script can never trap the user.
  if (mappedInput.isPressed(MappedInputManager::Button::Back) &&
      mappedInput.isPressed(MappedInputManager::Button::Power)) {
    activityManager.goHome();
    return;
  }

  if (lua) {
    dispatchInputEvents();

    // Periodic redraws scheduled via cp.frame(ms, fn).
    checkFrameDue();

    // Back with no script handler (ie the script did not call cp.input.on for
    // "back") returns to the file browser, so scripts can opt in to a custom
    // back action without losing the default escape hatch.
    if (!hasInputHandler("released", "back")) {
      if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
        activityManager.goToFileBrowser(scriptPath);
        return;
      }
    }
  } else {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      activityManager.goToFileBrowser(scriptPath);
      return;
    }
  }

  if (rerunRequested) {
    rerunRequested = false;
    startError.clear();
    scriptStarted = false;
    requestUpdate();
  }
}

void LuaRunnerActivity::checkFrameDue() {
  if (!lua) return;
  lua_State* L = lua->state();

  lua_getglobal(L, "_cp_frame");  // f
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    return;
  }

  lua_getfield(L, -1, "handler");
  const bool hasHandler = lua_isfunction(L, -1);
  lua_pop(L, 1);

  if (hasHandler) {
    lua_getfield(L, -1, "next_due");
    const lua_Integer nextDue = lua_tointeger(L, -1);
    lua_pop(L, 1);

    if (static_cast<unsigned long>(nextDue) <= millis()) {
      // Advance the due-timer and ask for a render that runs the handler.
      lua_getfield(L, -1, "interval_ms");
      const lua_Integer intervalMs = lua_tointeger(L, -1);
      lua_pop(L, 1);
      lua_pushinteger(L, static_cast<lua_Integer>(millis()) + intervalMs);
      lua_setfield(L, -2, "next_due");
      requestUpdate();
    }
  }
  lua_pop(L, 1);  // _cp_frame
}

void LuaRunnerActivity::invokeKeyboardHandler(const char* edge, const char* key) {
  if (!lua) return;
  lua_State* L = lua->state();
  lua_getglobal(L, "_cp_input");  // t
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    return;
  }
  lua_getfield(L, -1, edge);  // t e
  if (!lua_istable(L, -1)) {
    lua_pop(L, 2);
    return;
  }
  lua_pushstring(L, key);  // t e key
  lua_gettable(L, -2);     // t e handler
  lua_remove(L, -2);       // t handler
  lua_remove(L, -2);       // handler

  if (lua_isfunction(L, -1)) {
    lua_pushstring(L, key);
    const int status = lua_pcall(L, 1, 0, 0);
    if (status != LUA_OK) {
      const char* msg = lua_tostring(L, -1);
      LOG_ERR("LUA", "input handler error: %s", msg ? msg : "(unknown)");
      lua_pop(L, 1);
    }
  } else {
    lua_pop(L, 1);  // pop the nil handler
  }
}

void LuaRunnerActivity::invokeSwipeHandler(const char* dir) {
  if (!lua) return;
  lua_State* L = lua->state();
  lua_getglobal(L, "_cp_input");  // t
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    return;
  }
  lua_pushstring(L, "swipe");  // t "swipe"
  lua_gettable(L, -2);         // t handler
  lua_remove(L, -2);           // handler

  if (lua_isfunction(L, -1)) {
    lua_pushstring(L, dir);
    const int status = lua_pcall(L, 1, 0, 0);
    if (status != LUA_OK) {
      const char* msg = lua_tostring(L, -1);
      LOG_ERR("LUA", "swipe handler error: %s", msg ? msg : "(unknown)");
      lua_pop(L, 1);
    }
  } else {
    lua_pop(L, 1);
  }
}

bool LuaRunnerActivity::hasInputHandler(const char* edge, const char* key) {
  if (!lua) return false;
  lua_State* L = lua->state();
  lua_getglobal(L, "_cp_input");  // t
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    return false;
  }
  lua_getfield(L, -1, edge);  // t e
  const bool have = lua_istable(L, -1) && !lua_isnil(L, -1);
  if (have) {
    lua_pushstring(L, key);
    lua_gettable(L, -2);  // t e handler
    const bool func = lua_isfunction(L, -1);
    lua_pop(L, 1);
    lua_pop(L, 1);
    lua_pop(L, 1);
    return func;
  }
  lua_pop(L, 2);
  return false;
}

void LuaRunnerActivity::dispatchInputEvents() {
  if (!lua) return;

  struct KeyMap {
    const char* name;
    MappedInputManager::Button button;
  };
  // Order: most-specific first; PageBack/PageForward map to side buttons.
  static const KeyMap keys[] = {
      {"confirm", MappedInputManager::Button::Confirm},
      {"up", MappedInputManager::Button::Up},
      {"down", MappedInputManager::Button::Down},
      {"left", MappedInputManager::Button::Left},
      {"right", MappedInputManager::Button::Right},
      {"pageback", MappedInputManager::Button::PageBack},
      {"pageforward", MappedInputManager::Button::PageForward},
      {"back", MappedInputManager::Button::Back},
      {"power", MappedInputManager::Button::Power},
  };

  for (const auto& k : keys) {
    if (mappedInput.wasPressed(k.button)) {
      invokeKeyboardHandler("pressed", k.name);
    }
    if (mappedInput.wasReleased(k.button)) {
      invokeKeyboardHandler("released", k.name);
    }
  }

  switch (mappedInput.wasSwipe()) {
    case MappedInputManager::SwipeDir::Left:
      invokeSwipeHandler("left");
      break;
    case MappedInputManager::SwipeDir::Right:
      invokeSwipeHandler("right");
      break;
    case MappedInputManager::SwipeDir::Up:
      invokeSwipeHandler("up");
      break;
    case MappedInputManager::SwipeDir::Down:
      invokeSwipeHandler("down");
      break;
    default:
      break;
  }
}

void LuaRunnerActivity::render(RenderLock&&) {
  renderer.clearScreen();

  if (lua) {
    if (!scriptStarted) {
      scriptStarted = true;
      // The compiled chunk is on the stack from onEnter(). Run it; it will
      // draw into the framebuffer via cp.gfx and commit with cp.gfx.display().
      const int callStatus = lua_pcall(lua->state(), 0, LUA_MULTRET, 0);
      if (callStatus != LUA_OK) {
        const char* msg = lua_tostring(lua->state(), -1);
        startError = msg ? msg : tr(STR_FILE_OPEN_FAILED);
        lua_pop(lua->state(), 1);
        LOG_ERR("LUA", "runtime error: %s", startError.c_str());
      }
    } else {
      // Main chunk already ran once. If a cp.frame handler is registered and
      // due, run it (it should redraw); otherwise show a blank re-render.
      invokeFrameHandlerOnly();
    }
  }

  if (!startError.empty()) {
    // Overlay the error on top of whatever (partial) frame the script drew.
    const auto pageWidth = renderer.getScreenWidth();
    const auto metrics = UITheme::getInstance().getMetrics();
    renderer.fillRect(0, metrics.topPadding, pageWidth, metrics.headerHeight);
    renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, metrics.topPadding + metrics.headerHeight / 2,
                      scriptPath.c_str());
    renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, metrics.topPadding + metrics.headerHeight + 20,
                      startError.c_str());
    GUI.drawButtonHints(renderer, tr(STR_BACK), "", "", "");
  }

  // If the script called cp.gfx.display() the frame is already committed;
  // still, commit whatever is left in the buffer for header-less overlays.
  renderer.displayBuffer();
}

// Runs the registered cp.frame handler (no-op when none is registered). This
// gives scripts a periodic "tick" without re-running the main chunk, so their
// globals persist across ticks.
void LuaRunnerActivity::invokeFrameHandlerOnly() {
  if (!lua) return;
  lua_State* L = lua->state();

  lua_getglobal(L, "_cp_frame");  // f
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    return;
  }
  lua_getfield(L, -1, "handler");  // f h
  lua_remove(L, -2);               // h

  if (lua_isfunction(L, -1)) {
    const int status = lua_pcall(L, 0, 0, 0);
    if (status != LUA_OK) {
      const char* msg = lua_tostring(L, -1);
      LOG_ERR("LUA", "frame handler error: %s", msg ? msg : "(unknown)");
      lua_pop(L, 1);
    }
  } else {
    lua_pop(L, 1);
  }
}