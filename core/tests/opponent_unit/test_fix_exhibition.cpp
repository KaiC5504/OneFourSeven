// An exhibition layout with a ball off the cloth or in a pocket is refused, naming the ball.

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "ofs/opponent/exhibition.hpp"

#include "rig.hpp"

using namespace ofs;
using namespace ofs::opponent;

namespace {

bool loads_with_pink_at(double x, double y, std::vector<std::string>& problems, std::size_t& kept) {
    nlohmann::json f = rules::scenario::data_file("exhibitions.json");
    for (nlohmann::json& b : f["layouts"][0]["balls"]) {
        if (b["ball"] == "pink") {
            b["x"] = x;
            b["y"] = y;
        }
    }
    std::vector<Exhibition> out;
    const bool ok = load_exhibitions(f, out, problems);
    kept = f["layouts"].size() - out.size();
    return ok;
}

}  // namespace

TEST_CASE("an exhibition layout with a ball off the table, or in a pocket, is refused") {
    // exhibition.hpp: all fifteen reds and the six colours on the table. 1.80, 0.90 is in the top
    // left pocket's mouth; 5.0 is off the table altogether.
    for (const double x : {5.0, 1.80}) {
        CAPTURE(x);
        std::vector<std::string> problems;
        std::size_t refused = 0;
        CHECK_FALSE(loads_with_pink_at(x, 0.90, problems, refused));
        CHECK(refused == 1);
        REQUIRE(problems.size() == 1);
        CHECK(problems[0].find("the pink") != std::string::npos);
    }
}

TEST_CASE("a ball touching a cushion is on the cloth") {
    std::vector<std::string> problems;
    std::size_t refused = 0;
    const double hy = table::kPlayingWidth / 2.0 - table::kBallRadius;
    CHECK(loads_with_pink_at(0.9, hy, problems, refused));
    CHECK(problems.empty());
}

TEST_CASE("every pocket's drop circle lies beyond where a ball on the cloth can be") {
    // So the bound on the cloth refuses a ball in a pocket as well.
    const double hx = table::kPlayingLength / 2.0 - table::kBallRadius;
    const double hy = table::kPlayingWidth / 2.0 - table::kBallRadius;
    for (const phys::Pocket& p : rules::scenario::rig().table.pockets) {
        CAPTURE(p.name);
        const double nx = std::clamp(p.drop_centre.x, -hx, hx);
        const double ny = std::clamp(p.drop_centre.y, -hy, hy);
        CHECK(std::hypot(p.drop_centre.x - nx, p.drop_centre.y - ny) > p.drop_radius);
    }
}
