#pragma once

/** @file
    M2 skin-profile records (namespace wowlib::formats::m2::skin): the LOD
    view onto the model — local vertex/index/bone lookups, submeshes and
    render batches. Pre-WotLK the profiles sit embedded in the MD20 header;
    WotLK+ each profile is its own .skin file (see m2::skin::Skin). The
    profile is one offset record either way — offsets resolve against
    whichever file carries it. */

#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <vector>

#include <welder/vocabulary.hpp>

#include <wowlib/core/lang.hpp>
#include <wowlib/core/client_version.hpp>
#include <wowlib/formats/common/annotations.hpp>
#include <wowlib/formats/common/validation.hpp>
#include <wowlib/formats/m2/boundaries.hpp>
#include <wowlib/formats/m2/root/record/track.hpp>

namespace wowlib::formats::m2::skin
{
  /** The version-agnostic base of every M2SkinSection<V> (welded as "M2SkinSection").
      Bindings-only, like every *Base: it gives the per-version classes a
      common welded supertype, so the family surface hoists their shared
      members and containers of them carry a base-typed live view. */
  struct [[
    =welder::weld,
    =welder::weld_as("M2SkinSection"),
    WOWLIB_CS_FAMILY_SURFACE
    =welder::doc(R"(
        One renderable geometry section (submesh). Abstract over the client version; construct a concrete
        version with M2SkinSection.ForVersion / for_version.)")
  ]] M2SkinSectionBase
  {
    // The concretes default operator== — the base must be comparable too.
    bool operator==(const M2SkinSectionBase&) const = default;
  };

