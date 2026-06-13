# Drone / opponent speed model (synthesis, 2026-06-13)

Static analysis of the headerless ROM ("Top Gear 2 (USA)", CRC32 `2B88BEE8`).
All file offsets below assume **no copier header** (same convention as `main.cpp`,
which strips a 512-byte header before patching).
SNES = LoROM: `file = (bank-0x80)*0x8000 + (addr-0x8000)`.
For bank `$80` this simplifies to `file = addr - 0x8000`, e.g. `$80:D6FC -> 0x56FC`.

This document answers two related questions that came up during play-testing:

> *"There's a strong rubber-banding effect on the leading CPU car - is its speed
> based on the player's speed?"*

> *"Is there a per-track speed difference for the drones?"*

The short answer to both is **no**. The rest of this file is the evidence and the
exact mechanism, in enough detail to patch it.

---

## 1. TL;DR

* **No rubber banding.** The opponents never read the player's speed. The "leader
  pulls away" feeling is an illusion produced by a *static* per-slot speed ladder
  (see section 7) combined with AI cars that never make mistakes.
* **No per-track speed difference.** A drone's speed comes from two parameters that
  are assigned **once at race init**, purely from **slot-indexed** tables (plus one
  per-race random byte). The track index `$7E:0108` is never consulted by any drone
  speed code.
* **What *is* per-track / per-class** is only the drones' **starting positions** on
  the grid (section 8). Different tracks line the field up at different points; the
  speed profile they then run is identical everywhere.
* The same hardware (a unified 20-car array) drives both human players and the 18
  drones. The two human slots are kept in sync with the real player physics by
  adding the *already-computed* player speed into their array record each frame; the
  drones instead synthesise their own speed by ramping an accelerator toward a cap.

---

## 2. The unified car array `$7E:C400`

The road / collision / minimap engine treats **all** cars uniformly through one
array so it does not need separate code paths for "you" vs "them":

* **20 records, `0x40` bytes each**, based at `$7E:C400`.
* `Y` is used throughout as the record selector, i.e. `Y = slot * 0x40`.
* **Slot 0 = Player 1, slot 1 = Player 2, slots 2-19 = the 18 drones.**

Because slots 0/1 are in the same array, the engine can compare every car's
position against every other car's with one loop - including the player-proximity
check the drones use to decide whether to run full lateral simulation (section 6).

### 2.1 Record field layout

Offsets are within one `0x40` record. "P" = behaviour for the two human slots,
"D" = behaviour for the 18 drone slots. Confidence reflects how directly each was
observed in code.

| off | addr (slot 0) | meaning | conf |
|-----|---------------|---------|------|
| +0x10 | $C410 | slot/record index, `slot*2/2` written at `D22A` | med |
| +0x12 | $C412 | **D:** copied from `$7E:2000,X` (pre-scanned track edge data). **P:** paint colour, from `$0104`/`$0106` (`D2CA`/`D2D5`) | med |
| +0x14 | $C414 | lateral screen-X seed. **D:** from `$E2`. **P:** from `$0205`/`$0251` | med |
| +0x16 | $C416 | zeroed at init | low |
| +0x18 | $C418 | **on-track position, low word** (the fractional part of a 16.16 fixed-point position). **D:** seeded to `#$A800`, then advanced by the drone's own speed each frame. **P:** advanced by the player's speed each frame (`+= $0212`/`$024E`) | high |
| +0x1A | $C41A | **on-track position, high word** (integer part). **D:** seeded from `$E0` (= track length-1, stepped per slot). **P:** advanced by player speed high word (`+= $0214`/`$0250`). This is the word the proximity gate compares (section 6) | high |
| +0x1C | $C41C | **update-state selector**: `0` initially, `8` set at `D244`, `2` set when speed reaches the cap. Appears to pick which per-frame update routine runs | med |
| +0x1E/+0x1F | $C41E | **drone speed accumulator** (fixed-point). Integrated from accel toward the cap each frame. Unused as speed for the human slots | high |
| +0x20 | $C420 | zeroed at init | low |
| +0x22 | $C422 | zeroed at init | low |
| +0x24 | $C424 | **drone starting position** = `D684[slot] + (D5FE[track] + D67E[$0120]) - RNG_spread` (section 8) | high |
| +0x26 | $C426 | slot counter `$E4` (0,1,2,...) written per slot | med |
| +0x28 | $C428 | lateral / lane target derived from an RNG draw (`$E6`) | low |
| +0x2B | $C42B | flag bits; bit 0 is cleared at the top of the accel update (`E4D0`) | med |
| +0x30 | $C430 | **accel** = `D6D4[slot]` + RNG byte (section 5) | high |
| +0x32 | $C432 | **top-speed cap** = `D6FC[slot]` (section 5) | high |

