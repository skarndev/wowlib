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
#include <wowlib/formats/common/chunked_file.hpp>
#include <wowlib/formats/m2/boundaries.hpp>
#include <wowlib/formats/m2/chunked/records.hpp>

namespace wowlib::formats::m2::chunked
{
  using namespace wowlib::formats::m2::chunked::record;

  /** The version-agnostic base of every M2ChunkedFile<V> (welded as "M2ChunkedFile").

      This empty base exists ENTIRELY for the language bindings: a common
      welded supertype for the per-version M2ChunkedFile* classes (isinstance,
      M2ChunkedFile.for_version(expansion)). No role in the C++ API.

      @see https://wowdev.wiki/M2#Chunks */
  struct [[
    =welder::weld,
    =welder::weld_as("M2ChunkedFile"),
    =welder::doc(R"(
        A chunked .m2 shell (Legion+), abstract over the client version.
        Construct a concrete version with M2ChunkedFile.for_version(expansion); the
        per-version M2ChunkedFile* classes are subclasses. See
        https://wowdev.wiki/M2#Chunks.)")
  ]] M2ChunkedFileBase
  {
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
  template <ClientVersion V>
    requires (V >= m2_chunked_container)
  struct [[
    =welder::weld,
    =welder::doc(R"(
        The chunked .m2 shell for one Legion+ client version: the MD21 image
        blob plus the satellite chunks. An untouched shell rewrites
        byte-for-byte. See https://wowdev.wiki/M2#Chunks.)")
  ]] M2ChunkedFile : ChunkedFile<M2ChunkedFile<V>>, M2ChunkedFileBase
  {
    static constexpr ClientVersion version = V;
    static constexpr FourCCEndian unknown_fourcc_endian = FourCCEndian::forward;

    [[
      =chunk("MD21", FourCCEndian::forward),
      =welder::mark::exclude,
      =welder::doc("The MD20 image TRANSPORT blob; offsets inside are "
                   "relative to this payload. Hidden from the bindings: a "
                   "chunk-level read keeps it verbatim, but the M2 assembly "
                   "decodes it into M2.root and drops the bytes — the "
                   "decoded body is the source of truth, and the assembly "
                   "write re-encodes it here.")]]
    ChunkBlob md21;

    [[
      =chunk("PFID", FourCCEndian::forward),
      =since(builds::Legion_Alpha),
      =formats::optional,
      =welder::mark::no_reassign,
      =welder::doc(".phys FileDataID (PFID); 0 or 1 entries.")]]
    std::vector<std::uint32_t> phys_fdid;

    [[
      =chunk("SFID", FourCCEndian::forward),
      =since(builds::Legion_Alpha),
      =formats::optional,
      =welder::mark::no_reassign,
      =welder::doc(".skin FileDataIDs (SFID): num_skin_profiles view entries, "
                   "then the LOD-band skins.")]]
    std::vector<std::uint32_t> skin_fdids;

    [[
      =chunk("AFID", FourCCEndian::forward),
      =since(builds::Legion_Alpha),
      =formats::optional,
      =welder::mark::no_reassign,
      =welder::doc(".anim FileDataIDs (AFID), one per external (animation, "
                   "variation) pair.")]]
    std::vector<AnimFileEntry> anim_fdids;

    [[
      =chunk("BFID", FourCCEndian::forward),
      =since(builds::Legion_Alpha),
      =formats::optional,
      =welder::mark::no_reassign,
      =welder::doc(".bone FileDataIDs (BFID), one per FacePose variant.")]]
    std::vector<std::uint32_t> bone_fdids;

    [[
      =chunk("TXAC", FourCCEndian::forward),
      =since(builds::Legion),
      =formats::optional,
      =welder::mark::no_reassign,
      =welder::doc("Texture transform flags (TXAC), one 2-byte record per "
                   "material then per particle emitter."),
      // Nested container (a sequence of fixed arrays): no C# wire form yet.
      =welder::mark::exclude(welder::lang::cs)]]
    std::vector<std::array<std::uint8_t, 2>> texture_ac;

    [[
      =chunk("EXPT", FourCCEndian::forward),
      =since(builds::Legion),
      =formats::optional,
      =welder::mark::no_reassign,
      =welder::doc("Extended particle parameters (EXPT), one per emitter; "
                   "superseded by EXP2 when both exist.")]]
    std::vector<M2ExtendedParticleSimple> extended_particles;

    [[
      =chunk("EXP2", FourCCEndian::forward),
      =since(builds::Legion_ShadowsOfArgus_24500),
      =formats::optional,
      =welder::doc("Extended particle parameters with alpha-cutoff ramps "
                   "(EXP2), one per emitter.")]]
    Exp2Data extended_particles2{};

    [[
      =chunk("PABC", FourCCEndian::forward),
      =since(builds::Legion_ShadowsOfArgus_24500),
      =formats::optional,
      =welder::doc("Parent-model sequence blacklist (PABC).")]]
    PabcData parent_sequence_blacklist{};

    [[
      =chunk("PADC", FourCCEndian::forward),
      =since(builds::Legion_ShadowsOfArgus_24500),
      =formats::optional,
      =welder::doc("Parent texture weights (PADC); offset-based track "
                   "payload kept verbatim.")]]
    ChunkBlob parent_texture_weights;

    [[
      =chunk("PSBC", FourCCEndian::forward),
      =since(builds::Legion_ShadowsOfArgus_24500),
      =formats::optional,
      =welder::doc("Parent sequence bounds (PSBC).")]]
    PsbcData parent_sequence_bounds{};

    [[
      =chunk("PEDC", FourCCEndian::forward),
      =since(builds::Legion_ShadowsOfArgus_24500),
      =formats::optional,
      =welder::doc("Parent event data (PEDC); offset-based track payload "
                   "kept verbatim.")]]
    ChunkBlob parent_event_data;

    [[
      =chunk("SKID", FourCCEndian::forward),
      =since(builds::Legion_ShadowsOfArgus_24500),
      =formats::optional,
      =welder::mark::no_reassign,
      =welder::doc(".skel FileDataID (SKID); 0 or 1 entries.")]]
    std::vector<std::uint32_t> skeleton_fdid;

    [[
      =chunk("TXID", FourCCEndian::forward),
      =since(builds::BfA_Beta),
      =formats::optional,
      =welder::mark::no_reassign,
      =welder::doc("Texture FileDataIDs (TXID), replacing the in-image "
                   "texture filenames, one per texture.")]]
    std::vector<std::uint32_t> texture_fdids;

    [[
      =chunk("LDV1", FourCCEndian::forward),
      =since(builds::BfA_Beta),
      =formats::optional,
      =welder::doc("LOD data (LDV1); layout partially known, kept verbatim.")]]
    ChunkBlob lod_data;

    [[
      =chunk("RPID", FourCCEndian::forward),
      =since(builds::BfA_TidesOfVengeance),
      =formats::optional,
      =welder::mark::no_reassign,
      =welder::doc("Recursive (child-emitter) particle model FileDataIDs "
                   "(RPID), one per emitter.")]]
    std::vector<std::uint32_t> recursive_particle_model_fdids;

    [[
      =chunk("GPID", FourCCEndian::forward),
      =since(builds::BfA_TidesOfVengeance),
      =formats::optional,
      =welder::mark::no_reassign,
      =welder::doc("Geometry particle model FileDataIDs (GPID), one per "
                   "emitter.")]]
    std::vector<std::uint32_t> geometry_particle_model_fdids;

    [[
      =chunk("WFV1", FourCCEndian::forward),
      =since(builds::BfA_RiseOfAzshara),
      =formats::optional,
      =welder::doc("Waterfall render path v1 (WFV1); undocumented, kept "
                   "verbatim.")]]
    ChunkBlob waterfall_v1;

    [[
      =chunk("WFV2", FourCCEndian::forward),
      =since(builds::BfA_RiseOfAzshara),
      =formats::optional,
      =welder::doc("Waterfall render path v2 (WFV2); undocumented, kept "
                   "verbatim.")]]
    ChunkBlob waterfall_v2;

    [[
      =chunk("PGD1", FourCCEndian::forward),
      =since(builds::BfA_RiseOfAzshara),
      =formats::optional,
      =welder::doc("Particle geoset data (PGD1), one geoset per emitter "
                   "(wowdev dates it to the 1.13.2.30172 classic build — "
                   "the 8.2.0 retail era).")]]
    Pgd1Data particle_geoset_data{};

    [[
      =chunk("WFV3", FourCCEndian::forward),
      =since(builds::SL_Alpha_33978),
      =formats::optional,
      =welder::doc("Waterfall render path v3 (WFV3); shader parameter block, "
                   "kept verbatim.")]]
    ChunkBlob waterfall_v3;

    [[
      =chunk("PFDC", FourCCEndian::forward),
      =since(builds::BfA_VisionsOfNzoth_35662),
      =formats::optional,
      =welder::doc("Inline physics (PFDC): a whole .phys image plus "
                   "alignment padding, kept verbatim (structured PHYS is a "
                   "follow-up milestone). wowdev dates it to the 9.0.1 alpha, "
                   "but 8.3.7 shipped mid-alpha with the backport — 31 item "
                   "M2s in the fleet client carry it.")]]
    ChunkBlob inline_phys;

    [[
      =chunk("EDGF", FourCCEndian::forward),
      =since(builds::SL_Alpha_33978),
      =formats::optional,
      =welder::doc("Edge fade data (EDGF), applied to batches with flags2 "
                   "0x8; kept verbatim.")]]
    ChunkBlob edge_fade;

    [[
      =chunk("NERF", FourCCEndian::forward),
      =since(builds::SL_Alpha_33978),
      =formats::optional,
      =welder::doc("Distance-based model alpha attenuation coefficients "
                   "(NERF); kept verbatim.")]]
    ChunkBlob alpha_attenuation;

    [[
      =chunk("DETL", FourCCEndian::forward),
      =since(builds::SL_Alpha_34365),
      =formats::optional,
      =welder::doc("Per-light detail overrides (DETL), kept verbatim: real "
                   "9.x files carry 16-byte records where wowdev documents "
                   "12 (see M2LightDetail for the known half).")]]
    ChunkBlob light_details;

    [[
      =chunk("DBOC", FourCCEndian::forward),
      =since(builds::SL_Alpha_33978),
      =formats::optional,
      =welder::doc("DBOC; undocumented (16 or 32 bytes seen), kept "
                   "verbatim.")]]
    ChunkBlob dboc;

    [[
      =chunk("AFRA", FourCCEndian::forward),
      =since(builds::DF),
      =formats::optional,
      =welder::doc("AFRA (Dragonflight+); not yet observed, kept verbatim.")]]
    ChunkBlob afra;

    [[
      =chunk("PCOL", FourCCEndian::forward),
      =since(builds::TWW_LegacyOfArathor),
      =formats::optional,
      =welder::doc("Player-housing collision mesh (PCOL); offset-based "
                   "layout, kept verbatim.")]]
    ChunkBlob housing_collision;

    [[
      =chunk("DPIV", FourCCEndian::forward),
      =since(builds::TWW_LegacyOfArathor),
      =formats::optional,
      =welder::doc("DPIV; undocumented, kept verbatim.")]]
    ChunkBlob dpiv;

    bool operator==(const M2ChunkedFile&) const = default;
  };
}

namespace wowlib::formats::m2
{
  /** The chunked .m2 stream — the canonicalizing face of chunked::M2ChunkedFile:
      every Legion+ version maps to its range's first grid version
      (m2_file_pivots — the active chunk set is constant within a range).
      Pre-Legion versions stay a substitution failure, so the facade's era
      subsetting is unchanged. */
  template <ClientVersion V>
    requires (V >= m2_chunked_container)
  using M2ChunkedFile =
    chunked::M2ChunkedFile<canonical_version(V, m2_file_pivots, m2_chunked_versions)>;
}
