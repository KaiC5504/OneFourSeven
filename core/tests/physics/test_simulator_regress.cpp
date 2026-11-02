// Regression tests for the simulator's own mechanisms (overlap, contact solves, the slow tail),
// each from a recorded shot.

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include "ofs/log.hpp"
#include "ofs/log_schema.hpp"
#include "ofs/physics/motion.hpp"
#include "ofs/physics/simulator.hpp"

using namespace ofs::phys;

namespace {

constexpr double kR = ofs::table::kBallRadius;

nlohmann::json read_data(const char* name) {
    std::ifstream in(std::string(OFS_CORE_DATA_DIR) + "/" + name);
    REQUIRE(in.good());
    return nlohmann::json::parse(in);
}

const TableShape& table() {
    static const TableShape shape = [] {
        TableShape t;
        std::vector<std::string> problems;
        REQUIRE(load_table_shape(read_data("table_shape.json"), t, problems));
        return t;
    }();
    return shape;
}

const PhysicsParams& file_params() {
    static const PhysicsParams params = [] {
        PhysicsParams p;
        std::vector<std::string> problems;
        REQUIRE(load_physics_params(read_data("table_params.json"), p, problems));
        return p;
    }();
    return params;
}

struct Capture : ofs::log::Sink {
    std::vector<ofs::log::Event> events;
    void write(const ofs::log::Event& event, const std::string&) override { events.push_back(event); }
};

std::vector<ofs::log::Event> simulate_capturing(const SimInput& in, const PhysicsParams& p, ShotRecord& out) {
    auto capture = std::make_shared<Capture>();
    ofs::log::Logger::instance().clear_sinks();
    ofs::log::Logger::instance().add_sink(capture);
    out = simulate(in, table(), p);
    ofs::log::Logger::instance().clear_sinks();
    return capture->events;
}

// A shot the fuzz or the stress runs recorded, all balls at rest: the cue ball, the reds 0-14,
// then as many colours as there are, in order.
struct Recorded {
    const char* name;
    std::uint64_t seed;
    CueStrike strike;
    std::vector<std::array<double, 2>> r;
};

SimInput input(const Recorded& s) {
    static constexpr std::array<ofs::table::Ball, 6> kColours = {ofs::table::Ball::yellow, ofs::table::Ball::green,
                                                                ofs::table::Ball::brown,  ofs::table::Ball::blue,
                                                                ofs::table::Ball::pink,   ofs::table::Ball::black};
    SimInput in;
    for (std::size_t i = 0; i < s.r.size(); ++i) {
        BallState b;
        b.ball = i == 0 ? ofs::table::Ball::cue : i <= 15 ? ofs::table::Ball::red : kColours[i - 16];
        b.index = i >= 1 && i <= 15 ? static_cast<int>(i) - 1 : 0;
        b.r = {s.r[i][0], s.r[i][1], 0.0};
        in.start.balls.push_back(b);
    }
    in.cue_ball = 0;
    in.has_strike = true;
    in.strike = s.strike;
    in.seed = s.seed;
    in.params_hash = "test";
    return in;
}

// The closest any two balls on the table come at any event time, measured short of 2R.
double worst_overlap(const ShotRecord& r) {
    double worst = 0.0;
    for (const ShotEvent& e : r.events) {
        const TableState s = r.state_at(e.t);
        for (std::size_t i = 0; i < s.balls.size(); ++i) {
            for (std::size_t j = i + 1; j < s.balls.size(); ++j) {
                if (s.balls[i].motion == Motion::pocketed || s.balls[j].motion == Motion::pocketed) continue;
                const double d = std::hypot(s.balls[i].r.x - s.balls[j].r.x, s.balls[i].r.y - s.balls[j].r.y);
                worst = std::max(worst, 2.0 * kR - d);
            }
        }
    }
    return worst;
}

bool at_rest(const BallState& b) {
    return b.motion == Motion::pocketed ||
           (b.motion == Motion::stationary && b.v == ofs::Vec3{} && b.w == ofs::Vec3{});
}

std::size_t count(const ShotRecord& r, const char* kind) {
    std::size_t n = 0;
    for (const Anomaly& a : r.anomalies) n += a.kind == kind ? 1 : 0;
    return n;
}

BallState rolling(ofs::table::Ball kind, int index, double x, double y, double vx, double vy) {
    BallState b;
    b.ball = kind;
    b.index = index;
    b.r = {x, y, 0.0};
    b.v = {vx, vy, 0.0};
    b.w = {-vy / kR, vx / kR, 0.0};
    return b;
}

// The six shots of the check round in which a pair ended up 1.1-9.8 nm inside 2R: each had a
// contact resolved up to zero_time_s after its time, behind a lower-numbered pair's.
const std::vector<Recorded> kOverlapShots = {
    {"fuzz seed 4242 shot 14799", 5213826746170973102ULL,
     {0.05558046755252005, 0.22494205664908834, 0.1072671710879324, 0.05, 5.7741013497648455},
     {{-1.0474999999999999, -0.146}, {0.9447902521932359, 2.810299945518003e-06},
      {0.9903178711993773, 0.02629262434578752}, {0.9903232746527536, -0.026285844042039524},
      {1.0358469118385756, 0.05255252974759681}, {1.0358407602853055, 1.1751385758345792e-05},
      {1.0358320573471025, -0.05254891368111961}, {1.0813611782648638, 0.07882673910441718},
      {1.0813523219872145, 0.0262893509371555}, {1.0813592051533454, -0.026264998396064613},
      {1.0813409356657018, -0.07881839881959708}, {1.1268574199937307, 0.10512544422455178},
      {1.1268649102841262, 0.05255081225835267}, {1.1268709027454915, -4.005680860113609e-07},
      {1.12686227613459, -0.0525694173219599}, {1.1268579282173745, -0.10510542516374281},
      {-1.0474999999999999, -0.292}, {-1.0474999999999999, 0.292}, {-1.0474999999999999, 0.0}, {0.0, 0.0},
      {0.89225, 0.0}, {1.4605, 0.0}}},
    {"fuzz seed 987654 shot 8808", 12828508054284956633ULL,
     {-0.05500740228629478, -0.30413349205954593, 0.1274382171818439, 0.05, 8.914554389224723},
     {{-1.2626341798979022, 0.18726879513498573}, {0.9448140039037456, 1.3870555858799291e-05},
      {0.990305019217495, 0.026264868976357925}, {0.9903278528447933, -0.026274221399745914},
      {1.0358266503463922, 0.05255327633425724}, {1.035821275293086, -9.002418782933685e-06},
      {1.0358415899110098, -0.052562323814922624}, {1.0813379170868167, 0.07884091807295363},
      {1.08133828735951, 0.026271216202927748}, {1.0813494286743315, -0.026271734902341463},
      {1.0813422004270123, -0.07884504192627412}, {1.1268714718343555, 0.10511009294688334},
      {1.1268617300134214, 0.052558973740525254}, {1.126873242867534, -4.791131765541185e-06},
      {1.1268638107101914, -0.052539758286691364}, {1.1268569389131557, -0.105094648098512},
      {-1.0474999999999999, -0.292}, {-1.0474999999999999, 0.292}, {-1.0474999999999999, 0.0}, {0.0, 0.0},
      {0.89225, 0.0}, {1.4605, 0.0}}},
    {"fuzz seed 987654 shot 21113", 15523647037270407919ULL,
     {-0.04570653210120061, -0.3163416756922972, 0.15375522160900723, 0.05, 5.311054966488326},
     {{-1.0477185661155852, 0.1246186757391301}, {0.9448166939386841, 1.2943032152234835e-05},
      {0.9903066020304585, 0.026275944653231735}, {0.9903312336068402, -0.026285291487428586},
      {1.0358400243076293, 0.052569635150197275}, {1.0358418004469732, -9.829723681116177e-06},
      {1.0358234853843082, -0.05255437527391983}, {1.0813517398383603, 0.07882985609684863},
      {1.0813585922469269, 0.02628735840150784}, {1.0813562806449422, -0.026276008448810072},
      {1.081341934867549, -0.07883961242547287}, {1.1268638756902514, 0.1051133075000615},
      {1.1268671212522527, 0.052550311334588186}, {1.1268682425576457, -6.234581908427577e-06},
      {1.1268746713056252, -0.05255723417372403}, {1.1268635750750393, -0.10512504215255547},
      {-1.0474999999999999, -0.292}, {-1.0474999999999999, 0.292}, {-1.0474999999999999, 0.0}, {0.0, 0.0},
      {0.89225, 0.0}, {1.4605, 0.0}}},
    {"fuzz seed 1234 shot 36491", 16411352927619500300ULL,
     {0.04690669554922932, 0.3817695179690869, 0.2474931604578936, 0.05, 8.712526369844092},
     {{-1.0474999999999999, -0.146}, {0.9447912601178404, 1.0801736824301677e-05},
      {0.9903060291305207, 0.026270205037872998}, {0.9903302604029143, -0.026290042513798354},
      {1.0358440050841817, 0.05256267569542984}, {1.0358176410669748, 7.306400683984294e-06},
      {1.03584462762153, -0.052544693335012584}, {1.0813465112533338, 0.07883359133661791},
      {1.0813465611962387, 0.026292744335864274}, {1.081337811127583, -0.026277014315262415},
      {1.0813516521908966, -0.07884053023820904}, {1.1268709992336827, 0.10510861997120768},
      {1.1268601712692476, 0.052548529284491184}, {1.1268710029564992, 3.558170371695466e-06},
      {1.1268567093624573, -0.05255151720263937}, {1.1268508253947418, -0.10510213571215012},
      {-1.0474999999999999, -0.292}, {-1.0474999999999999, 0.292}, {-1.0474999999999999, 0.0}, {0.0, 0.0},
      {0.89225, 0.0}, {1.4605, 0.0}}},
    {"fuzz seed 99 shot 87373", 12464437847059355868ULL,
     {0.05515880708372842, 0.20077523838545436, 0.17381912543730235, 0.05, 9.147301038375826},
     {{-1.0474999999999999, -0.146}, {0.9448092282871604, -7.499321139553864e-06},
      {0.9903257820510728, 0.026265028461919037}, {0.9903191453155455, -0.026286754820463214},
      {1.0358323688164335, 0.05255825352634386}, {1.0358451477828847, -5.7113657090537215e-06},
      {1.0358264883533026, -0.05256846736139647}, {1.0813469590397904, 0.07882354137663952},
      {1.0813544483924191, 0.026280061881803214}, {1.0813438527974124, -0.02628913256717109},
      {1.0813545825900275, -0.0788463421356359}, {1.1268532511642047, 0.10511960879681939},
      {1.1268747688209584, 0.052556698449526894}, {1.1268766835109616, 1.0631049730095632e-05},
      {1.1268486620957598, -0.05256770420109402}, {1.126867555028708, -0.10511995465111096},
      {-1.0474999999999999, -0.292}, {-1.0474999999999999, 0.292}, {-1.0474999999999999, 0.0}, {0.0, 0.0},
      {0.89225, 0.0}, {1.4605, 0.0}}},
    {"fuzz seed 99 shot 96280", 17258799761289507517ULL,
     {0.06851943488515372, 0.07098528262748013, 0.04958508164926553, 0.05, 9.507246589237665},
     {{-1.0474999999999999, -0.146}, {0.9447936733323758, -9.156607928632089e-06},
      {0.9903239245371217, 0.026269685592189215}, {0.9903305394999881, -0.026274224855528654},
      {1.0358249284943328, 0.05256685677571079}, {1.0358302176587346, -4.9100951001242125e-06},
      {1.0358205129871345, -0.052557131668998516}, {1.0813528210085197, 0.07881696934171582},
      {1.0813532685603653, 0.026290699698412932}, {1.0813495061928924, -0.026280628234467706},
      {1.0813503830555224, -0.0788194977091189}, {1.1268514317291574, 0.10511376391332214},
      {1.1268763239473052, 0.05254054926972016}, {1.1268469354735233, 1.0882230463731258e-05},
      {1.126875021688054, -0.052549224461677146}, {1.1268728437998898, -0.10511446254888365},
      {-1.0474999999999999, -0.292}, {-1.0474999999999999, 0.292}, {-1.0474999999999999, 0.0}, {0.0, 0.0},
      {0.89225, 0.0}, {1.4605, 0.0}}},
};

// The stress run's slowest: a 0.3 m/s cue ball into the rack.
const Recorded kPackSlow590 =
    {"pack_slow seed 7 shot 590", 15610056916312542684ULL,
     {3.1298700232484653, 0.10236230662560414, 0.2935599052167643, 0.04997560160812806, 0.3},
     {{1.1811011668227525, -0.02864161439557267}, {0.9448009216853521, 1.116335770768583e-05},
      {0.9903134935107555, 0.026292407956247578}, {0.9903130345482468, -0.026270503824724824},
      {1.0358484469794753, 0.052563236164075704}, {1.0358412929074339, 9.20827538572624e-06},
      {1.03584638669964, -0.052552009166289965}, {1.0813570273945965, 0.07883873516739945},
      {1.0813386216456329, 0.026274601557014027}, {1.0813393974092775, -0.026276336076632753},
      {1.0813371761954902, -0.07884444128234036}, {1.126875750314084, 0.10511812512210883},
      {1.126848273220044, 0.052552247602167514}, {1.1268510608798483, -8.654198772958426e-06},
      {1.1268732499745127, -0.0525467773630936}, {1.1268660453495707, -0.10509998625640243},
      {-1.0474999999999999, -0.292}, {-1.0474999999999999, 0.292}, {-1.0474999999999999, 0.0}, {0.0, 0.0},
      {0.89225, 0.0}, {1.4605, 0.0}}};

// A break-off at 10 m/s with screw and side.
const Recorded kFuzz11_979 =
    {"fuzz seed 11 shot 979", 18016043639610330091ULL,
     {-1.242233572009671, 0.10803665091808991, -0.4184298557004154, 0.14432825927785695, 10.0},
     {{-1.0985065781784598, 0.18576216633007175}, {0.9448029818481366, 8.280867348964009e-06},
      {0.9903252164030456, 0.026286654904930498}, {0.9903101049589669, -0.02627168237784411},
      {1.0358247795773465, 0.05254447131331663}, {1.0358368551826629, -1.3702213540002973e-05},
      {1.0358335009818849, -0.052555114041009654}, {1.0813386982120883, 0.07884776388917807},
      {1.0813413039481192, 0.0262831294836046}, {1.0813512199467723, -0.02627133808945111},
      {1.0813321491098424, -0.07884686271541122}, {1.126870681447282, 0.10512421224209127},
      {1.1268513508479625, 0.05256868172709151}, {1.126852361767549, 2.7676851994517726e-07},
      {1.1268766322247386, -0.052539950386931836}, {1.1268715557832283, -0.10510115640238223},
      {-1.0474999999999999, -0.292}, {-1.0474999999999999, 0.292}, {-1.0474999999999999, 0.0}, {0.0, 0.0},
      {0.89225, 0.0}, {1.4605, 0.0}}};

// A 0.3 m/s cue ball into the rack.
const Recorded kPackSlow324 =
    {"pack_slow seed 7 shot 324", 5283913953806215594ULL,
     {1.576347453490683, -0.16743344028384768, 0.1667732110668659, 0.12718776140925678, 0.3},
     {{1.035832942433332, -0.12743406994105236}, {0.9448056975257756, 3.474803114655956e-06},
      {0.9903295144708022, 0.026287061680857902}, {0.990309040521577, -0.02626475353237509},
      {1.0358322400090085, 0.05255857885706079}, {1.0358238485094367, -9.906582898722725e-06},
      {1.035844118373428, -0.05256740433449485}, {1.081341959158136, 0.07884087351165647},
      {1.0813519643196556, 0.02628553026359347}, {1.0813551417255765, -0.026264068302881943},
      {1.0813518518233993, -0.07884266803377829}, {1.1268537303940287, 0.10509715854656537},
      {1.126869061709892, 0.052546979013213764}, {1.126852395113181, -1.3125692465086423e-05},
      {1.12686303573803, -0.05255402011327923}, {1.1268680555106785, -0.10509649496744132},
      {-1.0474999999999999, -0.292}, {-1.0474999999999999, 0.292}, {-1.0474999999999999, 0.0}, {0.0, 0.0},
      {0.89225, 0.0}, {1.4605, 0.0}}};

// C12's shot 1930 (seed 0x5eed0c12): a line of 19 frozen balls struck at 6.5 m/s.
const Recorded kC12_1930 =
    {"C12 seed 0x5eed0c12 shot 1930", 18045464461395506697ULL,
     {0.776383033389042, -0.17370751625617267, 0.3081819743514592, 0.2201357022394606, 6.482361666976972},
     {{-0.649783488388052, -0.6813793505533896}, {0.28812015285805925, 0.32765914182226963},
      {0.3150452324326346, 0.2825893061235089}, {0.34197031200720995, 0.23751947042474822},
      {0.3688953915817853, 0.19244963472598753}, {0.39582047115636065, 0.14737979902722684},
      {0.422745550730936, 0.10230996332846615}, {0.44967063030551135, 0.05724012762970545},
      {0.4765957098800867, 0.012170291930944752}, {0.503520789454662, -0.032899543767815946},
      {0.5304458690292374, -0.07796937946657664}, {0.5573709486038128, -0.12303921516533733},
      {0.5842960281783881, -0.16810905086409803}, {0.6112211077529635, -0.21317888656285872},
      {0.6381461873275388, -0.25824872226161943}, {0.6650712669021142, -0.30331855796038015},
      {0.6919963464766895, -0.34838839365914087}, {0.7189214260512649, -0.3934582293579016},
      {0.7458465056258402, -0.4385280650566623}, {0.7727715852004156, -0.483597900755423}}};

}  // namespace

