#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <memory>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

#include "ofs/log.hpp"
#include "ofs/table_geometry.hpp"

// Citations like 1.1(f) are Section 1 Rule 1(f) of the WPBSA Official Rules (2022
// rulebook); "Header" means the contract in table_geometry.hpp.

using namespace ofs::table;

namespace {

constexpr double kExact = 1e-12;
constexpr double kSpotTol = 1e-9;

const Ball kColours[] = {Ball::yellow, Ball::green, Ball::brown,
                         Ball::blue,   Ball::pink,  Ball::black};
const double kGaps[] = {0.0, 0.0005};

double dist(Vec2 a, Vec2 b) { return std::hypot(a.x - b.x, a.y - b.y); }

void check_near(Vec2 got, Vec2 want, double tol) {
    CAPTURE(got.x);
    CAPTURE(got.y);
    CAPTURE(want.x);
    CAPTURE(want.y);
    CHECK(std::abs(got.x - want.x) <= tol);
    CHECK(std::abs(got.y - want.y) <= tol);
}

// Reds are numbered row by row from the apex, so row k holds indices k(k+1)/2 onwards.
std::vector<std::vector<Vec2>> red_rows(const std::vector<BallPlacement>& layout) {
    std::vector<std::vector<Vec2>> rows(5);
    for (int k = 0; k < 5; ++k) {
        for (int j = 0; j <= k; ++j) {
            rows[k].push_back(layout[1 + k * (k + 1) / 2 + j].position);
        }
        std::sort(rows[k].begin(), rows[k].end(),
                  [](Vec2 a, Vec2 b) { return a.y < b.y; });
    }
    return rows;
}

const double kL2 = 3.569 / 2.0;
const double kBaulkLine = -kL2 + 0.737;

}  // namespace

TEST_CASE("table constants match the rulebook") {
    // 1.1(a): playing area 3569 mm x 1778 mm.
    CHECK(kPlayingLength == 3.569);
    CHECK(kPlayingWidth == 1.778);
    // 1.2(b): balls 52.5 mm.
    CHECK(kBallDiameter == 0.0525);
    CHECK(std::abs(kBallRadius - 0.02625) <= kExact);
    // 1.1(d): baulk line 737 mm from the bottom cushion face.
    CHECK(kBaulkLineFromBaulkCushion == 0.737);
    // 1.1(e): D radius 292 mm.
    CHECK(kDRadius == 0.292);
    // 1.1(f)(iv): black spot 324 mm from the top cushion face.
    CHECK(kBlackSpotFromTopCushion == 0.324);
}

TEST_CASE("cushion faces, baulk line and D centre") {
    // Header: origin at the centre of the playing area, +x towards the top cushion.
    CHECK(std::abs(baulk_cushion_x() - -kL2) <= kExact);
    CHECK(std::abs(top_cushion_x() - kL2) <= kExact);
    CHECK(std::abs(top_cushion_x() - baulk_cushion_x() - kPlayingLength) <= kExact);
    // 1.1(d)
    CHECK(std::abs(baulk_line_x() - kBaulkLine) <= kExact);
    CHECK(std::abs(baulk_line_x() - baulk_cushion_x() - 0.737) <= kExact);
    // 1.1(e): the centre of the D's straight section is the middle of the baulk line.
    check_near(d_centre(), {kBaulkLine, 0.0}, kExact);
}

TEST_CASE("colour spots") {
    // 1.1(f): yellow is the right-hand corner of the D seen from baulk, which is -y.
    check_near(spot(Ball::yellow), {kBaulkLine, -0.292}, kSpotTol);
    check_near(spot(Ball::green), {kBaulkLine, 0.292}, kSpotTol);
    // 1.1(f)(i)
    check_near(spot(Ball::brown), {kBaulkLine, 0.0}, kSpotTol);
    // 1.1(f)(ii)
    check_near(spot(Ball::blue), {0.0, 0.0}, kSpotTol);
    // 1.1(f)(iii): midway between the blue spot and the top cushion face.
    check_near(spot(Ball::pink), {kL2 / 2.0, 0.0}, kSpotTol);
    // 1.1(f)(iv)
    check_near(spot(Ball::black), {kL2 - 0.324, 0.0}, kSpotTol);
}

