// Contract stub (M3): replaced by the rules engine's builder.

#include "ofs/rules/engine.hpp"

namespace ofs::rules {

Judgement judge(const FrameState& pre, const ShotInput&, const ShotFacts&, const TableOracle&, const RefParams&) {
    Judgement j;
    j.post = pre;
    return j;
}

Judgement judge_shot(const FrameState& pre, const ShotInput& input, const phys::ShotRecord& record,
                     const phys::TableShape& table, const TableOracle& oracle, const RefParams& ref) {
    return judge(pre, input, observe(pre, record, table, ref), oracle, ref);
}

std::vector<DecisionKind> options(const FrameState&, int) { return {}; }

bool decide(FrameState&, Decision, const TableOracle&, const RefParams&, std::vector<Call>&, std::string& error) {
    error = "not implemented";
    return false;
}

}  // namespace ofs::rules
