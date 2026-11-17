// The pockets' mouths built once per decision, and every ball's open pockets in one pass, read
// the table exactly as building them on every call did: the digests below were captured at
// f27b5e8, before the change.

#include <doctest/doctest.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "ofs/opponent/pots.hpp"
#include "ofs/rng.hpp"

#include "rig.hpp"

using namespace ofs;
using namespace ofs::opponent;
using rules::Ball;
using rules::FrameState;

namespace {

constexpr double R = table::kBallRadius;

struct Digest {
    std::uint64_t h = 1469598103934665603ull;
    void bytes(const void* p, std::size_t n) {
        const auto* c = static_cast<const unsigned char*>(p);
        for (std::size_t i = 0; i < n; ++i) {
            h ^= c[i];
            h *= 1099511628211ull;
        }
    }
    void num(double v) { bytes(&v, sizeof v); }
    void num(int v) { bytes(&v, sizeof v); }
};

void add(Digest& d, const std::vector<Pot>& list) {
    d.num(int(list.size()));
    for (const Pot& p : list) {
        d.num(p.target);
        d.num(p.pocket);
        d.num(p.cue.x);
        d.num(p.cue.y);
        d.num(p.aim_point.x);
        d.num(p.aim_point.y);
        d.num(p.ghost.x);
        d.num(p.ghost.y);
        d.num(p.aim_rad);
        d.num(p.cut_rad);
        d.num(p.cue_travel_m);
        d.num(p.object_travel_m);
        d.num(p.margin_rad);
        d.num(int(p.awkward));
        d.num(int(p.along_cushion));
        d.num(p.value);
        d.num(int(p.input.has_nomination));
        d.num(int(p.input.nominated));
    }
}

std::uint64_t digest(const FrameState& s, const Env& env) {
    Digest d;
    add(d, pots(s, env, 1.2));
    add(d, pots(s, env, 1.35));
    add(d, pots_from(s, s.balls[std::size_t(s.cue_ball)].pos, env, 1.35));
    for (int i = 0; i < int(s.balls.size()); ++i) {
        if (i == s.cue_ball) continue;
        const std::vector<int> open = pockets_open_to(s, i, env);
        d.num(int(open.size()));
        for (const int k : open) d.num(k);
    }
    d.num(open_reds(s, env));
    return d.h;
}

// Balls at least a millimetre apart, a fifth of them against a cushion; reds and colours in any
// number, every kind of ball on, the cue ball sometimes in hand, sometimes a free ball.
FrameState random_position(std::uint64_t seed) {
    Rng rng(seed);
    const double hx = table::kPlayingLength / 2.0 - R;
    const double hy = table::kPlayingWidth / 2.0 - R;
    std::vector<rules::BallOnTable> balls;
    const auto place = [&](Ball b, int index) {
        for (int tries = 0; tries < 200; ++tries) {
            double x = rng.uniform(-hx, hx);
            double y = rng.uniform(-hy, hy);
            const double edge = rng.uniform();
            if (edge < 0.1) {
                x = x < 0.0 ? -hx : hx;
            } else if (edge < 0.2) {
                y = y < 0.0 ? -hy : hy;
            }
            bool clear = true;
            for (const rules::BallOnTable& o : balls) {
                if (std::hypot(o.pos.x - x, o.pos.y - y) < 2.0 * R + 0.001) clear = false;
            }
            if (clear) {
                balls.push_back({b, index, {x, y}});
                return;
            }
        }
    };
    place(Ball::cue, 0);
    const int reds = int(rng.uniform() * 16.0);
    for (int i = 0; i < reds; ++i) place(Ball::red, i);
    const Ball colours[] = {Ball::yellow, Ball::green, Ball::brown, Ball::blue, Ball::pink, Ball::black};
    const int first_colour = reds > 0 ? 0 : int(rng.uniform() * 6.0);
    for (int c = first_colour; c < 6; ++c) {
        if (reds > 0 && rng.uniform() < 0.15) continue;
        place(colours[c], 0);
    }
    FrameState s = rules::scenario::frame(balls, 0);
    const double kind = rng.uniform();
    if (reds > 0) {
        s.on = kind < 0.6 ? rules::BallOn{rules::OnKind::red, Ball::yellow}
                          : rules::BallOn{rules::OnKind::any_colour, Ball::yellow};
    } else {
        Ball lowest = Ball::black;
        for (const rules::BallOnTable& b : s.balls) {
            if (b.ball != Ball::cue && int(b.ball) < int(lowest)) lowest = b.ball;
        }
        s.on = {rules::OnKind::colour, lowest};
    }
    s.cue_in_hand = rng.uniform() < 0.15;
    s.free_ball = !s.cue_in_hand && rng.uniform() < 0.1;
    return s;
}

const char* const kFixtures[] = {"after_red", "black_only", "colours",   "cut_red",     "in_hand",
                                 "last_red",  "long_red",   "snookered", "straight_red"};
constexpr int kRandom = 300;

// Captured at f27b5e8 (OFS_CAPTURE_POTS=1 prints them): the fixtures, then the random positions.
const std::uint64_t kCaptured[] = {
    0x519e4f5a5d7dbe30ull,
    0x09c7d197b11d15c1ull,
    0xd920b5e76babf81bull,
    0x50f4b67a26ac7c38ull,
    0x24c2f1744ce5123cull,
    0x2df9ce77f730b6caull,
    0xc7b04a7696763431ull,
    0x102e7da8211ceb16ull,
    0x7e577ccdfea71b32ull,
    0x4af5e64f32b4383cull,
    0xa0b6419bc903231full,
    0x85b02c797e3f7a5bull,
    0x003513a329564f98ull,
    0x9c64c4df8cdd7666ull,
    0x7f8c013e6833cc18ull,
    0xd771b912c28a8773ull,
    0x5ee85ca1eb2a2048ull,
    0x52648b4885228352ull,
    0x7c6f342b01216ffbull,
    0xc30fa0bdfd065d4full,
    0xb3bbab128f257b03ull,
    0xb6558e7f35c67611ull,
    0x0d09e3e74ff68d0aull,
    0xa21f2faef3cfbb08ull,
    0x95aa560f7ba41fc8ull,
    0x56871cfd5c4f2791ull,
    0xeb5b17c7fd797625ull,
    0xc10d70589cdc2df1ull,
    0x58e96303160da54full,
    0x82d8706072b7f471ull,
    0x35c57a2bf33abbacull,
    0xae86eb0f0fb1bf33ull,
    0xc8ee7a1e93d6e9f2ull,
    0x1c9bd7b53a2e692cull,
    0xf1a8f9e4ec8096e6ull,
    0xde5a4ee38131cabeull,
    0x450182b8a6edb962ull,
    0x1297f42095eb6e84ull,
    0x7673f86fb31506a0ull,
    0xeb2e06b3d709bca8ull,
    0x40ceb2701684073dull,
    0xefac9f5e06f28ca8ull,
    0x5d0eaf61c389b955ull,
    0xcfc156b1ac492da3ull,
    0x243c24a9ef68168cull,
    0x92b6ce1d43c3a845ull,
    0x088f3f6e878c31e3ull,
    0x48a7f67dc1727e0dull,
    0x9152e90402d9a9b3ull,
    0x80b4136996e1da3dull,
    0x0c6f42e4485c533bull,
    0x3962d9d795e83358ull,
    0x3880670764f58c8eull,
    0xf0c5dc72190dd717ull,
    0x06b0792e332cb362ull,
    0x150d3a9a972caad0ull,
    0x58a4233ec81013c0ull,
    0x762c96d694e3a2b2ull,
    0xe12f766941b7b5d6ull,
    0xf91c112cf0437a50ull,
    0x14470ee80c003cc9ull,
    0xf780d8311153f312ull,
    0xf623402c930c4ef0ull,
    0x75312be4f5ce24eaull,
    0x026f0ed89b06eac2ull,
    0x82c78edb50b48556ull,
    0xd18170d99b4a1af5ull,
    0x5e3cc97addb2099eull,
    0xc6ef107dba77f461ull,
    0xeb7c3eaa51bce686ull,
    0x942fa04cd471b912ull,
    0x4c2ee92886653e0cull,
    0xd4a78a33ac28d12bull,
    0xf40162af51841cdfull,
    0x22c440a4d909b9b2ull,
    0x14fe618309aae688ull,
    0x9cd0e0eee61ed1bbull,
    0xf45b15e02597598eull,
    0xb38ef3f585e619c8ull,
    0xe0739a8cbcedcd0bull,
    0x872f5bf2dfde8ba5ull,
    0xab0576cc7e268600ull,
    0x2ea98d6cc61ee3cfull,
    0xc89f69ed7ab8fc77ull,
    0x4eb7ad24767c2fb1ull,
    0x197466056742628full,
    0x311811796fd2a81bull,
    0x30854350c1e9bf52ull,
    0x2dd031c80e68fd44ull,
    0xdf5147a3254c5122ull,
    0x997d5629d9b6ac28ull,
    0xef9a123c082ec12aull,
    0x1e7e5612d90cc99aull,
    0xc6972193da9d5a26ull,
    0xa9571ef208f9b278ull,
    0x6e902270ec8b87e9ull,
    0x60c9ebfb63ec12e3ull,
    0xdec303b9500558f8ull,
    0x54184acc06f76f04ull,
    0xbcd19ace942faadbull,
    0x9b2508b0ec3704f4ull,
    0xba7bba58c2ad21c0ull,
    0xb68aaa9a1331d1c8ull,
    0x6182114d4304bf4dull,
    0x98c0cace683d80efull,
    0xe7efb5151b4a6a63ull,
    0x19a5f2caf0d4a3a5ull,
    0x864de214a1031b0bull,
    0x28e74d0a95d7a749ull,
    0xbd0bdaf1aed20e25ull,
    0x235b4a789dccced6ull,
    0xc35b69daa8b53897ull,
    0xd2cfbb723a5af727ull,
    0x705f009dfc5b8a85ull,
    0x25538a637f2fb467ull,
    0x53df512489741d0bull,
    0xa855fc1a1248548aull,
    0xc5daac3f7e4775d9ull,
    0x90ee0441469e1f39ull,
    0xcfbc06bc14259248ull,
    0x4d8349e180326956ull,
    0xe455d51cace18238ull,
    0x49a35e9ed1eb81c3ull,
    0xcca9c901b1bae35eull,
    0x8addaeb39a532e8bull,
    0x9dfb7d24c8112f38ull,
    0x7f1f7af50006f62dull,
    0xbb607f6126f2de12ull,
    0x49b7a86ec1fdd10cull,
    0xa1afad40cec62cb3ull,
    0x845f86abafbae5bdull,
    0x5079352f1c7b5159ull,
    0x2307d9b5e2f60faaull,
    0x1466a973d2766efdull,
    0x2c7605b978f18996ull,
    0xf1bc4b46222fce9cull,
    0xec5c1ceaeb325b0cull,
    0xf0ab970e073994f3ull,
    0x9b1b785447686685ull,
    0x819a04c9846b2ac6ull,
    0x442251bfd663d856ull,
    0x15e320e525653bf7ull,
    0x82d6bef5103ec26aull,
    0xdf4a9417ef1dd4baull,
    0x956b34200471ab70ull,
    0x321347be1a77497full,
    0x21529ff47f7cd815ull,
    0x4a80765057905ee4ull,
    0x8b91f06aa4b2d55cull,
    0xc4ffbf60e01f94bfull,
    0x413780a9e486a8e9ull,
    0x84e704295eb45f08ull,
    0x0e96b537f5d3ad88ull,
    0xe97fb085f755374aull,
    0x80aae0cda7bb35dcull,
    0xf0e0d9730b205d76ull,
    0xecc1d3af03b5a0bcull,
    0x2625194383001d72ull,
    0x57980d7c87bd8d8full,
    0xb4d195f0a87aa099ull,
    0x1c444166ecf18b9full,
    0x145253bd18189a15ull,
    0x9e39aa2c46161054ull,
    0xcfbb8c7ed63bf3f8ull,
    0xbbacdf89d5efafe1ull,
    0xea2b992840651e3aull,
    0xb4b84304bf8082f2ull,
    0xe9e8ee0a8c7610f3ull,
    0x542f3548407cbbfeull,
    0x0bcac9ad19569553ull,
    0x366aad0bfbd86d08ull,
    0x84c12dc61f935f00ull,
    0x1e2dc6e5a32b2810ull,
    0xb27a7fe5e31fef95ull,
    0x2493bfee8fb83ea4ull,
    0x51c7b20b6c3923baull,
    0xce5480a78ed2b37bull,
    0x67c0854a6a1fdcdaull,
    0xb5962e13e7413943ull,
    0x09a8fd37dea5af18ull,
    0x05eef1422981181dull,
    0x75f8b422fd712229ull,
    0xe4e7a69520b9f6b9ull,
    0x0df912d1dc4a4e88ull,
    0x96db9440356009a2ull,
    0x3052c89ad456564full,
    0xb029d701a6419dceull,
    0x01d7d4adc6ca1b06ull,
    0x68671c0bc861ea78ull,
    0xf561d92398ced6c7ull,
    0x788f1798082977e5ull,
    0xd5be7323d649298aull,
    0x09431002843b4d18ull,
    0x24adfef8d5172f23ull,
    0x9547aaf35101b033ull,
    0xc705843b29dd7b8aull,
    0xbfe0ed81729e0c2full,
    0x0b7e14f8c6e5822cull,
    0x524ebae122e36506ull,
    0xb34abcd468265764ull,
    0xcaa5dc7e98dca6fcull,
    0x67a0518cc4ac1f5aull,
    0xdf0bd3fb56709989ull,
    0x15bf5957f0533d5cull,
    0xe4002f3ff7fd1dd8ull,
    0x182efe3a1e3bc9abull,
    0x28f55ee3809da954ull,
    0x934021d614b6d8f4ull,
    0xf9a9a3d13d2c7034ull,
    0xa090c4491a70156full,
    0x85291ae2d46d6a2dull,
    0x37b31c1834345943ull,
    0xf0b4c1525005d2a8ull,
    0x16f47b97a647c1a5ull,
    0xcdb80bc27a7f27caull,
    0xe798df30dde4ae6eull,
    0x2f871d198d70548bull,
    0x01522934c2f27630ull,
    0x2cfb460f42ac9041ull,
    0x67247820b517c15eull,
    0x4e493e1037189085ull,
    0x7f7bddf3b05db07eull,
    0xe14b32e0af077489ull,
    0x9a0e75bbbac9b320ull,
    0x108ab340601e0f00ull,
    0xbd1db911ae0d1a30ull,
    0xa95d40d09c947cdaull,
    0xacee0b1404c08104ull,
    0x0d5320f98da4bd37ull,
    0x2d391691256ffe68ull,
    0xe3608844a8f2afa8ull,
    0x0d8733e56223123bull,
    0xf81e99687e8fbd92ull,
    0x039ebe8972eb7f3dull,
    0xfa134ae9774ba004ull,
    0x9a4fa3b933008ba2ull,
    0xe02a7ba2132267d4ull,
    0x0767103765784156ull,
    0x08aeb3b6eb501b49ull,
    0xc6d4b4dbc2b6d0d4ull,
    0x17299185b2bd1ce8ull,
    0x45eb59532721753aull,
    0x1ea17184429c92c3ull,
    0xe938569cb76a5962ull,
    0xc8c47e4e08fd1162ull,
    0xcaad182ef1a838a5ull,
    0x4ee196bb4d7aee75ull,
    0x9b66a64a64a1bfceull,
    0x61b58ab8f35d3e80ull,
    0xa1e3888fc6af52d6ull,
    0xc65ad8f2852ff569ull,
    0xbb4d42fb2f08b4c7ull,
    0x90d128ea1f072cc5ull,
    0xc0789fc19b5eb209ull,
    0xa2acee986ee31175ull,
    0x03062a55b016acc2ull,
    0x8b74ee4735c32055ull,
    0x95363c59c5557847ull,
    0x60cc12c1d0c6967bull,
    0x1e82ebc327230e12ull,
    0x7af1de243b7ae25dull,
    0xf20e1c81e9be841cull,
    0xa74eecce43c37780ull,
    0xbc05de41c8b78ffeull,
    0x2b724b277fbf327cull,
    0xe3fdaaef4a6e2025ull,
    0xf5c71ca20bbdc90eull,
    0xb9a616393e7c9ea1ull,
    0x97a5197078a8ab18ull,
    0x949d59e709b65489ull,
    0xc28c05c14054cc4aull,
    0xaa92acefa3f0c276ull,
    0x8dd3eb046c407c02ull,
    0x08903b4ec685f059ull,
    0x87d75ccb1277ed1dull,
    0x1312d018353da75aull,
    0xe72235422dc9bd6full,
    0xb863d6c456287418ull,
    0x63bc0be9852f7d05ull,
    0xc2349fdeb856a0d1ull,
    0xbe024449a15f5b7cull,
    0xb2a4ff2d64a1a613ull,
    0xaa7c03496c2cc880ull,
    0x66cd627e1d886c66ull,
    0x5bf5400dd4d3b30bull,
    0x4b82b0213943f51aull,
    0x9e18c7bda7021b81ull,
    0x127a0ddda8e00b80ull,
    0xbb56fb1c91aa2ff1ull,
    0xb0b3271ecd9addf7ull,
    0x497b7d76fbfcb43full,
    0x1c3ef66e048b3875ull,
    0xdcf68e1701000198ull,
    0x3e32bb6013f0acf3ull,
    0x568b04015bebc501ull,
    0x7100c69439ec96acull,
    0xf969683037431359ull,
    0x0200991d5014bc02ull,
    0xba070ef24c7f90f6ull,
    0x006f079a155152f2ull,
    0xb15457167476b277ull,
    0x9c317d0d527528a8ull,
    0xf77e29585dd89d10ull,
    0xf89d8de02b7f0958ull,
    0x0f88ec7e4f5f38c5ull,
    0xf04feec8e4a003f2ull,
    0xd3de3509d7cc6ca9ull,
    0x076c38abfccf06f9ull,
    0x88e6235d9e386997ull,
};

}  // namespace

TEST_CASE("the pots, the open pockets and the open reds read as they did before the mouths were cached") {
    const Env env = rig::env();
    std::vector<std::uint64_t> now;
    for (const char* name : kFixtures) now.push_back(digest(rig::position(name), env));
    for (int i = 0; i < kRandom; ++i) now.push_back(digest(random_position(std::uint64_t(1000 + i)), env));

    if (std::getenv("OFS_CAPTURE_POTS") != nullptr) {
        for (const std::uint64_t h : now) std::printf("    0x%016llxull,\n", static_cast<unsigned long long>(h));
    }
    REQUIRE(sizeof(kCaptured) / sizeof(kCaptured[0]) == now.size());
    for (std::size_t i = 0; i < now.size(); ++i) {
        CAPTURE(i);
        CHECK(now[i] == kCaptured[i]);
    }
}