TEST_CASE("ball values") {
    // Header: 1 for red, 2 (yellow) through 7 (black), 0 for the cue ball.
    CHECK(ball_value(Ball::cue) == 0);
    CHECK(ball_value(Ball::red) == 1);
    CHECK(ball_value(Ball::yellow) == 2);
    CHECK(ball_value(Ball::green) == 3);
    CHECK(ball_value(Ball::brown) == 4);
    CHECK(ball_value(Ball::blue) == 5);
    CHECK(ball_value(Ball::pink) == 6);
    CHECK(ball_value(Ball::black) == 7);
}

TEST_CASE("ball names round-trip through parse_ball") {
    // Header: "cue", "red", "yellow", ...; the rest follow the enum spelling.
    const struct {
        Ball ball;
        std::string_view name;
    } cases[] = {{Ball::cue, "cue"},     {Ball::red, "red"},   {Ball::yellow, "yellow"},
                 {Ball::green, "green"}, {Ball::brown, "brown"}, {Ball::blue, "blue"},
                 {Ball::pink, "pink"},   {Ball::black, "black"}};
    for (const auto& c : cases) {
        CAPTURE(c.name);
        CHECK(ball_name(c.ball) == c.name);
        Ball parsed = Ball::cue;
        CHECK(parse_ball(ball_name(c.ball), parsed));
        CHECK(parsed == c.ball);
    }
}

TEST_CASE("parse_ball rejects unknown names") {
    for (std::string_view name : {"", "purple", "reds", "re", "blackk", "cue ball"}) {
        CAPTURE(name);
        Ball out = Ball::cue;
        CHECK_FALSE(parse_ball(name, out));
    }
}

TEST_CASE("in_d") {
    // 1.1(e) and 3.5: the cue ball may be placed on or within the lines of the D.
    const Vec2 c = d_centre();
    const double r = kDRadius;
    CHECK(in_d(c));

    // The corners are on both the arc and the baulk line, and exact in binary.
    CHECK(in_d(spot(Ball::yellow)));
    CHECK(in_d(spot(Ball::green)));
    CHECK(in_d({c.x - r * (1.0 - 1e-12), 0.0}));
    for (double deg : {15.0, 30.0, 45.0, 60.0, 75.0}) {
        CAPTURE(deg);
        const double a = deg * 3.14159265358979323846 / 180.0;
        const double in = r * (1.0 - 1e-12);
        CHECK(in_d({c.x - in * std::cos(a), in * std::sin(a)}));
        CHECK(in_d({c.x - in * std::cos(a), -in * std::sin(a)}));

        const double out = r * (1.0 + 1e-9);
        CHECK_FALSE(in_d({c.x - out * std::cos(a), out * std::sin(a)}));
        CHECK_FALSE(in_d({c.x - out * std::cos(a), -out * std::sin(a)}));
    }
    CHECK_FALSE(in_d({c.x - r * (1.0 + 1e-9), 0.0}));
    CHECK_FALSE(in_d({c.x, r * (1.0 + 1e-9)}));

    CHECK(in_d({baulk_line_x(), 0.1}));
    CHECK(in_d({baulk_line_x(), -0.2}));

    // In front of the baulk line is outside the D even within the radius.
    CHECK_FALSE(in_d({baulk_line_x() + 1e-9, 0.0}));
    CHECK_FALSE(in_d({baulk_line_x() + 0.01, 0.1}));
    CHECK_FALSE(in_d({baulk_line_x() + 0.2, 0.0}));

    CHECK_FALSE(in_d({0.0, 0.0}));
    CHECK_FALSE(in_d(spot(Ball::black)));
    CHECK_FALSE(in_d({baulk_cushion_x() + kBallRadius, 0.0}));
    CHECK_FALSE(in_d({c.x - 0.1, 0.5}));
}

