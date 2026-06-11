// Font IDs for CrossPoint Korean version
#pragma once

// UI font (Pretendard 10pt) - single font for all UI sizes
#define UI_FONT_ID (1983644244)
#define SMALL_FONT_ID UI_FONT_ID
#define UI_10_FONT_ID UI_FONT_ID
#define UI_12_FONT_ID UI_FONT_ID

// Korean EPUB reader font (KoPub Batang 14pt)
#define KOPUB_14_FONT_ID (-1446433084)

// Custom reader font loaded from SD card
#define CUSTOM_FONT_ID (-999999)

// User-selectable UI "system font" loaded from SD card.
// When set, it becomes the primary UI font (the whole UI renders with it) and Pretendard
// is kept as its glyph-level fallback for codepoints the SD font lacks. When unset, the UI
// uses Pretendard only.
#define SYSTEM_FONT_ID (-888888)