(Two records, `0x16/0x20/0x22`, are explicitly zeroed at init. A *parallel* array
`$7E:C3E8[slot*2]` is filled at `D1F2` with `0x40 * $7E:FE9E[slot]` computed via the
PPU multiplier; its purpose is not yet identified.)

---

## 3. Player slots (0/1): position is driven by *your* speed

For the two human slots the game has already computed the real player speed in its
main physics; the array record is just kept in lock-step by integrating that speed.
`$80:E6C4` handles P1, `$80:E6DE` handles P2:

```
$80:E6C4  LDA $C418,Y / CLC / ADC $0212 / STA $C418,Y   ; pos.lo (+0x18) += P1 speed.lo ($0212)
          LDA $C41A,Y /       ADC $0214 / STA $C41A,Y   ; pos.hi (+0x1A) += P1 speed.hi ($0214) + carry
          LDA $0205 / STA $C414,Y                       ; copy P1 lateral X ($0205)
          RTS
$80:E6DE  ... ADC $024E ... ADC $0250 ...               ; P2: same, with speed $024E/$0250
```

So `{+0x1A:+0x18}` is a 32-bit (16.16 fixed-point) **track position**, and
"`+= player speed`" simply means *position is the running integral of speed* - that
is what these handlers do each frame. The field stores position; the player's speed
is the per-frame increment.

That `$0212`/`$0214` is genuinely the player speed is corroborated by
`race-load-architecture.md`: *"`888BCA` advances track position `$020C/$020E` by
speed `$0212/$0214`, wraps at track length `$010A`."* `$024E/$0250` is the P2 pair.

Crucially, `$0212` and `$024E` (the player speed words) appear **nowhere** in the
opponent code region `0x6000-0x6900` except in these two handlers. The drones cannot
be reading them.

---

## 4. Drone slots (2-19): a self-contained accelerate-to-cap model

Drones do **not** have an externally-computed speed; they synthesise one. The
per-frame "accelerating" update (`$80:E4D0`, reached when the drone is far from both
players - section 6) is a clean three-stage pipeline.

A note on the multiplies below: this routine sets the **direct-page register to
`$2100`**, so the fast direct-page addresses `$1B`/`$1C`/`$34`/`$35`/`$36` are
actually the PPU Mode-7 hardware multiplier registers `$211B` (M7A, 16-bit
multiplicand), `$211C` (M7B, 8-bit multiplier) and `$2134-$2136` (24-bit signed
product). The data bank is `$7E`, so `$C4xx`, `$00F8`, etc. are WRAM.

### Stage 1 - ramp the speed by accel x frame-scalar

```
$80:E4D0  LDA $C42B,Y / AND #$FFFE / STA $C42B,Y   ; clear flag bit 0
$80:E4D9  LDA $C430,Y                              ; A = accel  (+0x30)
$80:E4DC  SEP #$20 / STA $1B / XBA / STA $1B       ; M7A = accel (16-bit)
$80:E4E3  LDA $00F8 / STA $1C                      ; M7B = $00F8 (per-frame time scalar)
          ; hardware product = accel * $00F8  -> $34/$35/$36
$80:E4E8  LDA $34 / CLC / ADC $C41E,Y / STA $C41E,Y   ; speed.lo (+0x1E) += product.lo
$80:E4F1  REP #$20
$80:E4F3  LDA $35 /       ADC $C41F,Y / STA $C41F,Y   ; speed.hi (+0x1F) += product.hi + carry
```