TEST_CASE("simulate resolves contacts in time order, so none is resolved with its balls inside each other") {
    // Two pairs close 0.5 ns apart at 5 m/s, the later one lower-numbered. Resolved first, it
    // would carry the earlier pair 2.5e-9 m into each other before that one's turn came.
    const double gap = 0.005;
    SimInput in;
    in.start.balls = {rolling(ofs::table::Ball::red, 0, -0.3, 0.3, 5.0, 0.0),
                      rolling(ofs::table::Ball::red, 1, -0.3 + 2.0 * kR + gap + 2.5e-9, 0.3, 0.0, 0.0),
                      rolling(ofs::table::Ball::red, 2, -0.3, -0.3, 5.0, 0.0),
                      rolling(ofs::table::Ball::red, 3, -0.3 + 2.0 * kR + gap, -0.3, 0.0, 0.0)};
    in.has_strike = false;
    const PhysicsParams& p = file_params();
    const ShotRecord r = simulate(in, table(), p);
    CHECK(r.anomalies.empty());
    std::vector<ShotEvent> contacts;
    for (const ShotEvent& e : r.events) {
        if (e.kind == EventKind::ball_ball) contacts.push_back(e);
    }
    REQUIRE(contacts.size() >= 2);
    CHECK(contacts[0].a == 2);
    CHECK(contacts[0].b == 3);
    CHECK(contacts[1].a == 0);
    CHECK(contacts[1].b == 1);
    CHECK(contacts[1].t > contacts[0].t);
    CHECK(contacts[1].t - contacts[0].t < p.zero_time_s);
    CHECK(worst_overlap(r) <= kOverlapTolerance_m);
}

