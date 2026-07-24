#pragma once

/** @file
    M2 skeleton records (namespace wowlib::formats::m2::records): M2CompBone
    across its eras. Bones are offset records — their transform tracks nest
    per-sequence arrays behind M2Array references (WotLK+). */

#include <cstdint>

#include <welder/vocabulary.hpp>

#include <wowlib/core/client_version.hpp>
#include <wowlib/formats/m2/boundaries.hpp>
#include <wowlib/formats/m2/records/track.hpp>

namespace wowlib::formats::m2::records
{
  /** M2CompBone::flags bits. Wire fields stay plain ints; test with
      has_flag(). */
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

  template <ClientVersion V>
  struct M2CompBone;

  /** Vanilla (v256/257): raw-quaternion rotations, no name CRC. */
  template <ClientVersion V>
    requires (V < m2_compressed_bones)
  struct M2CompBone<V>
  {
    std::int32_t key_bone_id = -1;  /**< Key-bone-lookup back reference, -1 if none. */
    std::uint32_t flags = 0;        /**< See BoneFlags. */
    std::int16_t parent_bone = -1;  /**< Parent bone index, -1 for roots. */
    std::uint16_t submesh_id = 0;   /**< Mesh part id. */
    M2Track<C3Vector, V> translation{};
    M2Track<C4Quaternion, V> rotation{};
    M2Track<C3Vector, V> scale{};
    C3Vector pivot{};               /**< The bone's pivot point. */

    bool operator==(const M2CompBone&) const = default;
  };

  /** TBC+: compressed-quaternion rotations plus the debug name CRC (the
      track era inside follows the entity version). */
  template <ClientVersion V>
    requires (V >= m2_compressed_bones)
  struct M2CompBone<V>
  {
    std::int32_t key_bone_id = -1;    /**< Key-bone-lookup back reference, -1 if none. */
    std::uint32_t flags = 0;          /**< See BoneFlags. */
    std::int16_t parent_bone = -1;    /**< Parent bone index, -1 for roots. */
    std::uint16_t submesh_id = 0;     /**< Mesh part id. */
    std::uint32_t bone_name_crc = 0;  /**< CRC of the authoring bone name (debug only). */
    M2Track<C3Vector, V> translation{};
    M2Track<M2CompQuat, V> rotation{};
    M2Track<C3Vector, V> scale{};
    C3Vector pivot{};                 /**< The bone's pivot point. */

    bool operator==(const M2CompBone&) const = default;
  };
}
