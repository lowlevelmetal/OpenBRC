#pragma once

// TrailNode fields used only by the certificates C1..C4 and the riding lesson (TrailCertificates.cpp). Field
// comments give the original node offsets.

namespace brc::engine {
class Anim;
}

namespace brc::game {

struct CertificateState {
    engine::Anim* flower = nullptr; // +0x798: C1 roses, tulips or daisies at this stop
    bool daisyHint = false;         // +0x79C: db0203 said on rainbowS
    bool tulipHint = false;         // +0x79E: db0203 said on bridgeS
    bool roseHint = false;          // +0x7A0: db0203 said on appleW
    bool natureAwarded = false;     // +0x7A2
    engine::Anim* animal = nullptr; // +0x7A4: C2 owl, hawk or bluebird (click 0x2724)
    bool owlHint = false;           // +0x7AC
    bool hawkHint = false;          // +0x7AE
    bool bluebirdHint = false;      // +0x7B0
    // (+0x7B4: a hotspot C2 deletes at the stop's end; nothing ever sets it.)
    engine::Anim* certificate3 = nullptr; // +0x7B8: `Certificate3` (click 0x2725)
    bool rainbowHint = false;             // +0x7BC: db04084 said
};

} // namespace brc::game
