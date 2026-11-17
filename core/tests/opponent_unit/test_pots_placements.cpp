#include <doctest/doctest.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <set>
#include <utility>

#include "ofs/opponent/exhibition.hpp"
#include "ofs/opponent/pots.hpp"

#include "rig.hpp"

using namespace ofs;
using namespace ofs::opponent;
using rules::Ball;
using rules::scenario::ball;
using rules::scenario::place_of;

namespace {

constexpr double kWide = 1.3;

int pocket(const char* name) {
    const auto& pockets = rules::scenario::rig().table.pockets;
    for (std::size_t i = 0; i < pockets.size(); ++i) {
        if (pockets[i].name == name) return int(i);
    }
    FAIL("no pocket named " << name);
    return -1;
}

bool legal(const rules::Position& p, rules::Vec2 at) {
    return rig::env().oracle.cue_placement_legal(p.balls, p.cue_ball, at);
}

}  // namespace

TEST_CASE("in_hand: pots from several places in the D, each legal; the red near the bottom left from close by") {
    const rules::FrameState s = rig::position("in_hand");
    REQUIRE(s.cue_in_hand);
    const std::vector<Pot> list = pots(s, rig::env(), kWide);
    REQUIRE_FALSE(list.empty());
    std::set<std::pair<double, double>> places;
    for (const Pot& p : list) {
        CHECK(legal(s, p.cue));
        CHECK(table::in_d(p.cue));
        places.insert({p.cue.x, p.cue.y});
    }
    CHECK(places.size() > 1);
    const int red = place_of(s, Ball::red);
    const auto easiest = std::find_if(list.begin(), list.end(), [&](const Pot& p) { return p.target == red; });
    REQUIRE(easiest != list.end());
    CHECK(easiest->pocket == pocket("bottom_left"));
    CHECK(easiest->cue_travel_m < 0.6);
    MESSAGE("in_hand: easiest red from (" << easiest->cue.x << ", " << easiest->cue.y << "), cue travel "
                                          << easiest->cue_travel_m << ", cut " << easiest->cut_rad);
    for (std::size_t i = 1; i < list.size(); ++i) CHECK(list[i - 1].margin_rad >= list[i].margin_rad);

    // Not in hand, the same table plays from the cue ball alone.
    rules::FrameState placed = s;
    placed.cue_in_hand = false;
    for (const Pot& p : pots(placed, rig::env(), kWide)) {
        CHECK(p.cue.x == s.balls[0].pos.x);
        CHECK(p.cue.y == s.balls[0].pos.y);
    }
}

TEST_CASE("cue_placements: never empty, every place legal, the listed one first, at most count") {
    for (const char* name : {"in_hand", "straight_red", "colours", "snookered"}) {
        CAPTURE(name);
        const rules::FrameState s = rig::position(name);
        const rules::Vec2 listed = s.balls[std::size_t(s.cue_ball)].pos;
        for (const int count : {1, 3, kPlacements, 20}) {
            CAPTURE(count);
            const std::vector<rules::Vec2> at = cue_placements(s, rig::env(), count);
            REQUIRE_FALSE(at.empty());
            CHECK(int(at.size()) <= count);
            for (std::size_t i = 0; i < at.size(); ++i) {
                CHECK(legal(s, at[i]));
                for (std::size_t j = 0; j < i; ++j) CHECK(std::hypot(at[i].x - at[j].x, at[i].y - at[j].y) >= 0.01);
            }
            if (legal(s, listed)) {
                CHECK(at.front().x == listed.x);
                CHECK(at.front().y == listed.y);
            }
        }
    }
    // With the colours on their spots the baulk line's middle and both ends are taken: the listed
    // place and seven others.
    CHECK(cue_placements(rig::position("in_hand"), rig::env(), kPlacements).size() == 8);

    // A ball on every place it would try, the listed one too: the oracle's nearest legal place.
    const double b = table::baulk_line_x();
    std::vector<rules::BallOnTable> balls = {ball(Ball::cue, b - 0.15, 0.0), ball(Ball::red, b - 0.15, 0.02, 0)};
    for (const double y : {0.0, 0.10, -0.10, 0.20, -0.20, 0.27, -0.27}) balls.push_back(ball(Ball::red, b - 0.03, y, int(balls.size())));
    for (const double y : {0.12, -0.12}) balls.push_back(ball(Ball::red, b - 0.15, y, int(balls.size())));
    balls.push_back(ball(Ball::red, b - 0.25, 0.0, int(balls.size())));
    rules::FrameState crowded = rules::scenario::frame(balls);
    crowded.cue_in_hand = true;
    const std::vector<rules::Vec2> one = cue_placements(crowded, rig::env(), kPlacements);
    REQUIRE(one.size() == 1);
    CHECK(legal(crowded, one[0]));
    const rules::Vec2 nearest = rig::env().oracle.nearest_cue_placement(crowded.balls, 0, balls[0].pos);
    CHECK(one[0].x == nearest.x);
    CHECK(one[0].y == nearest.y);
}

