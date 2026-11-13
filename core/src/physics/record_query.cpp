#include "ofs/physics/record_query.hpp"

namespace ofs::phys {

double event_impact_speed(const ShotRecord&, std::size_t, const TableShape&) {
    return 0.0;
}

std::vector<table::Vec2> sample_path(const ShotRecord&, int, double, std::size_t) {
    return {};
}

}  // namespace ofs::phys
