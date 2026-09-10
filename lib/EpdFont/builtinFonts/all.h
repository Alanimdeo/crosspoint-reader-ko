#pragma once

// Korean build ships only KoPub Batang (reader) + Pretendard (UI) to stay within the
// 6.25 MB OTA size budget. Upstream's NotoSerif/NotoSans/Ubuntu built-ins are intentionally
// NOT included here; src/main.cpp loads only the fonts below. The NOTOSERIF_*/NOTOSANS_*
// font IDs in fontIds.h remain defined so upstream code compiles, but their font data is absent.

// UI fonts (Pretendard 10pt) - Regular only, synthetic bold used when needed
#include <builtinFonts/pretendard_10_regular.h>

// Korean EPUB reader font (KoPub Batang 14pt) - Regular only, synthetic bold used when needed
#include <builtinFonts/kopub_14_regular.h>

// Sleep-screen clock fonts (minimal: digits + Korean date syllables)
#include <builtinFonts/sleep_clock_100.h>
#include <builtinFonts/sleep_clock_20.h>