TEST_CASE("simulate the check round's overlapping break-offs: no pair ever closer than 2R - kOverlapTolerance_m") {
    const PhysicsParams& p = file_params();
    for (const Recorded& s : kOverlapShots) {
        INFO(std::string(s.name));
        const ShotRecord r = simulate(input(s), table(), p);
        CHECK(r.anomalies.empty());
        CHECK(worst_overlap(r) <= kOverlapTolerance_m);
    }
}

TEST_CASE("simulate a slow cue ball into the pack: it settles into held contacts instead of chattering") {
    // Pushes opened neighbouring held contacts at 1e-8-1e-6 m/s, which re-closed as new pushes
    // microseconds later: 2947 events (2840 contacts under 1 cm/s) and 16 ms.
    const ShotRecord r = simulate(input(kPackSlow590), table(), file_params());
    CHECK(r.anomalies.empty());
    MESSAGE(r.events.size() << " events");
    CHECK(r.events.size() < 400);
    for (const BallState& b : r.final_state.balls) CHECK(at_rest(b));
    CHECK(worst_overlap(r) <= kOverlapTolerance_m);
}

TEST_CASE("simulate a break-off whose held groups ran contact solves to 2000 sweeps") {
    // 791 events and 5.5 ms, 24k of its 53k Gauss-Seidel sweeps in 12 solves that never converged.
    const ShotRecord r = simulate(input(kFuzz11_979), table(), file_params());
    CHECK(r.anomalies.empty());
    MESSAGE(r.events.size() << " events");
    CHECK(r.events.size() < 400);
    for (const BallState& b : r.final_state.balls) CHECK(at_rest(b));
    CHECK(worst_overlap(r) <= kOverlapTolerance_m);
}

