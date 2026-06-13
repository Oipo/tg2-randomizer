#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>
#include <numeric>
#include <random>
#include <string>
#include <utility>
#include <vector>

#include <spdlog/spdlog.h>

// std::shuffle and std::uniform_int_distribution are implementation-defined: the
// native CLI links libstdc++ while the Emscripten build links libc++, so they
// consume/map the mt19937 stream differently and the same seed would yield
// different ROMs on each. mt19937 itself is standardized, so these portable
// helpers (which only use raw rng() outputs) guarantee CLI/web parity for a seed.
// Changing either of these changes the output for every seed, so keep them stable.
inline uint32_t bounded_rng(std::mt19937& rng, uint32_t bound) {
    return static_cast<uint32_t>(rng()) % bound;  // bound must be > 0
}

template<typename T>
inline void portable_shuffle(std::vector<T>& v, std::mt19937& rng) {
    // Fisher-Yates, drawing one bounded value per step from the front.
    for(size_t i = v.size(); i > 1; --i) {
        size_t j = bounded_rng(rng, static_cast<uint32_t>(i));  // j in [0, i)
        std::swap(v[i - 1], v[j]);
    }
}

using PC_addr = uint32_t;

// this works for LoROM files only
struct SNES_addr {
    uint8_t bank;
    uint16_t addr;

    [[nodiscard]] PC_addr toPc() const {
    return (static_cast<PC_addr>(bank & 0x7F) * 0x8000u) + (addr & 0x7FFFu);
    }

    void fromPc(PC_addr pc) {
        bank = static_cast<uint8_t>(((pc / 0x8000u) & 0x7F) | 0x80);
        addr = static_cast<uint16_t>((pc % 0x8000u) | 0x8000u);
    }
};

// struct block_addr {
//     uint32_t start{};
//     uint32_t end{};
// };

struct BlockInfo {
    uint32_t track;
    uint32_t start;
    uint32_t end;
    uint32_t length;
    std::vector<char> data;
};

struct RandomizerOptions {
    bool randomize_weather{};
    bool enable_space_weather{};
    bool show_starts{};
    bool randomize_tracks{};
    // Inject the per-race drone difficulty ramp (see install_drone_ramp / the
    // reverse-engineering-docs/drone-speed.md). Recommended together with
    // randomize_tracks, which otherwise can drop a late-game track onto race 1.
    bool ramp_drone_difficulty{};
    uint32_t weather_type{std::numeric_limits<uint32_t>::max()};
    // When use_seed is set the RNG is seeded deterministically, so the same
    // input + options + seed always produces the same ROM (shareable seeds).
    // Otherwise a random_device seed is used, matching the original behaviour.
    bool use_seed{};
    uint64_t seed{};
};

struct RandomizerResult {
    bool success{};
    std::string error;
    std::vector<char> data;
};

// CRC32 (IEEE 802.3, the same variant used by zlib and the No-Intro database)
// of the headerless ROM image. Used to confirm the input is the exact ROM the
// randomizer's hardcoded offsets were reverse-engineered against.
inline uint32_t crc32_ieee(const std::vector<char>& data) {
    uint32_t crc = 0xFFFFFFFFu;
    for(char c : data) {
        crc ^= static_cast<unsigned char>(c);
        for(int k = 0; k < 8; ++k) {
            crc = (crc & 1u) ? (crc >> 1) ^ 0xEDB88320u : (crc >> 1);
        }
    }
    return crc ^ 0xFFFFFFFFu;
}

