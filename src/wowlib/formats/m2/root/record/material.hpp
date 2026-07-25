#pragma once

/** @file
    M2 geometry, texture and material records (namespace
    wowlib::formats::m2::root::record): the global vertex, the texture definitions
    and their animated color/weight/transform companions. */

#include <array>
#include <cstdint>
#include <string>

#include <welder/vocabulary.hpp>

#include <wowlib/core/client_version.hpp>
#include <wowlib/formats/m2/boundaries.hpp>
#include <wowlib/formats/m2/root/record/track.hpp>

namespace wowlib::formats::m2::root::record
{
  struct [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::doc("An M2 vertex: position, 4-bone weights/indices, normal and "
                 "two texture coordinate sets.")
  ]] M2Vertex
  {
    [[=welder::doc("Position in model space.")]]
    C3Vector pos{};
    [[=welder::doc("Influence weights of the four bones; they sum to 255.")]]
    std::array<std::uint8_t, 4> bone_weights{};
    [[=welder::doc("The four influencing bone indices.")]]
    std::array<std::uint8_t, 4> bone_indices{};
    [[=welder::doc("Normal in model space.")]]
    C3Vector normal{};
    [[=welder::doc("Two UV sets; the shader picks which are used.")]]
    std::array<C2Vector, 2> tex_coords{};

    bool operator==(const M2Vertex&) const = default;
  };
  static_assert(sizeof(M2Vertex) == 48);

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

  struct [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::doc("An M2 material: render flags (see MaterialFlags) and the "
                 "blending mode (see M2/Rendering M2BLEND).")
  ]] M2Material
  {
    [[=welder::doc("See MaterialFlags.")]]
    std::uint16_t flags = 0;
    [[=welder::doc("M2BLEND blending mode (see M2/Rendering).")]]
    std::uint16_t blending_mode = 0;

    bool operator==(const M2Material&) const = default;
  };
  static_assert(sizeof(M2Material) == 4);

  struct [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::doc("A texture definition: type 0 references the filename (TXID "
                 "FileDataIDs replace it in 8.0+); non-zero types are runtime component "
                 "slots (skin, hair, monster skins, ...).")
  ]] M2Texture
  {
    [[=welder::doc("0 = filename-referenced, else a component slot.")]]
    std::uint32_t type = 0;
    [[=welder::doc("0x1 wrap X, 0x2 wrap Y.")]]
    std::uint32_t flags = 0;
    [[=welder::doc("For type 0; the serializer writes the NUL inside the count.")]]
    std::string filename;

    bool operator==(const M2Texture&) const = default;
  };

namespace detail
  {
    // The annotated era layouts; instantiate through the canonicalizing
    // aliases below, never directly.
      template <ClientVersion V>
    struct [[
      =welder::weld(welder::lang::py, welder::lang::lua),
      =welder::doc("A vertex color + alpha animation pair, referenced from skin batches "
                   "by color index.")
    ]] M2Color
    {
      [[=welder::doc("RGB vertex color.")]]
      record::M2Track<C3Vector, V> color{};
      [[=welder::doc("0 transparent .. 0x7FFF opaque.")]]
      record::M2Track<fixed16, V> alpha{};

      bool operator==(const M2Color&) const = default;
    };

    template <ClientVersion V>
    struct [[
      =welder::weld(welder::lang::py, welder::lang::lua),
      =welder::doc("A global texture weight (transparency) track.")
    ]] M2TextureWeight
    {
      [[=welder::doc("0 transparent .. 0x7FFF opaque; multiplies the color "
                     "block's alpha.")]]
      record::M2Track<fixed16, V> weight{};

      bool operator==(const M2TextureWeight&) const = default;
    };

    template <ClientVersion V>
    struct [[
      =welder::weld(welder::lang::py, welder::lang::lua),
      =welder::doc("A pre-WotLK texture flipbook slot; never observed engaged in files.")
    ]] M2TextureFlipbook
    {
      [[=welder::doc("Frame index keyframes.")]]
      record::M2Track<std::uint16_t, V> frames{};

      bool operator==(const M2TextureFlipbook&) const = default;
    };

    template <ClientVersion V>
    struct [[
      =welder::weld(welder::lang::py, welder::lang::lua),
      =welder::doc("A UV animation: translation/rotation/scaling keyframes for the "
                   "texture matrix (rotation pivots at texture center 0.5, 0.5).")
    ]] M2TextureTransform
    {
      [[=welder::doc("UV translation keyframes.")]]
      record::M2Track<C3Vector, V> translation{};
      [[=welder::doc("UV rotation keyframes, pivoting at the texture center.")]]
      record::M2Track<C4Quaternion, V> rotation{};
      [[=welder::doc("UV scaling keyframes.")]]
      record::M2Track<C3Vector, V> scaling{};

      bool operator==(const M2TextureTransform&) const = default;
    };
  }

  // The canonicalizing faces: these records' only version axis is the track
  // era of the animations they embed (m2_track_pivots).
  template <ClientVersion V>
  using M2Color = detail::M2Color<canonical_version(V, m2_track_pivots, m2_versions)>;
  template <ClientVersion V>
  using M2TextureWeight =
    detail::M2TextureWeight<canonical_version(V, m2_track_pivots, m2_versions)>;
  template <ClientVersion V>
  using M2TextureFlipbook =
    detail::M2TextureFlipbook<canonical_version(V, m2_track_pivots, m2_versions)>;
  template <ClientVersion V>
  using M2TextureTransform =
    detail::M2TextureTransform<canonical_version(V, m2_track_pivots, m2_versions)>;

}