### Stage 2 - clamp / flag at the cap

```
$80:E4FB  LDA $C41F,Y / CMP $C432,Y     ; current speed vs cap (+0x32)
$80:E501  BMI E50C                      ; still below cap -> keep accelerating next frame
$80:E503  LDA #$0002 / STA $C41C,Y      ; reached cap -> set update-state (+0x1C) = 2
$80:E509  LDA $C41F,Y                    ; (reload speed)
```

The speed is not hard-overwritten with the cap value here; instead the `+0x1C`
state is set to `2`, which on subsequent frames routes the drone to a constant-speed
"cruise" update (one of the sibling routines at `E637`/`E6A7`/`E77B`/`E7EB`) that
advances position by the now-fixed speed without adding more accel. Net effect:
speed rises linearly until it hits the cap, then holds.

### Stage 3 - advance position by the (current) speed

```
$80:E50C  SEP #$20 / STA $1B / XBA / STA $1B   ; M7A = speed (16-bit)
$80:E513  LDA $00F8 / ASL / STA $1C            ; M7B = $00F8 * 2
          ; hardware product = speed * (2 * $00F8)  -> $34/$35/$36 (signed)
$80:E519  STZ $06E3 / LDA $36 / (BPL +; DEC $06E3) / STA $06E2   ; sign-extend product hi
$80:E528  LDA $34 / STA $06E0                                    ; product lo word
$80:E52D  LDA $C418,Y / CLC / ADC $06E0 / STA $C418,Y           ; pos.lo (+0x18) += distance.lo
$80:E537  LDA $C41A,Y /       ADC $06E2 / STA $C41A,Y           ; pos.hi (+0x1A) += distance.hi
$80:E540  RTS
```

So a drone's position (`{+0x1A:+0x18}`, the same field the player handlers write) is
advanced by its **own** speed (`+0x1E/+0x1F`), which itself ramps from `accel`
(`+0x30`) toward `cap` (`+0x32`). `$00F8` is the only per-frame multiplier and it is
a **global** time scalar (not slot- or track-indexed); it scales every car
identically, so it cannot create a drone-specific or per-track difference.

---

## 5. Where drone speed is born: init `$80:D2B1` (file 0x52B1)

The race-init loop walks every slot once (`X = slot*2`, counted from `0x26` down to
`0` via `DEX/DEX/BMI`) and assigns the two speed parameters:

```
$80:D2A9  JSL $81E25B            ; RNG -> A  (a fresh random byte per slot, per race)
$80:D2B1  AND #$00FF
$80:D2B4  CLC
$80:D2B5  ADC $80D6D4,X          ; + accel base table[slot]
$80:D2B9  STA $C430,Y            ; -> car record +0x30  (ACCEL)
$80:D2BC  LDA $80D6FC,X          ; top-speed table[slot]
$80:D2C0  STA $C432,Y            ; -> car record +0x32  (TOP-SPEED CAP)
```

`$80:D2C0` / `$80:D2B9` are the **only writers of the cap / accel fields anywhere in
the ROM** (verified by full xref, section 9). There is no track index, no player
state, no later re-scaling. The RNG byte added to accel is why the same slot feels a
little more or less aggressive from race to race, but the *ceiling* (the cap) is
fixed per slot for all time.

---

## 6. The proximity gate (`$80:E424` / `$80:E541`) is not the speed code

