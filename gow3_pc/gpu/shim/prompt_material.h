// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>

namespace Gow3Prompts {
enum class Family : uint8_t { None, Face, Shoulder, Dpad };

// These material inputs are shared by multiple controls. Their original GPU image
// stays usable for controllers and for draws with no corresponding keyboard colour.
constexpr Family SharedMaskFamily(uint64_t hash, bool bc4) {
    switch (hash) {
    case 0x16e0facc77adc53cULL: return !bc4 ? Family::Face : Family::None; // button-large-trans.tga
    case 0x1d1b6fa2a3cc3d97ULL: return !bc4 ? Family::Dpad : Family::None; // up-button-look-trans.tga
    case 0x242b9eb131bfa049ULL: return !bc4 ? Family::Face : Family::None; // circle-button-down-trans.tga
    case 0x31e0b53a6c874ec5ULL: return !bc4 ? Family::Dpad : Family::None; // up-button-up-trans.tga
    case 0x44950230b9feb4b7ULL: return !bc4 ? Family::Face : Family::None; // button-trans.tga
    case 0x500708f95eb962edULL: return !bc4 ? Family::Face : Family::None; // button-up-trans-round.tga
    case 0x7dea0d660fbc266cULL: return !bc4 ? Family::Shoulder : Family::None; // button-trans-shoulder2-big.tga
    case 0x864de1edbbf47780ULL: return !bc4 ? Family::Shoulder : Family::None; // button-trans-shoulder2-small.tga
    case 0x8c92da1b4623177eULL: return !bc4 ? Family::Face : Family::None; // circle-button-up-trans.tga
    case 0xb1da60283b3f7b84ULL: return !bc4 ? Family::Shoulder : Family::None; // button-trans-shoulder1.tga
    case 0xbb769d6fdea61888ULL: return !bc4 ? Family::Face : Family::None; // button-small-trans.tga
    case 0xf63e4fb97d71d9edULL: return !bc4 ? Family::Dpad : Family::None; // up-button-down-trans.tga
    case 0xff0ed08fea5d8070ULL: return !bc4 ? Family::Face : Family::None; // button-down-trans-round.tga
    case 0x05a85abca1cae355ULL: return bc4 ? Family::Dpad : Family::None; // up-button-up-trans.tga
    case 0x1809d04f1d7262a3ULL: return bc4 ? Family::Face : Family::None; // button-up-trans-round.tga
    case 0x1cce1cc389615a3dULL: return bc4 ? Family::Shoulder : Family::None; // button-trans-shoulder2-big.tga
    case 0x27082ee3cd7b36adULL: return bc4 ? Family::Shoulder : Family::None; // button-trans-shoulder2-small.tga
    case 0x30bb625badc7162bULL: return bc4 ? Family::Dpad : Family::None; // bp-up-button-down-trans.tga
    case 0x5c23367eaec6dd12ULL: return bc4 ? Family::Face : Family::None; // button-small-trans.tga
    case 0x79b752555405c029ULL: return bc4 ? Family::Face : Family::None; // button-trans.tga
    case 0x9c0a7fadaca3f33fULL: return bc4 ? Family::Face : Family::None; // button-large-trans.tga
    case 0xa347b65a56efd6d0ULL: return bc4 ? Family::Dpad : Family::None; // up-button-look-trans.tga
    case 0xb18d085752718de0ULL: return bc4 ? Family::Dpad : Family::None; // up-button-down-trans.tga
    case 0xbefba1a62d87f5c6ULL: return bc4 ? Family::Dpad : Family::None; // bp-up-button-up-trans.tga
    case 0xd31774c09df91667ULL: return bc4 ? Family::Face : Family::None; // button-down-trans-round.tga
    case 0xd9e24e61cfbb0748ULL: return bc4 ? Family::Shoulder : Family::None; // button-trans-shoulder2.tga
    case 0xebf34b3f7995b974ULL: return bc4 ? Family::Shoulder : Family::None; // button-trans-shoulder1.tga
    default: return Family::None;
    }
}
} // namespace Gow3Prompts
