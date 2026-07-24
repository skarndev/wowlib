#pragma once

/** @file
    M2 geometry, texture and material records (namespace
    wowlib::formats::m2::records): the global vertex, the texture definitions
    and their animated color/weight/transform companions. */

#include <array>
#include <cstdint>
#include <string>

#include <welder/vocabulary.hpp>

#include <wowlib/core/client_version.hpp>
#include <wowlib/formats/m2/boundaries.hpp>
#include <wowlib/formats/m2/records/track.hpp>

namespace wowlib::formats::m2::records
{
  /** A global model vertex; skins reference these through their local
      lookups. Model space is Z-up. */
  struct [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::doc("An M2 vertex: position, 4-bone weights/indices, normal and "
                 "two texture coordinate sets.")
  ]] M2Vertex
  {
    C3Vector pos{};
    std::array<std::uint8_t, 4> bone_weights{};
    std::array<std::uint8_t, 4> bone_indices{};
    C3Vector normal{};
    std::array<C2Vector, 2> tex_coords{};

    bool operator==(const M2Vertex&) const = default;
  };
  static_assert(sizeof(M2Vertex) == 48);

  /** M2Material::flags bits. */
  enum class [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::doc("M2Material render flags.")
  ]] MaterialFlags : std::uint16_t
  {
    Unlit [[=welder::doc("No lighting.")]] = 0x1,
    Unfogged [[=welder::doc("No fog.")]] = 0x2,
    TwoSided [[=welder::doc("No backface culling.")]] = 0x4,
    DepthTest [[=welder::doc("Depth test enabled.")]] = 0x8,
    DepthWrite [[=welder::doc("Depth write enabled.")]] = 0x10,
    NoCustomAlpha [[=welder::doc("MoP+: force fully opaque/transparent for "
                                 "custom elements.")]] = 0x800
  };

  /** Render flags plus blending mode, referenced from skin batches. */
  struct [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::doc("An M2 material: render flags (see MaterialFlags) and the "
                 "blending mode (see M2/Rendering M2BLEND).")
  ]] M2Material
  {
    std::uint16_t flags = 0;
    std::uint16_t blending_mode = 0;

    bool operator==(const M2Material&) const = default;
  };
  static_assert(sizeof(M2Material) == 4);

  /** A texture definition: type 0 references the filename (pre-8.0; TXID
      FileDataIDs replace it), non-zero types are runtime-substituted
      component textures (skin, hair, monster skins, ...). */
  struct M2Texture
  {
    std::uint32_t type = 0;  /**< 0 = filename-referenced, else a component slot. */
    std::uint32_t flags = 0; /**< 0x1 wrap X, 0x2 wrap Y. */
    std::string filename;    /**< For type 0; the serializer writes the NUL inside the count. */

    bool operator==(const M2Texture&) const = default;
  };

  /** Vertex-color + alpha animation pair (GEOA equivalent), referenced from
      skin batches by color index. */
  template <ClientVersion V>
  struct M2Color
  {
    M2Track<C3Vector, V> color{};  /**< RGB vertex color. */
    M2Track<fixed16, V> alpha{};   /**< 0 transparent .. 0x7FFF opaque. */

    bool operator==(const M2Color&) const = default;
  };

  /** A global texture weight (transparency) track. */
  template <ClientVersion V>
  struct M2TextureWeight
  {
    M2Track<fixed16, V> weight{};

    bool operator==(const M2TextureWeight&) const = default;
  };

  /** Pre-WotLK texture flipbook slot — never observed engaged in files. */
  template <ClientVersion V>
  struct M2TextureFlipbook
  {
    M2Track<std::uint16_t, V> frames{};

    bool operator==(const M2TextureFlipbook&) const = default;
  };

  /** A UV animation: the keyframes feed the texture transform matrix
      (rotation pivots at texture center 0.5, 0.5). */
  template <ClientVersion V>
  struct M2TextureTransform
  {
    M2Track<C3Vector, V> translation{};
    M2Track<C4Quaternion, V> rotation{};
    M2Track<C3Vector, V> scaling{};

    bool operator==(const M2TextureTransform&) const = default;
  };
}
