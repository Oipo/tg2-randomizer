# Upgrades / car stats (synthesis, 2026-06-12)

Static analysis; file offsets assume NO copier header. LoROM: file = (bank-0x80)*0x8000 + (addr-0x8000).
Bank $80 addr -> file = addr - 0x8000.

## Where the upgrade state lives

* **Shop-side (authoritative)**: `$7E:1CD7`, 0x11C bytes. The shop reads/writes here
  (e.g. money deduction refs at `$9F:AA0F+`, sequential field reads at `$9F:D70D-D787`).
* **Race-side (handoff copy)**: `$7E:FE00`, written by `$9F:8190` right before the
  `JML [$0100]` into the race module. Bytes 4..0x99 are summed into a 16-bit checksum
  at `FE00`, complement at `FE02` (any direct ROM/RAM poke of the 1CD7 block must
  keep this in mind for the FE copy only — the shop recomputes it each transition).
  `$9F:81D4` copies FE00 -> 1CD7 back after the race and adds the per-race earnings
  (`FE9A/FE9C` = `$1D71/$1D73`) into money (`$1D19/$1D42`).
* Survives the per-race WRAM wipe because the wipe only covers `$7E:0800-FDFF`.

## Per-player car block (stride 0x29)

P1 at `$7E:FE1C` (shop `$1CF3`), P2 at `$7E:FE45` (shop `$1D1C`). Offsets within block:

| off  | P1 addr | meaning | evidence |
|------|---------|---------|----------|
| +0x00..0x0B | FE1C | player name, 12 chars, space-padded into $01E4/$01F0 | $80:CDCA centering loop |
| +0x0C | FE28 | word; 0 -> $019A=FFFF (inactive?) | $80:CEFE |
| +0x0E | FE2A | bit15 -> $01AA (transmission?); low = 5-way index into 7 word tables $80:D048-D08D | $80:CF19/CF33 |
| +0x10 | FE2C | **engine level 0-3** -> accel $0698 (28/2C/30/34) + nitro-accel $0699 (68/6C/70/74) | tables $80:D036/D03A |
| +0x12 | FE2E | **wet tire level** (used when weather 3=rain or 4=snow) | weather table $80:D092 selects FE2E+0 |
| +0x14 | FE30 | **dry tire level** (all other weather) | $80:D092 selects FE2E+2 |
| +0x16 | FE32 | **gearbox level 0-3** -> $068A raw + gear count $06C0 (5,6,6,7 incl. neutral slot) | $80:D0D2; ratio tables below |
| +0x18 | FE34 | **nitro level 0-3 (probable)** -> $06C4 = 1000/0B00/0500/0000 | $80:D0CA; verify vs FE36 |
| +0x1A | FE36 | 4-level field -> $068E = EC/F0/F4/F8 (with $0690=0, $0692=$6800) | $80:D08E; see bug below |
| +0x1C | FE38 | **armour zone A level** -> $0697 via $80:D03E (18 10 0C 08 04) | collision reader $80:F016 |
| +0x1E | FE3A | **armour zone B level** -> $0695 via $80:D043 (20 1C 10 0C 08) | readers $80:E9AA/EEF9 |
| +0x20 | FE3C | **armour zone C level** -> $0696 via $80:D03E | reader $80:EFF9 |
| +0x22 | FE3E | -> $0104/$0106 — likely **paint** (8 values); verify | $80:CD18/CD1F |
| +0x26 | FE42 | **money** (shop $1D19/$1D42); race earnings added at $9F:81F8 | |

Which armour zone is front/side/rear is not yet pinned; the collision handlers in
`$80:E9xx-F0xx` would tell (zone B uses the harsher table $80:D043).

## The race-side loader: `$80:CD5E` (file 0x4D5E)

Called via `JSL $80CD5E` from `$81:815B` (race init) — translates every FE-block
field through ROM tables into the `$06xx` per-player parameter block. The companion
`JSL $80CD09` (from `$81:8147`) fills `$0102-$0120` incl. the track index `$0108`.

**Game bug at `$80:CDB3`**: P1 path is `LDA $7EFE36 / TAX / LDA $80D08E,X`, P2 path is
`LDA $7EFE5F / ASL / LDA $80D08E,X` — `0A` (ASL) where `AA` (TAX) was intended, so X
still holds P1's index and **P2's $06B0 always uses P1's FE36 level**.

