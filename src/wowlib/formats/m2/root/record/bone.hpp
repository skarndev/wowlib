#pragma once

/** @file
    M2 skeleton records (namespace wowlib::formats::m2::root::record): M2CompBone
    across its eras. Bones are offset records — their transform tracks nest
    per-sequence arrays behind M2Array references (WotLK+). */

#include <cstdint>

#include <welder/vocabulary.hpp>

#include <wowlib/core/client_version.hpp>
#include <wowlib/formats/m2/boundaries.hpp>
#include <wowlib/formats/m2/root/record/track.hpp>

namespace wowlib::formats::m2::root::record
{
  enum class [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::doc("M2CompBone flags: parent-transform exemptions, billboarding "
                 "and physics participation.")
  ]] BoneFlags : std::uint32_t
  {
    IgnoreParentTranslate [[=welder::doc("Do not inherit parent translation.")]] = 0x1,
    IgnoreParentScale [[=welder::doc("Do not inherit parent scale.")]] = 0x2,
    IgnoreParentRotation [[=welder::doc("Do not inherit parent rotation.")]] = 0x4,
    SphericalBillboard [[=welder::doc("Always face the viewer.")]] = 0x8,
    CylindricalBillboardLockX [[=welder::doc("Billboard around the X axis.")]] = 0x10,
    CylindricalBillboardLockY [[=welder::doc("Billboard around the Y axis.")]] = 0x20,
    CylindricalBillboardLockZ [[=welder::doc("Billboard around the Z axis.")]] = 0x40,
    Transformed [[=welder::doc("Has an animated transform.")]] = 0x200,
    KinematicBone [[=welder::doc("MoP+: physics may drive this bone.")]] = 0x400,
    HelmetAnimScaled [[=welder::doc("Scale by the helmet-anim-scaling record.")]] = 0x1000,
    SequenceId [[=welder::doc("BfA+: parent_bone/submesh_id form a sequence id.")]] = 0x2000
  };

  namespace detail
  {
    // The annotated era layouts; instantiate through the canonicalizing
    // aliases below, never directly.
    template <ClientVersion V>
    struct M2CompBone;

    template <ClientVersion V>
      requires (V < m2_compressed_bones)
    struct [[
      =welder::weld(welder::lang::py, welder::lang::lua),
      =welder::doc("A bone, vanilla layout: raw-quaternion rotations, no name CRC.")
    ]] M2CompBone<V>
    {
      [[=welder::doc("Key-bone-lookup back reference, -1 if none.")]]
      std::int32_t key_bone_id = -1;
      [[=welder::doc("See BoneFlags.")]]
      std::uint32_t flags = 0;
      [[=welder::doc("Parent bone index, -1 for roots.")]]
      std::int16_t parent_bone = -1;
      [[=welder::doc("Mesh part id.")]]
      std::uint16_t submesh_id = 0;
      record::M2Track<C3Vector, V> translation{};
      record::M2Track<C4Quaternion, V> rotation{};
      record::M2Track<C3Vector, V> scale{};
      [[=welder::doc("The bone's pivot point.")]]
      C3Vector pivot{};

      bool operator==(const M2CompBone&) const = default;
    };

    template <ClientVersion V>
      requires (V >= m2_compressed_bones)
    struct [[
      =welder::weld(welder::lang::py, welder::lang::lua),
      =welder::doc("A bone (TBC+): compressed-quaternion rotations plus the debug name "
                   "CRC; the track era inside follows the entity version.")
    ]] M2CompBone<V>
    {
      [[=welder::doc("Key-bone-lookup back reference, -1 if none.")]]
      std::int32_t key_bone_id = -1;
      [[=welder::doc("See BoneFlags.")]]
      std::uint32_t flags = 0;
      [[=welder::doc("Parent bone index, -1 for roots.")]]
      std::int16_t parent_bone = -1;
      [[=welder::doc("Mesh part id.")]]
      std::uint16_t submesh_id = 0;
      [[=welder::doc("CRC of the authoring bone name (debug only).")]]
      std::uint32_t bone_name_crc = 0;
      record::M2Track<C3Vector, V> translation{};
      record::M2Track<M2CompQuat, V> rotation{};
      record::M2Track<C3Vector, V> scale{};
      [[=welder::doc("The bone's pivot point.")]]
      C3Vector pivot{};

      bool operator==(const M2CompBone&) const = default;
    };
  }

  /** A bone — the canonicalizing face of detail::M2CompBone: every client
      version maps to its range's first grid version (m2_bone_pivots: TBC's
      compressed quaternions + name CRC, WotLK's per-sequence timelines). */
  template <ClientVersion V>
  using M2CompBone =
    detail::M2CompBone<canonical_version(V, m2_bone_pivots, m2_versions)>;
}
