// Contract stub (M3): replaced by the geometry builder.

#include "ofs/rules/oracle.hpp"

#include "spotting.hpp"

namespace ofs::rules {

namespace {

class GeometryOracle final : public TableOracle {
public:
    GeometryOracle(const phys::TableShape& table, const RefParams& ref) : table_(table), ref_(ref) {}

    bool snookered(const std::vector<BallOnTable>&, int, const std::vector<int>&, bool) const override {
        return false;
    }
    std::vector<int> snookering_balls(const std::vector<BallOnTable>&, int, const std::vector<int>&) const override {
        return {};
    }
    bool clear_path(const std::vector<BallOnTable>&, int, const std::vector<int>&) const override { return true; }
    bool full_ball(const std::vector<BallOnTable>&, int, const std::vector<int>&) const override { return true; }
    bool can_hit(const std::vector<BallOnTable>&, int, const std::vector<int>&) const override { return true; }
    std::vector<int> touching(const std::vector<BallOnTable>&, int) const override { return {}; }
    std::vector<BallOnTable> spot(const std::vector<BallOnTable>& on_table,
                                  const std::vector<Ball>& colours) const override {
        return spot_colours(on_table, colours, ref_.touching_gap_m);
    }
    bool cue_placement_legal(const std::vector<BallOnTable>&, int, Vec2 pos) const override {
        return table::in_d(pos);
    }
    Vec2 nearest_cue_placement(const std::vector<BallOnTable>&, int, Vec2 wanted) const override {
        return table::in_d(wanted) ? wanted : table::default_cue_ball_position();
    }

private:
    phys::TableShape table_;
    RefParams ref_;
};

}  // namespace

std::unique_ptr<TableOracle> make_geometry_oracle(const phys::TableShape& table, const RefParams& ref) {
    return std::make_unique<GeometryOracle>(table, ref);
}

}  // namespace ofs::rules
