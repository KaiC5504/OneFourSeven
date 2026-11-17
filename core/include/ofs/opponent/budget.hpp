#pragma once

// What one decision may spend (spec 10.6), shared by the search for pots and the tactics so that
// a decision's steps are counted in one place: a logged decision is made again from the steps
// it took (docs/opponent/README.md, "Budget").

#include <chrono>
#include <string>

#include "ofs/opponent/level.hpp"
#include "ofs/opponent/opponent.hpp"

namespace ofs::opponent {

class Budget {
public:
    Budget(const Level& level, const Think& think)
        : think_(think), max_sims_(think.max_sims > 0 ? think.max_sims : level.max_sims), started_(std::chrono::steady_clock::now()) {}

    bool cancelled() const { return think_.cancel != nullptr && think_.cancel->load(std::memory_order_relaxed); }

    // Whether another step may be taken; once it may not, `stopped` says why and stays.
    bool open() {
        if (halted_) return false;
        const char* why = nullptr;
        if (cancelled()) {
            why = "cancelled";
        } else if (think_.max_steps > 0 && steps >= think_.max_steps) {
            why = "steps";
        } else if (steps > 0) {
            // The first step is always taken: a decision logged as stopped is made again from the
            // steps it took, and no steps is no limit.
            if (sims >= max_sims_) {
                why = "sims";
            } else if (think_.max_ms > 0.0 &&
                       std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started_).count() >= think_.max_ms) {
                why = "time";
            }
        }
        if (why == nullptr) return true;
        halted_ = true;
        stopped = why;
        return false;
    }

    bool halted() const { return halted_; }
    void halt(const char* why) {
        halted_ = true;
        stopped = why;
    }

    int sims = 0;
    int steps = 0;
    std::string stopped = "done";

private:
    const Think& think_;
    int max_sims_;
    std::chrono::steady_clock::time_point started_;
    bool halted_ = false;
};

}  // namespace ofs::opponent
