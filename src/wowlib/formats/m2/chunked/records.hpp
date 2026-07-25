#pragma once

/** @file
    Legion+ companion-chunk records (namespace wowlib::formats::m2::chunked::record):
    the payload types of the chunked .m2 shell's satellite chunks (AFID
    entries, extended-particle blocks, parent-model overrides). The FourCCs of
    these chunks are NOT reversed on disk, unlike every other WoW format. */

#include <array>
#include <cstdint>
#include <vector>

#include <welder/vocabulary.hpp>

#include <wowlib/core/client_version.hpp>
#include <wowlib/formats/common/offset_file.hpp>
#include <wowlib/formats/m2/root/record/track.hpp>

namespace wowlib::formats::m2::chunked::record
{
  /** One AFID entry: which FileDataID serves a sequence's .anim data. */
  struct [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::doc("An AFID entry: the .anim FileDataID for one (animation, "
                 "variation) pair; 0 means none.")
  ]] AnimFileEntry
  {
    [[=welder::doc("AnimationData.dbc id of the sequence.")]]
    std::uint16_t anim_id = 0;
    [[=welder::doc("The sequence variation index.")]]
    std::uint16_t sub_anim_id = 0;
    [[=welder::doc("FileDataID of the .anim file; 0 means none.")]]
    std::uint32_t file_id = 0;

    bool operator==(const AnimFileEntry&) const = default;
  };
  static_assert(sizeof(AnimFileEntry) == 8);

  /** One EXPT record: the pre-EXP2 extended particle parameters (the client
      reconstructs EXP2 from these when EXP2 is absent). */
  struct [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::doc("An EXPT record: z-source, color and alpha multipliers for "
                 "one particle emitter.")
  ]] M2ExtendedParticleSimple
  {
    [[=welder::doc("If > 0, initial velocity points from (0, 0, z_source) to "
                   "the spawn point.")]]
    float z_source = 0;
    [[=welder::doc("Multiplied into the particle's diffuse color.")]]
    float color_mult = 1;
    [[=welder::doc("Multiplied into the particle's opacity.")]]
    float alpha_mult = 1;

    bool operator==(const M2ExtendedParticleSimple&) const = default;
  };
  static_assert(sizeof(M2ExtendedParticleSimple) == 12);

  struct [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::doc("An EXP2 record: the EXPT parameters plus the per-lifetime "
                 "alpha-cutoff ramp.")
  ]] M2ExtendedParticle
  {
    [[=welder::doc("If > 0, initial velocity points from (0, 0, z_source) to "
                   "the spawn point.")]]
    float z_source = 0;
    [[=welder::doc("Multiplied into the particle's diffuse color.")]]
    float color_mult = 1;
    [[=welder::doc("Multiplied into the particle's opacity.")]]
    float alpha_mult = 1;
    [[=welder::doc("Alpha-test threshold sampled by the particle's lifetime.")]]
    root::record::M2PartTrack<fixed16> alpha_cutoff{};

    bool operator==(const M2ExtendedParticle&) const = default;
  };

  /** One DETL record: per-light shadow/diffuse overrides (9.0+); the halves
      are raw IEEE half-floats. */
  struct [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::doc("A DETL record (9.0+): per-light flags, shadow-RT scale and "
                 "diffuse multiplier (raw half-floats).")
  ]] M2LightDetail
  {
    [[=welder::doc("Unknown per-light flags.")]]
    std::uint16_t flags = 0;
    [[=welder::doc("Scale for the shadow RT matrix (raw half-float).")]]
    std::uint16_t scale_half = 0;
    [[=welder::doc("Multiplier for the light's diffuse color (raw "
                   "half-float).")]]
    std::uint16_t diffuse_color_mult_half = 0;
    [[=welder::doc("Unknown.")]]
    std::uint16_t unknown0 = 0;
    [[=welder::doc("Unknown.")]]
    std::uint32_t unknown1 = 0;

    bool operator==(const M2LightDetail&) const = default;
  };
  static_assert(sizeof(M2LightDetail) == 12);

  template <ClientVersion V>
  struct [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::doc("The EXP2 chunk payload: extended particle records, one per "
                 "particle emitter.")
  ]] Exp2Data : OffsetFile<Exp2Data<V>>
  {
    static constexpr ClientVersion version = V;

    [[=welder::mark::no_reassign,
      =welder::doc("Extended particle parameters, one per particle emitter.")]]
    std::vector<M2ExtendedParticle> content;

    /** Chunk engagement: shadow OffsetFile's always-false empty() so the
        optional chunk is emitted only when data is present. */
    [[=welder::mark::exclude]]
    bool empty() const { return content.empty(); }

    bool operator==(const Exp2Data&) const = default;
  };

  template <ClientVersion V>
  struct [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::doc("The PABC chunk payload: the parent-model sequence-id "
                 "blacklist ('BlacklistAnimData').")
  ]] PabcData : OffsetFile<PabcData<V>>
  {
    static constexpr ClientVersion version = V;

    [[=welder::mark::no_reassign,
      =welder::doc("Replacement parent-sequence lookups: a plain list of "
                   "present AnimationData ids.")]]
    std::vector<std::uint16_t> replacement_parent_sequence_lookups;

    /** Chunk engagement: shadow OffsetFile's always-false empty() so the
        optional chunk is emitted only when data is present. */
    [[=welder::mark::exclude]]
    bool empty() const { return replacement_parent_sequence_lookups.empty(); }

    bool operator==(const PabcData&) const = default;
  };

  template <ClientVersion V>
  struct [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::doc("The PSBC chunk payload: parent sequence bounds.")
  ]] PsbcData : OffsetFile<PsbcData<V>>
  {
    static constexpr ClientVersion version = V;

    [[=welder::mark::no_reassign,
      =welder::doc("Parent sequence bounds, one per parent sequence.")]]
    std::vector<root::record::M2Bounds> parent_sequence_bounds;

    /** Chunk engagement: shadow OffsetFile's always-false empty() so the
        optional chunk is emitted only when data is present. */
    [[=welder::mark::exclude]]
    bool empty() const { return parent_sequence_bounds.empty(); }

    bool operator==(const PsbcData&) const = default;
  };

  template <ClientVersion V>
  struct [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::doc("The PGD1 chunk payload: per-particle-emitter geoset "
                 "assignments.")
  ]] Pgd1Data : OffsetFile<Pgd1Data<V>>
  {
    static constexpr ClientVersion version = V;

    [[=welder::mark::no_reassign,
      =welder::doc("The geoset each particle emitter obeys (M2SkinSection "
                   "geoset rules).")]]
    std::vector<std::uint16_t> geosets;

    /** Chunk engagement: shadow OffsetFile's always-false empty() so the
        optional chunk is emitted only when data is present. */
    [[=welder::mark::exclude]]
    bool empty() const { return geosets.empty(); }

    bool operator==(const Pgd1Data&) const = default;
  };
}
