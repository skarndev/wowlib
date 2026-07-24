#pragma once

/** @file
    The M2 animation vocabulary (namespace wowlib::formats::m2::records): the
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

namespace wowlib::formats::m2::records
{
  /** An inclusive index/timestamp range (pre-WotLK track slicing, sequence
      replay bounds). */
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

  /** An extent box plus bounding-sphere radius (sequence/model bounds). */
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

  /** One global-sequence upper timestamp. */
  struct [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::doc("A global-loop entry: the timestamp a global sequence wraps at.")
  ]] M2Loop
  {
    std::uint32_t timestamp = 0;

    bool operator==(const M2Loop&) const = default;
  };
  static_assert(sizeof(M2Loop) == 4);

  /** A quaternion compressed to signed 16-bit components (TBC+ bone
      rotations); (32767, 32767, 32767, 65535-as-signed) is identity. */
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

  /** A min/max vector pair (particle tumble ranges). */
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

  /** A spline keyframe: the value plus its incoming/outgoing tangents
      (hermite/bezier interpolated tracks — cameras). Trivially copyable when
      T is. */
  template <typename T>
  struct M2SplineKey
  {
    T value{};
    T in_tan{};
    T out_tan{};

    bool operator==(const M2SplineKey&) const = default;
  };
  static_assert(sizeof(M2SplineKey<float>) == 12);
  static_assert(sizeof(M2SplineKey<C3Vector>) == 36);

  /** An animation track for value type @a T, laid out for client version
      @a V. See the file comment for the two eras. Interpolation types:
      0 none, 1 linear, 2 bezier, 3 hermite (spline types only valid for
      M2SplineKey tracks). A track bound to a global sequence (index != -1)
      has a single timeline clamped to that loop. */
  template <typename T, ClientVersion V>
  struct M2Track;

  /** Pre-WotLK: one global timeline, interpolation_ranges slicing it per
      sequence (sequences reference it by their global start/end
      timestamps). */
  template <typename T, ClientVersion V>
    requires (V < m2_per_sequence_timelines)
  struct M2Track<T, V>
  {
    std::uint16_t interpolation_type = 0;
    std::uint16_t global_sequence = 0xFFFF; /**< -1: none. */
    std::vector<M2Range> interpolation_ranges;
    std::vector<std::uint32_t> timestamps;
    std::vector<T> values;

    bool operator==(const M2Track&) const = default;
  };

  /** WotLK+: one timestamp/value array per sequence; a sequence with
      `(flags & 0x130) == 0` keeps these in its .anim file (the offset I/O
      contexts route them). */
  template <typename T, ClientVersion V>
    requires (V >= m2_per_sequence_timelines)
  struct M2Track<T, V>
  {
    std::uint16_t interpolation_type = 0;
    std::uint16_t global_sequence = 0xFFFF; /**< -1: none. */

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
  struct M2TrackBase<V>
  {
    std::uint16_t interpolation_type = 0;
    std::uint16_t global_sequence = 0xFFFF;
    std::vector<M2Range> interpolation_ranges;
    std::vector<std::uint32_t> timestamps;

    bool operator==(const M2TrackBase&) const = default;
  };

  template <ClientVersion V>
    requires (V >= m2_per_sequence_timelines)
  struct M2TrackBase<V>
  {
    std::uint16_t interpolation_type = 0;
    std::uint16_t global_sequence = 0xFFFF;

    [[=formats::sequence_data]]
    std::vector<std::vector<std::uint32_t>> timestamps;

    bool operator==(const M2TrackBase&) const = default;
  };

  /** The header-less "fake" animation block (WotLK+ particle color/alpha/
      scale/UV ramps): sequence-independent short timestamps plus keys,
      always pointing at inline data. */
  template <typename T>
  struct FBlock
  {
    std::vector<std::uint16_t> timestamps;
    std::vector<T> keys;

    bool operator==(const FBlock&) const = default;
  };
}
