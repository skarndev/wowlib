#pragma once

/** @file
    The M2 animation vocabulary (namespace wowlib::formats::m2::root::record): the
    small fixed-size primitives (M2Range, M2Bounds, M2Loop, M2CompQuat, M2Box,
    M2SplineKey) and the track types every animated record embeds. Tracks are
    offset records — the serializer recurses into them inline at the record
    cursor; their arrays live in blocks behind M2Array references.

    Two track eras (m2_per_sequence_timelines pivot, MD20 v264 / WotLK):
    pre-WotLK tracks share ONE global timeline, sliced per sequence by
    interpolation ranges; WotLK+ tracks nest one timestamp/value array per
    sequence, and those inner arrays are `sequence_data` — a low-priority
    sequence's data lives in its .anim file, routed through the offset I/O
    contexts. */

#include <algorithm>
#include <span>
#include <cstdint>
#include <format>
#include <utility>
#include <vector>

#include <welder/vocabulary.hpp>

#include <wowlib/core/lang.hpp>
#include <wowlib/core/client_version.hpp>
#include <wowlib/core/error.hpp>
#include <wowlib/formats/common/annotations.hpp>
#include <wowlib/formats/common/types.hpp>
#include <wowlib/formats/m2/boundaries.hpp>

namespace wowlib::formats::m2::root::record
{
  /** The version-agnostic base of every M2Track<T, V> VALUE FAMILY, one
      base per value type T (welded per family as "M2TrackC3Vector", ... —
      see the alias tables in bindings/instantiations/m2_ranges.hpp).
      Bindings-only, like every *Base. */
  template <typename T>
  struct [[
    =welder::weld,
    WOWLIB_CS_FAMILY_SURFACE
    =welder::doc(R"(
        An animation track family for one value type, abstract over the
        client version; the per-version classes are subclasses.)")
  ]] M2TrackFamilyBase
  {
    // The concretes default operator== — the base must be comparable too.
    bool operator==(const M2TrackFamilyBase&) const = default;
  };

  /** The version-agnostic base of every M2EventTrack<V> (welded as "M2EventTrack").
      Bindings-only, like every *Base: it gives the per-version classes a
      common welded supertype, so the family surface hoists their shared
      members and containers of them carry a base-typed live view. */
  struct [[
    =welder::weld,
    =welder::weld_as("M2EventTrack"),
    WOWLIB_CS_FAMILY_SURFACE
    =welder::doc(R"(
        A timestamp-only event track (every key fires). Abstract over the client version; construct a concrete
        version with M2EventTrack.ForVersion / for_version.)")
  ]] M2EventTrackBase
  {
    // The concretes default operator== — the base must be comparable too.
    bool operator==(const M2EventTrackBase&) const = default;
  };

  struct [[
    =welder::weld,
    =welder::doc("An inclusive u32 range: pre-WotLK track interpolation "
                 "ranges, sequence replay bounds.")
  ]] M2Range
  {
    [[=welder::doc("The inclusive lower bound.")]]
    std::uint32_t minimum = 0;

    [[=welder::doc("The inclusive upper bound.")]]
    std::uint32_t maximum = 0;

    bool operator==(const M2Range&) const = default;
  };
  static_assert(sizeof(M2Range) == 8);

  struct [[
    =welder::weld,
    =welder::doc("A bounding volume: axis-aligned extent plus sphere radius.")
  ]] M2Bounds
  {
    [[=welder::doc("The axis-aligned extent.")]]
    CAaBox extent{};

    [[=welder::doc("The bounding-sphere radius.")]]
    float radius = 0;

    bool operator==(const M2Bounds&) const = default;
  };
  static_assert(sizeof(M2Bounds) == 28);

  struct [[
    =welder::weld,
    =welder::doc("A global-loop entry: the timestamp a global sequence wraps at.")
  ]] M2Loop
  {
    [[=welder::doc("The timestamp the global sequence wraps at.")]]
    std::uint32_t timestamp = 0;

    bool operator==(const M2Loop&) const = default;
  };
  static_assert(sizeof(M2Loop) == 4);