TEST_CASE("simulate a ball wedged almost in line between two others: the contact solve settles") {
    // Updating every violated contact of the active set at once cycled here until the rounds ran
    // out, and Gauss-Seidel then left 1e-4 m/s^2 of closing acceleration.
    const ShotRecord r = simulate(input(kPackSlow324), table(), file_params());
    CHECK(r.anomalies.empty());
    for (const BallState& b : r.final_state.balls) CHECK(at_rest(b));
}

TEST_CASE("simulate a ball creeping out between two in line comes to rest instead of rolling on") {
    // The contact forces holding it from the gap fed it its rolling losses back: it rolled at
    // 3e-8 m/s, gaining energy, until time_cap_s.
    const PhysicsParams& p = file_params();
    const ShotRecord r = simulate(input(kC12_1930), table(), p);
    CHECK(r.anomalies.empty());
    CHECK(r.t_end < 20.0);
    for (const BallState& b : r.final_state.balls) CHECK(at_rest(b));
    double last = std::numeric_limits<double>::infinity();
    for (const ShotEvent& e : r.events) {
        if (e.kind == EventKind::strike) continue;
        double ke = 0.0;
        for (const BallState& b : r.state_at(e.t).balls) ke += kinetic_energy(b, p);
        CHECK(ke <= last * (1.0 + 1e-9) + 1e-12);
        last = ke;
    }
}

