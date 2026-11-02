#include "ofs/physics/detect.hpp"

// Stub from the M1 contracts commit; the events agent replaces it.

namespace ofs::phys {

double ball_ball_time(const Trajectory&, const Trajectory&, double, double) { return kNever; }
double ball_line_time(const Trajectory&, const LineCushion&, double, double) { return kNever; }
double ball_arc_time(const Trajectory&, const ArcCushion&, double, double) { return kNever; }
double ball_pocket_time(const Trajectory&, const Pocket&, double) { return kNever; }

}  // namespace ofs::phys
