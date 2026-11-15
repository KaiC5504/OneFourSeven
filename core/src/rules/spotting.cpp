// Contract stub (M3): replaced by the geometry builder.

#include "spotting.hpp"

namespace ofs::rules {

std::vector<BallOnTable> spot_colours(const std::vector<BallOnTable>&, const std::vector<Ball>& colours, double) {
    std::vector<BallOnTable> out;
    for (Ball colour : colours) {
        out.push_back({colour, 0, table::spot(colour)});
    }
    return out;
}

}  // namespace ofs::rules