inline void print_track_starts(const std::vector<char>& data) {


    // 0x084000 is not code, it looks like an index table
    // 0x086C00 looks like planar graphic data
    // 0x008000, 0x008400, 0x002E00, 0x009000 dissasembly as code routines
    // 0x50400, 0x50600, 0x50A00, 0x50C00, where bytes 3..5 decode cleanly as lorom addresses for 35-52 rows

    // use fmt::println to print the start and end of each track block
    std::vector<uint32_t> block_starts;
    std::vector<BlockInfo> blocks{};
    block_starts.reserve(64);
    for(uint32_t track = 0; track < 64; track++) {
        const uint32_t row_offset = 0x50000 + track * 8;
        const uint16_t block_addr = static_cast<unsigned char>(data[row_offset])
                                    | (static_cast<uint16_t>(static_cast<unsigned char>(data[row_offset + 1])) << 8);
        block_starts.push_back(0x50000 + (block_addr - 0x8000));
    }

    for(uint32_t track = 0; track < 64; track++) {
        const uint32_t row_offset = 0x50000 + track * 8;
        const uint16_t word1 = static_cast<unsigned char>(data[row_offset + 2])
                               | (static_cast<uint16_t>(static_cast<unsigned char>(data[row_offset + 3])) << 8);
        const uint32_t block_start = block_starts[track];

        uint32_t block_end = data.size() - 1;
        for(uint32_t other_block_start : block_starts) {
            if(other_block_start > block_start) {
                block_end = std::min(block_end, other_block_start - 1);
            }
        }

        if(block_end == data.size() - 1) {
            for(uint32_t off = block_start; off + 3 < data.size(); off += 4) {
                const uint16_t record_word0 = static_cast<unsigned char>(data[off])
                                              | (static_cast<uint16_t>(static_cast<unsigned char>(data[off + 1])) << 8);
                const uint16_t record_word1 = static_cast<unsigned char>(data[off + 2])
                                              | (static_cast<uint16_t>(static_cast<unsigned char>(data[off + 3])) << 8);
                const bool looks_like_track_record = record_word0 >= 0x1000 && record_word1 <= 0x2000;
                if(!looks_like_track_record) {
                    block_end = off - 1;
                    break;
                }
            }
        }

        uint32_t c000_count = 0;
        uint32_t ec00_count = 0;
        for(uint32_t off = block_start; off + 1 <= block_end; off += 2) {
            const uint16_t word = static_cast<unsigned char>(data[off])
                                  | (static_cast<uint16_t>(static_cast<unsigned char>(data[off + 1])) << 8);
            if(word == 0xC000) {
                c000_count++;
            }
            if(word == 0xEC00) {
                ec00_count++;
            }
        }

        auto bytes_start = data.begin() + block_start;
        auto bytes_end = data.begin() + block_end;
        blocks.emplace_back(track, block_start, block_end, block_end - block_start + 1, std::vector<char>(bytes_start, bytes_end));
        const uint32_t block_length = block_end >= block_start ? (block_end - block_start + 1) : 0;
        fmt::println(
            "track {:02}: {:06X} - {:06X} len {:04X} word1 {:04X} C000 {} EC00 {}",
            track,
            block_start,
            block_end,
            block_length,
            word1,
            c000_count,
            ec00_count
        );
    }
    //
    // sort(blocks.begin(), blocks.end(), [](const auto& a, const auto& b) {
    //     return a.start < b.start;
    // });
    //
    // vector<BlockInfo> source = blocks;
    // reverse(source.begin(), source.end());
    // uint32_t cursor = blocks.front().start;
    //
    // for (size_t i = 0; i < blocks.size(); i++) {
    //     auto& dst_slot = blocks[i];
    //     auto& src_block = source[i];
    //
    //     copy(src_block.data.begin(), src_block.data.end(), bytes.begin() + cursor);
    //
    //     uint16_t new_word0 = static_cast<uint16_t>(0x8000 + (cursor - 0x50000));
    //     uint32_t row_offset = 0x50000 + dst_slot.track * 8;
    //     bytes[row_offset + 0] = static_cast<char>(new_word0 & 0xFF);
    //     bytes[row_offset + 1] = static_cast<char>(new_word0 >> 8);
    //
    //     cursor += static_cast<uint32_t>(src_block.data.size());
    // }

    // for(uint32_t addr = 0x84000; addr < 0x87000; addr += 1) {
    //     bytes[addr] = 0x3D;
    // }

    // SNES_addr asset_table{.bank = 0x9B, .addr = 0x8000};
    // auto asset_table_pc = asset_table.toPc();
    //
    // auto swap_words = [&](PC_addr a, PC_addr b) {
    //     std::swap(bytes[a + 0], bytes[b + 0]);
    //     std::swap(bytes[a + 1], bytes[b + 1]);
    // };
    //
    // swap_words(asset_table_pc + 2, asset_table_pc + 4);
}

// CRC32 of "Top Gear 2 (USA)" with any copier header removed.
inline constexpr uint32_t TG2_USA_CRC32 = 0x2B88BEE8u;

