#include "ofs/rules/engine.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>

#include "ofs/rng.hpp"
#include "ofs/rules/rules_log.hpp"

namespace ofs::rules {

namespace {

int other(int player) { return 1 - player; }

bool is_colour(Ball b) { return b != Ball::cue && b != Ball::red; }

bool contains(const std::vector<int>& places, int place) {
    return std::find(places.begin(), places.end(), place) != places.end();
}

struct Left {
    int reds = 0;
    int colours = 0;
    bool black = false;
    Ball lowest = Ball::black;
};

Left balls_left(const Position& p) {
    Left left;
    for (std::size_t i = 0; i < p.balls.size(); ++i) {
        const Ball b = p.balls[i].ball;
        if (int(i) == p.cue_ball || b == Ball::cue) continue;
        if (b == Ball::red) {
            ++left.reds;
            continue;
        }
        ++left.colours;
        left.black = left.black || b == Ball::black;
        if (table::ball_value(b) < table::ball_value(left.lowest)) left.lowest = b;
    }
    return left;
}

bool only_black(const Position& p) {
    const Left left = balls_left(p);
    return left.reds == 0 && left.colours == 1 && left.black;
}

int place_of(const Position& p, Ball b) {
    for (std::size_t i = 0; i < p.balls.size(); ++i) {
        if (p.balls[i].ball == b) return int(i);
    }
    return -1;
}

// On, or one that could be on, before anything is declared.
bool could_be_on(const Position& p, Ball b) {
    switch (p.on.kind) {
        case OnKind::red: return b == Ball::red;
        case OnKind::any_colour: return is_colour(b);
        case OnKind::colour: return b == p.on.colour;
    }
    return false;
}

std::vector<int> object_places(const Position& p) {
    std::vector<int> out;
    for (std::size_t i = 0; i < p.balls.size(); ++i) {
        if (int(i) != p.cue_ball) out.push_back(int(i));
    }
    return out;
}

bool has_cue_ball(const Position& p) { return p.cue_ball >= 0 && p.cue_ball < int(p.balls.size()); }

// The object balls touching the cue ball: real places, in order, whatever an oracle answers.
std::vector<int> touching_places(const Position& p, const TableOracle& oracle) {
    std::vector<int> out;
    if (!has_cue_ball(p)) return out;
    for (int place : oracle.touching(p.balls, p.cue_ball)) {
        if (place < 0 || place >= int(p.balls.size()) || place == p.cue_ball || contains(out, place)) continue;
        out.push_back(place);
    }
    std::sort(out.begin(), out.end());
    return out;
}

// S3.8(a): those of them the referee calls for the next stroke. With FREE BALL called any ball
// could be nominated, so any ball could be on.
std::vector<int> touching_to_call(const Position& p, const TableOracle& oracle) {
    std::vector<int> out;
    if (p.cue_in_hand) return out;
    for (int place : touching_places(p, oracle)) {
        if (p.free_ball || could_be_on(p, p.balls[std::size_t(place)].ball)) out.push_back(place);
    }
    return out;
}

std::string touching_names(const Position& p) {
    std::string out;
    for (int value = 1; value <= 7; ++value) {
        for (int place : p.touching) {
            if (place < 0 || place >= int(p.balls.size())) continue;
            const Ball b = p.balls[std::size_t(place)].ball;
            if (table::ball_value(b) != value) continue;
            if (!out.empty()) out += ",";
            out += table::ball_name(b);
            break;
        }
    }
    return out;
}

void finish(FrameState& s, int winner, EndReason reason) {
    s.over = true;
    s.winner = winner;
    s.end_reason = reason;
    s.pending = Pending::none;
}

Call frame_over(const FrameState& s) {
    return {CallKind::frame_over, s.winner, std::string(end_reason_name(s.end_reason))};
}

// S3.7: `places` of p.balls go to spots in one call, on the balls that stay where they are.
void spot_colours(Position& p, const std::vector<int>& places, const TableOracle& oracle,
                  std::vector<BallOnTable>& spotted) {
    if (places.empty()) return;
    std::vector<BallOnTable> on_table;
    for (std::size_t i = 0; i < p.balls.size(); ++i) {
        if (contains(places, int(i)) || (int(i) == p.cue_ball && p.cue_in_hand)) continue;
        on_table.push_back(p.balls[i]);
    }
    std::vector<Ball> colours;
    for (int place : places) colours.push_back(p.balls[std::size_t(place)].ball);
    const std::vector<BallOnTable> where = oracle.spot(on_table, colours);
    if (where.size() != colours.size()) {
        std::string names;
        for (Ball b : colours) names += (names.empty() ? "" : ",") + std::string(table::ball_name(b));
        log_invalid("spot", "the oracle placed " + std::to_string(where.size()) + " of " +
                                std::to_string(colours.size()) + " colours (" + names + ")");
    }
    for (std::size_t k = 0; k < places.size(); ++k) {
        BallOnTable& b = p.balls[std::size_t(places[k])];
        b.pos = k < where.size() ? where[k].pos : table::spot(b.ball);
        spotted.erase(std::remove_if(spotted.begin(), spotted.end(),
                                     [&b](const BallOnTable& s) { return s.ball == b.ball && s.index == b.index; }),
                      spotted.end());
        spotted.push_back(b);
    }
}

void take_cue_in_hand(Position& p, const TableOracle& oracle) {
    p.cue_in_hand = true;
    p.touching.clear();
    p.balls[std::size_t(p.cue_ball)].pos =
        oracle.nearest_cue_placement(p.balls, p.cue_ball, table::default_cue_ball_position());
}

// S3.4(b), S3.17(d): the black on its spot, the cue ball in hand, nothing else changed.
void set_black_again(Position& p, const TableOracle& oracle, std::vector<BallOnTable>& spotted) {
    p.cue_in_hand = true;
    p.free_ball = false;
    p.on = {OnKind::colour, Ball::black};
    spot_colours(p, {place_of(p, Ball::black)}, oracle, spotted);
    take_cue_in_hand(p, oracle);
}

// S3.17: the frame starts again. The match racks it, except in a re-spotted black, where only the
// black is set again and the same player opens (S3.17(d)): no foul is ever left pending there, so
// `offender` holds who opened.
void rerack(FrameState& s, const TableOracle& oracle) {
    s.pending = Pending::none;
    s.rerack_accepted = -1;
    s.rerack_deadline = -1;
    s.quiet_strokes = 0;
    if (!s.respotted_black || place_of(s, Ball::black) < 0 || !has_cue_ball(s)) {
        s.restart = true;
        return;
    }
    std::vector<BallOnTable> spotted;
    set_black_again(s, oracle, spotted);
    s.striker = s.offender == 1 ? 1 : 0;
    s.break_points = 0;
    s.miss_run = 0;
    s.miss_warned = false;
    s.miss_streak = 0;
}

// S2.1(c): with only the black left and more than seven behind, the player to play has lost.
bool award_if_trailing(FrameState& s) {
    if (s.over || s.restart || s.pending != Pending::none || s.respotted_black || !only_black(s)) return false;
    const int striker = s.striker == 1 ? 1 : 0;
    if (s.scores[std::size_t(other(striker))] - s.scores[std::size_t(striker)] <= 7) return false;
    finish(s, other(striker), EndReason::awarded);
    return true;
}

bool can_claim(const FrameState& s, int player) {
    return player == s.striker && !s.respotted_black && only_black(s) &&
           s.scores[std::size_t(player)] - s.scores[std::size_t(other(player))] > 7;
}

std::string malformed(const FrameState& pre, const ShotFacts& facts) {
    if (pre.over) return "the frame is over";
    if (pre.restart) return "the frame is to be racked again";
    if (pre.pending != Pending::none) return "a decision is pending: " + std::string(pending_name(pre.pending));
    if (pre.striker != 0 && pre.striker != 1) return "striker " + std::to_string(pre.striker);
    const int n = int(pre.balls.size());
    if (pre.cue_ball < 0 || pre.cue_ball >= n || pre.balls[std::size_t(pre.cue_ball)].ball != Ball::cue) {
        return "no cue ball at place " + std::to_string(pre.cue_ball) + " of " + std::to_string(n);
    }
    for (int i = 0; i < n; ++i) {
        if (i != pre.cue_ball && pre.balls[std::size_t(i)].ball == Ball::cue) {
            return "a second cue ball at place " + std::to_string(i);
        }
    }
    if (int(facts.rest.size()) != n || int(facts.closest_gap_m.size()) != n) {
        return "facts for " + std::to_string(facts.rest.size()) + " resting places and " +
               std::to_string(facts.closest_gap_m.size()) + " gaps, " + std::to_string(n) + " balls";
    }
    for (int i = 0; i < n; ++i) {
        const Vec2 r = facts.rest[std::size_t(i)];
        if (!std::isfinite(r.x) || !std::isfinite(r.y)) return "place " + std::to_string(i) + " rests nowhere";
    }
    const auto object = [&pre, n](int place) { return place >= 0 && place < n && place != pre.cue_ball; };
    for (int place : facts.first_hit) {
        if (!object(place)) return "first_hit names place " + std::to_string(place);
    }
    for (const Pot& pot : facts.pots) {
        if (!object(pot.ball)) return "pots names place " + std::to_string(pot.ball);
    }
    for (int place : facts.touching_moved) {
        if (!object(place)) return "touching_moved names place " + std::to_string(place);
    }
    return "";
}

// One stroke as the rules see it: what was declared and what each ball is worth.
struct Stroke {
    explicit Stroke(const FrameState& frame) : pre(frame) {}