## Effect tables (randomizer targets), bank $80

| SNES | file | contents |
|------|------|----------|
| $80:D036 | 0x5036 | engine: base accel per level `28 2C 30 34` -> $0698 |
| $80:D03A | 0x503A | engine: nitro accel per level `68 6C 70 74` -> $0699 |
| $80:D03E | 0x503E | armour A/C: damage per level `18 10 0C 08 04` (5 entries) |
| $80:D043 | 0x5043 | armour B: damage per level `20 1C 10 0C 08` |
| $80:D048-D08D | 0x5048 | 7 tables x 5 words, FE2A-indexed (control/setup presets?) |
| $80:D08E | 0x508E | FE36 effect `EC F0 F4 F8` -> $068E |
| $80:D092 | 0x5092 | weather -> tire byte selector (words: `2 2 2 0 0 2 2 2`) |
| $80:D0A2 | 0x50A2 | grip grid, 8 weathers x 4 levels (lower = grippier); rain row `40 38 32 28`, snow `48 40 38 30` |
| $80:D0C2 | 0x50C2 | nitro charge count per level — constant `0006` |
| $80:D0CA | 0x50CA | nitro effect per level `1000 0B00 0500 0000` -> $06C4 |
| $80:D0D2 | 0x50D2 | gear count per level `0005 0006 0006 0007` -> $06C0 |

## Gears

`$80:ECED`: `LDA $068A (gearbox lvl) / ASL / TAX / LDA $0630 / ADC $80BDF3,X / TAX /
LDA $80BDFB,X / STA $06DE / ... STA $4206` — gearbox level selects a per-level
**gear table** used as hardware-division divisor (gear ratios):

* offsets `$80:BDF3` (file 0x3DF3): `0000 0005 000B 0011` -> table lengths **5,6,6,7**
* ratios `$80:BDFB` (file 0x3DFB):
  * lvl 0: `E0 40 4D 5A 67` (neutral/reverse + 4 gears)
  * lvl 1: `E0 40 4B 56 61 6F` (+5th gear)
  * lvl 2: `E0 40 4D 5B 68 76` (still 6 — taller ratios instead of a new gear)
  * lvl 3: `E0 40 4D 59 66 72 7F` (+6th gear)
* second per-gear table set at file 0x3E13 (same 5/6/6/7 lengths, values `7F 70 28 19 11`...)
* per-level word table at file 0x3E2B (offsets `0 2 4 6`) + values `5D88 58F8 58F8 57D4`
  (file 0x3E33) — top-speed/RPM scale; levels 1 and 2 equal again.

This matches the in-game behaviour exactly: 4 gears base, two upgrades add a gear
(-> 5 -> 6), one upgrade improves ratios without adding a gear.

## Acceleration

`$80:9AFB-9B16` (P1; P2 mirror ~`$80:B3xx`): speed update uses the PPU multiplier —
`STA $211B (16-bit) / LDA $0699 / STA $211C / result $2135 + #$0600 -> $059B` and
`LDA $0698 / ASL -> $059D`. So $0698/$0699 (engine level values) are the
acceleration multipliers; nitro state picks the $0699 path.

## Nitro runtime

`$80:8E05-8E5D`: on button, if charges (`$01A6` low nibble) nonzero: `DEC $01A6`,
boost timer `$2C = #$0230` (dp), `TSB #$8000 $01A6` = boost-active flag.
Charges start at 6 (`$80:D0C2`), `INC $01A6` on track pickups (`$80:9D95/9DAF`).

## Open items / quick experiments

* Poke shop bytes (P1): engine `$7E:1D03`, wet `$1D05`, dry `$1D07`, gearbox `$1D09`,
  nitro? `$1D0B`, FE36-field `$1D0D`, armours `$1D0F/$1D11/$1D13`, paint? `$1D15`,
  money `$1D19` — then start a race and observe. Settles FE34-vs-FE36 nitro and paint.
* Armour zone order (front/side/rear): read collision handlers `$80:E9AA/EEF9/EFF9/F016`.
* Where `$1CD7` gets its new-game defaults (the init template) — not yet located;
  likely a small ROM template copied by the title/name-entry code in bank $9F.
