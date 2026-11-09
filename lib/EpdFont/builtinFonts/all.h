#pragma once

// Korean build ships only Pretendard 10pt (reader and UI) to stay within the
// 6.25 MB OTA size budget. Upstream's NotoSerif/NotoSans/Ubuntu built-ins are intentionally
// NOT included here; src/main.cpp loads only the fonts below. The NOTOSERIF_*/NOTOSANS_*
// font IDs in fontIds.h remain defined so upstream code compiles, but their font data is absent.

// Reader and UI font (Pretendard 10pt; Hanja and anything else Pretendard lacks from KoPub Batang) -
// Regular only, synthetic bold used when needed. All-KoPub reading ships as a release .epdfont.
#include <builtinFonts/pretendard_10_regular.h>

// Sleep-screen clock fonts (minimal: digits + Korean date syllables)
#include <builtinFonts/sleep_clock_100.h>
#include <builtinFonts/sleep_clock_20.h>
