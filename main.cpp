#include <fstream>
#include <iterator>
#include <lyra/lyra.hpp>
#include <spdlog/spdlog.h>
#include <vector>

#include "randomizer.hpp"

using namespace std;

int main(int argc, const char** argv) {
    string input_file;
    string output_file;

    bool help{};
    RandomizerOptions options;
    uint64_t seed{numeric_limits<uint64_t>::max()};

    auto cli = lyra::opt(input_file, "input")
                ["-i"]["--input"]("Which rom to take as input").required()
               | lyra::opt(output_file, "output")
                ["-o"]["--output"]("What filename to output to").required()
               | lyra::opt(options.randomize_weather)
                ["-w"]["--randomize-weather"]("Randomize weather for all tracks")
               | lyra::opt(options.enable_space_weather)
                ["--enable-space"]("When randomizing weather, add space type to the rng distribution")
               | lyra::opt(options.show_starts)
                ["--show-starts"]("Debug option to show where tracks (I think) start")
               | lyra::opt(options.randomize_tracks)
                ["-t"]["--randomize-tracks"]("Randomize track order")
               | lyra::opt(options.ramp_drone_difficulty)
                ["-d"]["--ramp-drone-difficulty"]("Scale drone speed by race order so difficulty ramps with progress (recommended with -t)")
               | lyra::opt(options.weather_type, "weather type")
                ["--weather-type"]("Sets weather for all tracks to given type (0 to 7)")
               | lyra::opt(seed, "seed")
                ["-s"]["--seed"]("Seed the RNG with this value (same seed + options reproduces the same ROM, identical to the web version)");

    cli.add_argument(lyra::help(help));

    auto parse = cli.parse({argc, argv});

    if(help) {
        cout << cli;
        return 0;
    }

    if(!parse) {
        spdlog::error("Error in command line: {}", parse.message());
        return 1;
    }

    if(seed != numeric_limits<uint64_t>::max()) {
        options.use_seed = true;
        options.seed = seed;
    }

    ifstream input(input_file, ios::binary);
    ofstream output(output_file, ios::binary | ios::trunc);

    vector<char> bytes{istreambuf_iterator<char>(input), istreambuf_iterator<char>()};

    auto result = randomize_rom(std::move(bytes), options);
    if(!result.success) {
        spdlog::error("Input ROM \"{}\": {}", input_file, result.error);
        return 1;
    }

    copy(result.data.begin(), result.data.end(), ostreambuf_iterator<char>(output));

    return 0;
}