TEST_CASE("default cue ball position") {
    // Header: in the D, halfway between the brown and yellow spots.
    const Vec2 brown = spot(Ball::brown);
    const Vec2 yellow = spot(Ball::yellow);
    check_near(default_cue_ball_position(),
               {(brown.x + yellow.x) / 2.0, (brown.y + yellow.y) / 2.0}, kExact);
    check_near(default_cue_ball_position(), {kBaulkLine, -0.146}, kSpotTol);
    CHECK(in_d(default_cue_ball_position()));
}

TEST_CASE("opening layout order and identity") {
    // Header: 22 placements, cue first, reds 0-14, then yellow through black.
    for (double gap : kGaps) {
        CAPTURE(gap);
        const auto layout = opening_layout(gap);
        REQUIRE(layout.size() == 22);
        CHECK(layout[0].ball == Ball::cue);
        CHECK(layout[0].index == 0);
        for (int i = 0; i < 15; ++i) {
            CAPTURE(i);
            CHECK(layout[1 + i].ball == Ball::red);
            CHECK(layout[1 + i].index == i);
        }
        for (std::size_t i = 0; i < 6; ++i) {
            CAPTURE(i);
            CHECK(layout[16 + i].ball == kColours[i]);
            CHECK(layout[16 + i].index == 0);
        }
    }
}

TEST_CASE("opening layout colours and cue ball") {
    // 3.2(a)(ii): the six colours on their spots.
    for (double gap : kGaps) {
        CAPTURE(gap);
        const auto layout = opening_layout(gap);
        REQUIRE(layout.size() == 22);
        for (std::size_t i = 0; i < 6; ++i) {
            CAPTURE(i);
            check_near(layout[16 + i].position, spot(kColours[i]), kSpotTol);
        }
        check_near(layout[0].position, default_cue_ball_position(), kExact);
        CHECK(in_d(layout[0].position));
    }
}

TEST_CASE("opening layout apex red") {
    // 3.2(a)(i): apex on the centre line, above (behind) the pink, as close as allowed;
    // the header fixes the surface clearance at `gap`.
    for (double gap : kGaps) {
        CAPTURE(gap);
        const auto layout = opening_layout(gap);
        REQUIRE(layout.size() == 22);
        const Vec2 apex = layout[1].position;
        const Vec2 pink = spot(Ball::pink);
        CHECK(std::abs(apex.y) <= kExact);
        CHECK(apex.x > pink.x);
        CHECK(std::abs(dist(apex, pink) - (kBallDiameter + gap)) <= kExact);
    }
}

TEST_CASE("opening layout red triangle") {
    // 3.2(a)(i): a tightly packed equilateral triangle, base parallel to the top cushion.
    for (double gap : kGaps) {
        CAPTURE(gap);
        const auto layout = opening_layout(gap);
        REQUIRE(layout.size() == 22);
        const auto rows = red_rows(layout);
        const double s = kBallDiameter + gap;
        const double row_step = s * std::sqrt(3.0) / 2.0;
        const double apex_x = layout[1].position.x;

        for (int k = 0; k < 5; ++k) {
            CAPTURE(k);
            REQUIRE(rows[k].size() == static_cast<std::size_t>(k + 1));
            for (std::size_t j = 0; j < rows[k].size(); ++j) {
                CAPTURE(j);
                CHECK(std::abs(rows[k][j].x - (apex_x + k * row_step)) <= kExact);
                // Each row is centred on the centre line.
                CHECK(std::abs(rows[k][j].y - (j - k / 2.0) * s) <= kExact);
            }
            for (std::size_t j = 1; j < rows[k].size(); ++j) {
                CAPTURE(j);
                CHECK(std::abs(dist(rows[k][j], rows[k][j - 1]) - s) <= kExact);
            }
            if (k > 0) {
                // Ball j of this row nests between balls j-1 and j of the row in front.
                for (std::size_t j = 0; j < rows[k].size(); ++j) {
                    CAPTURE(j);
                    if (j > 0) {
                        CHECK(std::abs(dist(rows[k][j], rows[k - 1][j - 1]) - s) <= kExact);
                    }
                    if (j < rows[k - 1].size()) {
                        CHECK(std::abs(dist(rows[k][j], rows[k - 1][j]) - s) <= kExact);
                    }
                }
            }
        }

        // The base row is the one nearest the top cushion.
        for (int k = 0; k < 4; ++k) {
            CAPTURE(k);
            CHECK(rows[k][0].x < rows[4][0].x);
        }
        // The pack sits between the pink and the black, clear of the black.
        const Vec2 black = spot(Ball::black);
        for (const Vec2& p : rows[4]) {
            CHECK(p.x < black.x);
            CHECK(dist(p, black) >= kBallDiameter);
        }
    }
}

