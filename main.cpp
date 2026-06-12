#include <fstream>
#include <iterator>
#include <lyra/lyra.hpp>
#include <spdlog/spdlog.h>
#include <vector>
#include <random>
#include <numeric>

using namespace std;

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
    vector<char> data;
};

int main(int argc, const char** argv) {
    string input_file;
    string output_file;

    bool help{};
    bool randomize_weather{};
    bool enable_space_weather{};
    bool show_starts{};
    bool randomize_tracks{};
    uint32_t weather_type{numeric_limits<uint32_t>::max()};

    auto cli = lyra::opt(input_file, "input")
                ["-i"]["--input"]("Which rom to corrupt").required()
               | lyra::opt(output_file, "output")
                ["-o"]["--output"]("What filename to output to").required()
               | lyra::opt(randomize_weather)
                ["-w"]["--randomize-weather"]("Randomize weather for all tracks")
               | lyra::opt(enable_space_weather)
                ["--enable-space"]("When randomizing weather, add space type to the rng distribution")
               | lyra::opt(show_starts)
                ["--show-starts"]("When randomizing weather, add space type to the rng distribution")
               | lyra::opt(randomize_tracks)
                ["-t"]["--randomize-tracks"]("When randomizing weather, add space type to the rng distribution")
               | lyra::opt(weather_type, "weather type")
                ["--weather-type"]("Sets weather for all tracks to given type (0 to 7)");

    cli.add_argument(lyra::help(help));

    auto result = cli.parse({argc, argv});

    if(help) {
        cout << cli;
        return 0;
    }

    if(!result) {
        spdlog::error("Error in command line: {}", result.message());
        return 1;
    }

    if (weather_type != numeric_limits<uint32_t>::max() && weather_type > 7) {
        spdlog::error("Weather type cannot be more than 7");
        return 1;
    }

    ifstream input(input_file, ios::binary);
    ofstream output(output_file, ios::binary | ios::trunc);

    vector<char> bytes{istreambuf_iterator<char>(input), istreambuf_iterator<char>()};

    bool has_copier_header = (bytes.size() % 1024) == 512;
    if(has_copier_header) {
        bytes.erase(bytes.begin(), bytes.begin() + 512);
    }

    if(bytes[0x7FD9] != 0x01) {
        spdlog::error("Given input ROM \"{}\" is not NTSC SNES ROM. Instead, got {}.", input_file, bytes[0x7FD9]);
        return 1;
    }

    random_device dev;
    mt19937 rng(dev());

    if(randomize_weather) {
        // 0 to 2 = normal
        // 3 = rain
        // 4 = snow
        // 5 = fog
        // 6 = night
        // 7 = space
        int end = 6;
        if(enable_space_weather) {
            end = 7;
        }
        uniform_int_distribution<mt19937::result_type> dist6(2,end);
        for(uint32_t i = 0x50006; i <= 0x501fe; i += 8) {
            if(weather_type != numeric_limits<uint32_t>::max()) {
                bytes[i] = weather_type;
            } else {
                bytes[i] = dist6(rng);
            }
        }
    }

    if(show_starts) {
        uint32_t j = 0;
        for(uint32_t i = 0x50000; i <= 0x501f8; i += 8) {
            spdlog::warn("track {}: {:X} {:X}, addr {:X} {:X}", j, static_cast<unsigned char>(bytes[i]), static_cast<unsigned char>(bytes[i+1]), i, i+1);
            j++;
        }
    }

    // 0x084000 is not code, it looks like an index table
    // 0x086C00 looks like planar graphic data
    // 0x008000, 0x008400, 0x002E00, 0x009000 dissasembly as code routines
    // 0x50400, 0x50600, 0x50A00, 0x50C00, where bytes 3..5 decode cleanly as lorom addresses for 35-52 rows

    // use fmt::println to print the start and end of each track block
    // vector<uint32_t> block_starts;
    // vector<BlockInfo> blocks{};
    // block_starts.reserve(64);
    // for(uint32_t track = 0; track < 64; track++) {
    //     const uint32_t row_offset = 0x50000 + track * 8;
    //     const uint16_t block_addr = static_cast<unsigned char>(bytes[row_offset])
    //                                 | (static_cast<uint16_t>(static_cast<unsigned char>(bytes[row_offset + 1])) << 8);
    //     block_starts.push_back(0x50000 + (block_addr - 0x8000));
    // }
    //
    // for(uint32_t track = 0; track < 64; track++) {
    //     const uint32_t row_offset = 0x50000 + track * 8;
    //     const uint16_t word1 = static_cast<unsigned char>(bytes[row_offset + 2])
    //                            | (static_cast<uint16_t>(static_cast<unsigned char>(bytes[row_offset + 3])) << 8);
    //     const uint32_t block_start = block_starts[track];
    //
    //     uint32_t block_end = bytes.size() - 1;
    //     for(uint32_t other_block_start : block_starts) {
    //         if(other_block_start > block_start) {
    //             block_end = min(block_end, other_block_start - 1);
    //         }
    //     }
    //
    //     if(block_end == bytes.size() - 1) {
    //         for(uint32_t off = block_start; off + 3 < bytes.size(); off += 4) {
    //             const uint16_t record_word0 = static_cast<unsigned char>(bytes[off])
    //                                           | (static_cast<uint16_t>(static_cast<unsigned char>(bytes[off + 1])) << 8);
    //             const uint16_t record_word1 = static_cast<unsigned char>(bytes[off + 2])
    //                                           | (static_cast<uint16_t>(static_cast<unsigned char>(bytes[off + 3])) << 8);
    //             const bool looks_like_track_record = record_word0 >= 0x1000 && record_word1 <= 0x2000;
    //             if(!looks_like_track_record) {
    //                 block_end = off - 1;
    //                 break;
    //             }
    //         }
    //     }
    //
    //     uint32_t c000_count = 0;
    //     uint32_t ec00_count = 0;
    //     for(uint32_t off = block_start; off + 1 <= block_end; off += 2) {
    //         const uint16_t word = static_cast<unsigned char>(bytes[off])
    //                               | (static_cast<uint16_t>(static_cast<unsigned char>(bytes[off + 1])) << 8);
    //         if(word == 0xC000) {
    //             c000_count++;
    //         }
    //         if(word == 0xEC00) {
    //             ec00_count++;
    //         }
    //     }
    //
    //     auto bytes_start = bytes.begin() + block_start;
    //     auto bytes_end = bytes.begin() + block_end;
    //     blocks.emplace_back(track, block_start, block_end, block_end - block_start + 1, vector<char>(bytes_start, bytes_end));
    //     const uint32_t block_length = block_end >= block_start ? (block_end - block_start + 1) : 0;
    //     fmt::println(
    //         "track {:02}: {:06X} - {:06X} len {:04X} word1 {:04X} C000 {} EC00 {}",
    //         track,
    //         block_start,
    //         block_end,
    //         block_length,
    //         word1,
    //         c000_count,
    //         ec00_count
    //     );
    // }
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


    if(randomize_tracks) {
        vector<uint32_t> perm(64);
        iota(perm.begin(), perm.end(), 0);
        shuffle(perm.begin(), perm.end(), rng);

        constexpr PC_addr path_table = 0x50000;        // 8-byte rows: path offset, length, weather
        constexpr PC_addr gfx_record_table = 0x3A676;  // 16-bit record offsets in bank $87 (scenery + minimap)
        constexpr PC_addr music_table = 0x0FA20C;      // 1 byte per track

        vector<char> orig(bytes);
        for(uint32_t slot = 0; slot < 64; slot++) {
            uint32_t src = perm[slot];
            copy_n(orig.begin() + path_table + src*8, 8, bytes.begin() + path_table + slot*8);
            copy_n(orig.begin() + gfx_record_table + src*2, 2, bytes.begin() + gfx_record_table + slot*2);
            bytes[music_table + slot] = orig[music_table + src];
        }
    }

    copy(bytes.begin(), bytes.end(), ostreambuf_iterator<char>(output));

    return 0;
}
