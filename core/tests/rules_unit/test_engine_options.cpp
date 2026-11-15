#include <doctest/doctest.h>

#include <vector>

#include "test_engine_support.hpp"

using namespace ofs::rules;
using namespace ofs::rules::engine_test;

namespace {

using Options = std::vector<DecisionKind>;

}  // namespace

TEST_CASE("with nothing pending and no leave to concede, nobody needing penalty points, nothing is offered") {
    const FrameState s = open_table(15);
    CHECK(options(s, 0).empty());
    CHECK(options(s, 1).empty());
}

TEST_CASE("after a foul the non-offender chooses to play or have the offender play again (S3.10(h))") {
    FrameState s = open_table(15);
    s.pending = Pending::after_foul;
    s.decider = 1;
    s.striker = 1;
    s.offender = 0;
    CHECK(options(s, 1) == Options{DecisionKind::play, DecisionKind::play_again});
    CHECK(options(s, 0).empty());
    // A decider with leave to concede may also concede; the other player may not, leave or not.
    s.concede_any_time = {true, true};
    CHECK(options(s, 1) == Options{DecisionKind::play, DecisionKind::play_again, DecisionKind::concede_frame,
                                   DecisionKind::concede_match});
    CHECK(options(s, 0).empty());
}

TEST_CASE("after FOUL AND A MISS the balls may be replaced as well (S3.14(b))") {
    FrameState s = open_table(15);
    s.pending = Pending::after_foul;
    s.decider = 0;
    s.offender = 1;
    s.last_miss = true;
    CHECK(options(s, 0) == Options{DecisionKind::play, DecisionKind::play_again, DecisionKind::replace});
    CHECK(options(s, 1).empty());
}

TEST_CASE("a re-rack offer is answered by the player asked (S3.17)") {
    FrameState s = open_table(15);
    s.pending = Pending::rerack;
    s.decider = 1;
    CHECK(options(s, 1) == Options{DecisionKind::accept_rerack, DecisionKind::refuse_rerack});
    CHECK(options(s, 0).empty());
}

TEST_CASE("the winner of the lots chooses the order for a re-spotted black (S3.4(b)(ii))") {
    FrameState s = colours_left({Ball::black});
    s.respotted_black = true;
    s.pending = Pending::toss;
    s.decider = 0;
    CHECK(options(s, 0) == Options{DecisionKind::play_first, DecisionKind::play_second});
    CHECK(options(s, 1).empty());
}

TEST_CASE("nothing is offered once the frame is over or to be racked again, or to nobody") {
    FrameState s = open_table(15);
    s.concede_any_time = {true, true};
    CHECK(options(s, 2).empty());
    CHECK(options(s, -1).empty());
    s.restart = true;
    CHECK(options(s, 0).empty());
    s.restart = false;
    s.over = true;
    CHECK(options(s, 0).empty());
    CHECK(options(s, 1).empty());
}

TEST_CASE("the claim comes first, then the concessions") {
    FrameState s = colours_left({Ball::black});
    s.scores = {70, 30};
    s.concede_any_time = {true, true};
    CHECK(options(s, 0) ==
          Options{DecisionKind::claim_frame, DecisionKind::concede_frame, DecisionKind::concede_match});
    CHECK(options(s, 1) == Options{DecisionKind::concede_frame, DecisionKind::concede_match});
}
