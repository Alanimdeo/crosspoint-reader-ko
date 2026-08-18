# Firmware Size Budget

The released `gh_release` firmware must install onto a locked X4 in a single OTA
step. The OTA path writes the new image into the inactive app partition of the
device's current (stock Crosspoint) layout, so the firmware has to fit that
partition.

## The constraint

Stock Crosspoint 1.3.0 app partition (`upstream/release/1.3.0:partitions.csv`):

```
app0, app, ota_0, 0x10000,  0x640000   ; 6,553,600 bytes = 6.25 MB
app1, app, ota_1, 0x650000, 0x640000
```

`gh_release` `firmware.bin` must stay at or below **6,553,600 bytes**. If it is
larger, the OTA unlocker cannot flash KO directly and the user is forced through
the two-step path (OTA to Crosspoint, then SD-card flash to KO — see issue #15).

This repo's own `partitions.csv` keeps the larger `0x6A0000` (6.625 MB) app
partitions for the full-flash / SD-update path; that is independent of the OTA
size limit above.

## What gets us under the limit

Korean fonts dominate the image. KoPub Batang 14 (the reader font, 3.13 MB
including its 4,620 Hanja) is the product and is kept in full. Pretendard 10 (the
UI font) is the one that gets cut, because a UI font only needs the syllables that
appear in menus, titles and filenames.

| Cut | Where | Saving |
|-----|-------|--------|
| Pretendard UI font restricted to KS X 1001 Hangul | `convert-korean-builtin-fonts.sh` | **828 KB** |
| Drop Latin/Cyrillic hyphenation tries | `-DCP_HYPHENATION_LANGS=0` | ~310 KB |
| Ship only Korean + English UI | only `english.yaml` + `korean.yaml` | ~184 KB |
| Omit serial logging | no `-DENABLE_SERIAL_LOG` (`gh_release`) | ~27 KB |
| Drop upstream's `.cpfont` SD font engine | deleted in the 1.5.0 merge | ~7 KB |

### The Pretendard cut

Pretendard 10pt carried all 11,172 precomposed Hangul syllables for 1,070 KB.
Restricting it to the 2,350 syllables of KS X 1001 (the classic wansung set)
leaves 222 KB of syllables plus 20 KB of interval table -- a net 828 KB. The
syllables are scattered through U+AC00..U+D7A3, so they collapse into 1,613 short
runs rather than one range; `ks_x_1001_intervals.py` generates them.

KS X 1001 covers modern Korean orthography. What drops out is archaic and
onomatopoeic combinations (`뷁`, `똠`), which effectively never appear in UI
labels but *can* appear in book titles and filenames. Those glyphs do not render
unless the user selects an SD-card system font -- there is no built-in fallback
for them, which is a deliberate trade (see the release notes for 1.5.0-ko.1). A
full-coverage Pretendard `.epdfont` is published with each release for users who
want them back:

| File | Size |
|------|------|
| `pretendard_10_regular.epdfont` | 1.06 MB |
| `pretendard_10_bold.epdfont` | 1.10 MB |

Korean text is not Liang-hyphenated, so dropping the hyphenation tries has no
effect on Korean reading. `CP_HYPHENATION_LANGS` defaults to `1` (all languages)
for upstream parity; individual languages toggle with
`CP_HYPHEN_LANG_<EN|FR|DE|RU|ES|IT|UK|SV|PL|FI>`. See
`lib/Epub/Epub/hyphenation/LanguageRegistry.cpp`.

## Measured

| Build | `firmware.bin` | vs 6.25 MB |
|-------|---------------|------------|
| 1.3.0-ko.1 | 6,461,088 | 92,512 under |
| 1.5.0-ko.0 (upstream 1.5.0 merged) | 6,869,392 | **315,792 over** |
| 1.5.0-ko.1 (Pretendard cut to KS X 1001) | 6,041,152 | 512,448 under |

1.5.0-ko.0 shipped over the limit. That does not just force the two-step install
for new users: **it breaks OTA for existing ones.** OTA and the SD-card update
both write only the app partition and never rewrite the partition table, so a
device that reached KO through OTA still has the stock 6.25 MB table and rejects
an oversized image with `TOO_LARGE` (see `FirmwareFlasher.cpp`). Only a full
flash changes the table, and some units have USB disabled, leaving them with no
route at all. Hence 1.5.0-ko.1.

Headroom is now ~512 KB. Before adding flash-resident data, rebuild `gh_release`
and check `firmware.bin` against 6,553,600 bytes. Remaining levers, largest
first: reducing Hanja coverage in `kopub_14_regular` (~840 KB), dropping
Pretendard entirely and falling the UI back to KoPub (~282 KB now), excluding
X3-only code (tilt/gyro/NTP, currently always linked), and trimming the wolfSSL
cipher set (~195 KB total, most of it needed for HTTPS).
