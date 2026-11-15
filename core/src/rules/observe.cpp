// Contract stub (M3): replaced by the builder of the referee's eyes.

#include "ofs/rules/facts.hpp"

namespace ofs::rules {

ShotFacts observe(const Position& pre, const phys::ShotRecord&, const phys::TableShape&, const RefParams&) {
    ShotFacts f;
    for (const BallOnTable& b : pre.balls) {
        f.rest.push_back(b.pos);
        f.closest_gap_m.push_back(0.0);
    }
    return f;
}

}  // namespace ofs::rules
