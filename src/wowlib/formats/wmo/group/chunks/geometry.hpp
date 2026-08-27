#pragma once

/** @file
    WMO group geometry: polys, batches, BSP (MOPY, MOBA, MOBN) (namespace wowlib::formats::wmo::group::chunks). */

#include <array>
#include <cstdint>
#include <utility>

#include <welder/vocabulary.hpp>

#include <wowlib/core/client_version.hpp>
#include <wowlib/core/lang.hpp>
#include <wowlib/formats/common/flags.hpp>
#include <wowlib/formats/common/types.hpp>
#include <wowlib/formats/wmo/boundaries.hpp>

namespace wowlib::formats::wmo::group::chunks {
  // --- MOPY / MPY2 / MOBA / MORB / MOBS / MOBN --------------------------------

  enum class [[
      =welder::weld,
      =welder::doc("Per-triangle flag bits (MOPY and MPY2).")
    ]] PolyFlags : std::uint8_t {
    Transition [[=
      welder::doc("A transition face, blending exterior and interior lighting.")
    ]] = 0x1,
    NoCamCollide [[=welder::doc("The camera does not collide with this face.")
    ]] = 0x2,
    Detail [[=welder::doc("A detail face.")]] = 0x4,
    Collision [[=welder::doc("Collision-only (also turns off water ripples).")]] = 0x8,
    Hint [[=welder::doc("A hint face.")]] = 0x10,
    Render [[=welder::doc("A rendered face.")]] = 0x20,
    CullObjects [[=
      welder::doc("Enables game-object culling against this face.")]] = 0x40,
    CollideHit [[=welder::doc("Collides with projectiles/hit tests.")]] = 0x80
  };

  struct [[
      =welder::weld,
      =welder::doc("Per-triangle material info (MOPY).")
    ]] SMOPoly {
    [[=welder::doc("Triangle flags; PolyFlags bits.")]]
    std::uint8_t flags = 0;

    [[=welder::doc("Index into MOMT; 0xFF for collision-only faces.")]]
    std::uint8_t materialId = 0;
  };

  static_assert(sizeof(SMOPoly) == 0x2);

  struct [[
      =welder::weld,
      =welder::doc(R"(
        Per-triangle material info v2 (MPY2, 10.0+): the MOPY replacement with
        16-bit fields for multiple-material support.)")
    ]] Poly2 {
    [[=welder::doc("Triangle flags; PolyFlags bits, plus 0x100 for MOQG "
      "ground-type queries.")]]
    std::uint16_t flags = 0;

    [[=welder::doc("Index into MOMT.")]]
    std::uint16_t materialId = 0;
  };

  static_assert(sizeof(Poly2) == 0x4);

  /** The version-agnostic base of every SMOBatch<V>, welded as "WMOBatch".

      This base exists ENTIRELY for the language bindings (Python, Lua): it gives
      the per-version WMOBatch* classes a common welded supertype so binding users
      can write version-agnostic code (isinstance,
      WMOBatch.for_version(expansion)). It has no role in the C++ API, where you
      use the concrete SMOBatch<V> directly. It is an empty (elided) base, so the
      binary specializations stay trivially copyable and 0x18 bytes. */
  struct [[
      =welder::weld,
      =welder::weld_as("WMOBatch"),
  WOWLIB_CS_FAMILY_SURFACE
      =welder::doc("A MOBA render batch, abstract over the client version. "
        "Construct a concrete version with "
        "WMOBatch.for_version(expansion).")
    ]] WMOBatchBase {};

  /** One render batch, 24 bytes. Before 7.0 the leading 12 bytes are an int16
      culling box; from 7.0 they are unused except for a uint16 material id
      backing the large-material flag bit. */
  namespace detail {
    // The annotated era layouts; instantiate through the canonicalizing
    // aliases below, never directly.
    template <ClientVersion V>
    struct SMOBatch;

    template <ClientVersion V> requires(V < WmoBatchLargeMaterial)
    struct [[
      =welder::weld,
      =welder::doc(
        "One MOBA render batch: an index range sharing one material, "
        "with its low-resolution culling box.")
    ]] WOWLIB_EMPTY_BASES SMOBatch<V> : WMOBatchBase{
      [[=welder::doc("Culling box minimum corner (rounded vertex bounds).")]]
      std::array<std::int16_t, 3> boxMin{};

      [[=welder::doc("Culling box maximum corner.")]]
      std::array<std::int16_t, 3> boxMax{};

      [[=welder::doc("First face index in MOVI.")]]
      std::uint32_t startIndex = 0;

      [[=welder::doc("Number of MOVI indices.")]]
      std::uint16_t count = 0;

      [[=welder::doc("First vertex used in MOVT.")]]
      std::uint16_t minIndex = 0;

      [[=welder::doc("Last vertex used, inclusive.")]]
      std::uint16_t maxIndex = 0;

      [[=welder::doc("Batch flags.")]]
      std::uint8_t flags = 0;

      [[=welder::doc("Index into MOMT.")]]
      std::uint8_t materialId = 0;



    };