An earlier session mislabeled `E424`/`E541` as the drone "cruise" routine; they are
actually the **player-proximity test** that decides which kind of update a drone
gets. Both compute the drone's on-track distance (`$C41A,Y`, the +0x1A position) to
each player position (`$06EA` for P1, `$06EC` for P2), using a `#$0060` window and
wrapping by track length `$010A`:

* **Within `0x60` of either player** -> full lateral simulation (`E473` family):
  lane edges from the pre-scanned tables at `$7E:2000`/`$3300`, lane changes,
  collisions - and it still ramps speed and advances position the same way.
* **Far from both players** -> the lightweight longitudinal update `E4D0`
  (sections 4): speed ramp + position advance only, no lateral work.

Either way, speed is sourced from the per-slot accel/cap fields and the track index
is never read. The proximity test only changes *how much lateral detail* is
simulated, not how fast the car goes.

---

## 7. The speed tables (the static ladder)

Both tables live in bank `$80`, 20 little-endian words (one per slot), and are the
primary randomizer targets for difficulty.

### Top-speed cap - `$80:D6FC`, file `0x56FC`

```
2199 2332 24CB 2664 27FD 2996 2B2F 2CC8 2E61 2FFA
3193 322C 34C5 365E 37F7 3990 3B29 3CC2 3E5B 3E5B
```

Ascending by slot index: slot 0 caps at `0x2199`, the two highest slots (18/19) cap
at `0x3E5B` - roughly **1.9x** the slowest. The fastest-capped drones are therefore
the ones that, once up to speed, you cannot out-drag in a stock car until you have
bought enough upgrades.

### Accel base - `$80:D6D4`, file `0x56D4`

```
1599 1B32 20CB 2664 2BFD 3196 372F 3CC8 4261 47FA
4B93 532C 58C5 5E5E 63F7 6990 6F29 74C2 7A5B 7F00
```

Also ascending: higher slots both cap higher *and* get there faster. The per-race
RNG byte (section 5) is added on top, so the exact reach-cap time wobbles slightly
each race.

### Why this *feels* like rubber-banding

There is no feedback term anywhere. The sensation is a static-ladder illusion:

1. The cap spread means whatever pace you run, some slice of the field is near you.
2. The AI never errs, so a car at its cap holds it perfectly.
3. The top slots are simply faster than an un-upgraded player car.

Together that reads as "the leader speeds up when I catch up" even though nothing in
the code observes you.

---

## 8. What is genuinely per-track / per-class: starting positions

The only place the track index `$0108` enters drone setup is the start-position
calculation in init (`$80:D1B8`):

```
$80:D1B8  LDA $0108 / ASL / TAX / LDA $80D5FE,X / STA $E8   ; per-track start base
$80:D1C3  LDA $0120 / ASL / TAX / LDA $80D67E,X / ADC $E8   ; + per-class start base
...
$80:D28C  ADC $80D684,X         ; + per-slot grid offset
$80:D290  ADC $E8 / STA $C424,Y ; -> starting position (+0x24)
```

* **`$80:D5FE` (file `0x55FE`, 64 words, one per track)** - signed start-line
  offsets that ascend with track number. They place the whole field at the right
  spot for each track; they do **not** affect speed.

  ```
  tracks  0- 7: FB00 FB60 FBB0 FC00 FDC0 FDD0 FE60 FE80
  tracks  8-15: 0030 0080 00B0 00F0 0280 0280 0280 0280
  tracks 16-23: 0480 0480 0480 0480 05A0 05C0 0600 0620
  tracks 24-31: 0780 07B0 0820 0880 0990 09A0 09B0 09C0
  tracks 32-39: 0AD0 0AE0 0AF0 0B00 0C20 0C30 0C40 0C50
  tracks 40-47: 0CE0 0CF0 0D00 0D08 0D90 0D98 0DA0 0DA8
  tracks 48-55: 0E2C 0E30 0E34 0E38 0EBC 0EC0 0EC4 0EC8
  tracks 56-63: 0F4C 0F50 0F54 0F58 0FDC 0FE0 0FE4 0FE8
  ```