  /** The version-agnostic base of every M2SkinProfile<V> (welded as "M2SkinProfile").
      Bindings-only, like every *Base: it gives the per-version classes a
      common welded supertype, so the family surface hoists their shared
      members and containers of them carry a base-typed live view. */
  struct [[
    =welder::weld,
    =welder::weld_as("M2SkinProfile"),
    WOWLIB_CS_FAMILY_SURFACE
    =welder::doc(R"(
        One skin profile (a whole LOD's sections and batches). Abstract over the client version; construct a concrete
        version with M2SkinProfile.ForVersion / for_version.)")
  ]] M2SkinProfileBase
  {
    // The concretes default operator== — the base must be comparable too.
    bool operator==(const M2SkinProfileBase&) const = default;
  };

namespace detail
  {
    // The annotated era layouts; instantiate through the canonicalizing
    // aliases below, never directly.
      template <ClientVersion V>
    struct M2SkinSection;

    template <ClientVersion V>
      requires (V < m2_compressed_bones)
    struct [[
      =welder::weld,
      =welder::doc("A skin submesh, vanilla layout (no sort data yet).")
    ]] M2SkinSection<V> : M2SkinSectionBase
    {
      [[=welder::doc("Mesh part (geoset) id.")]]
      std::uint16_t skin_section_id = 0;
      [[=welder::doc("Extends the 16-bit index_start by (level << 16) — index lists outgrow 64k first; vertex_start stays plain (verified on level=1 client files).")]]
      std::uint16_t level = 0;
      [[=welder::doc("First local vertex.")]]
      std::uint16_t vertex_start = 0;
      [[=welder::doc("Local vertex count.")]]
      std::uint16_t vertex_count = 0;
      [[=welder::doc("First triangle index.")]]
      std::uint16_t index_start = 0;
      [[=welder::doc("Triangle index count.")]]
      std::uint16_t index_count = 0;
      [[=welder::doc("Bone-lookup entries used.")]]
      std::uint16_t bone_count = 0;
      [[=welder::doc("First bone-lookup entry.")]]
      std::uint16_t bone_combo_index = 0;
      [[=welder::doc("Highest bones-per-vertex in the submesh.")]]
      std::uint16_t bone_influences = 0;
      [[=welder::doc("The bone nearest the submesh center; wowdev leaves it otherwise undescribed.")]]
      std::uint16_t center_bone_index = 0;
      [[=welder::doc("Average vertex position.")]]
      C3Vector center_position{};

      bool operator==(const M2SkinSection&) const = default;
    };

    template <ClientVersion V>
      requires (V >= m2_compressed_bones)
    struct [[
      =welder::weld,
      =welder::doc("A skin submesh (TBC+): adds the sort center and radius.")
    ]] M2SkinSection<V> : M2SkinSectionBase
    {
      [[=welder::doc("Mesh part (geoset) id.")]]
      std::uint16_t skin_section_id = 0;
      [[=welder::doc("Extends the 16-bit index_start by (level << 16) — index lists outgrow 64k first; vertex_start stays plain (verified on level=1 client files).")]]
      std::uint16_t level = 0;
      [[=welder::doc("First local vertex.")]]
      std::uint16_t vertex_start = 0;
      [[=welder::doc("Local vertex count.")]]
      std::uint16_t vertex_count = 0;
      [[=welder::doc("First triangle index.")]]
      std::uint16_t index_start = 0;
      [[=welder::doc("Triangle index count.")]]
      std::uint16_t index_count = 0;
      [[=welder::doc("Bone-lookup entries used.")]]
      std::uint16_t bone_count = 0;
      [[=welder::doc("First bone-lookup entry.")]]
      std::uint16_t bone_combo_index = 0;
      [[=welder::doc("Highest bones-per-vertex in the submesh.")]]
      std::uint16_t bone_influences = 0;
      [[=welder::doc("The bone nearest the submesh center; wowdev leaves it otherwise undescribed.")]]
      std::uint16_t center_bone_index = 0;
      [[=welder::doc("Average vertex position.")]]
      C3Vector center_position{};
      [[=welder::doc("Center of the submesh bounding box.")]]
      C3Vector sort_center_position{};
      [[=welder::doc("Distance of the farthest vertex from it.")]]
      float sort_radius = 0;

      bool operator==(const M2SkinSection&) const = default;
    };
  }

  /** A skin submesh — the canonicalizing face of detail::M2SkinSection
      (m2_skin_section_pivots: TBC's sort center/radius tail). */
  template <ClientVersion V>
  using M2SkinSection =
    detail::M2SkinSection<canonical_version(V, m2_skin_section_pivots, m2_versions)>;


  static_assert(sizeof(M2SkinSection<versions::vanilla>) == 32);
  static_assert(sizeof(M2SkinSection<versions::wotlk>) == 48);

  struct [[
    =welder::weld,
    =welder::doc("An M2 skin render batch (texture unit): submesh + material "
                 "+ the lookup bases the shaders consume.")
  ]] M2Batch
  {
    [[=welder::doc("0x10 static texture, 0x40 transparency quirk, ...")]]
    std::uint8_t flags = 0;
    [[=welder::doc("Draw-order priority plane.")]]
    std::int8_t priority_plane = 0;
    [[=welder::doc("Pre-Cata: 0 on disk, computed at runtime.")]]
    std::uint16_t shader_id = 0;
    [[=welder::doc("The submesh this batch renders.")]]
    std::uint16_t skin_section_index = 0;
    [[=welder::doc("BfA+: renamed flags2 (0x2 projected, 0x8 EDGF).")]]
    std::uint16_t geoset_index = 0;
    [[=welder::doc("Into the model colors, -1 if none.")]]
    std::uint16_t color_index = 0;
    [[=welder::doc("Into the model materials.")]]
    std::uint16_t material_index = 0;
    [[=welder::doc("Capped at 7.")]]
    std::uint16_t material_layer = 0;
    [[=welder::doc("1..4 consecutive lookup entries.")]]
    std::uint16_t texture_count = 0;
    [[=welder::doc("Into the texture lookup.")]]
    std::uint16_t texture_combo_index = 0;
    [[=welder::doc("Into the texture-mapping lookup.")]]
    std::uint16_t texture_coord_combo_index = 0;
    [[=welder::doc("Into the transparency lookup.")]]
    std::uint16_t texture_weight_combo_index = 0;
    [[=welder::doc("Into the UV-animation lookup.")]]
    std::uint16_t texture_transform_combo_index = 0;

    bool operator==(const M2Batch&) const = default;
  };
  static_assert(sizeof(M2Batch) == 24);

  struct [[
    =welder::weld,
    =welder::doc("An M2 skin shadow batch (Cata+); the client can also "
                 "generate these from the render batches.")
  ]] M2ShadowBatch
  {
    [[=welder::doc("If auto-generated: the source render batch's flags.")]]
    std::uint8_t flags = 0;
    [[=welder::doc("If auto-generated: bits derived from the material's flags and blend mode.")]]
    std::uint8_t flags2 = 0;
    [[=welder::doc("Unknown.")]]
    std::uint16_t unknown1 = 0;
    [[=welder::doc("The submesh shadowed.")]]
    std::uint16_t submesh_id = 0;
    [[=welder::doc("Already looked up.")]]
    std::uint16_t texture_id = 0;
    [[=welder::doc("Into the model colors.")]]
    std::uint16_t color_id = 0;
    [[=welder::doc("Already looked up.")]]
    std::uint16_t transparency_id = 0;

    bool operator==(const M2ShadowBatch&) const = default;
  };
  static_assert(sizeof(M2ShadowBatch) == 12);

namespace detail
  {
    // The annotated era layouts; instantiate through the canonicalizing
    // aliases below, never directly.
      template <ClientVersion V>
    struct [[
      =welder::weld,
      =welder::doc("One LOD view: local vertex/bone lookups into the model, submeshes and "
                   "render batches; embedded in the model pre-WotLK, an own .skin file "
                   "after.")
    ]] M2SkinProfile : M2SkinProfileBase
    {
      [[=welder::doc("Local -> global vertex lookup.")]]
      std::vector<std::uint16_t> vertices;
      [[
        =formats::count_multiple_of(3),
        =formats::indexes("vertices"),
        =welder::doc("Triangle list into the local vertices.")]]
      std::vector<std::uint16_t> indices;
      [[
        =formats::count_matches("vertices"),
        =welder::doc("Per-vertex 4-bone indices.")]]
      std::vector<std::array<std::uint8_t, 4>> bones;
      // through the skin:: alias, NOT the sibling detail raw — member types
    // must collapse to the same canonical the welded classes use
    [[=welder::doc("The submeshes (skin sections).")]]
    std::vector<skin::M2SkinSection<V>> submeshes;
      [[=welder::doc("The render batches (texture units).")]]
      std::vector<M2Batch> batches;
      [[=welder::doc("Max bones per draw call (21/53/64/256).")]]
      std::uint32_t bone_count_max = 0;

      [[
        =since(m2_multitex_particles),
        =welder::doc("Shadow batches (Cata+).")]]
      std::vector<M2ShadowBatch> shadow_batches;

      /** Validation hook (see formats::detail::validate_value): the submesh and
          batch slices the annotations cannot express. Contracts reaching into
          the MODEL (batch material/color/texture-combo indices, a submesh's
          bone-lookup slice) belong to the M2 assembly's validate(), which is
          where both sides exist.

          A submesh's index slice starts at `index_start + (level << 16)` — the
          level field extends the 16-bit start, since index lists outgrow 64k
          before vertex lists do.
          @param report the report findings land in. */
      [[=welder::mark::exclude]]
      void validate_extra(ValidationReport& report) const
      {
        for (std::size_t i = 0; i < submeshes.size(); ++i)
        {
          const auto& submesh = submeshes[i];
          const std::size_t index_start =
            std::size_t{submesh.index_start} + (std::size_t{submesh.level} << 16);
          if (std::size_t{submesh.vertex_start} + submesh.vertex_count > vertices.size())
            report.add_error(std::format("submeshes[{}]", i),
                             std::format("vertex range [{}, {}) overruns the {} local vertices",
                                         submesh.vertex_start,
                                         submesh.vertex_start + submesh.vertex_count,
                                         vertices.size()));
          if (index_start + submesh.index_count > indices.size())
            report.add_error(std::format("submeshes[{}]", i),
                             std::format("index range [{}, {}) overruns the {} indices",
                                         index_start, index_start + submesh.index_count,
                                         indices.size()));
          if (submesh.index_count % 3 != 0)
            report.add_error(std::format("submeshes[{}]", i),
                             std::format("index count {} is not a multiple of 3",
                                         submesh.index_count));
        }

        for (std::size_t i = 0; i < batches.size(); ++i)
          if (batches[i].skin_section_index >= submeshes.size())
            report.add_error(std::format("batches[{}]", i),
                             std::format("skin_section_index {} out of range: {} submeshes",
                                         batches[i].skin_section_index, submeshes.size()));
      }

      bool operator==(const M2SkinProfile&) const = default;
    };
  }

  /** One LOD view — the canonicalizing face of detail::M2SkinProfile
      (m2_skin_profile_pivots: TBC's section layout, Cata's shadow batches). */
  template <ClientVersion V>
  using M2SkinProfile =
    detail::M2SkinProfile<canonical_version(V, m2_skin_profile_pivots, m2_versions)>;

}
