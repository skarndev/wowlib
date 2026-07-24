#pragma once

/** @file
    The Legion+ chunked .m2 shell (namespace wowlib::formats::m2): M2File —
    the on-disk chunk stream that wraps the MD20 image (MD21) beside the
    satellite chunks (FileDataID references, extended particle data,
    parent-model overrides, inline physics). Chunk ids are NOT reversed on
    disk, unlike every other WoW chunk format.

    MD21 stays a ChunkBlob at this level: its content is the offset-addressed
    MD20 image, which the M2 assembly decodes into M2Data with the satellite
    context (.anim resolution) in hand — a plain shell read preserves it
    verbatim. Undocumented/unstable payloads stay ChunkBlob too. */

#include <array>
#include <cstdint>
#include <vector>

#include <welder/vocabulary.hpp>

#include <wowlib/core/client_version.hpp>
#include <wowlib/formats/common/chunk.hpp>
#include <wowlib/formats/common/serializer.hpp>
#include <wowlib/formats/m2/boundaries.hpp>
#include <wowlib/formats/m2/records/companion.hpp>

namespace wowlib::formats::m2
{
  using namespace wowlib::formats::m2::records;

  /** The version-agnostic base of every M2File<V> (welded as "M2File").

      This empty base exists ENTIRELY for the language bindings: a common
      welded supertype for the per-version M2File* classes (isinstance,
      M2File.for_version(expansion)). No role in the C++ API.

      @see https://wowdev.wiki/M2#Chunks */
  struct [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::weld_as("M2File"),
    =welder::doc(R"(
        A chunked .m2 shell (Legion+), abstract over the client version.
        Construct a concrete version with M2File.for_version(expansion); the
        per-version M2File* classes are subclasses. See
        https://wowdev.wiki/M2#Chunks.)")
  ]] M2FileBase
  {
    bool operator==(const M2FileBase&) const = default;
  };