TEST_CASE("open_reds: every red on the exhibition table, few in a fresh rack") {
    const rules::FrameState exhibition = exhibition_frame(rig::levels().exhibitions[0], 0, 1);
    const int open_now = open_reds(exhibition, rig::env());
    MESSAGE("open reds on " << rig::levels().exhibitions[0].name << ": " << open_now);
    CHECK(open_now >= 14);
    const rules::FrameState rack = rig::racked();
    const int racked = open_reds(rack, rig::env());
    MESSAGE("open reds in a fresh rack: " << racked);
    CHECK(racked <= 3);

    const rules::FrameState black = rules::scenario::frame({ball(Ball::cue, 0.0, 0.0), rules::scenario::spotted(Ball::black)});
    const std::vector<int> open = pockets_open_to(black, 1, rig::env());
    CHECK(std::is_sorted(open.begin(), open.end()));
    CHECK(std::find(open.begin(), open.end(), pocket("top_left")) != open.end());
    CHECK(std::find(open.begin(), open.end(), pocket("top_right")) != open.end());

    // A red ringed by six others, a ball's width apart, is open nowhere until the ring is gone.
    std::vector<rules::BallOnTable> ring = {ball(Ball::cue, -1.0, 0.0), ball(Ball::red, 0.5, 0.0, 0)};
    for (int i = 0; i < 6; ++i) {
        const double a = i * 3.14159265358979323846 / 3.0 + 0.2;
        ring.push_back(ball(Ball::red, 0.5 + 0.06 * std::cos(a), 0.06 * std::sin(a), i + 1));
    }
    const rules::FrameState ringed = rules::scenario::frame(ring);
    CHECK(pockets_open_to(ringed, 1, rig::env()).empty());
    CHECK(open_reds(ringed, rig::env()) == 6);
    const rules::FrameState alone = rules::scenario::frame({ring[0], ring[1]});
    CHECK(pockets_open_to(alone, 1, rig::env()).size() == 6);
    CHECK(open_reds(alone, rig::env()) == 1);

    // The cue ball does not close a pocket to the ball.
    const rules::FrameState blocked = rules::scenario::frame({ball(Ball::cue, 1.6, 0.0), rules::scenario::spotted(Ball::black)});
    CHECK(pockets_open_to(blocked, 1, rig::env()) == open);
}

TEST_CASE("pots_from on an exhibition table is quick") {
    const rules::FrameState s = exhibition_frame(rig::levels().exhibitions[0], 0, 1);
    const rules::Vec2 cue = s.balls[std::size_t(s.cue_ball)].pos;
    constexpr int kRuns = 200;
    std::size_t listed = 0;
    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < kRuns; ++i) listed += pots_from(s, cue, rig::env(), kWide).size();
    const double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count() / kRuns;
    MESSAGE("pots_from on " << rig::levels().exhibitions[0].name << ": " << us << " us a call, " << listed / kRuns
                            << " pots");
    CHECK(listed > 0);
}
