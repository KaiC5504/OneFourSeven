#include "ofs/physics/table_shape.hpp"

// Stub from the M1 contracts commit; the cushions agent replaces it.

namespace ofs::phys {

bool load_table_shape(const nlohmann::json&, TableShape& out, std::vector<std::string>&) {
    out = TableShape{};
    return true;
}

}  // namespace ofs::phys