* **`$80:D67E` (file `0x567E`)** is effectively a **3-entry** table `0000 0300 0600`
  (entry 3 onward overlaps the grid table, so `$0120 ∈ {0,1,2}`). It looks like a
  class / division **starting handicap**: a higher class starts the field further
  along. `$0120` is filled by the `$80:CD09` handoff routine; its exact source is
  not yet pinned (open item).

* **`$80:D684` (file `0x5684`, 20 words)** - per-slot grid spacing, **descending**:

  ```
  3800 371A 3634 354E 3468 3382 329C 31B6 30D0 2FEA
  2F04 2E1E 2D38 2C52 2B6C 2A86 29A0 28BA 27D4 26EE
  ```

Note the inverse pairing between this and the cap table: the highest-capped drones
(slots 18-19, cap `0x3E5B`) get the **smallest** grid offsets `0x27D4`/`0x26EE`,
i.e. the fastest cars start at the back of the field and carve forward - which is
exactly what produces a believable "a fast car is coming through" race shape from
otherwise dumb constant-speed AI.

---

## 9. Proof there is no track / player coupling

* **Track index xref.** Exhaustive scan for every reader of `$7E:0108`
  (`AD/AE/AC/CD/EC/CC/0D/2D/6D 08 01` and the long form `AF 08 01 7E`): all 24 hits
  are in track-load / geometry / graphics code (`$80:CExx`, bank `$81`, bank `$88`).
  **Zero** hits fall in the drone code region `0x6400-0x6900`. The single `$0108`
  read inside drone *init* (`$80:D1B8`) feeds starting position only (section 8).
* **Speed-field writers.** `$C430`/`$C432` (accel/cap) are written **only** by the
  init at `0x52B9`/`0x52C0`.
* **Player-speed readers.** `$0212`/`$024E` occur only in the slot-0/1 handlers
  `E6C4`/`E6DE`; the drone update never references them.

Taken together: drone speed is a function of `slot` and a per-race RNG byte, full
stop. It is independent of the track, of the player's speed, and of race progress.

---

## 10. Randomizer patch targets (little-endian words)

| table | file offset | size | stock contents | effect of editing |
|-------|-------------|------|----------------|-------------------|
| drone top-speed cap | **0x56FC** | 20 words | `2199 ... 3E5B 3E5B` ascending | lower the high slots to make leaders catchable; set all words equal for a nose-to-tail pack |
| drone accel base | **0x56D4** | 20 words | `1599 ... 7F00` ascending | how quickly each slot reaches its cap (a per-race RNG byte is added on top) |
| per-slot grid offset | **0x5684** | 20 words | `3800 ... 26EE` descending | starting spacing across the grid |
| per-track start base | **0x55FE** | 64 words | per-track signed offset | where the field lines up at each track's start line |
| per-class start base | **0x567E** | 3 words | `0000 0300 0600` | class/division starting handicap |

To add a **genuine per-track speed knob** (which the stock game does not have), the
drones need new code: e.g. a JSL stub in the init at `0x52B1` that scales
`D6FC[slot]` (and/or `D6D4[slot]`) by a new 64-entry table indexed by `$0108`, then
stores the scaled value into `+0x32` / `+0x30`. There is free space considerations
and the usual LoROM bank limits to respect; the cleanest hook is right after the
existing `LDA $80D6FC,X` at `0x52BC`.

---

## 11. Open / unverified items

* **Drone livery / colour.** `$80:D6AC` (file `0x56AC`) holds `0000 0001 ... 0007`
  repeating per slot and was previously guessed to be the colour table, but it has
  **no static xref** from the opponent code - the actual source of drone colours is
  not yet located. Treat the colour-table claim as unconfirmed.