// Injects a 65816 stub that scales each drone's accel (+0x30) and top-speed cap
// (+0x32) by a per-RACE factor, indexed by the race index $0108, so AI difficulty
// ramps with campaign progress instead of being a fixed per-grid-slot ladder.
// Stock drone speed is track-independent, so track randomization otherwise breaks
// the difficulty curve (a hard track can land on race 1 with a no-upgrade car).
// Full analysis + the assembly listing live in reverse-engineering-docs/drone-speed.md.
//
// The stub and its factor table live in unused $89 bank padding (file 0x48440).
// The scale uses the Mode-7 hardware multiplier the surrounding init code already
// drives; factor is kept <= 0x7F so the signed 16x8 multiply stays positive, and
// the result is value * factor / 128 (race 0 = 0x40/128 = 0.50x, race 63 ~= 1.0x).
inline bool install_drone_ramp(std::vector<char>& bytes, std::string& error) {
    constexpr uint32_t patch_site = 0x52B5;   // ADC $80D6D4,X .. STA $C432,Y (14 bytes)
    constexpr uint32_t code_file  = 0x48440;  // $89:8440  drone_stat
    constexpr uint32_t scale_file = 0x48460;  // $89:8460  scale16
    constexpr uint32_t table_file = 0x48480;  // $89:8480  factor table (64 bytes)
    constexpr uint8_t  factor_min = 0x40;     // race 0  scale = 0x40/128 = 0.50x
    constexpr uint8_t  factor_max = 0x7F;     // race 63 scale ~= 1.0x  (0x7F/128)

    // Safety: confirm we are overwriting exactly the stock init instructions.
    static constexpr uint8_t orig[14] = {0x7F,0xD4,0xD6,0x80, 0x99,0x30,0xC4,
                                         0xBF,0xFC,0xD6,0x80, 0x99,0x32,0xC4};
    for (uint32_t i = 0; i < 14; ++i) {
        if (static_cast<uint8_t>(bytes[patch_site + i]) != orig[i]) {
            error = "drone ramp: init patch site does not match the expected stock bytes";
            return false;
        }
    }
    // Safety: confirm the target free space is actually empty.
    for (uint32_t i = 0; i < 0xC0; ++i) {
        if (bytes[code_file + i] != 0) {
            error = "drone ramp: free space at file 0x48440 is not empty";
            return false;
        }
    }

    auto put = [&](uint32_t off, std::initializer_list<uint8_t> b) {
        uint32_t i = 0;
        for (uint8_t v : b) bytes[off + i++] = static_cast<char>(v);
    };

    // Patch site: JSL $89:8440 then pad the remaining 10 bytes with NOPs.
    put(patch_site, {0x22,0x40,0x84,0x89,
                     0xEA,0xEA,0xEA,0xEA,0xEA,0xEA,0xEA,0xEA,0xEA,0xEA});

    // drone_stat @ $89:8440  (entry: A = RNG&0xFF, C=0, X=slot*2, Y=slot*0x40, DB=$7E)
    put(code_file, {
        0x18,                    // CLC
        0x7F,0xD4,0xD6,0x80,     // ADC $80D6D4,X      ; A = RNG + accel_base[slot]
        0x20,0x60,0x84,          // JSR $8460          ; scale16
        0x99,0x30,0xC4,          // STA $C430,Y        ; +0x30 accel
        0xBF,0xFC,0xD6,0x80,     // LDA $80D6FC,X      ; A = cap[slot]
        0x20,0x60,0x84,          // JSR $8460          ; scale16
        0x99,0x32,0xC4,          // STA $C432,Y        ; +0x32 cap
        0x6B,                    // RTL
    });

    // scale16 @ $89:8460  (A16 -> A16 * factor[$0108] / 128; preserves X, Y)
    put(scale_file, {
        0xDA,                    // PHX
        0xE2,0x20,               // SEP #$20           ; 8-bit A
        0x8F,0x1B,0x21,0x00,     // STA $00211B        ; M7A low  = value.lo
        0xEB,                    // XBA
        0x8F,0x1B,0x21,0x00,     // STA $00211B        ; M7A high = value.hi
        0xAE,0x08,0x01,          // LDX $0108          ; X = race index (0..63)
        0xBF,0x80,0x84,0x89,     // LDA $898480,X      ; factor byte
        0x8F,0x1C,0x21,0x00,     // STA $00211C        ; M7B = factor -> multiply
        0xC2,0x20,               // REP #$20           ; 16-bit A
        0xAF,0x35,0x21,0x00,     // LDA $002135        ; product >> 8
        0x0A,                    // ASL                ; product >> 7  (* factor / 128)
        0xFA,                    // PLX
        0x60,                    // RTS
    });

    // factor table @ $89:8480 : linear ramp factor_min -> factor_max over races 0..63.
    for (int i = 0; i < 64; ++i) {
        int f = factor_min + ((factor_max - factor_min) * i) / 63;
        bytes[table_file + i] = static_cast<char>(static_cast<uint8_t>(f));
    }
    return true;
}