TEST_CASE("simulate records a cushion impact solve that gave up as solver_fallback, naming the cushion") {
    // At a contact height of 2R (the impact solver gives up from about 0.05249 m) the loader
    // refuses the value, so the parameters are set directly.
    PhysicsParams p = file_params();
    p.cushion_contact_height_m = 0.0525;
    // near_left_cushion.json: 0.3 m from the left cushion at the table's middle, struck at 45 degrees.
    SimInput in;
    BallState cue;
    cue.r = {0.0, 0.589, 0.0};
    in.start.balls = {cue};
    in.strike.aim_rad = 0.7853981633974483;
    in.strike.speed_m_s = 3.0;
    in.params_hash = "test";
    ShotRecord r;
    const auto logged = simulate_capturing(in, p, r);
    REQUIRE(count(r, "solver_fallback") >= 1);
    const Anomaly* fallback = nullptr;
    for (const Anomaly& a : r.anomalies) {
        if (a.kind == "solver_fallback" && fallback == nullptr) fallback = &a;
    }
    CHECK(fallback->balls == std::vector<int>{0});
    const ShotEvent* hit = nullptr;
    for (const ShotEvent& e : r.events) {
        if (e.kind == EventKind::ball_line && e.t == fallback->t && hit == nullptr) hit = &e;
    }
    REQUIRE(hit != nullptr);
    CHECK(fallback->detail.find("cushion line " + std::to_string(hit->body)) != std::string::npos);
    // Every bit of the state it hit with, so the impact can be rerun alone.
    const std::vector<Segment>& segs = r.segments[0];
    std::size_t k = 0;
    while (k + 1 < segs.size() && segs[k + 1].t0 < hit->t) ++k;
    const double dt = std::min(hit->t - segs[k].t0, transition_time(segs[k].start, p, segs[k].nap));
    const BallState before = evolve(segs[k].start, dt, p, segs[k].nap);
    char v[128];
    std::snprintf(v, sizeof v, "v = (%.17g, %.17g, %.17g)", before.v.x, before.v.y, before.v.z);
    CHECK_MESSAGE(fallback->detail.find(v) != std::string::npos, fallback->detail);
    std::snprintf(v, sizeof v, "w = (%.17g, %.17g, %.17g)", before.w.x, before.w.y, before.w.z);
    CHECK_MESSAGE(fallback->detail.find(v) != std::string::npos, fallback->detail);
    const nlohmann::json schema = ofs::log::load_schema(std::string(OFS_CORE_DATA_DIR) + "/log_schema.json");
    bool found = false;
    for (const ofs::log::Event& e : logged) {
        if (e.event != "physics.anomaly" || e.data["kind"] != "solver_fallback") continue;
        found = true;
        const auto problem = ofs::log::check(e, schema);
        CHECK_MESSAGE(!problem.has_value(), problem.value_or(""));
    }
    CHECK(found);
}