    template <ClientVersion V> requires(V >= WmoBatchLargeMaterial)
    struct [[
      =welder::weld,
      =welder::doc(
        "One MOBA render batch: an index range sharing one material "
        "(Legion+ layout with the 16-bit material id).")
    ]] WOWLIB_EMPTY_BASES SMOBatch<V> : WMOBatchBase{
      /** The nulled remains of the pre-7.0 culling box. */
      [[=welder::mark::exclude]]std::array<std::uint8_t, 10> unknown{};

      [[=welder::doc("16-bit material id; used when flags has 0x2.")]]
      std::uint16_t materialIdLarge = 0;

      [[=welder::doc("First face index in MOVI.")]]
      std::uint32_t startIndex = 0;

      [[=welder::doc("Number of MOVI indices.")]]
      std::uint16_t count = 0;

      [[=welder::doc("First vertex used in MOVT.")]]
      std::uint16_t minIndex = 0;

      [[=welder::doc("Last vertex used, inclusive.")]]
      std::uint16_t maxIndex = 0;

      [[=welder::doc("Batch flags; 0x2: use material_id_large.")]]
      std::uint8_t flags = 0;

      [[=welder::doc("Index into MOMT (when it fits 8 bits).")]]
      std::uint8_t materialId = 0;



    };
  }

  /** A MOBA render batch — the canonicalizing face of detail::SMOBatch
      (WmoBatchPivots: the culling box gives way to the large material id
      at Legion), so two instantiations cover all eleven releases. */
  template <ClientVersion V>
  using SMOBatch = detail::SMOBatch<canonicalVersion(V, WmoBatchPivots, WmoVersions)>;


  static_assert(sizeof(SMOBatch<versions::Wotlk>) == 0x18);
  static_assert(sizeof(SMOBatch<versions::Shadowlands>) == 0x18);

  struct [[
      =welder::weld,
      =welder::doc(R"(
        One MORB entry (Cata+): a triangle-strip override of the matching MOBA
        batch's start and count. Same count as MOBA; ignored unless the client
        renders strips.)")
    ]] RenderBatchOverride {
    [[=welder::doc("Replacement first-index into the MORI strips.")]]
    std::uint32_t startIndex = 0;

    [[=welder::doc("Replacement index count.")]]
    std::uint16_t indexCount = 0;

    [[=welder::doc("Alignment padding; zero in client files.")]]
    std::uint16_t padding = 0;
  };

  static_assert(sizeof(RenderBatchOverride) == 0x8);

  struct [[
      =welder::weld,
      =welder::doc(
        "One MOBS shadow batch (Cata+): the shadow-pass counterpart of "
        "a MOBA render batch.")
    ]] ShadowBatch {
    /** Undeciphered leading fields. */
    [[=welder::mark::exclude]] std::array<std::uint8_t, 10> unknown{};

    [[=welder::doc("16-bit material id; used when flags has 0x2.")]]
    std::uint16_t materialIdLarge = 0;

    [[=welder::doc("Start value; divided by 3 on use (a face index).")]]
    std::int32_t start = 0;

    [[=welder::doc("Count value; divided by 3 on use (a face count).")]]
    std::int16_t count = 0;

    /** Undeciphered middle fields. */
    [[=welder::mark::exclude]] std::array<std::uint8_t, 4> unknown2{};

    [[=welder::doc("Batch flags; 0x2: use material_id_large.")]]
    std::uint8_t flags = 0;

    [[=welder::doc("Index into MOMT (when it fits 8 bits).")]]
    std::uint8_t materialId = 0;
  };

  static_assert(sizeof(ShadowBatch) == 0x18);

  struct [[
      =welder::weld,
      =welder::doc("One MOBN BSP node for collision: a split plane or a leaf "
        "referencing faces through MOBR.")
    ]] CAaBspNode {
    [[=welder::doc("0-2: split axis, 0x4: leaf.")]]
    std::uint16_t flags = 0;

    [[=welder::doc("Negative-side child node, -1 for none.")]]
    std::int16_t negChild = -1;

    [[=welder::doc("Positive-side child node, -1 for none.")]]
    std::int16_t posChild = -1;

    [[=welder::doc("Face count in MOBR (leaves).")]]
    std::uint16_t nFaces = 0;

    [[=welder::doc("First face index in MOBR.")]]
    std::uint32_t faceStart = 0;

    [[=welder::doc("Split plane distance from the model origin.")]]
    float planeDist = 0;
  };

  static_assert(sizeof(CAaBspNode) == 0x10);
}