// Applies the randomizer to a raw ROM image and returns the patched bytes.
// This is the single source of truth shared by the native CLI and the web
// (Emscripten) build, so both produce identical output for identical input.
inline RandomizerResult randomize_rom(std::vector<char> bytes, const RandomizerOptions& options) {
    using std::numeric_limits;
    using std::vector;

    RandomizerResult result;

    if (options.weather_type != numeric_limits<uint32_t>::max() && options.weather_type > 7) {
        result.error = "Weather type cannot be more than 7";
        return result;
    }

    bool has_copier_header = (bytes.size() % 1024) == 512;
    if(has_copier_header) {
        bytes.erase(bytes.begin(), bytes.begin() + 512);
    }

    if(bytes.size() <= 0x7FD9) {
        result.error = "Given input ROM is too small to be a Top Gear 2 ROM.";
        return result;
    }

    // Verify this is exactly the ROM the randomizer was built for. All the patch
    // offsets below are hardcoded against "Top Gear 2 (USA)", so a different
    // region/revision/dump would be silently corrupted.
    const uint32_t crc = crc32_ieee(bytes);
    if(crc != TG2_USA_CRC32) {
        result.error = fmt::format(
            "Given input ROM is not \"Top Gear 2 (USA)\" (CRC32 {:08X}, expected {:08X}).",
            crc, TG2_USA_CRC32);
        return result;
    }

    std::mt19937 rng;
    if(options.use_seed) {
        rng.seed(static_cast<std::mt19937::result_type>(options.seed));
    } else {
        std::random_device dev;
        rng.seed(dev());
    }

    if(options.randomize_weather) {
        // 0 to 2 = normal
        // 3 = rain
        // 4 = snow
        // 5 = fog
        // 6 = night
        // 7 = space
        int end = 6;
        if(options.enable_space_weather) {
            end = 7;
        }
        for(uint32_t i = 0x50006; i <= 0x501fe; i += 8) {
            if(options.weather_type != numeric_limits<uint32_t>::max()) {
                bytes[i] = options.weather_type;
            } else {
                // inclusive range [2, end]
                bytes[i] = static_cast<char>(2 + bounded_rng(rng, static_cast<uint32_t>(end - 2 + 1)));
            }
        }
    }

    if(options.show_starts) {
        uint32_t j = 0;
        for(uint32_t i = 0x50000; i <= 0x501f8; i += 8) {
            spdlog::warn("track {}: {:X} {:X}, addr {:X} {:X}", j, static_cast<unsigned char>(bytes[i]), static_cast<unsigned char>(bytes[i+1]), i, i+1);
            j++;
        }
    }

    // probably a 5 x 4 = 20 byte table, could be longer?
    // constexpr PC_addr track_audio_selector_table = 0x0FA20C;
    // std::swap(bytes[track_audio_selector_table + 0], bytes[track_audio_selector_table + 1]);
    //
    // constexpr PC_addr experiment = 0x3A676;
    // std::swap(bytes[experiment + 0], bytes[experiment + 2]);
    // std::swap(bytes[experiment + 1], bytes[experiment + 3]);

    // for (int i = 0; i < 8; i++) {
    //     std::swap(bytes[0x50000 + i], bytes[0x50008 + i]);
    // }

    auto print = [&](char const * str, uint32_t addr, uint32_t len) {
        while(len > 0) {
            fmt::println("{} {:X}: {:X}", str, addr, static_cast<unsigned char>(bytes[addr]));
            addr++;
            len--;
        }
        fmt::println("");
    };

    print("engine?", 0x5036, 4);
    print("engine nitro?", 0x503A, 4);

    print("nitro count", 0x50C2, 8);
    print("nitro time", 0x50CA, 8);
    print("tire grip?", 0x50A2, 32);
    print("gearbox count?", 0x50D2, 8);
    print("gearbox offset?", 0x3DF3, 8);
    print("gearbox ratios?", 0x3DFB, 24);
    print("acceleration???", 0x3E2B, 8);
    print("gearbox top-speed/RPM?", 0x3E33, 8);

    // bytes[0x5036] = 0x34; // no idea
    // bytes[0x5037] = 0x34;
    // bytes[0x5038] = 0x34;
    // bytes[0x5039] = 0x34;
    //
    // bytes[0x503A] = 0x74; // no idea
    // bytes[0x503B] = 0x74;
    // bytes[0x503C] = 0x74;
    // bytes[0x503D] = 0x74;
    //
    // bytes[0x50C2] = 0x09; // confirmed nitro count, above 9 the graphics disappear but it still seems to work
    // bytes[0x50C4] = 0x09;
    // bytes[0x50C6] = 0x09;
    // bytes[0x50C8] = 0x09;

    // bytes[0x50A2] = 0x10;
    // bytes[0x50A3] = 0x10;
    // bytes[0x50A4] = 0x10;
    // bytes[0x50A5] = 0x10;
    // bytes[0x50A6] = 0x10;
    // bytes[0x50A7] = 0x10;
    // bytes[0x50A8] = 0x10;
    // bytes[0x50A9] = 0x10;
    // bytes[0x50AA] = 0x10;
    // bytes[0x50AB] = 0x10;
    // bytes[0x50AC] = 0x10;
    // bytes[0x50AD] = 0x10;
    // bytes[0x50AE] = 0x10;
    // bytes[0x50AF] = 0x10;
    // bytes[0x50B0] = 0x10;
    // bytes[0x50B1] = 0x10;
    // bytes[0x50B2] = 0x10;
    // bytes[0x50B3] = 0x10;
    // bytes[0x50B4] = 0x10;
    // bytes[0x50B5] = 0x10;
    // bytes[0x50B6] = 0x10;
    // bytes[0x50B7] = 0x10;
    // bytes[0x50B8] = 0x10;
    // bytes[0x50B9] = 0x10;
    // bytes[0x50BA] = 0x10;
    // bytes[0x50BB] = 0x10;
    // bytes[0x50BC] = 0x10;
    // bytes[0x50BD] = 0x10;
    // bytes[0x50BE] = 0x10;
    // bytes[0x50BF] = 0x10;
    // bytes[0x50C0] = 0x10;
    // bytes[0x50C1] = 0x10;
    //
    // bytes[0x1141] = 0xF0; // changes steering on straights
    // bytes[0x29A3] = 0xF0;

    // bytes[0x50CB] = 0x00; // confirmed nitro time effect, lower is faster
    // bytes[0x50CD] = 0x00;
    // bytes[0x50CF] = 0x00;
    // bytes[0x50D1] = 0x00;
    //
    // bytes[0x3DF3] = 0x11; // confirmed changes the number of gears (but not the graphics!)
    // bytes[0x3DF5] = 0x11;
    // bytes[0x3DF7] = 0x11;
    // bytes[0x3DF9] = 0x11;
    //
    // bytes[0x3E2C] = 0x06; // changes something about the acceleration, maybe this is engine acceleration?
    // bytes[0x3E2E] = 0x02;
    // bytes[0x3E30] = 0x04;
    // bytes[0x3E32] = 0x06;

    // bytes[0x3E33] = 0xF8; // no idea
    // bytes[0x3E34] = 0x58;

    // bytes[0x50D2] = 0x06;
    // bytes[0x50D4] = 0x07;
    // bytes[0x50D6] = 0x07;
    // bytes[0x50D8] = 0x08;

    // return 1;


    if(options.randomize_tracks) {
        vector<uint32_t> perm(64);
        std::iota(perm.begin(), perm.end(), 0);
        portable_shuffle(perm, rng);

        constexpr PC_addr path_table = 0x50000;        // 8-byte rows: path offset, length, weather
        constexpr PC_addr gfx_record_table = 0x3A676;  // 16-bit record offsets in bank $87 (scenery + minimap)
        constexpr PC_addr music_table = 0x0FA20C;      // 1 byte per track
        constexpr PC_addr drone_start_table = 0x55FE;  // 16-bit per-track drone start-line base ($80:D5FE)

        vector<char> orig(bytes);
        for(uint32_t slot = 0; slot < 64; slot++) {
            uint32_t src = perm[slot];
            std::copy_n(orig.begin() + path_table + src*8, 8, bytes.begin() + path_table + slot*8);
            std::copy_n(orig.begin() + gfx_record_table + src*2, 2, bytes.begin() + gfx_record_table + slot*2);
            // D5FE is indexed by the same track index $0108 as the path table, so the
            // drone start positions must travel with the geometry/length (see
            // reverse-engineering-docs/drone-speed.md section 8).
            std::copy_n(orig.begin() + drone_start_table + src*2, 2, bytes.begin() + drone_start_table + slot*2);
            bytes[music_table + slot] = orig[music_table + src];
        }
    }

    if(options.ramp_drone_difficulty) {
        if(!install_drone_ramp(bytes, result.error)) {
            return result;
        }
    }

    result.success = true;
    result.data = std::move(bytes);
    return result;
}
