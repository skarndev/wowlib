#pragma once

/** @file
    The Legion+ chunked .m2 shell (namespace wowlib::formats::m2::chunked): M2ChunkedFile —
    the on-disk chunk stream that wraps the MD20 image (MD21) beside the
    satellite chunks (FileDataID references, extended particle data,
    parent-model overrides, inline physics). Chunk ids are NOT reversed on
    disk, unlike every other WoW chunk format.

    MD21 stays a ChunkBlob at this level: its content is the offset-addressed
    MD20 image, which the M2 assembly decodes into its `root` (M2Root) with
    the satellite context (.anim resolution) in hand, then DROPS — the blob
    is transport, not state; a plain chunk-level read still preserves it
    verbatim. Undocumented/unstable payloads stay ChunkBlob too. */

#include <array>
#include <cstdint>
#include <vector>

#include <welder/vocabulary.hpp>

#include <wowlib/core/client_builds.hpp>
#include <wowlib/core/client_version.hpp>
#include <wowlib/core/lang.hpp>
#include <wowlib/formats/common/chunked_file.hpp>
#include <wowlib/formats/m2/boundaries.hpp>
#include <wowlib/formats/m2/chunked/records.hpp>

namespace wowlib::formats::m2::chunked {
  using namespace wowlib::formats::m2::chunked::record;

