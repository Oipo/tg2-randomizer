# Race load architecture (synthesis, 2026-06-12)

Derived from temp_minimap*.txt session logs + static analysis of the headerless ROM.
All file offsets below assume NO copier header (same convention as main.cpp).
SNES = LoROM: file = (bank-0x80)*0x8000 + (addr-0x8000).

## TL;DR

* The race engine identifies the track by a single global index in `$7E:0108`:
  `track = country*4 + race_in_country` (0..63).
* It is computed at `$80:CD2C-CD39` (file 0x4D2C): `LDA $7EFE0A / ASL / ASL / ADC $7EFE0E / STA $0108`,
  inside routine `$80:CD09`, called via `JSL $80CD09` from `$81:8147` during the
  shop->race black screen. Demo-mode override: if `$7E:FF1A != 0`, `$0108` is instead
  loaded from table `$80:F2DD` (file 0x72DD; demo tracks 0x0C, 0x0B, 0x21, 0x01).
* Everything per-track downstream is selected from `$0108` via three independent ROM tables:
  1. **Road geometry + weather**: `$8A:8000` row table (file 0x50000), 64 rows x 8 bytes.
     word0 = offset of path records in bank $8A, word1 = length, byte6 = weather.
     Consumed by the bank $88 engine (`LDA $0108 / ASL x3 / TAX / LDA $8A8000,X` at
     888022/888422/888608/888A07/888CAD/...).
  2. **Graphics asset records**: bank $87 descriptor records, one per (country, race).
     Index: `word[$87:A656 + country*2]` -> country list; `+ race*2` -> record offset.
     The 16 country lists are contiguous, so the 64 record-pointer words live at
     `$87:A676-$87:A6F5` (file 0x3A676-0x3A6F5). Records are 0x58 bytes each, first
     record at `$87:A6F6`. The loader masks `$0108 & 0x3C` for the country and
     `$0108 & 0x03` for the race (sites `$81:8834`, `$81:8F24`, `$81:D47E`, `$81:D8B1`).
  3. **Music**: byte table at `$9F:A20C` (file 0x0FA20C), 64 bytes, one song ID per track
     (values cycle 01 02 03). Selected on the *shop side* at `$9F:8126-8139` from
     `$7E:1CE1` (country) and `$7E:1CE5` (race), i.e. NOT via `$0108`.

## Full shop -> race timeline

1. **Shop / menu** (bank $9F module, D=$0100, DB=$9F; $0000-$1FFF are WRAM mirrors).
   Country in `$7E:1CE1`, race-in-country in `$7E:1CE5`. Mirrored into the handoff
   block at `$7E:FE00+` (`$7E:FE0A` = country, `$7E:FE0E` = race). $7E:FE00+ survives
   the per-race WRAM wipe (which only clears 0800-FDFF).
2. **Player starts race**: `$9F:8126-8139` picks song = `$9FA20C[country*4 | race]`,
   stores to `$01C1`, `JSR $F0A3` = **SPC700 music upload** (`$9F:F0A3`):
   descriptor table `$9B:8000`, bytecode/LZ decompressor `$9F:BF14` (writes via
   `STA [$84],Y`), streams from banks $98-$9C, APU handshake on `$2141`
   (`LDA #$77 / CMP $2141` wait loop proves this is audio).
   This uses low WRAM `$7F:0000+` as scratch — **this caused the "first write to
   $7F:0A80" breakpoint hits. Audio. Red herring for the minimap.**
3. **Module handoff**: `$9F:8190` clears/fills the `$0100` parameter block,
   `$9F:818D JML [$0100]` -> race module entry `$81:8045`.
4. **Race init** (the black screen):
   * `$81:8062 JSR $F470` -> `$81:F470` = **full wipe**: OAM (ch0), all 64KB VRAM (ch1),
     CGRAM (ch2), `$7E:0800-FDFF` (ch3), all of `$7F:0000-FFFF` (ch4) via
     B->A DMA from `$2134`. **This caused the "first write to $7F:1CA0" hit at
     81F52A. A memory clear. Red herring #2.** Consequence: nothing graphics-related
     survives from boot or the previous race; everything below runs *every* race.
   * `$81:8147 JSL $80CD09` -> fills `$0102/$0104/$0106/$0108/$0120` from `$7E:FE00+`
     (the `$0108` computation above).
   * `$81:8158 JSR $8717` -> **asset load** using the bank $87 record:
     - multiple calls into decompressor `$81:DE85` (WMDATA-based: dest set via
       `$2181-2183`, bytes written through `$2180`).
       Entry convention: **X = source addr, A low = source bank,
       A high bit0 = WRAM dest bank ($7E/$7F), Y = dest addr.**
     - record field +$00/+$02: manifest -> `$7F:0000`, then iterated by `$81:D473`
       (observed sources `8E:B393`, then `8C:F787`, `87:8006`, ...).
     - field +$46/+$48 -> `$7E:6200`; field +$22/+$24 -> `$7E:6400`
       (both regions are read by the bank $88 road/minimap engine);
     - field +$4A/+$4C -> `$7F:0000+` — **this is what fills the $7F tile-pixel
       buffers ($7F:0A80/1CA0/2A00/37C0) that 8885AA uploads to VRAM.**
       For country 0 race 0 the source is `$8D:C572`.
     - field +$54/+$56: numeric params (negated into $0216/$0252...).
   * `$81:815B JSL $80CD5E`, `$81:816E JSL $80D0DA`, `$81:8172 JSL $88CB74`
     (per-track pointers observed: A=$8244 vs $8164), `$81:8176 JSL $8B8000`.
   * HDMA setup `$81:81A3-8219`; channel tables are *built in WRAM* at `$7E:9FF6`
     and `$7E:A991` ($028C/$028E are WRAM addresses, not ROM pointers).
