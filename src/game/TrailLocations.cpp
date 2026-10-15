// The seven trail locations (0x40a2f0–0x40a97f): TrailNode subclasses that
// only choose the trail the ride starts on and the map location. Their
// vtables (0x7f03a0 … 0x7f0a00) differ from TrailNode's in the destructor
// slot alone. See docs/engine/nodes.md "TrailNode".
#include "game/TrailNode.h"

namespace brc::game {
namespace {

class TrailLocation final : public TrailNode {
public:
    TrailLocation(Godiva& app, const std::string& name, int trail, int mapLocation) : TrailNode(app, name, trail) {
        // Each constructor clears app+0x338 (keep the trail resources) and sets +0x7EC.
        app.session().keepTrailResources = 0;
        setMapLocation(mapLocation);
    }
};

template <int Trail, int MapLocation>
std::unique_ptr<engine::Node> create(Godiva& app, const std::string& name) {
    return std::make_unique<TrailLocation>(app, name, Trail, MapLocation);
}

} // namespace

BRC_REGISTER_NODE("appleW", (create<0, 0>));   // 0x40a2f0: trail 0 aaW (AppleW.smk)
BRC_REGISTER_NODE("beachE", (create<5, 5>));   // 0x40a3a0: trail 5 trE (TurtleE.smk)
BRC_REGISTER_NODE("cottageL", (create<1, 1>)); // 0x40a450: trail 1 cwL (CottageW.smk)
BRC_REGISTER_NODE("bridgeS", (create<2, 2>));  // 0x40a500: trail 2 obS (Bridge.smk)
BRC_REGISTER_NODE("rabbitE", (create<4, 4>));  // 0x40a5b0: trail 4 rrE (RabbitE.smk)
BRC_REGISTER_NODE("rainbowS", (create<3, 3>)); // 0x40a660: trail 3 rfS (Rainbow.smk)
BRC_REGISTER_NODE("wishingL", (create<6, 6>)); // 0x40a710: trail 6 wwE (WishingN.smk)

} // namespace brc::game
