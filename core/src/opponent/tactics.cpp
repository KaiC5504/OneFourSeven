// The tactics (spec 10.2, docs/opponent/README.md "Tactics"). Until they are built nothing is
// found, and the search plays the stand-in's stroke as it did in stage 1.

#include "ofs/opponent/tactics.hpp"

namespace ofs::opponent {

Tactic choose_tactic(const rules::FrameState&, const Level&, const Env&, Budget&, Rng&) { return {}; }

}  // namespace ofs::opponent
