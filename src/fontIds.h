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
// Registered as the glyph-level fallback for the Pretendard UI font so book titles
// and menus can render Hanja/Kana glyphs that Pretendard (Hangul + Latin only) lacks.
#define SYSTEM_FONT_ID (-888888)
