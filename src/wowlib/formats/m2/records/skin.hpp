#pragma once

/** @file
    M2 skin-profile records (namespace wowlib::formats::m2::records): the LOD
    view onto the model — local vertex/index/bone lookups, submeshes and
    render batches. Pre-WotLK the profiles sit embedded in the MD20 header;
    WotLK+ each profile is its own .skin file (see m2::skin::Skin). The
    profile is one offset record either way — offsets resolve against
    whichever file carries it. */

#include <array>
#include <cstdint>
#include <vector>

#include <welder/vocabulary.hpp>

#include <wowlib/core/client_version.hpp>
#include <wowlib/formats/common/annotations.hpp>
#include <wowlib/formats/m2/boundaries.hpp>
#include <wowlib/formats/m2/records/track.hpp>

namespace wowlib::formats::m2::records
{
  template <ClientVersion V>
  struct M2SkinSection;

  template <ClientVersion V>
    requires (V < m2_compressed_bones)
  struct [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::doc("A skin submesh, vanilla layout (no sort data yet).")
  ]] M2SkinSection<V>
  {
    [[=welder::doc("Mesh part (geoset) id.")]]
    std::uint16_t skin_section_id = 0;
    [[=welder::doc("Extends the 16-bit index_start by (level << 16) — index lists outgrow 64k first; vertex_start stays plain (verified on level=1 client files).")]]
    std::uint16_t level = 0;
    [[=welder::doc("First local vertex.")]]
    std::uint16_t vertex_start = 0;
    std::uint16_t vertex_count = 0;
    [[=welder::doc("First triangle index.")]]
    std::uint16_t index_start = 0;
    std::uint16_t index_count = 0;
    [[=welder::doc("Bone-lookup entries used.")]]
    std::uint16_t bone_count = 0;
    [[=welder::doc("First bone-lookup entry.")]]
    std::uint16_t bone_combo_index = 0;
    [[=welder::doc("Highest bones-per-vertex in the submesh.")]]
    std::uint16_t bone_influences = 0;
    std::uint16_t center_bone_index = 0;
    [[=welder::doc("Average vertex position.")]]
    C3Vector center_position{};

    bool operator==(const M2SkinSection&) const = default;
  };

  template <ClientVersion V>
    requires (V >= m2_compressed_bones)
  struct [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::doc("A skin submesh (TBC+): adds the sort center and radius.")
  ]] M2SkinSection<V>
  {
    [[=welder::doc("Mesh part (geoset) id.")]]
    std::uint16_t skin_section_id = 0;
    [[=welder::doc("Extends the 16-bit index_start by (level << 16) — index lists outgrow 64k first; vertex_start stays plain (verified on level=1 client files).")]]
    std::uint16_t level = 0;
    [[=welder::doc("First local vertex.")]]
    std::uint16_t vertex_start = 0;
    std::uint16_t vertex_count = 0;
    [[=welder::doc("First triangle index.")]]
    std::uint16_t index_start = 0;
    std::uint16_t index_count = 0;
    [[=welder::doc("Bone-lookup entries used.")]]
    std::uint16_t bone_count = 0;
    [[=welder::doc("First bone-lookup entry.")]]
    std::uint16_t bone_combo_index = 0;
    [[=welder::doc("Highest bones-per-vertex in the submesh.")]]
    std::uint16_t bone_influences = 0;
    std::uint16_t center_bone_index = 0;
    [[=welder::doc("Average vertex position.")]]
    C3Vector center_position{};
    [[=welder::doc("Center of the submesh bounding box.")]]
    C3Vector sort_center_position{};
    [[=welder::doc("Distance of the farthest vertex from it.")]]
    float sort_radius = 0;

    bool operator==(const M2SkinSection&) const = default;
  };

  static_assert(sizeof(M2SkinSection<versions::vanilla>) == 32);
  static_assert(sizeof(M2SkinSection<versions::wotlk>) == 48);

  struct [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::doc("An M2 skin render batch (texture unit): submesh + material "
                 "+ the lookup bases the shaders consume.")
  ]] M2Batch
  {
    [[=welder::doc("0x10 static texture, 0x40 transparency quirk, ...")]]
    std::uint8_t flags = 0;
    std::int8_t priority_plane = 0;
    [[=welder::doc("Pre-Cata: 0 on disk, computed at runtime.")]]
    std::uint16_t shader_id = 0;
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
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::doc("An M2 skin shadow batch (Cata+); the client can also "
                 "generate these from the render batches.")
  ]] M2ShadowBatch
  {
    std::uint8_t flags = 0;
    std::uint8_t flags2 = 0;
    std::uint16_t unknown1 = 0;
    std::uint16_t submesh_id = 0;
    [[=welder::doc("Already looked up.")]]
    std::uint16_t texture_id = 0;
    std::uint16_t color_id = 0;
    [[=welder::doc("Already looked up.")]]
    std::uint16_t transparency_id = 0;

    bool operator==(const M2ShadowBatch&) const = default;
  };
  static_assert(sizeof(M2ShadowBatch) == 12);

  template <ClientVersion V>
  struct [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::doc("One LOD view: local vertex/bone lookups into the model, submeshes and "
                 "render batches; embedded in the model pre-WotLK, an own .skin file "
                 "after.")
  ]] M2SkinProfile
  {
    [[=welder::doc("Local -> global vertex lookup.")]]
    std::vector<std::uint16_t> vertices;
    [[=welder::doc("Triangle list into the local vertices.")]]
    std::vector<std::uint16_t> indices;
    [[=welder::doc("Per-vertex 4-bone indices.")]]
    std::vector<std::array<std::uint8_t, 4>> bones;
    std::vector<M2SkinSection<V>> submeshes;
    std::vector<M2Batch> batches;
    [[=welder::doc("Max bones per draw call (21/53/64/256).")]]
    std::uint32_t bone_count_max = 0;

    [[
      =since(m2_multitex_particles),
      =welder::doc("Shadow batches (Cata+).")]]
    std::vector<M2ShadowBatch> shadow_batches;

    bool operator==(const M2SkinProfile&) const = default;
  };
}