TEST_CASE("opening layout has no overlaps and stays on the table") {
    // 1.2(b) all balls are 52.5 mm; 1.1(a) the playing area is within the cushion faces.
    for (double gap : {0.0, 1e-9, 0.0005, 0.003}) {
        CAPTURE(gap);
        const auto layout = opening_layout(gap);
        REQUIRE(layout.size() == 22);
        for (std::size_t i = 0; i < layout.size(); ++i) {
            CAPTURE(i);
            const Vec2 p = layout[i].position;
            CHECK(p.x >= baulk_cushion_x() + kBallRadius);
            CHECK(p.x <= top_cushion_x() - kBallRadius);
            CHECK(std::abs(p.y) <= kPlayingWidth / 2.0 - kBallRadius);
            for (std::size_t j = i + 1; j < layout.size(); ++j) {
                CAPTURE(j);
                CHECK(dist(p, layout[j].position) >= kBallDiameter - kExact);
            }
        }
    }
}

TEST_CASE("opening layout at gap 0 is touching") {
    // Header: gap = 0 gives touching balls, apex to pink included.
    const auto layout = opening_layout(0.0);
    REQUIRE(layout.size() == 22);
    const auto rows = red_rows(layout);
    CHECK(std::abs(dist(layout[1].position, spot(Ball::pink)) - kBallDiameter) <= kExact);
    for (int k = 0; k < 5; ++k) {
        for (std::size_t j = 1; j < rows[k].size(); ++j) {
            CAPTURE(k);
            CAPTURE(j);
            CHECK(std::abs(dist(rows[k][j], rows[k][j - 1]) - kBallDiameter) <= kExact);
        }
    }
    std::size_t touching = 0;
    for (std::size_t i = 1; i <= 15; ++i) {
        for (std::size_t j = i + 1; j <= 15; ++j) {
            if (std::abs(dist(layout[i].position, layout[j].position) - kBallDiameter) <=
                kExact) {
                ++touching;
            }
        }
    }
    // A 15-ball triangle has 30 touching pairs: 10 within rows and 20 between them.
    CHECK(touching == 30);
}

// Debug builds abort on this assert, so only a release test build can watch it.
#ifdef NDEBUG
TEST_CASE("spot of a non-colour logs an assert in release builds") {
    struct Capture : ofs::log::Sink {
        std::vector<std::string> lines;
        void write(const ofs::log::Event&, const std::string& line) override { lines.push_back(line); }
    };
    auto capture = std::make_shared<Capture>();
    ofs::log::Logger::instance().clear_sinks();
    ofs::log::Logger::instance().add_sink(capture);
    const Vec2 got = spot(Ball::red);
    ofs::log::Logger::instance().clear_sinks();
    CHECK(got.x == 0.0);
    CHECK(got.y == 0.0);
    REQUIRE(capture->lines.size() == 1);
    ofs::log::Event e;
    REQUIRE(ofs::log::from_jsonl(capture->lines[0], e));
    CHECK(e.event == "app.error");
    CHECK(e.data["kind"] == "assert");
    CHECK(e.data["message"].get<std::string>().find("spot() needs a colour") != std::string::npos);
}
#endif