    const FrameState& pre;
    bool declared = false;
    Ball declared_ball = Ball::yellow;
    bool free_used = false;  // the declared ball is a free ball (S3.12(a))
    int on_value = 0;        // 0 while a colour of choice is on and none was declared

    Ball ball(int place) const { return pre.balls[std::size_t(place)].ball; }
    bool is_free(int place) const { return free_used && ball(place) == declared_ball; }
    bool is_on(int place) const {
        if (pre.on.kind == OnKind::any_colour) return declared && ball(place) == declared_ball;
        return could_be_on(pre, ball(place));
    }
    // S3.12(a)(ii): a free ball has the value of the ball on.
    int value(int place) const { return is_free(place) ? on_value : table::ball_value(ball(place)); }
    // S3.11: four, the ball on or the ball concerned, whichever is highest.
    int penalty(int concerned) const { return std::max({4, on_value, concerned}); }
    // The highest-valued of `places`, the lowest place among equals; -1 for none.
    int dearest(const std::vector<int>& places) const {
        int best = -1;
        for (int place : places) {
            if (best < 0 || value(place) > value(best) || (value(place) == value(best) && place < best)) best = place;
        }
        return best;
    }
};

// S2.12 with nobody to ask (docs/rules/README.md): the declaration given, else the one ball hit
// first, else the one ball that could be on left touching and unmoved (S3.8(c)(ii)).
void declare(Stroke& k, const ShotInput& input, const ShotFacts& facts, const std::vector<int>& touching_still) {
    const std::vector<Ball> open = nominations(k.pre);
    const auto listed = [&open](Ball b) { return std::find(open.begin(), open.end(), b) != open.end(); };
    const auto take = [&k](Ball b) {
        k.declared = true;
        k.declared_ball = b;
    };
    if (input.has_nomination) {
        if (listed(input.nominated)) return take(input.nominated);
        log_invalid("nomination", std::string(table::ball_name(input.nominated)) + " cannot be declared with " +
                                      std::string(on_kind_name(k.pre.on.kind)) + " on");
    }
    if (open.empty()) return;
    if (!facts.first_hit.empty()) {
        if (facts.first_hit.size() == 1 && listed(k.ball(facts.first_hit[0]))) take(k.ball(facts.first_hit[0]));
        return;
    }
    int candidate = -1;
    for (int place : touching_still) {
        if (k.pre.on.kind != OnKind::any_colour && could_be_on(k.pre, k.ball(place))) return;
        if (!listed(k.ball(place))) continue;
        if (candidate >= 0) return;
        candidate = place;
    }
    if (candidate >= 0) take(k.ball(candidate));
}

// S3.6, S3.8, S3.11(b)(iv), (c), (d)(iii), S3.12(b)(i). Returns whether the first impact reached a
// ball on, which is what S2.21 asks.
bool first_impact(const Stroke& k, const ShotFacts& facts, const std::vector<int>& touching_still,
                  std::vector<Foul>& fouls) {
    for (int place : touching_still) {
        if (k.is_on(place) || k.is_free(place)) return true;
    }
    const std::vector<int>& hit = facts.first_hit;
    if (hit.empty()) {
        fouls.push_back({FoulKind::no_hit, k.penalty(0), -1});
        return false;
    }
    bool all_reds = true;
    bool all_on = true;
    bool any_on = false;
    bool any_free = false;
    for (int place : hit) {
        all_reds = all_reds && k.ball(place) == Ball::red;
        all_on = all_on && (k.is_on(place) || k.is_free(place));
        any_on = any_on || k.is_on(place);
        any_free = any_free || k.is_free(place);
    }
    const bool reached = k.free_used ? any_free : any_on;
    if (all_on && reached) return true;

    const int concerned = k.dearest(hit);
    if (k.pre.on.kind == OnKind::any_colour && !k.declared && all_reds) {
        fouls.push_back({FoulKind::reds_in_succession, 7, hit.front()});
    } else if (hit.size() == 1 || all_on) {
        fouls.push_back({FoulKind::first_hit_not_on, k.penalty(k.value(concerned)), concerned});
    } else {
        fouls.push_back({FoulKind::simultaneous_hit, k.penalty(k.value(concerned)), concerned});
    }
    return reached;
}

// How far the cue ball must run to arrive at the nearest of `could`; 0 when there is none.
double reach_m(const Position& p, const std::vector<int>& could) {
    const Vec2 cue = p.balls[std::size_t(p.cue_ball)].pos;
    double nearest = table::kBallDiameter;
    bool any = false;
    for (int place : could) {
        const Vec2 pos = p.balls[std::size_t(place)].pos;
        const double apart = std::hypot(pos.x - cue.x, pos.y - cue.y);
        if (!any || apart < nearest) nearest = apart;
        any = true;
    }
    return nearest - table::kBallDiameter;
}

// S3.14, in the order of docs/rules/README.md. `could` is every ball that is or could be on.
bool miss_called(const FrameState& pre, const FrameState& post, const ShotFacts& facts, const std::vector<int>& could,
                 const TableOracle& oracle, const RefParams& ref, std::string& reason) {
    if (pre.miss_run >= 1) {
        reason = "S3.14(d)";
        return true;
    }
    if (penalty_points_required(pre, 0) > 0 || penalty_points_required(pre, 1) > 0 ||
        penalty_points_required(post, 0) > 0 || penalty_points_required(post, 1) > 0) {
        reason = "S3.14(a)(i)";
        return false;
    }
    if (!oracle.can_hit(pre.balls, pre.cue_ball, could)) {
        // The attempt is taken for granted only of a stroke with the strength to have got there:
        // one a ball stopped, or one that ran as far as the nearest ball on is away.
        if (!facts.first_hit.empty() || facts.cue_travel_m >= reach_m(pre, could)) {
            reason = "S3.14(a)(ii)";
            return false;
        }
        reason = "S3.14(a)";
        return true;
    }
    if (oracle.clear_path(pre.balls, pre.cue_ball, could)) {
        reason = "S3.14(c)";
        return true;
    }
    if (ref.miss_lenient_gap_m > 0.0) {
        for (int place : could) {
            if (facts.closest_gap_m[std::size_t(place)] <= ref.miss_lenient_gap_m) {
                reason = "lenient";
                return false;
            }
        }
    }
    reason = "S3.14(a)";
    return true;
}

}  // namespace

Judgement judge(const FrameState& pre, const ShotInput& input, const ShotFacts& facts, const TableOracle& oracle,
                const RefParams& ref) {
    Judgement j;
    j.post = pre;
    const std::string problem = malformed(pre, facts);
    if (!problem.empty()) {
        log_invalid("judge", problem);
        return j;
    }
    FrameState& post = j.post;
    std::vector<Foul>& fouls = j.fouls;
    const int n = int(pre.balls.size());
    const int striker = pre.striker;
    const int opponent = other(striker);

    const std::vector<int> touching = touching_places(pre, oracle);
    std::vector<int> touching_still;
    for (int place : touching) {
        if (!contains(facts.touching_moved, place)) touching_still.push_back(place);
    }

    Stroke k(pre);
    declare(k, input, facts, touching_still);
    k.free_used = k.declared && pre.free_ball && pre.on.kind != OnKind::any_colour;
    switch (pre.on.kind) {
        case OnKind::red: k.on_value = 1; break;
        case OnKind::any_colour: k.on_value = k.declared ? table::ball_value(k.declared_ball) : 0; break;
        case OnKind::colour: k.on_value = table::ball_value(pre.on.colour); break;
    }
    j.nominated = {k.declared, k.declared_ball};
    j.free_ball_used = k.free_used;

    const bool reached = first_impact(k, facts, touching_still, fouls);

    // S3.8(b), S2.19.
    std::vector<int> pushed = facts.touching_moved;
    if (facts.push) {
        for (int place : facts.first_hit) {
            if (!contains(pushed, place)) pushed.push_back(place);
        }
    }
    if (facts.push || !pushed.empty()) {
        const int concerned = k.dearest(pushed);
        fouls.push_back({FoulKind::push_stroke, k.penalty(concerned < 0 ? 0 : k.value(concerned)), concerned});
    }

    std::vector<char> potted(std::size_t(n), 0);
    for (const Pot& pot : facts.pots) potted[std::size_t(pot.ball)] = 1;
    int reds_potted = 0;
    bool on_potted = false;
    bool free_potted = false;
    for (int place = 0; place < n; ++place) {
        if (!potted[std::size_t(place)]) continue;
        if (k.is_free(place)) {
            free_potted = true;
        } else if (k.is_on(place)) {
            on_potted = true;
            if (k.ball(place) == Ball::red) ++reds_potted;
        } else {
            fouls.push_back({FoulKind::pot_not_on, k.penalty(k.value(place)), place});
        }
    }
    if (facts.cue_potted) fouls.push_back({FoulKind::in_off, k.penalty(0), -1});

    // S3.3(g), (h), S3.12(c)-(e): what the stroke scores if it turns out legal.
    int points = 0;
    if (fouls.empty()) {
        switch (pre.on.kind) {
            case OnKind::red: points = reds_potted + (free_potted ? 1 : 0); break;
            case OnKind::any_colour: points = on_potted ? k.on_value : 0; break;
            case OnKind::colour: points = on_potted || free_potted ? k.on_value : 0; break;
        }
    }

    // The table after the stroke. Reds stay off; a colour stays off only when potted as the colour
    // on in the final sequence (S3.3(h)(iii)), and is spotted otherwise (S3.7, S3.12(c)).
    const bool spot_all = !fouls.empty() || pre.on.kind == OnKind::any_colour;
    post.balls.clear();
    post.cue_in_hand = facts.cue_potted;
    post.free_ball = false;
    post.touching.clear();
    std::vector<int> from;  // the pre-shot place of each ball still listed
    std::vector<int> to_spot;
    for (int place = 0; place < n; ++place) {
        if (potted[std::size_t(place)]) {
            if (k.ball(place) == Ball::red || !(spot_all || k.is_free(place))) continue;
            to_spot.push_back(int(post.balls.size()));
        }
        if (place == pre.cue_ball) post.cue_ball = int(post.balls.size());
        post.balls.push_back({k.ball(place), pre.balls[std::size_t(place)].index, facts.rest[std::size_t(place)]});
        from.push_back(place);
    }
    spot_colours(post, to_spot, oracle, j.spotted);
    if (post.cue_in_hand) take_cue_in_hand(post, oracle);

    // S3.12(b)(ii), on the balls as they lie once spotted; not with only two object balls left.
    if (k.free_used && points == 0 && !post.cue_in_hand && post.balls.size() != 3) {
        std::vector<int> on_now;
        int free_now = -1;
        for (std::size_t i = 0; i < post.balls.size(); ++i) {
            if (int(i) == post.cue_ball) continue;
            if (k.is_free(from[i])) {
                free_now = int(i);
            } else if (k.is_on(from[i])) {
                on_now.push_back(int(i));
            }
        }
        if (free_now >= 0 && oracle.snookered(post.balls, post.cue_ball, on_now, false) &&
            contains(oracle.snookering_balls(post.balls, post.cue_ball, on_now), free_now)) {
            fouls.push_back({FoulKind::free_ball_snooker, k.penalty(0), from[std::size_t(free_now)]});
        }
    }

    // S3.11(d)(vi): any foul but playing at a red, with a colour of choice on and none declared.
    if (pre.on.kind == OnKind::any_colour && !k.declared &&
        std::any_of(fouls.begin(), fouls.end(), [](const Foul& f) { return f.kind != FoulKind::reds_in_succession; })) {
        fouls.push_back({FoulKind::before_nominating, 7, -1});
    }
    std::stable_sort(fouls.begin(), fouls.end(), [](const Foul& a, const Foul& b) {
        if (a.value != b.value) return a.value > b.value;
        if (a.kind != b.kind) return a.kind < b.kind;
        return a.ball < b.ball;
    });
    const bool foul = !fouls.empty();

    PlayerStats& stats = post.stats[std::size_t(striker)];
    ++stats.shots;
    if (facts.pot_attempt) ++stats.pot_attempts;
    post.strokes = pre.strokes + 1;
    post.break_points = 0;
    if (foul) {
        j.penalty = fouls.front().value;
        post.scores[std::size_t(opponent)] += j.penalty;
        post.stats[std::size_t(opponent)].points_from_fouls += j.penalty;
        ++stats.fouls;
    } else if (points > 0) {
        j.points = points;
        post.scores[std::size_t(striker)] += points;
        post.break_points = pre.break_points + points;
        // Pots are counted against attempts: a safety that drops a red scores but makes no pot.
        if (facts.pot_attempt) ++stats.pots;
        stats.highest_break = std::max(stats.highest_break, post.break_points);
    }

    const Left left = balls_left(post);
    const BallOn lowest = left.reds > 0 ? BallOn{OnKind::red, Ball::yellow} : BallOn{OnKind::colour, left.lowest};
    if (points > 0) {
        post.on = pre.on.kind == OnKind::red ? BallOn{OnKind::any_colour, Ball::yellow} : lowest;
    } else {
        post.on = lowest;
        post.striker = opponent;
    }

    // S3.4: the first pot or foul with only the black left ends the frame, unless it levels the scores.
    bool respot = false;
    if (foul ? only_black(pre) : left.reds + left.colours == 0) {
        if (post.scores[0] != post.scores[1]) {
            finish(post, post.scores[0] > post.scores[1] ? 0 : 1,
                   pre.respotted_black ? EndReason::respotted_black : EndReason::black);
        } else {
            respot = true;
        }
    }

    post.miss_run = 0;
    post.miss_warned = false;
    post.miss_streak = 0;
    if (foul && !reached && !post.over && !respot) {
        const std::vector<int> could = pre.free_ball ? object_places(pre) : places_on(pre);
        j.miss = miss_called(pre, post, facts, could, oracle, ref, j.miss_reason);
        if (j.miss) {
            ++stats.misses;
            post.miss_streak = pre.miss_streak + 1;
            // S3.14(d): the run counts misses from a position where a full ball was there to be hit,
            // or where the cue ball touched a ball that could be on.
            bool run = pre.miss_run >= 1 || oracle.full_ball(pre.balls, pre.cue_ball, could);
            for (int place : touching) run = run || contains(could, place);
            if (run) {
                post.miss_run = pre.miss_run + 1;
                post.miss_warned = pre.miss_warned;
                if (pre.miss_warned) finish(post, opponent, EndReason::three_misses);
            }
        }
    }

    if (respot) {
        if (place_of(post, Ball::black) < 0) {
            const int was = place_of(pre, Ball::black);
            std::size_t at = 0;
            while (was >= 0 && at < from.size() && from[at] < was) ++at;
            if (was < 0) at = post.balls.size();
            post.balls.insert(post.balls.begin() + std::ptrdiff_t(at),
                              BallOnTable{Ball::black, 0, table::spot(Ball::black)});
            if (int(at) <= post.cue_ball) ++post.cue_ball;
        }
        set_black_again(post, oracle, j.spotted);
        post.respotted_black = true;
        post.break_points = 0;
        post.pending = Pending::toss;
        // A fresh draw for each re-spotted black, the same one whenever the frame is replayed.
        Rng lots(post.seed + 7919 * std::uint64_t(post.strokes));
        post.decider = int(lots.next_u64() & 1);
        post.striker = post.decider;
    } else if (foul && !post.over) {
        post.pending = Pending::after_foul;
        post.decider = opponent;
        post.offender = striker;
        post.last_penalty = j.penalty;
        post.last_miss = j.miss;
        post.original = static_cast<const Position&>(pre);
    } else {
        award_if_trailing(post);
    }

    // S3.17. A foul stroke leaves the count and the grace strokes as they were (docs/rules/README.md).
    const bool quiet =
        facts.pots.empty() && !facts.cue_potted && facts.object_travel_m < ref.stalemate_quiet_travel_m;
    bool reracked = false;
    bool offered = false;
    post.rerack_accepted = -1;
    if (respot || (!foul && !quiet)) {
        post.quiet_strokes = 0;
        post.rerack_deadline = -1;
    } else if (!foul) {
        post.quiet_strokes = pre.quiet_strokes + 1;
        if (pre.rerack_deadline >= 0) {
            post.rerack_deadline = pre.rerack_deadline - 1;
            reracked = post.rerack_deadline <= 0 && !post.over;
        }
    }
    if (reracked) {
        rerack(post, oracle);
    } else if (!post.over && post.pending == Pending::none && post.rerack_deadline < 0 &&
               post.quiet_strokes >= ref.stalemate_offer_strokes) {
        post.pending = Pending::rerack;
        post.decider = post.striker;
        offered = true;
    }

    if (!post.over && !reracked && !respot) {
        if (foul) post.free_ball = oracle.snookered(post.balls, post.cue_ball, places_on(post), post.cue_in_hand);
        post.touching = touching_to_call(post, oracle);
    }

    if (foul) j.calls.push_back({CallKind::foul, j.penalty, std::string(foul_name(fouls.front().kind))});
    if (j.miss) j.calls.push_back({CallKind::miss, 0, j.miss_reason});
    if (post.free_ball) j.calls.push_back({CallKind::free_ball, 0, ""});
    if (!post.touching.empty()) j.calls.push_back({CallKind::touching_ball, 0, touching_names(post)});
    if (respot) j.calls.push_back({CallKind::respotted_black, post.decider, ""});
    if (offered) j.calls.push_back({CallKind::rerack_offer, post.decider, ""});
    if (reracked) j.calls.push_back({CallKind::rerack, 0, ""});
    if (post.over) j.calls.push_back(frame_over(post));
    return j;
}

Judgement judge_shot(const FrameState& pre, const ShotInput& input, const phys::ShotRecord& record,
                     const phys::TableShape& table, const TableOracle& oracle, const RefParams& ref) {
    return judge(pre, input, observe(pre, record, table, ref), oracle, ref);
}

std::vector<DecisionKind> options(const FrameState& s, int player) {
    std::vector<DecisionKind> out;
    if (s.over || s.restart || (player != 0 && player != 1)) return out;
    const bool any_time = s.concede_any_time[std::size_t(player)];
    bool may_concede = false;
    switch (s.pending) {
        case Pending::after_foul:
            if (player != s.decider) break;
            out = {DecisionKind::play, DecisionKind::play_again};
            if (s.last_miss) out.push_back(DecisionKind::replace);
            may_concede = any_time;
            break;
        case Pending::rerack:
            if (player != s.decider) break;
            out = {DecisionKind::accept_rerack, DecisionKind::refuse_rerack};
            may_concede = any_time;
            break;
        case Pending::toss:
            if (player != s.decider) break;
            out = {DecisionKind::play_first, DecisionKind::play_second};
            may_concede = any_time;
            break;
        case Pending::none:
            if (can_claim(s, player)) out.push_back(DecisionKind::claim_frame);
            may_concede = any_time || penalty_points_required(s, 0) > 0 || penalty_points_required(s, 1) > 0;
            break;
    }
    if (may_concede) {
        out.push_back(DecisionKind::concede_frame);
        out.push_back(DecisionKind::concede_match);
    }
    return out;
}

bool decide(FrameState& s, Decision d, const TableOracle& oracle, const RefParams& ref, std::vector<Call>& calls,
            std::string& error) {
    calls.clear();
    const std::vector<DecisionKind> open = options(s, d.player);
    if (std::find(open.begin(), open.end(), d.kind) == open.end()) {
        error = std::string(decision_name(d.kind)) + " is not open to player " + std::to_string(d.player);
        return false;
    }
    const bool replacing = d.kind == DecisionKind::replace;
    if (replacing && !has_cue_ball(s.original)) {
        error = "there is no position to put back";
        return false;
    }
    error.clear();

    bool warned = false;
    switch (d.kind) {
        case DecisionKind::play:
        case DecisionKind::play_again:
        case DecisionKind::replace:
            if (replacing) {
                static_cast<Position&>(s) = s.original;
                // S3.14(d)(ii): asked to play again from where a full ball was missed twice, or at
                // the first chance after that if no warning was given.
                warned = s.miss_run >= 2 && !s.miss_warned;
                s.miss_warned = s.miss_warned || warned;
            } else {
                s.miss_run = 0;
                s.miss_warned = false;
                s.miss_streak = 0;
            }
            if (d.kind == DecisionKind::play_again) {
                s.free_ball = false;  // S3.12(f)
                s.touching = touching_to_call(s, oracle);
            }
            s.striker = d.kind == DecisionKind::play ? d.player : other(d.player);
            s.pending = Pending::none;
            s.last_penalty = 0;
            s.last_miss = false;
            s.original = Position{};
            if (replacing) {
                if (s.free_ball) calls.push_back({CallKind::free_ball, 0, ""});
                if (!s.touching.empty()) calls.push_back({CallKind::touching_ball, 0, touching_names(s)});
                if (warned) calls.push_back({CallKind::warning, 0, ""});
            }
            if (award_if_trailing(s)) calls.push_back(frame_over(s));
            break;
        case DecisionKind::accept_rerack:
            if (s.rerack_accepted < 0) {
                s.rerack_accepted = d.player;
                s.decider = other(d.player);
                calls.push_back({CallKind::rerack_offer, s.decider, ""});
            } else {
                rerack(s, oracle);
                calls.push_back({CallKind::rerack, 0, ""});
            }
            break;
        case DecisionKind::refuse_rerack:
            s.pending = Pending::none;
            s.rerack_accepted = -1;
            s.rerack_deadline = std::max(ref.stalemate_grace_strokes, 1);
            break;
        case DecisionKind::play_first:
        case DecisionKind::play_second:
            s.striker = d.kind == DecisionKind::play_first ? d.player : other(d.player);
            s.offender = s.striker;  // who opened, for rerack()
            s.pending = Pending::none;
            break;
        case DecisionKind::claim_frame:
            finish(s, d.player, EndReason::claimed);
            calls.push_back(frame_over(s));
            break;
        case DecisionKind::concede_frame:
        case DecisionKind::concede_match:
            // Conceded with a choice pending, the choice goes with the frame.
            s.rerack_accepted = -1;
            s.last_penalty = 0;
            s.last_miss = false;
            s.original = Position{};
            finish(s, other(d.player),
                   d.kind == DecisionKind::concede_frame ? EndReason::conceded : EndReason::match_conceded);
            calls.push_back(frame_over(s));
            break;
    }
    return true;
}

}  // namespace ofs::rules
