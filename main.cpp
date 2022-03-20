#include <fstream>
#include <iterator>
#include <lyra/lyra.hpp>
#include <spdlog/spdlog.h>
#include <vector>
#include <random>

using namespace std;

int main(int argc, const char** argv)
{
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
        for(uint64_t i = 0x50006; i <= 0x501fe; i += 8) {
            if(weather_type != numeric_limits<uint32_t>::max()) {
                bytes[i] = weather_type;
            } else {
                bytes[i] = dist6(rng);
            }
        }
    }

    if(show_starts) {
        uint64_t j = 0;
        for(uint64_t i = 0x50000; i <= 0x501f8; i += 8) {
            spdlog::warn("track {}: {:X} {:X}, addr {:X} {:X}", j, static_cast<unsigned char>(bytes[i]), static_cast<unsigned char>(bytes[i+1]), i, i+1);
            j++;
        }
    }

    if(randomize_tracks) {
        vector<vector<char>> temp_tracks;
        for(uint64_t i = 0; i < 64; i++) {
            auto start = bytes.begin() + 0x50000 + i*8;
            temp_tracks.emplace_back(start, start + 8);
        }

        uint64_t j = 0x50000;
        while(!temp_tracks.empty()) {
            uniform_int_distribution<mt19937::result_type> dist(0, temp_tracks.size() - 1);
            auto pos = dist(rng);
            auto &vec = temp_tracks.at(pos);
            spdlog::info("track {} size {}", j, vec.size());
            copy(vec.begin(), vec.end(), bytes.begin() + j);
            j += 8;
            temp_tracks.erase(temp_tracks.begin() + pos);
        }
    }

    copy(bytes.begin(), bytes.end(), ostreambuf_iterator<char>(output));

    return 0;
}