  /** The version-agnostic base of every M2ChunkedFile<V> (welded as "M2ChunkedFile").

      This empty base exists ENTIRELY for the language bindings: a common
      welded supertype for the per-version M2ChunkedFile* classes (isinstance,
      M2ChunkedFile.for_version(expansion)). No role in the C++ API.

      @see https://wowdev.wiki/M2#Chunks */
  struct [[
      =welder::weld,
      =welder::weld_as("M2ChunkedFile"),
  WOWLIB_CS_FAMILY_SURFACE
      =welder::doc(R"(
        A chunked .m2 shell (Legion+), abstract over the client version.
        Construct a concrete version with M2ChunkedFile.for_version(expansion); the
        per-version M2ChunkedFile* classes are subclasses. See
        https://wowdev.wiki/M2#Chunks.)")
    ]] M2ChunkedFileBase {
    bool operator==(const M2ChunkedFileBase&) const = default;
  };

  /** The chunked .m2 shell for one Legion+ client version. Reading is
      chunk-order independent and journaled, so an untouched shell rewrites
      byte-for-byte (the chunk-framework guarantee applies to the SHELL; the
      MD21 payload inside follows the offset-format semantic guarantee once
      the assembly re-encodes it).
      Instantiate through the canonicalizing m2::M2ChunkedFile alias, never
      directly.
      @tparam V the client version this shell targets.
      @see https://wowdev.wiki/M2#Chunks */
  template <ClientVersion V> requires (V >= M2ChunkedContainer)
  struct [[
      =welder::weld,
      =welder::doc(R"(
        The chunked .m2 shell for one Legion+ client version: the MD21 image
        blob plus the satellite chunks. An untouched shell rewrites
        byte-for-byte. See https://wowdev.wiki/M2#Chunks.)")
    ]] M2ChunkedFile : ChunkedFile<M2ChunkedFile<V>>, M2ChunkedFileBase {
    static constexpr ClientVersion Version = V;
    static constexpr FourCCEndian UnknownFourccEndian = FourCCEndian::Forward;

    [[
      =chunk("MD21", FourCCEndian::Forward),
      =welder::mark::exclude,
      =welder::doc("The MD20 image TRANSPORT blob; offsets inside are "
        "relative to this payload. Hidden from the bindings: a "
        "chunk-level read keeps it verbatim, but the M2 assembly "
        "decodes it into M2.root and drops the bytes — the "
        "decoded body is the source of truth, and the assembly "
        "write re-encodes it here.")]]
    ChunkBlob md21;

    [[
      =chunk("PFID", FourCCEndian::Forward),
      =since(builds::Legion_Alpha),
      =formats::Optional,
      =welder::mark::no_reassign,
      =welder::doc(".phys FileDataID (PFID); 0 or 1 entries.")]]
    std::vector<std::uint32_t> physFdid;

    [[
      =chunk("SFID", FourCCEndian::Forward),
      =since(builds::Legion_Alpha),
      =formats::Optional,
      =welder::mark::no_reassign,
      =welder::doc(".skin FileDataIDs (SFID): num_skin_profiles view entries, "
        "then the LOD-band skins.")]]
    std::vector<std::uint32_t> skinFdids;

    [[
      =chunk("AFID", FourCCEndian::Forward),
      =since(builds::Legion_Alpha),
      =formats::Optional,
      =welder::mark::no_reassign,
      =welder::doc(".anim FileDataIDs (AFID), one per external (animation, "
        "variation) pair.")]]
    std::vector<AnimFileEntry> animFdids;

    [[
      =chunk("BFID", FourCCEndian::Forward),
      =since(builds::Legion_Alpha),
      =formats::Optional,
      =welder::mark::no_reassign,
      =welder::doc(".bone FileDataIDs (BFID), one per FacePose variant.")]]
    std::vector<std::uint32_t> boneFdids;

    [[
      =chunk("TXAC", FourCCEndian::Forward),
      =since(builds::Legion),
      =formats::Optional,
      =welder::mark::no_reassign,
      =welder::doc("Texture transform flags (TXAC), one 2-byte record per "
        "material then per particle emitter.")]]
    std::vector<std::array<std::uint8_t, 2>> textureAc;

    [[
      =chunk("EXPT", FourCCEndian::Forward),
      =since(builds::Legion),
      =formats::Optional,
      =welder::mark::no_reassign,
      =welder::doc("Extended particle parameters (EXPT), one per emitter; "
        "superseded by EXP2 when both exist.")]]
    std::vector<M2ExtendedParticleSimple> extendedParticles;

    [[
      =chunk("EXP2", FourCCEndian::Forward),
      =since(builds::Legion_ShadowsOfArgus_24500),
      =formats::Optional,
      =welder::doc("Extended particle parameters with alpha-cutoff ramps "
        "(EXP2), one per emitter.")]]
    Exp2Data extendedParticles2{};

    [[
      =chunk("PABC", FourCCEndian::Forward),
      =since(builds::Legion_ShadowsOfArgus_24500),
      =formats::Optional,
      =welder::doc("Parent-model sequence blacklist (PABC).")]]
    PabcData parentSequenceBlacklist{};

    [[
      =chunk("PADC", FourCCEndian::Forward),
      =since(builds::Legion_ShadowsOfArgus_24500),
      =formats::Optional,
      =welder::doc("Parent texture weights (PADC); offset-based track "
        "payload kept verbatim.")]]
    ChunkBlob parentTextureWeights;

    [[
      =chunk("PSBC", FourCCEndian::Forward),
      =since(builds::Legion_ShadowsOfArgus_24500),
      =formats::Optional,
      =welder::doc("Parent sequence bounds (PSBC).")]]
    PsbcData parentSequenceBounds{};

    [[
      =chunk("PEDC", FourCCEndian::Forward),
      =since(builds::Legion_ShadowsOfArgus_24500),
      =formats::Optional,
      =welder::doc("Parent event data (PEDC); offset-based track payload "
        "kept verbatim.")]]
    ChunkBlob parentEventData;

    [[
      =chunk("SKID", FourCCEndian::Forward),
      =since(builds::Legion_ShadowsOfArgus_24500),
      =formats::Optional,
      =welder::mark::no_reassign,
      =welder::doc(".skel FileDataID (SKID); 0 or 1 entries.")]]
    std::vector<std::uint32_t> skeletonFdid;

    [[
      =chunk("TXID", FourCCEndian::Forward),
      =since(builds::BfA_Beta),
      =formats::Optional,
      =welder::mark::no_reassign,
      =welder::doc("Texture FileDataIDs (TXID), replacing the in-image "
        "texture filenames, one per texture.")]]
    std::vector<std::uint32_t> textureFdids;

    [[
      =chunk("LDV1", FourCCEndian::Forward),
      =since(builds::BfA_Beta),
      =formats::Optional,
      =welder::doc("LOD data (LDV1); layout partially known, kept verbatim.")]]
    ChunkBlob lodData;

    [[
      =chunk("RPID", FourCCEndian::Forward),
      =since(builds::BfA_TidesOfVengeance),
      =formats::Optional,
      =welder::mark::no_reassign,
      =welder::doc("Recursive (child-emitter) particle model FileDataIDs "
        "(RPID), one per emitter.")]]
    std::vector<std::uint32_t> recursiveParticleModelFdids;

    [[
      =chunk("GPID", FourCCEndian::Forward),
      =since(builds::BfA_TidesOfVengeance),
      =formats::Optional,
      =welder::mark::no_reassign,
      =welder::doc("Geometry particle model FileDataIDs (GPID), one per "
        "emitter.")]]
    std::vector<std::uint32_t> geometryParticleModelFdids;

    [[
      =chunk("WFV1", FourCCEndian::Forward),
      =since(builds::BfA_RiseOfAzshara),
      =formats::Optional,
      =welder::doc("Waterfall render path v1 (WFV1); undocumented, kept "
        "verbatim.")]]
    ChunkBlob waterfallV1;

    [[
      =chunk("WFV2", FourCCEndian::Forward),
      =since(builds::BfA_RiseOfAzshara),
      =formats::Optional,
      =welder::doc("Waterfall render path v2 (WFV2); undocumented, kept "
        "verbatim.")]]
    ChunkBlob waterfallV2;

    [[
      =chunk("PGD1", FourCCEndian::Forward),
      =since(builds::BfA_RiseOfAzshara),
      =formats::Optional,
      =welder::doc("Particle geoset data (PGD1), one geoset per emitter "
        "(wowdev dates it to the 1.13.2.30172 classic build — "
        "the 8.2.0 retail era).")]]
    Pgd1Data particleGeosetData{};

    [[
      =chunk("WFV3", FourCCEndian::Forward),
      =since(builds::SL_Alpha_33978),
      =formats::Optional,
      =welder::doc("Waterfall render path v3 (WFV3); shader parameter block, "
        "kept verbatim.")]]
    ChunkBlob waterfallV3;

    [[
      =chunk("PFDC", FourCCEndian::Forward),
      =since(builds::BfA_VisionsOfNzoth_35662),
      =formats::Optional,
      =welder::doc("Inline physics (PFDC): a whole .phys image plus "
        "alignment padding, kept verbatim (structured PHYS is a "
        "follow-up milestone). wowdev dates it to the 9.0.1 alpha, "
        "but 8.3.7 shipped mid-alpha with the backport — 31 item "
        "M2s in the fleet client carry it.")]]
    ChunkBlob inlinePhys;

    [[
      =chunk("EDGF", FourCCEndian::Forward),
      =since(builds::SL_Alpha_33978),
      =formats::Optional,
      =welder::doc("Edge fade data (EDGF), applied to batches with flags2 "
        "0x8; kept verbatim.")]]
    ChunkBlob edgeFade;

    [[
      =chunk("NERF", FourCCEndian::Forward),
      =since(builds::SL_Alpha_33978),
      =formats::Optional,
      =welder::doc("Distance-based model alpha attenuation coefficients "
        "(NERF); kept verbatim.")]]
    ChunkBlob alphaAttenuation;

    [[
      =chunk("DETL", FourCCEndian::Forward),
      =since(builds::SL_Alpha_34365),
      =formats::Optional,
      =welder::doc("Per-light detail overrides (DETL), kept verbatim: real "
        "9.x files carry 16-byte records where wowdev documents "
        "12 (see M2LightDetail for the known half).")]]
    ChunkBlob lightDetails;

    [[
      =chunk("DBOC", FourCCEndian::Forward),
      =since(builds::SL_Alpha_33978),
      =formats::Optional,
      =welder::doc("DBOC; undocumented (16 or 32 bytes seen), kept "
        "verbatim.")]]
    ChunkBlob dboc;

    [[
      =chunk("AFRA", FourCCEndian::Forward),
      =since(builds::DF),
      =formats::Optional,
      =welder::doc("AFRA (Dragonflight+); not yet observed, kept verbatim.")]]
    ChunkBlob afra;

    [[
      =chunk("PCOL", FourCCEndian::Forward),
      =since(builds::TWW_LegacyOfArathor),
      =formats::Optional,
      =welder::doc("Player-housing collision mesh (PCOL); offset-based "
        "layout, kept verbatim.")]]
    ChunkBlob housingCollision;

    [[
      =chunk("DPIV", FourCCEndian::Forward),
      =since(builds::TWW_LegacyOfArathor),
      =formats::Optional,
      =welder::doc("DPIV; undocumented, kept verbatim.")]]
    ChunkBlob dpiv;

    bool operator==(const M2ChunkedFile&) const = default;
  };
}

namespace wowlib::formats::m2 {
  /** The chunked .m2 stream — the canonicalizing face of chunked::M2ChunkedFile:
      every Legion+ version maps to its range's first grid version
      (M2FilePivots — the active chunk set is constant within a range).
      Pre-Legion versions stay a substitution failure, so the facade's era
      subsetting is unchanged. */
  template <ClientVersion V> requires (V >= M2ChunkedContainer)
  using M2ChunkedFile = chunked::M2ChunkedFile<canonicalVersion(V, M2FilePivots, M2ChunkedVersions)>;
}