  /** The chunked .m2 shell for one Legion+ client version. Reading is
      chunk-order independent and journaled, so an untouched shell rewrites
      byte-for-byte (the chunk-framework guarantee applies to the SHELL; the
      MD21 payload inside follows the offset-format semantic guarantee once
      the assembly re-encodes it).
      @tparam V the client version this shell targets.
      @see https://wowdev.wiki/M2#Chunks */
  template <ClientVersion V>
    requires (V >= m2_chunked_container)
  struct [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::doc(R"(
        The chunked .m2 shell for one Legion+ client version: the MD21 image
        blob plus the satellite chunks. An untouched shell rewrites
        byte-for-byte. See https://wowdev.wiki/M2#Chunks.)")
  ]] M2File : ChunkedFile<M2File<V>>, M2FileBase
  {
    static constexpr ClientVersion version = V;

    [[
      =chunk("MD21", FourCCEndian::forward),
      =welder::doc("The MD20 image, verbatim; offsets inside are relative to "
                   "this payload. The M2 assembly decodes it into M2Data.")]]
    ChunkBlob md21;

    [[
      =chunk("PFID", FourCCEndian::forward),
      =since(ClientVersion{7, 0, 1, 20740}),
      =formats::optional,
      =welder::mark::no_reassign,
      =welder::doc(".phys FileDataID (PFID); 0 or 1 entries.")]]
    std::vector<std::uint32_t> phys_fdid;

    [[
      =chunk("SFID", FourCCEndian::forward),
      =since(ClientVersion{7, 0, 1, 20740}),
      =formats::optional,
      =welder::mark::no_reassign,
      =welder::doc(".skin FileDataIDs (SFID): num_skin_profiles view entries, "
                   "then the LOD-band skins.")]]
    std::vector<std::uint32_t> skin_fdids;

    [[
      =chunk("AFID", FourCCEndian::forward),
      =since(ClientVersion{7, 0, 1, 20740}),
      =formats::optional,
      =welder::mark::no_reassign,
      =welder::doc(".anim FileDataIDs (AFID), one per external (animation, "
                   "variation) pair.")]]
    std::vector<AnimFileEntry> anim_fdids;

    [[
      =chunk("BFID", FourCCEndian::forward),
      =since(ClientVersion{7, 0, 1, 20740}),
      =formats::optional,
      =welder::mark::no_reassign,
      =welder::doc(".bone FileDataIDs (BFID), one per FacePose variant.")]]
    std::vector<std::uint32_t> bone_fdids;

    [[
      =chunk("TXAC", FourCCEndian::forward),
      =since(ClientVersion{7, 0, 0, 0}),
      =formats::optional,
      =welder::mark::no_reassign,
      =welder::doc("Texture transform flags (TXAC), one 2-byte record per "
                   "material then per particle emitter.")]]
    std::vector<std::array<std::uint8_t, 2>> texture_ac;

    [[
      =chunk("EXPT", FourCCEndian::forward),
      =since(ClientVersion{7, 0, 0, 0}),
      =formats::optional,
      =welder::mark::no_reassign,
      =welder::doc("Extended particle parameters (EXPT), one per emitter; "
                   "superseded by EXP2 when both exist.")]]
    std::vector<M2ExtendedParticleSimple> extended_particles;

    [[
      =chunk("EXP2", FourCCEndian::forward),
      =since(ClientVersion{7, 3, 0, 24500}),
      =formats::optional,
      =welder::doc("Extended particle parameters with alpha-cutoff ramps "
                   "(EXP2), one per emitter.")]]
    Exp2Data<V> extended_particles2{};

    [[
      =chunk("PABC", FourCCEndian::forward),
      =since(ClientVersion{7, 3, 0, 24500}),
      =formats::optional,
      =welder::doc("Parent-model sequence blacklist (PABC).")]]
    PabcData<V> parent_sequence_blacklist{};

    [[
      =chunk("PADC", FourCCEndian::forward),
      =since(ClientVersion{7, 3, 0, 24500}),
      =formats::optional,
      =welder::doc("Parent texture weights (PADC); offset-based track "
                   "payload kept verbatim.")]]
    ChunkBlob parent_texture_weights;

    [[
      =chunk("PSBC", FourCCEndian::forward),
      =since(ClientVersion{7, 3, 0, 24500}),
      =formats::optional,
      =welder::doc("Parent sequence bounds (PSBC).")]]
    PsbcData<V> parent_sequence_bounds{};

    [[
      =chunk("PEDC", FourCCEndian::forward),
      =since(ClientVersion{7, 3, 0, 24500}),
      =formats::optional,
      =welder::doc("Parent event data (PEDC); offset-based track payload "
                   "kept verbatim.")]]
    ChunkBlob parent_event_data;

    [[
      =chunk("SKID", FourCCEndian::forward),
      =since(ClientVersion{7, 3, 0, 24500}),
      =formats::optional,
      =welder::mark::no_reassign,
      =welder::doc(".skel FileDataID (SKID); 0 or 1 entries.")]]
    std::vector<std::uint32_t> skeleton_fdid;

    [[
      =chunk("TXID", FourCCEndian::forward),
      =since(ClientVersion{8, 0, 1, 26629}),
      =formats::optional,
      =welder::mark::no_reassign,
      =welder::doc("Texture FileDataIDs (TXID), replacing the in-image "
                   "texture filenames, one per texture.")]]
    std::vector<std::uint32_t> texture_fdids;

    [[
      =chunk("LDV1", FourCCEndian::forward),
      =since(ClientVersion{8, 0, 1, 26629}),
      =formats::optional,
      =welder::doc("LOD data (LDV1); layout partially known, kept verbatim.")]]
    ChunkBlob lod_data;

    [[
      =chunk("RPID", FourCCEndian::forward),
      =since(ClientVersion{8, 1, 0, 27826}),
      =formats::optional,
      =welder::mark::no_reassign,
      =welder::doc("Recursive (child-emitter) particle model FileDataIDs "
                   "(RPID), one per emitter.")]]
    std::vector<std::uint32_t> recursive_particle_model_fdids;

    [[
      =chunk("GPID", FourCCEndian::forward),
      =since(ClientVersion{8, 1, 0, 27826}),
      =formats::optional,
      =welder::mark::no_reassign,
      =welder::doc("Geometry particle model FileDataIDs (GPID), one per "
                   "emitter.")]]
    std::vector<std::uint32_t> geometry_particle_model_fdids;

    [[
      =chunk("WFV1", FourCCEndian::forward),
      =since(ClientVersion{8, 2, 0, 30080}),
      =formats::optional,
      =welder::doc("Waterfall render path v1 (WFV1); undocumented, kept "
                   "verbatim.")]]
    ChunkBlob waterfall_v1;

    [[
      =chunk("WFV2", FourCCEndian::forward),
      =since(ClientVersion{8, 2, 0, 30080}),
      =formats::optional,
      =welder::doc("Waterfall render path v2 (WFV2); undocumented, kept "
                   "verbatim.")]]
    ChunkBlob waterfall_v2;

    [[
      =chunk("PGD1", FourCCEndian::forward),
      =since(ClientVersion{8, 2, 0, 30080}),
      =formats::optional,
      =welder::doc("Particle geoset data (PGD1), one geoset per emitter "
                   "(wowdev dates it to the 1.13.2.30172 classic build — "
                   "the 8.2.0 retail era).")]]
    Pgd1Data<V> particle_geoset_data{};

    [[
      =chunk("WFV3", FourCCEndian::forward),
      =since(ClientVersion{9, 0, 1, 33978}),
      =formats::optional,
      =welder::doc("Waterfall render path v3 (WFV3); shader parameter block, "
                   "kept verbatim.")]]
    ChunkBlob waterfall_v3;

    [[
      =chunk("PFDC", FourCCEndian::forward),
      =since(ClientVersion{9, 0, 1, 33978}),
      =formats::optional,
      =welder::doc("Inline physics (PFDC): a whole .phys image plus "
                   "alignment padding, kept verbatim (structured PHYS is a "
                   "follow-up milestone).")]]
    ChunkBlob inline_phys;

    [[
      =chunk("EDGF", FourCCEndian::forward),
      =since(ClientVersion{9, 0, 1, 33978}),
      =formats::optional,
      =welder::doc("Edge fade data (EDGF), applied to batches with flags2 "
                   "0x8; kept verbatim.")]]
    ChunkBlob edge_fade;

    [[
      =chunk("NERF", FourCCEndian::forward),
      =since(ClientVersion{9, 0, 1, 33978}),
      =formats::optional,
      =welder::doc("Distance-based model alpha attenuation coefficients "
                   "(NERF); kept verbatim.")]]
    ChunkBlob alpha_attenuation;

    [[
      =chunk("DETL", FourCCEndian::forward),
      =since(ClientVersion{9, 0, 1, 34365}),
      =formats::optional,
      =welder::mark::no_reassign,
      =welder::doc("Per-light detail overrides (DETL), one per light.")]]
    std::vector<M2LightDetail> light_details;

    [[
      =chunk("DBOC", FourCCEndian::forward),
      =since(ClientVersion{9, 0, 1, 33978}),
      =formats::optional,
      =welder::doc("DBOC; undocumented (16 or 32 bytes seen), kept "
                   "verbatim.")]]
    ChunkBlob dboc;

    [[
      =chunk("AFRA", FourCCEndian::forward),
      =since(ClientVersion{10, 0, 0, 0}),
      =formats::optional,
      =welder::doc("AFRA (Dragonflight+); not yet observed, kept verbatim.")]]
    ChunkBlob afra;

    [[
      =chunk("PCOL", FourCCEndian::forward),
      =since(ClientVersion{11, 1, 7, 60520}),
      =formats::optional,
      =welder::doc("Player-housing collision mesh (PCOL); offset-based "
                   "layout, kept verbatim.")]]
    ChunkBlob housing_collision;

    [[
      =chunk("DPIV", FourCCEndian::forward),
      =since(ClientVersion{11, 1, 7, 60520}),
      =formats::optional,
      =welder::doc("DPIV; undocumented, kept verbatim.")]]
    ChunkBlob dpiv;

    bool operator==(const M2File&) const = default;
  };
}
