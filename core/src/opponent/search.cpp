#include "ofs/opponent/opponent.hpp"

#include "ofs/opponent/standin.hpp"

namespace ofs::opponent {

// Until the search is built the stand-in plays every stroke, with the level's error in place of
// its own.
Stroke choose_stroke(const rules::FrameState& s, const Level& level, const Env& env, Rng& rng, const Think&) {
    StandinParams sp = env.standin;
    sp.aim_error_rad = level.aim_sigma_rad;
    sp.speed_error = level.speed_sigma;
    const StandinShot shot = choose_shot(s, env.table, env.params, env.oracle, env.ref, sp, rng);
    Stroke out;
    out.decides = shot.decides;
    out.decision = shot.decision;
    out.places_cue = shot.places_cue;
    out.cue_pos = shot.cue_pos;
    out.input = shot.input;
    out.strike = shot.strike;
    out.meant = shot.strike;
    out.why = shot.why.is_object() ? shot.why : nlohmann::json::object();
    out.why["level"] = level.id;
    if (out.why.contains("candidates") && out.why["candidates"].is_number_integer()) {
        out.candidates = out.why["candidates"].get<int>();
    }
    if (out.why.contains("sims") && out.why["sims"].is_number_integer()) out.sims = out.why["sims"].get<int>();
    out.steps = 1;
    return out;
}

}  // namespace ofs::opponent
