# Startup and save routines

Addresses here use the normal project convention: headerless ROM PC offsets and
LoROM SNES addresses.

## Save/new-game block

The player/save structure starts at WRAM `$1CD7`. A race-facing copy is built
elsewhere at `$7E:FE00`, but the persistent/menu-side structure is rooted at
`$1CD7`.

Known fields inside this structure:

| WRAM address | Struct offset | Meaning |
| --- | ---: | --- |
| `$1CF3` | `$001C` | P1 car block, mirrored at `$7E:FE1C` |
| `$1D1C` | `$0045` | P2 car block, mirrored at `$7E:FE45` |
| `$1D19` | `$0042` | P1 money |
| `$1D42` | `$006B` | P2 money |

Both money fields are 16-bit little-endian values.

## Per-player car blocks

Inside the `$1CD7` save/new-game block are two per-player car blocks. P1 starts
at shop WRAM `$1CF3` / save-work `$7E:FE1C`. P2 starts at shop WRAM `$1D1C` /
save-work `$7E:FE45`. The block stride is `$29` bytes.

The first 12 bytes are the player name. In the new-game template these are
`PLAYER 1` and `PLAYER 2`, padded with zeroes:

| Player | Shop WRAM | Save-work WRAM | Template PC | Stock bytes |
| --- | --- | --- | ---: | --- |
| P1 | `$1CF3` | `$7E:FE1C` | `0x0F8403` | `50 4C 41 59 45 52 20 31 00 00 00 00` |
| P2 | `$1D1C` | `$7E:FE45` | `0x0F842C` | `50 4C 41 59 45 52 20 32 00 00 00 00` |

Upgrade/item fields are one byte every two bytes. The second byte in each pair
is currently zero in the new-game template.

| Field | P1 shop | P1 save-work | P2 shop | P2 save-work |
| --- | --- | --- | --- | --- |
| engine | `$1D03` | `$7E:FE2C` | `$1D2C` | `$7E:FE55` |
| wet tires | `$1D05` | `$7E:FE2E` | `$1D2E` | `$7E:FE57` |
| dry tires | `$1D07` | `$7E:FE30` | `$1D30` | `$7E:FE59` |
| gearbox | `$1D09` | `$7E:FE32` | `$1D32` | `$7E:FE5B` |
| nitro | `$1D0B` | `$7E:FE34` | `$1D34` | `$7E:FE5D` |
| mystery 4-level field | `$1D0D` | `$7E:FE36` | `$1D36` | `$7E:FE5F` |
| armour 1 | `$1D0F` | `$7E:FE38` | `$1D38` | `$7E:FE61` |
| armour 2 | `$1D11` | `$7E:FE3A` | `$1D3A` | `$7E:FE63` |
| armour 3 | `$1D13` | `$7E:FE3C` | `$1D3C` | `$7E:FE65` |
| paint, probable | `$1D15` | `$7E:FE3E` | `$1D3E` | `$7E:FE67` |
| money | `$1D19` | `$7E:FE42` | `$1D42` | `$7E:FE6B` |

## Startup validation/load routine

The routine at `$9F:835D` / PC `0x0F835D` handles save startup. It is called
from `$9F:805D` / PC `0x0F805D`.

High-level flow:

1. Clear `$0100`.
2. Add bytes from SRAM/work buffer `$7E:FE04..$7E:FE99` into `$0100`.
3. Check `$7E:FE00` and `$7E:FE02` against the inverse checksum pair.
4. If the save is valid, copy `$7E:FE00..` into WRAM `$1CD7..`.
5. If the save is invalid/new, copy the ROM default template into WRAM
   `$1CD7..`.

Relevant bytes around the decision:

```asm
; $9F:8377
CMP $7EFE00
BNE new_file
EOR #$FFFF
CMP $7EFE02
BNE new_file

; valid save path
PHB
LDX #$FE00
LDY #$1CD7
LDA #$011B
MVN $00,$7E        ; copy save/work buffer to $00:1CD7
PLB
...
CLC
RTS

; new/invalid save path at $9F:83C8
PHB
LDX #$83E7
LDY #$1CD7
LDA #$011B
MVN $00,$9F        ; copy ROM default template to $00:1CD7
PLB
JSR $DA3C          ; rebuild/checksum save data
LDA #$7E65
STA $1CD7
LDA #$5543
STA $1CD9
SEC
RTS
```

## New-game default template

The new-game/default structure template starts at `$9F:83E7` / PC `0x0F83E7`.
It is copied to WRAM `$1CD7` by the new/invalid save path above.

Template money offsets:

| Field | WRAM destination | Template SNES | Template PC | Stock bytes |
| --- | --- | --- | ---: | --- |
| P1 money | `$1D19` | `$9F:8429` | `0x0F8429` | `00 00` |
| P2 money | `$1D42` | `$9F:8452` | `0x0F8452` | `00 00` |

This is the right place to patch randomized starting money for a fresh game.
For example, `10000` decimal is `$2710`, so the bytes to write are `10 27` at
both `0x0F8429` and `0x0F8452`.

Important: this template only affects new games or invalid save data. If a
valid save already exists, startup loads the saved values from `$7E:FE00` and
bypasses the template.

## Other money writes

There are later routines that modify or reset money during gameplay/shop flow,
but they are not the new-game source of the initial zeroes:

| SNES | PC | Notes |
| --- | ---: | --- |
| `$9F:C9F3` | `0x0FC9F3` | Writes hardware multiplication result into `$1D19`; conditionally mirrors to `$1D42`. |
| `$9F:CFF5` | `0x0FCFF5` | Similar write path, also clears other state. |
| `$9F:DA28` | `0x0FDA28` | Explicitly `STZ $1D19`, `STZ $1D42`, `STZ $1CE5`, `STZ $1CE1`. This is a reset/helper routine, not the ROM default template. |
| `$9F:ED2E` | `0x0FED2E` | Adds award/table value into `$1D19`. |
| `$9F:ED43` | `0x0FED43` | Adds award/table value into `$1D42`. |
