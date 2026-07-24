#pragma once

/** @file
    The M2 animation vocabulary (namespace wowlib::formats::m2::body::records): the
    small wire primitives (M2Range, M2Bounds, M2Loop, M2CompQuat, M2Box,
    M2SplineKey) and the track types every animated record embeds. Tracks are
    offset records — the serializer recurses into them inline at the record
    cursor; their arrays live in blocks behind M2Array references.

    Two track eras (m2_per_sequence_timelines pivot, MD20 v264 / WotLK):
    pre-WotLK tracks share ONE global timeline, sliced per sequence by
    interpolation ranges; WotLK+ tracks nest one timestamp/value array per
    sequence, and those inner arrays are `sequence_data` — a low-priority
    sequence's data lives in its .anim file, routed through the offset I/O
    contexts. */

#include <cstdint>
#include <vector>

#include <welder/vocabulary.hpp>

#include <wowlib/core/client_version.hpp>
#include <wowlib/formats/common/annotations.hpp>
#include <wowlib/formats/common/types.hpp>
#include <wowlib/formats/m2/boundaries.hpp>

namespace wowlib::formats::m2::body::records
{
  struct [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::doc("An inclusive u32 range: pre-WotLK track interpolation "
                 "ranges, sequence replay bounds.")
  ]] M2Range
  {
    std::uint32_t minimum = 0;
    std::uint32_t maximum = 0;

    bool operator==(const M2Range&) const = default;
  };
  static_assert(sizeof(M2Range) == 8);

  struct [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::doc("A bounding volume: axis-aligned extent plus sphere radius.")
  ]] M2Bounds
  {
    CAaBox extent{};
    float radius = 0;

    bool operator==(const M2Bounds&) const = default;
  };
  static_assert(sizeof(M2Bounds) == 28);

  struct [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::doc("A global-loop entry: the timestamp a global sequence wraps at.")
  ]] M2Loop
  {
    std::uint32_t timestamp = 0;

    bool operator==(const M2Loop&) const = default;
  };
  static_assert(sizeof(M2Loop) == 4);

  struct [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::doc("A quaternion compressed to i16 x, y, z, w (TBC+ bone "
                 "rotations); decompress as (v < 0 ? v + 32768 : v - 32767) / 32767.")
  ]] M2CompQuat
  {
    std::int16_t x = 32767;
    std::int16_t y = 32767;
    std::int16_t z = 32767;
    std::int16_t w = -1;  // 65535 as the client stores identity w

    bool operator==(const M2CompQuat&) const = default;
  };
  static_assert(sizeof(M2CompQuat) == 8);

  struct [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::doc("A model-space box: minimum and maximum corner vectors.")
  ]] M2Box
  {
    C3Vector minimum{};
    C3Vector maximum{};

    bool operator==(const M2Box&) const = default;
  };
  static_assert(sizeof(M2Box) == 24);

  template <typename T>
  struct [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::doc("A spline keyframe: the value plus incoming/outgoing tangents "
                 "(bezier/hermite camera tracks).")
  ]] M2SplineKey
  {
    T value{};
    T in_tan{};
    T out_tan{};

    bool operator==(const M2SplineKey&) const = default;
  };
  static_assert(sizeof(M2SplineKey<float>) == 12);
  static_assert(sizeof(M2SplineKey<C3Vector>) == 36);

  namespace detail
  {
  // The annotated era layouts; instantiate through the canonicalizing
  // aliases below, never directly.
  /** An animation track for value type @a T, laid out for client version
        @a V. See the file comment for the two eras. Interpolation types:
        0 none, 1 linear, 2 bezier, 3 hermite (spline types only valid for
        M2SplineKey tracks). A track bound to a global sequence (index != -1)
        has a single timeline clamped to that loop. */
    template <typename T, ClientVersion V>
    struct M2Track;

    template <typename T, ClientVersion V>
      requires (V < m2_per_sequence_timelines)
    struct [[
      =welder::weld(welder::lang::py, welder::lang::lua),
      =welder::doc("An animation track, pre-WotLK layout: one global timeline with "
                   "per-sequence interpolation ranges.")
    ]] M2Track<T, V>
    {
      std::uint16_t interpolation_type = 0;
      [[=welder::doc("-1: none.")]]
      std::uint16_t global_sequence = 0xFFFF;
      std::vector<M2Range> interpolation_ranges;
      std::vector<std::uint32_t> timestamps;
      std::vector<T> values;

      bool operator==(const M2Track&) const = default;
    };

    template <typename T, ClientVersion V>
      requires (V >= m2_per_sequence_timelines)
    struct [[
      =welder::weld(welder::lang::py, welder::lang::lua),
      =welder::doc("An animation track, WotLK+ layout: one timestamp/value array per "
                   "sequence; an external sequence keeps its arrays in the .anim file.")
    ]] M2Track<T, V>
    {
      std::uint16_t interpolation_type = 0;
      [[=welder::doc("-1: none.")]]
      std::uint16_t global_sequence = 0xFFFF;

      [[=formats::sequence_data]]
      std::vector<std::vector<std::uint32_t>> timestamps;

      [[=formats::sequence_data]]
      std::vector<std::vector<T>> values;

      bool operator==(const M2Track&) const = default;
    };

    /** A timestamp-only track (event triggers: every key is an implicit "fire
        now"). Same two eras as M2Track. */
    template <ClientVersion V>
    struct M2TrackBase;

    template <ClientVersion V>
      requires (V < m2_per_sequence_timelines)
    struct [[
      =welder::weld(welder::lang::py, welder::lang::lua),
      =welder::doc("A timestamp-only event track, pre-WotLK layout (every key fires).")
    ]] M2TrackBase<V>
    {
      std::uint16_t interpolation_type = 0;
      std::uint16_t global_sequence = 0xFFFF;
      std::vector<M2Range> interpolation_ranges;
      std::vector<std::uint32_t> timestamps;

      bool operator==(const M2TrackBase&) const = default;
    };

    template <ClientVersion V>
      requires (V >= m2_per_sequence_timelines)
    struct [[
      =welder::weld(welder::lang::py, welder::lang::lua),
      =welder::doc("A timestamp-only event track, WotLK+ layout (every key fires).")
    ]] M2TrackBase<V>
    {
      std::uint16_t interpolation_type = 0;
      std::uint16_t global_sequence = 0xFFFF;

      [[=formats::sequence_data]]
      std::vector<std::vector<std::uint32_t>> timestamps;

      bool operator==(const M2TrackBase&) const = default;
    };
  }

  /** An animation track for value type @a T — the canonicalizing face of
      detail::M2Track: every client version maps to its range's first grid
      version (m2_track_pivots), so one instantiation serves the whole range.
      See the detail primary for the era semantics. */
  template <typename T, ClientVersion V>
  using M2Track =
    detail::M2Track<T, canonical_version(V, m2_track_pivots, m2_versions)>;

  /** A timestamp-only event track — the canonicalizing face of
      detail::M2TrackBase (same two eras and pivots as M2Track). */
  template <ClientVersion V>
  using M2TrackBase =
    detail::M2TrackBase<canonical_version(V, m2_track_pivots, m2_versions)>;


  template <typename T>
  struct [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::doc("The header-less 'fake' animation block: sequence-independent u16 "
                 "timestamps plus keys (WotLK+ particle ramps).")
  ]] FBlock
  {
    std::vector<std::uint16_t> timestamps;
    std::vector<T> keys;

    bool operator==(const FBlock&) const = default;
  };

  template <typename T>
  struct [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::doc("A partial track: normalized fixed16 times plus values (Legion+ EXP2 "
                 "alpha cutoffs).")
  ]] M2PartTrack
  {
    std::vector<fixed16> times;
    std::vector<T> values;

    bool operator==(const M2PartTrack&) const = default;
  };
}