5. **Race loop** (from `$81:8272`): per frame `JSL $808A41 / $80D726 / $888BCA / $88CB74 / $8B8000`.
   Bank $88 engine:
   * `888000` / `8885E5`: per-frame builders (one per player screen), walk the
     `$8A` path records, build entries in `$7E:9000-93FF` / `$9400-97FF`.
   * `888BCA`: advances track position `$020C/$020E` by speed `$0212/$0214`,
     wraps at track length `$010A`.
   * `$5800` / `$5D00` (in $7E, built from record data): position-triggered upload
     tables — records of (position word, id word). When the position passes an
     entry, the id is resolved through `$8B:DBE3/DBE5` and `$8B:F34D/F34F` into a
     ($7F source, VRAM dest word $4000+id*16 / $5000+..., size) and DMA'd
     (ch7, `8885AA` / `888B5F`). At race start everything with position <= 0x48
     is uploaded (the 4 bursts: $7F:0A80 sz 0x860, $7F:1CA0 sz 0x3E0,
     $7F:2A00 sz 0x2C0, $7F:37C0 sz 0x4A0). This re-upload is what heals
     corrupted minimap VRAM each frame.

## What this means for the randomizer

Swapping only the 0x50000 rows swaps road geometry + weather, but the tile
graphics (incl. minimap pixels) are chosen via the bank $87 record words and the
music via 0x0FA20C — three tables keyed by the same index. Two consistent options:

1. **Data-level**: apply the *same* 64-entry permutation to:
   * 8-byte rows at file 0x50000,
   * 16-bit record words at file 0x3A676 (64 words),
   * bytes at file 0x0FA20C (64 bytes),
   * 16-bit words at file 0x55FE (`$80:D5FE`, per-track drone start-line base — also
     keyed by `$0108`; if it is left unpermuted the drone grid spawns at the wrong
     spot for the swapped track. See drone-speed.md section 8).
   Caveat: `$7E:6200/$6400` data from the $87 record is consumed by the road engine
   alongside the `$8A` path data, so these two tables must always travel together.
2. **Index-level**: patch `$80:CD2C-CD39` (file 0x4D2C) to route the computed index
   through a permutation table in free ROM space (needs a JSL stub; only 14 bytes
   on-site). Music still needs the 0x0FA20C permutation since the shop picks it
   from `$1CE1/$1CE5` directly. Track-name/flag display in the shop is also
   selected shop-side and would keep showing the original country.

## Verification experiments (cheap, decisive)

* Swap the two words at file 0x3A676/0x3A678 (country 0, races 1<->2 records):
  graphics sets (scenery + road tiles + presumably minimap) should swap between
  races 1 and 2 while geometry stays. Determines exactly what bank $87 controls.
* Keep the existing 0x50000 row swap: determines what the path table controls
  (geometry, weather, minimap *shape* if procedural).
* Listen after the existing 0x0FA20C swap: races 1/2 music swapped = audio table
  confirmed by ear.
* Breakpoint `$80:CD37` (STA $0108) to watch track selection happen live;
  log A at `$81:DE85` entry to enumerate every decompressed asset per track.

## Red herrings, resolved

* `9FF0A3 / 9FBF14 / 9FC0xx` writes to `$7F:0A80+`: SPC music upload scratch.
* `81F52A` "first write to $7F:1CA0": the per-race full-WRAM/VRAM wipe.
* The long black screen at **boot**: cannot matter for the minimap — the per-race
  wipe destroys all of $7F and VRAM before every race, so all race graphics are
  reloaded between shop and race. (Boot is likely SPC driver + menu assets.)
* The shop->race window was the correct place to look all along; the trap was
  "first write" semantics on WRAM that gets wiped and reused by audio first.
