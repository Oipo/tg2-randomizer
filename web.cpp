// Emscripten/embind entry point for the browser build.
//
// Exposes a single function `Module.randomizeRom(romBytes, options)` to
// JavaScript. It takes the uploaded ROM as a Uint8Array plus a plain options
// object and returns { success, error, data }, where `data` is a Uint8Array
// holding the patched ROM. The actual work happens in the shared randomize_rom
// in randomizer.hpp, so the browser and the native CLI behave identically.

#include <cstdint>
#include <vector>

#include <emscripten/bind.h>
#include <emscripten/val.h>

#include "randomizer.hpp"

using namespace emscripten;

namespace {

bool get_bool(const val& opts, const char* key, bool fallback) {
    if(opts.isUndefined() || opts.isNull()) {
        return fallback;
    }
    val v = opts[key];
    if(v.isUndefined() || v.isNull()) {
        return fallback;
    }
    return v.as<bool>();
}

val randomize(val input, val opts) {
    RandomizerOptions options;
    options.randomize_weather    = get_bool(opts, "randomizeWeather", false);
    options.enable_space_weather = get_bool(opts, "enableSpaceWeather", false);
    options.show_starts          = get_bool(opts, "showStarts", false);
    options.randomize_tracks     = get_bool(opts, "randomizeTracks", false);
    options.ramp_drone_difficulty = get_bool(opts, "rampDroneDifficulty", false);

    if(!opts.isUndefined() && !opts.isNull()) {
        val weather_type = opts["weatherType"];
        if(!weather_type.isUndefined() && !weather_type.isNull()) {
            options.weather_type = weather_type.as<uint32_t>();
        }
        val seed = opts["seed"];
        if(!seed.isUndefined() && !seed.isNull()) {
            options.use_seed = true;
            options.seed = static_cast<uint64_t>(seed.as<double>());
        }
    }

    std::vector<uint8_t> in = convertJSArrayToNumberVector<uint8_t>(input);
    std::vector<char> bytes(in.begin(), in.end());

    RandomizerResult res = randomize_rom(std::move(bytes), options);

    val ret = val::object();
    ret.set("success", res.success);
    ret.set("error", res.error);
    if(res.success) {
        // Allocate a JS-owned Uint8Array and copy into it before res.data dies.
        val out = val::global("Uint8Array").new_(res.data.size());
        out.call<void>("set", val(typed_memory_view(
            res.data.size(), reinterpret_cast<const uint8_t*>(res.data.data()))));
        ret.set("data", out);
    }
    return ret;
}

} // namespace

EMSCRIPTEN_BINDINGS(tg2_randomizer) {
    function("randomizeRom", &randomize);
}
