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

namespace wowlib::formats::m2::skin {
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
    ]] M2SkinSectionBase {
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
    ]] M2SkinProfileBase {
    // The concretes default operator== — the base must be comparable too.
    bool operator==(const M2SkinProfileBase&) const = default;
  };

  namespace detail {
    // The annotated era layouts; instantiate through the canonicalizing
    // aliases below, never directly.
    template <ClientVersion V>
    struct M2SkinSection;

    template <ClientVersion V> requires (V < M2CompressedBones)
    struct [[
        =welder::weld,
        =welder::doc("A skin submesh, vanilla layout (no sort data yet).")
      ]] M2SkinSection<V> : M2SkinSectionBase {
      [[=welder::doc("Mesh part (geoset) id.")]]
      std::uint16_t skinSectionId = 0;
      [[=welder::doc(
          "Extends the 16-bit index_start by (level << 16) — index lists outgrow 64k first; vertex_start stays plain (verified on level=1 client files).")
      ]]
      std::uint16_t level = 0;
      [[=welder::doc("First local vertex.")]]
      std::uint16_t vertexStart = 0;
      [[=welder::doc("Local vertex count.")]]
      std::uint16_t vertexCount = 0;
      [[=welder::doc("First triangle index.")]]
      std::uint16_t indexStart = 0;
      [[=welder::doc("Triangle index count.")]]
      std::uint16_t indexCount = 0;
      [[=welder::doc("Bone-lookup entries used.")]]
      std::uint16_t boneCount = 0;
      [[=welder::doc("First bone-lookup entry.")]]
      std::uint16_t boneComboIndex = 0;
      [[=welder::doc("Highest bones-per-vertex in the submesh.")]]
      std::uint16_t boneInfluences = 0;
      [[=welder::doc(
          "The bone nearest the submesh center; wowdev leaves it otherwise undescribed.")
      ]]
      std::uint16_t centerBoneIndex = 0;
      [[=welder::doc("Average vertex position.")]]
      C3Vector centerPosition{};

      bool operator==(const M2SkinSection&) const = default;
    };

    template <ClientVersion V> requires (V >= M2CompressedBones)
    struct [[
        =welder::weld,
        =welder::doc("A skin submesh (TBC+): adds the sort center and radius.")
      ]] M2SkinSection<V> : M2SkinSectionBase {
      [[=welder::doc("Mesh part (geoset) id.")]]
      std::uint16_t skinSectionId = 0;
      [[=welder::doc(
          "Extends the 16-bit index_start by (level << 16) — index lists outgrow 64k first; vertex_start stays plain (verified on level=1 client files).")
      ]]
      std::uint16_t level = 0;
      [[=welder::doc("First local vertex.")]]
      std::uint16_t vertexStart = 0;
      [[=welder::doc("Local vertex count.")]]
      std::uint16_t vertexCount = 0;
      [[=welder::doc("First triangle index.")]]
      std::uint16_t indexStart = 0;
      [[=welder::doc("Triangle index count.")]]
      std::uint16_t indexCount = 0;
      [[=welder::doc("Bone-lookup entries used.")]]
      std::uint16_t boneCount = 0;
      [[=welder::doc("First bone-lookup entry.")]]
      std::uint16_t boneComboIndex = 0;
      [[=welder::doc("Highest bones-per-vertex in the submesh.")]]
      std::uint16_t boneInfluences = 0;
      [[=welder::doc(
          "The bone nearest the submesh center; wowdev leaves it otherwise undescribed.")
      ]]
      std::uint16_t centerBoneIndex = 0;
      [[=welder::doc("Average vertex position.")]]
      C3Vector centerPosition{};
      [[=welder::doc("Center of the submesh bounding box.")]]
      C3Vector sortCenterPosition{};
      [[=welder::doc("Distance of the farthest vertex from it.")]]
      float sortRadius = 0;

      bool operator==(const M2SkinSection&) const = default;
    };
  }

  /** A skin submesh — the canonicalizing face of detail::M2SkinSection
      (M2SkinSectionPivots: TBC's sort center/radius tail). */
  template <ClientVersion V>
  using M2SkinSection = detail::M2SkinSection<canonicalVersion(V, M2SkinSectionPivots, M2Versions)>;


  static_assert(sizeof(M2SkinSection<versions::Vanilla>) == 32);
  static_assert(sizeof(M2SkinSection<versions::Wotlk>) == 48);

  struct [[
      =welder::weld,
      =welder::doc("An M2 skin render batch (texture unit): submesh + material "
        "+ the lookup bases the shaders consume.")
    ]] M2Batch {
    [[=welder::doc("0x10 static texture, 0x40 transparency quirk, ...")]]
    std::uint8_t flags = 0;
    [[=welder::doc("Draw-order priority plane.")]]
    std::int8_t priorityPlane = 0;
    [[=welder::doc("Pre-Cata: 0 on disk, computed at runtime.")]]
    std::uint16_t shaderId = 0;
    [[=welder::doc("The submesh this batch renders.")]]
    std::uint16_t skinSectionIndex = 0;
    [[=welder::doc("BfA+: renamed flags2 (0x2 projected, 0x8 EDGF).")]]
    std::uint16_t geosetIndex = 0;
    [[=welder::doc("Into the model colors, -1 if none.")]]
    std::uint16_t colorIndex = 0;
    [[=welder::doc("Into the model materials.")]]
    std::uint16_t materialIndex = 0;
    [[=welder::doc("Capped at 7.")]]
    std::uint16_t materialLayer = 0;
    [[=welder::doc("1..4 consecutive lookup entries.")]]
    std::uint16_t textureCount = 0;
    [[=welder::doc("Into the texture lookup.")]]
    std::uint16_t textureComboIndex = 0;
    [[=welder::doc("Into the texture-mapping lookup.")]]
    std::uint16_t textureCoordComboIndex = 0;
    [[=welder::doc("Into the transparency lookup.")]]
    std::uint16_t textureWeightComboIndex = 0;
    [[=welder::doc("Into the UV-animation lookup.")]]
    std::uint16_t textureTransformComboIndex = 0;

    bool operator==(const M2Batch&) const = default;
  };

  static_assert(sizeof(M2Batch) == 24);

  struct [[
      =welder::weld,
      =welder::doc("An M2 skin shadow batch (Cata+); the client can also "
        "generate these from the render batches.")
    ]] M2ShadowBatch {
    [[=welder::doc("If auto-generated: the source render batch's flags.")]]
    std::uint8_t flags = 0;
    [[=welder::doc(
        "If auto-generated: bits derived from the material's flags and blend mode.")
    ]]
    std::uint8_t flags2 = 0;
    [[=welder::doc("Unknown.")]]
    std::uint16_t unknown1 = 0;
    [[=welder::doc("The submesh shadowed.")]]
    std::uint16_t submeshId = 0;
    [[=welder::doc("Already looked up.")]]
    std::uint16_t textureId = 0;
    [[=welder::doc("Into the model colors.")]]
    std::uint16_t colorId = 0;
    [[=welder::doc("Already looked up.")]]
    std::uint16_t transparencyId = 0;

    bool operator==(const M2ShadowBatch&) const = default;
  };

  static_assert(sizeof(M2ShadowBatch) == 12);

  namespace detail {
    // The annotated era layouts; instantiate through the canonicalizing
    // aliases below, never directly.
    template <ClientVersion V>
    struct [[
        =welder::weld,
        =welder::doc(
          "One LOD view: local vertex/bone lookups into the model, submeshes and "
          "render batches; embedded in the model pre-WotLK, an own .skin file "
          "after.")
      ]] M2SkinProfile : M2SkinProfileBase {
      [[=welder::doc("Local -> global vertex lookup.")]]
      std::vector<std::uint16_t> vertices;
      [[
        =formats::countMultipleOf(3),
        =formats::indexes("vertices"),
        =welder::doc("Triangle list into the local vertices.")]]
      std::vector<std::uint16_t> indices;
      [[
        =formats::countMatches("vertices"),
        =welder::doc("Per-vertex 4-bone indices.")]]
      std::vector<std::array<std::uint8_t, 4>> bones;
      // through the skin:: alias, NOT the sibling detail raw — member types
      // must collapse to the same canonical the welded classes use
      [[=welder::doc("The submeshes (skin sections).")]]
      std::vector<skin::M2SkinSection<V>> submeshes;
      [[=welder::doc("The render batches (texture units).")]]
      std::vector<M2Batch> batches;
      [[=welder::doc("Max bones per draw call (21/53/64/256).")]]
      std::uint32_t boneCountMax = 0;

      [[
        =since(M2MultitexParticles),
        =welder::doc("Shadow batches (Cata+).")]]
      std::vector<M2ShadowBatch> shadowBatches;

      /** Validation hook (see formats::detail::validateValue): the submesh and
          batch slices the annotations cannot express. Contracts reaching into
          the MODEL (batch material/color/texture-combo indices, a submesh's
          bone-lookup slice) belong to the M2 assembly's validate(), which is
          where both sides exist.

          A submesh's index slice starts at `indexStart + (level << 16)` — the
          level field extends the 16-bit start, since index lists outgrow 64k
          before vertex lists do.
          @param report the report findings land in. */
      [[=welder::mark::exclude]]
      void validateExtra(ValidationReport& report) const {
        for (std::size_t i = 0; i < submeshes.size(); ++i) {
          const auto& submesh = submeshes[i];
          const std::size_t indexStart = std::size_t{submesh.indexStart} + (std::size_t{submesh.level} << 16);
          if (std::size_t{submesh.vertexStart} + submesh.vertexCount > vertices.size())
            report.addError(std::format("submeshes[{}]", i),
                             std::format("vertex range [{}, {}) overruns the {} local vertices", submesh.vertexStart,
                                         submesh.vertexStart + submesh.vertexCount, vertices.size()));
          if (indexStart + submesh.indexCount > indices.size())
            report.addError(std::format("submeshes[{}]", i),
                             std::format("index range [{}, {}) overruns the {} indices", indexStart,
                                         indexStart + submesh.indexCount, indices.size()));
          if (submesh.indexCount % 3 != 0)
            report.addError(std::format("submeshes[{}]", i),
                             std::format("index count {} is not a multiple of 3", submesh.indexCount));
        }

        for (std::size_t i = 0; i < batches.size(); ++i)
          if (batches[i].skinSectionIndex >= submeshes.size())
            report.addError(std::format("batches[{}]", i),
                             std::format("skin_section_index {} out of range: {} submeshes",
                                         batches[i].skinSectionIndex, submeshes.size()));
      }

      bool operator==(const M2SkinProfile&) const = default;
    };
  }

  /** One LOD view — the canonicalizing face of detail::M2SkinProfile
      (M2SkinProfilePivots: TBC's section layout, Cata's shadow batches). */
  template <ClientVersion V>
  using M2SkinProfile = detail::M2SkinProfile<canonicalVersion(V, M2SkinProfilePivots, M2Versions)>;
}
