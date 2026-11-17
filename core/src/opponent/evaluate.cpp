#include "ofs/opponent/evaluate.hpp"

namespace ofs::opponent {

Read read_pots(const std::vector<Pot>&, const Level&, const Env&) { return {}; }

Read read_position(const rules::FrameState&, const Level&, const Env&) { return {}; }

bool maximum_on(const rules::FrameState&) { return false; }

rules::Ball maximum_next(const rules::FrameState&) { return rules::Ball::red; }

}  // namespace ofs::opponent