* **`+0x1C` state machine.** Values `0 / 2 / 8` clearly select among the per-frame
  update routines (`E4D0` accel, plus the cruise/lateral variants at
  `E637`/`E6A7`/`E77B`/`E7EB`), but the full state graph and the per-slot dispatcher
  (called from `JSL $808A41`) were not exhaustively traced. The accel path `E4D0`
  and both player paths `E6C4`/`E6DE` were traced in full.
* **`$0120` origin.** Used as a class/division index (range 0-2) for the start
  handicap; confirm where `$80:CD09` derives it.
* **Weather grip on drones.** The drone integrator (section 4) has no grip term,
  which suggests drones ignore the `$80:D0A2` (file 0x50A2) tire-grip grid that
  applies to the player. The lateral-simulation branch (`E473` family) was not fully
  traced, so a grip effect there is not 100% ruled out.
* **`$7E:C3E8[slot*2]`** parallel array (`= 0x40 * $7E:FE9E[slot]`) - purpose TBD.

---

## 12. Implemented fix: per-race difficulty ramp (randomizer `-d`)

Because stock drone speed is a fixed per-grid-slot ladder with no track or race
coupling, randomizing track order leaves race 1 as hard as race 64 (section 1). The
randomizer's `--ramp-drone-difficulty` (`-d`) option injects a small 65816 stub that
scales each drone's accel (`+0x30`) and cap (`+0x32`) by a factor indexed by the
**race index `$0108`**, so difficulty now ramps with campaign progress regardless of
which track is placed where. Implemented in `install_drone_ramp` in `randomizer.hpp`.

**Hook.** The init's per-slot assignment at `0x52B5` (the 14 bytes
`ADC $80D6D4,X / STA $C430,Y / LDA $80D6FC,X / STA $C432,Y`) is replaced by
`JSL $89:8440` + 10x `NOP`. The CLC at `0x52B4`, and the `DEX/DEX/BMI` loop tail at
`0x52C3`, are left intact.

**Stub** (in unused bank-`$89` padding, file `0x48440`):

```
drone_stat  $89:8440           ; entry A=RNG&0xFF, C=0, X=slot*2, Y=slot*0x40, DB=$7E
  CLC
  ADC $80D6D4,X                ; A = RNG + accel_base[slot]
  JSR scale16
  STA $C430,Y                  ; scaled accel -> +0x30
  LDA $80D6FC,X                ; cap[slot]
  JSR scale16
  STA $C432,Y                  ; scaled cap   -> +0x32
  RTL

scale16     $89:8460           ; A16 -> A16 * factor[$0108] / 128 ; preserves X,Y
  PHX
  SEP #$20
  STA $00211B / XBA / STA $00211B   ; M7A = value (Mode-7 hardware multiplier)
  LDX $0108
  LDA $898480,X                ; factor byte (<= 0x7F so the signed 16x8 stays positive)
  STA $00211C                  ; M7B = factor -> product at $2134-$2136
  REP #$20
  LDA $002135                  ; product >> 8
  ASL                          ; product >> 7   ( == value * factor / 128 )
  PLX
  RTS
```

This reuses the same Mode-7 multiplier the surrounding init already drives at
`$80:D1DE`, so it is safe in this context (forced blank, no contention).

**Factor table** (`$89:8480`, 64 bytes, one per race index): a linear ramp
`0x40 .. 0x7F`, i.e. race 0 = `0x40/128` = **0.50x**, race 63 = `0x7F/128` ~=
**0.99x**. Tunable via `factor_min` in `install_drone_ramp` (lower = easier early
races). Because both accel and cap scale by the same factor, the accelerate-to-cap
*profile shape* is preserved - drones are simply uniformly slower in early races.

**Status.** The injected bytes were verified by disassembly (they assemble to the
listing above and the loop tail is preserved), and the patch is gated behind `-d`
(no other bytes change without it). **Not yet validated on real hardware / emulator** -
the `factor_min` value in particular wants play-testing. Recommended invocation:
`tg2-randomizer -i in.sfc -o out.sfc -t -d`.
```