  struct [[
    =welder::weld,
    =welder::doc("A quaternion compressed to i16 x, y, z, w (TBC+ bone "
                 "rotations); decompress as (v < 0 ? v + 32768 : v - 32767) / 32767.")
  ]] M2CompQuat
  {
    [[=welder::doc("The compressed x component.")]]
    std::int16_t x = 32767;

    [[=welder::doc("The compressed y component.")]]
    std::int16_t y = 32767;

    [[=welder::doc("The compressed z component.")]]
    std::int16_t z = 32767;

    [[=welder::doc("The compressed w component (identity stores 65535).")]]
    std::int16_t w = -1;  // 65535 as the client stores identity w

    bool operator==(const M2CompQuat&) const = default;
  };
  static_assert(sizeof(M2CompQuat) == 8);

  struct [[
    =welder::weld,
    =welder::doc("A model-space box: minimum and maximum corner vectors.")
  ]] M2Box
  {
    [[=welder::doc("The minimum corner.")]]
    C3Vector minimum{};

    [[=welder::doc("The maximum corner.")]]
    C3Vector maximum{};

    bool operator==(const M2Box&) const = default;
  };
  static_assert(sizeof(M2Box) == 24);

  template <typename T>
  struct [[
    =welder::weld,
    =welder::doc("A spline keyframe: the value plus incoming/outgoing tangents "
                 "(bezier/hermite camera tracks).")
  ]] M2SplineKey
  {
    [[=welder::doc("The keyframe value.")]]
    T value{};

    [[=welder::doc("The incoming tangent.")]]
    T in_tan{};

    [[=welder::doc("The outgoing tangent.")]]
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
      =welder::weld,
      =welder::doc("An animation track, pre-WotLK layout: one global timeline with "
                   "per-sequence interpolation ranges.")
    ]] M2Track<T, V> : M2TrackFamilyBase<T>
    {
      [[=welder::doc("Interpolation: 0 none, 1 linear, 2 bezier, 3 hermite "
                     "(spline types only valid for spline-key tracks).")]]
      std::uint16_t interpolation_type = 0;

      [[=welder::doc("The global sequence driving this track; -1: none.")]]
      std::uint16_t global_sequence = 0xFFFF;

      [[=welder::doc("Per-sequence [first, last] key-index ranges into the "
                     "global timeline.")]]
      std::vector<M2Range> interpolation_ranges;

      [[=welder::doc("The global timeline's keyframe timestamps.")]]
      std::vector<std::uint32_t> timestamps;

      [[
        =formats::count_matches("timestamps"),
        =welder::doc("The keyframe values, one per timestamp.")]]
      std::vector<T> values;

      // --- the version-agnostic TIMELINE surface -----------------------------
      // Keys live on TIMELINES: here (pre-WotLK) one shared global array,
      // sliced per sequence by interpolation_ranges; a global-sequence or
      // rangeless track exposes the whole array as its single timeline 0.
      // WotLK+ stores one array per sequence outright - same accessors,
      // different backing, so version-agnostic code never branches.

      [[=welder::doc("The number of timelines this track carries: one per "
                     "interpolation range, or a single timeline 0 for a "
                     "global-sequence-driven or rangeless track (0 when the "
                     "track is empty).")]]
      std::size_t timeline_count() const
      {
        if (global_sequence != 0xFFFF || interpolation_ranges.empty())
          return timestamps.empty() ? std::size_t{0} : std::size_t{1};
        return interpolation_ranges.size();
      }

      [[nodiscard]]
      [[=welder::doc("The number of keys on one timeline."),
        =welder::returns("the key count; errors when timeline is out of range")]]
      Result<std::size_t> key_count(std::size_t timeline
                                    [[=welder::doc("the timeline index")]]) const
      {
        return timeline_slice(timeline).transform([](auto s) { return s.second; });
      }

      [[nodiscard]]
      [[=welder::doc("One timeline's keyframe timestamps, as a copy."),
        =welder::returns("the timestamps; errors when timeline is out of range")]]
      Result<std::vector<std::uint32_t>> timeline_timestamps(
        std::size_t timeline [[=welder::doc("the timeline index")]]) const
      {
        return timeline_slice(timeline).transform([this](auto s) {
          const auto window = std::span(timestamps).subspan(s.first, s.second);
          return std::vector<std::uint32_t>(window.begin(), window.end());
        });
      }

      [[nodiscard]]
      [[=welder::doc("One timeline's keyframe values, as a copy."),
        =welder::returns("the values; errors when timeline is out of range")]]
      Result<std::vector<T>> timeline_values(
        std::size_t timeline [[=welder::doc("the timeline index")]]) const
      {
        return timeline_slice(timeline).transform([this](auto s) {
          const std::size_t max_first = std::min(s.first, values.size());
          const std::size_t count = std::min(s.second, values.size() - max_first);
          const auto window = std::span(values).subspan(max_first, count);
          return std::vector<T>(window.begin(), window.end());
        });
      }

    private:
      /** The [first, count) window of @a timeline into the global arrays:
          the whole array for a global-sequence/rangeless track, else the
          inclusive interpolation range clamped to the array.
          @param timeline the timeline index.
          @return the window, or InvalidEntityState past timeline_count(). */
      Result<std::pair<std::size_t, std::size_t>> timeline_slice(std::size_t timeline) const
      {
        if (timeline >= timeline_count())
          return make_error(ErrorCode::InvalidEntityState,
                            std::format("timeline {} out of range ({} timelines)",
                                        timeline, timeline_count()));
        if (global_sequence != 0xFFFF || interpolation_ranges.empty())
          return std::pair<std::size_t, std::size_t>{0, timestamps.size()};
        const M2Range& r = interpolation_ranges[timeline];
        const std::size_t first = std::min<std::size_t>(r.minimum, timestamps.size());
        const std::size_t end =
          std::min<std::size_t>(std::size_t{r.maximum} + 1, timestamps.size());
        return std::pair<std::size_t, std::size_t>{first, end < first ? 0 : end - first};
      }

    public:
      bool operator==(const M2Track&) const = default;
    };

    template <typename T, ClientVersion V>
      requires (V >= m2_per_sequence_timelines)
    struct [[
      =welder::weld,
      =welder::doc("An animation track, WotLK+ layout: one timestamp/value array per "
                   "sequence; an external sequence keeps its arrays in the .anim file.")
    ]] M2Track<T, V> : M2TrackFamilyBase<T>
    {
      [[=welder::doc("Interpolation: 0 none, 1 linear, 2 bezier, 3 hermite "
                     "(spline types only valid for spline-key tracks).")]]
      std::uint16_t interpolation_type = 0;

      [[=welder::doc("The global sequence driving this track; -1: none.")]]
      std::uint16_t global_sequence = 0xFFFF;

      [[
        =formats::sequence_data,
        =welder::doc("Keyframe timestamps, one array per sequence (an external "
                     "sequence keeps its arrays in the .anim file).")]]
      std::vector<std::vector<std::uint32_t>> timestamps;

      [[
        =formats::sequence_data,
        =formats::count_matches("timestamps"),
        =welder::doc("Keyframe values, per sequence, parallel to timestamps.")]]
      std::vector<std::vector<T>> values;

      // --- the version-agnostic TIMELINE surface -----------------------------
      // Same accessors as the pre-WotLK layout, over the native per-sequence
      // arrays - version-agnostic code never branches on the era.

      [[=welder::doc("The number of timelines this track carries: one per "
                     "sequence (a global-sequence-driven track stores a "
                     "single timeline; an external sequence's timeline is "
                     "empty until its .anim data is loaded).")]]
      std::size_t timeline_count() const { return timestamps.size(); }

      [[nodiscard]]
      [[=welder::doc("The number of keys on one timeline."),
        =welder::returns("the key count; errors when timeline is out of range")]]
      Result<std::size_t> key_count(std::size_t timeline
                                    [[=welder::doc("the timeline index")]]) const
      {
        if (timeline >= timestamps.size())
          return make_error(ErrorCode::InvalidEntityState,
                            std::format("timeline {} out of range ({} timelines)",
                                        timeline, timestamps.size()));
        return timestamps[timeline].size();
      }

      [[nodiscard]]
      [[=welder::doc("One timeline's keyframe timestamps, as a copy."),
        =welder::returns("the timestamps; errors when timeline is out of range")]]
      Result<std::vector<std::uint32_t>> timeline_timestamps(
        std::size_t timeline [[=welder::doc("the timeline index")]]) const
      {
        if (timeline >= timestamps.size())
          return make_error(ErrorCode::InvalidEntityState,
                            std::format("timeline {} out of range ({} timelines)",
                                        timeline, timestamps.size()));
        return timestamps[timeline];
      }

      [[nodiscard]]
      [[=welder::doc("One timeline's keyframe values, as a copy."),
        =welder::returns("the values; errors when timeline is out of range")]]
      Result<std::vector<T>> timeline_values(
        std::size_t timeline [[=welder::doc("the timeline index")]]) const
      {
        if (timeline >= values.size())
          return make_error(ErrorCode::InvalidEntityState,
                            std::format("timeline {} out of range ({} timelines)",
                                        timeline, values.size()));
        return values[timeline];
      }
      bool operator==(const M2Track&) const = default;
    };

    /** A timestamp-only track (event triggers: every key is an implicit "fire
        now"). Same two eras as M2Track. */
    template <ClientVersion V>
    struct M2TrackBase;

    template <ClientVersion V>
      requires (V < m2_per_sequence_timelines)
    struct [[
      =welder::weld,
      =welder::doc("A timestamp-only event track, pre-WotLK layout (every key fires).")
    ]] M2TrackBase<V> : M2EventTrackBase
    {
      [[=welder::doc("Interpolation: 0 none, 1 linear (keys fire, no value to "
                     "interpolate).")]]
      std::uint16_t interpolation_type = 0;

      [[=welder::doc("The global sequence driving this track; -1: none.")]]
      std::uint16_t global_sequence = 0xFFFF;

      [[=welder::doc("Per-sequence [first, last] key-index ranges into the "
                     "global timeline.")]]
      std::vector<M2Range> interpolation_ranges;

      [[=welder::doc("The global timeline's trigger timestamps.")]]
      std::vector<std::uint32_t> timestamps;

      // The version-agnostic TIMELINE surface (see M2Track): the event
      // track's timestamps only.

      [[=welder::doc("The number of timelines this track carries: one per "
                     "interpolation range, or a single timeline 0 for a "
                     "global-sequence-driven or rangeless track (0 when the "
                     "track is empty).")]]
      std::size_t timeline_count() const
      {
        if (global_sequence != 0xFFFF || interpolation_ranges.empty())
          return timestamps.empty() ? std::size_t{0} : std::size_t{1};
        return interpolation_ranges.size();
      }

      [[nodiscard]]
      [[=welder::doc("The number of trigger keys on one timeline."),
        =welder::returns("the key count; errors when timeline is out of range")]]
      Result<std::size_t> key_count(std::size_t timeline
                                    [[=welder::doc("the timeline index")]]) const
      {
        return timeline_slice(timeline).transform([](auto s) { return s.second; });
      }

      [[nodiscard]]
      [[=welder::doc("One timeline's trigger timestamps, as a copy."),
        =welder::returns("the timestamps; errors when timeline is out of range")]]
      Result<std::vector<std::uint32_t>> timeline_timestamps(
        std::size_t timeline [[=welder::doc("the timeline index")]]) const
      {
        return timeline_slice(timeline).transform([this](auto s) {
          const auto window = std::span(timestamps).subspan(s.first, s.second);
          return std::vector<std::uint32_t>(window.begin(), window.end());
        });
      }

    private:
      /** The [first, count) window of @a timeline into the global timeline
          (see M2Track's twin).
          @param timeline the timeline index.
          @return the window, or InvalidEntityState past timeline_count(). */
      Result<std::pair<std::size_t, std::size_t>> timeline_slice(std::size_t timeline) const
      {
        if (timeline >= timeline_count())
          return make_error(ErrorCode::InvalidEntityState,
                            std::format("timeline {} out of range ({} timelines)",
                                        timeline, timeline_count()));
        if (global_sequence != 0xFFFF || interpolation_ranges.empty())
          return std::pair<std::size_t, std::size_t>{0, timestamps.size()};
        const M2Range& r = interpolation_ranges[timeline];
        const std::size_t first = std::min<std::size_t>(r.minimum, timestamps.size());
        const std::size_t end =
          std::min<std::size_t>(std::size_t{r.maximum} + 1, timestamps.size());
        return std::pair<std::size_t, std::size_t>{first, end < first ? 0 : end - first};
      }

    public:
      bool operator==(const M2TrackBase&) const = default;
    };

    template <ClientVersion V>
      requires (V >= m2_per_sequence_timelines)
    struct [[
      =welder::weld,
      =welder::doc("A timestamp-only event track, WotLK+ layout (every key fires).")
    ]] M2TrackBase<V> : M2EventTrackBase
    {
      [[=welder::doc("Interpolation: 0 none, 1 linear (keys fire, no value to "
                     "interpolate).")]]
      std::uint16_t interpolation_type = 0;

      [[=welder::doc("The global sequence driving this track; -1: none.")]]
      std::uint16_t global_sequence = 0xFFFF;

      [[
        =formats::sequence_data,
        =welder::doc("Trigger timestamps, one array per sequence (an external "
                     "sequence keeps its arrays in the .anim file).")]]
      std::vector<std::vector<std::uint32_t>> timestamps;

      // The version-agnostic TIMELINE surface (see M2Track): the event
      // track's timestamps only, over the native per-sequence arrays.

      [[=welder::doc("The number of timelines this track carries: one per "
                     "sequence (an external sequence's timeline is empty "
                     "until its .anim data is loaded).")]]
      std::size_t timeline_count() const { return timestamps.size(); }

      [[nodiscard]]
      [[=welder::doc("The number of trigger keys on one timeline."),
        =welder::returns("the key count; errors when timeline is out of range")]]
      Result<std::size_t> key_count(std::size_t timeline
                                    [[=welder::doc("the timeline index")]]) const
      {
        if (timeline >= timestamps.size())
          return make_error(ErrorCode::InvalidEntityState,
                            std::format("timeline {} out of range ({} timelines)",
                                        timeline, timestamps.size()));
        return timestamps[timeline].size();
      }

      [[nodiscard]]
      [[=welder::doc("One timeline's trigger timestamps, as a copy."),
        =welder::returns("the timestamps; errors when timeline is out of range")]]
      Result<std::vector<std::uint32_t>> timeline_timestamps(
        std::size_t timeline [[=welder::doc("the timeline index")]]) const
      {
        if (timeline >= timestamps.size())
          return make_error(ErrorCode::InvalidEntityState,
                            std::format("timeline {} out of range ({} timelines)",
                                        timeline, timestamps.size()));
        return timestamps[timeline];
      }
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
    =welder::weld,
    =welder::doc("The header-less 'fake' animation block: sequence-independent u16 "
                 "timestamps plus keys (WotLK+ particle ramps).")
  ]] FBlock
  {
    [[=welder::doc("Sequence-independent keyframe timestamps.")]]
    std::vector<std::uint16_t> timestamps;

    [[
      =formats::count_matches("timestamps"),
      =welder::doc("The keys, one per timestamp.")]]
    std::vector<T> keys;

    bool operator==(const FBlock&) const = default;
  };

  template <typename T>
  struct [[
    =welder::weld,
    =welder::doc("A partial track: normalized fixed16 times plus values (Legion+ EXP2 "
                 "alpha cutoffs).")
  ]] M2PartTrack
  {
    [[=welder::doc("Normalized fixed16 key times.")]]
    std::vector<fixed16> times;

    [[
      =formats::count_matches("times"),
      =welder::doc("The values, one per time.")]]
    std::vector<T> values;

    bool operator==(const M2PartTrack&) const = default;
  };
}
