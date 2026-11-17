#include "ofs/opponent/exhibition.hpp"

#include <array>
#include <cmath>

#include "ofs/rules/serialize.hpp"

namespace ofs::opponent {

namespace {

// Why the layout is not a full table for a maximum; "" when it is.
std::string unsound(const Exhibition& e) {
    std::array<bool, 15> reds{};
    std::array<int, 8> seen{};
    for (const rules::BallOnTable& b : e.balls) {
        if (b.ball == rules::Ball::red) {
            if (b.index < 0 || b.index > 14) return "a red's index is not 0 to 14";
            if (reds[std::size_t(b.index)]) return "red " + std::to_string(b.index) + " is listed twice";
            reds[std::size_t(b.index)] = true;
        } else {
            ++seen[std::size_t(b.ball)];
        }
    }
    for (std::size_t i = 0; i < reds.size(); ++i) {
        if (!reds[i]) return "red " + std::to_string(i) + " is missing";
    }
    for (const rules::Ball b : {rules::Ball::cue, rules::Ball::yellow, rules::Ball::green, rules::Ball::brown,
                                rules::Ball::blue, rules::Ball::pink, rules::Ball::black}) {
        if (seen[std::size_t(b)] != 1) return "the " + std::string(table::ball_name(b)) + " is not listed once";
    }
    if (e.balls.at(std::size_t(e.cue_ball)).ball != rules::Ball::cue) return "cue_ball is not the cue ball's place";
    // A ball touching a cushion sits at exactly this bound, which rounding can push a hair past.
    constexpr double kSlack_m = 1.0e-9;
    const double hx = table::kPlayingLength / 2.0 - table::kBallRadius + kSlack_m;
    const double hy = table::kPlayingWidth / 2.0 - table::kBallRadius + kSlack_m;
    for (const rules::BallOnTable& b : e.balls) {
        if (std::abs(b.pos.x) <= hx && std::abs(b.pos.y) <= hy) continue;
        const std::string name = b.ball == rules::Ball::red ? "red " + std::to_string(b.index)
                                                            : "the " + std::string(table::ball_name(b.ball));
        return name + " is off the cloth or in a pocket (" + std::to_string(b.pos.x) + ", " +
               std::to_string(b.pos.y) + ")";
    }
    for (std::size_t i = 0; i < e.balls.size(); ++i) {
        for (std::size_t j = i + 1; j < e.balls.size(); ++j) {
            const double dx = e.balls[i].pos.x - e.balls[j].pos.x;
            const double dy = e.balls[i].pos.y - e.balls[j].pos.y;
            if (std::hypot(dx, dy) < table::kBallDiameter) {
                return "balls " + std::to_string(i) + " and " + std::to_string(j) + " overlap";
            }
        }
    }
    return {};
}

}  // namespace

bool load_exhibitions(const nlohmann::json& file, std::vector<Exhibition>& out, std::vector<std::string>& problems) {
    const std::size_t before = problems.size();
    out.clear();
    if (!file.is_object() || !file.contains("layouts") || !file["layouts"].is_array()) {
        problems.push_back("layouts: missing or not a list");
        return false;
    }
    int number = 0;
    for (const nlohmann::json& layout : file["layouts"]) {
        ++number;
        std::string name = "layout " + std::to_string(number);
        if (layout.is_object() && layout.contains("name") && layout["name"].is_string()) {
            name = layout["name"].get<std::string>();
        }
        if (!layout.is_object() || !layout.contains("balls") || !layout.contains("cue_ball")) {
            problems.push_back(name + ": needs \"balls\" and \"cue_ball\"");
            continue;
        }
        nlohmann::json position;
        position["balls"] = layout["balls"];
        position["cue_ball"] = layout["cue_ball"];
        position["cue_in_hand"] = false;
        position["on"] = {{"kind", "red"}};
        position["free_ball"] = false;
        position["touching"] = nlohmann::json::array();
        rules::Position parsed;
        std::string error;
        if (!rules::position_from_json(position, parsed, error)) {
            problems.push_back(name + ": " + error);
            continue;
        }
        Exhibition e;
        e.name = name;
        e.balls = parsed.balls;
        e.cue_ball = parsed.cue_ball;
        const std::string why = unsound(e);
        if (!why.empty()) {
            problems.push_back(name + ": " + why);
            continue;
        }
        out.push_back(std::move(e));
    }
    return problems.size() == before;
}

rules::FrameState exhibition_frame(const Exhibition& layout, int striker, std::uint64_t seed) {
    rules::FrameState s;
    s.balls = layout.balls;
    s.cue_ball = layout.cue_ball;
    s.on = {rules::OnKind::red, rules::Ball::yellow};
    s.striker = striker;
    s.seed = seed;
    return s;
}

}  // namespace ofs::opponent
