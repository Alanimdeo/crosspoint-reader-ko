# `.epdfont` SD Card Font Format

The `.epdfont` binary format lets the firmware load custom fonts from the SD card on demand instead of compiling them into flash. It is consumed by `lib/EpdFont/SdFont` and produced by the docs-site converter at `crosspoint-reader-docs/font-converter/ttf_to_epdfont.py`.

## File layout (v1, current)

```
┌──────────────────────────────────────────┐
│ Header                       (32 bytes)  │
├──────────────────────────────────────────┤
│ Intervals[intervalCount]    (×12 bytes)  │
├──────────────────────────────────────────┤
│ Glyphs[glyphCount]          (×16 bytes)  │
├──────────────────────────────────────────┤
│ Bitmap data                  (variable)  │
└──────────────────────────────────────────┘
```

All multi-byte integers are little-endian. Structs are `#pragma pack(push, 1)`.

### Header (32 B)

| Offset | Type | Field | Notes |
|---|---|---|---|
| 0 | `uint32` | `magic` | `0x46445045` ("EPDF") |
| 4 | `uint16` | `version` | `1` |
| 6 | `uint8` | `is2Bit` | 1 = 2-bit grayscale, 0 = 1-bit |
| 7 | `uint8` | reserved | |
| 8 | `uint8` | `advanceY` | Line height (pixels) |
| 9 | `int8` | `ascender` | Max height above baseline |
| 10 | `int8` | `descender` | Max depth below baseline (negative) |
| 11 | `uint8` | reserved | |
| 12 | `uint32` | `intervalCount` | |
| 16 | `uint32` | `glyphCount` | |
| 20 | `uint32` | `intervalsOffset` | File offset to intervals |
| 24 | `uint32` | `glyphsOffset` | File offset to glyph table |
| 28 | `uint32` | `bitmapOffset` | File offset to bitmap blob |

### Interval (12 B)

| Offset | Type | Field |
|---|---|---|
| 0 | `uint32` | `first` (codepoint) |
| 4 | `uint32` | `last` (codepoint) |
| 8 | `uint32` | `offset` (index into glyph table) |

### Glyph (16 B)

| Offset | Type | Field | Notes |
|---|---|---|---|
| 0 | `uint8` | `width` | Bitmap width (px) |
| 1 | `uint8` | `height` | Bitmap height (px) |
| 2 | `uint8` | `advanceX` | **Integer pixels** — see fixed-point note below |
| 3 | `uint8` | reserved | |
| 4 | `int16` | `left` | X offset from cursor |
| 6 | `int16` | `top` | Y offset from cursor |
| 8 | `uint32` | `dataLength` | Bitmap byte count |
| 12 | `uint32` | `dataOffset` | Offset into bitmap blob |

## Fixed-point convention (read this if you touch the loader)

Upstream 1.2.0 (`#1168 — Use fixed-point fractional x-advance and kerning`) changed the **runtime** `EpdGlyph::advanceX` to a `uint16_t` holding **12.4 fixed-point pixels** so layout can do per-glyph differential rounding (`fp4::toPixel(prevAdvance + kern)`).

`.epdfont` files predate that change and stay on the legacy v1 layout: `advanceX` is a plain `uint8_t` in **integer pixels**. The compatibility shim lives in `SdFont.cpp::loadGlyphFromSD`:

```cpp
outGlyph->advanceX = static_cast<uint16_t>(fileGlyph.advanceX) << fp4::FRAC_BITS;
```

`SdFont::getTextDimensions` mirrors the renderer and applies `fp4::toPixel` when accumulating the cursor.

**Effect of forgetting the shift:** the renderer interprets the raw pixel value as fp4, dividing it by 16. A 14-px advance becomes ~1 px and every glyph in an SD-loaded font is drawn on top of the previous one. Built-in flash fonts are unaffected because their `advanceX` is generated as fp4 by `lib/EpdFont/scripts/fontconvert.py`.

**Generators stay on integer pixels.** `ttf_to_epdfont.py` keeps writing the v1 layout — no need to regenerate existing `.epdfont` files when upgrading firmware.

## When to bump to v2

Open the door to v2 only when one of the following is genuinely needed:

1. Sub-pixel `advanceX` in SD fonts (kerning/ligatures from disk). Requires widening to `uint16_t` in fp4 and adding kern/ligature tables — almost a different file format.
2. `advanceX > 255` px. Possible for very large display fonts but not realistic for the X4 800×480 panel.

Until then v1 + the loader-side `<< 4` shift is the cheaper path.

## References

- Format definition: `lib/EpdFont/SdFontFormat.h`
- Loader / runtime conversion: `lib/EpdFont/SdFont.cpp`
- Flash font equivalents: `lib/EpdFont/EpdFontData.h` (search for `fp4` namespace)
- Upstream PR that introduced fp4: https://github.com/crosspoint-reader/crosspoint-reader/pull/1168
- Generator: `crosspoint-reader-docs/font-converter/ttf_to_epdfont.py`
