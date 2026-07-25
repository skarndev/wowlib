#pragma once

/** @file
    M2 animation-sequence records (namespace wowlib::formats::m2::root::record):
    M2Sequence across its three layout eras, the sequence flags, and the
    pre-WotLK playable-animation fallback entry. All trivially copyable —
    they memcpy straight out of their blocks. */

#include <cstdint>

#include <welder/vocabulary.hpp>

#include <wowlib/core/client_version.hpp>
#include <wowlib/formats/common/flags.hpp>
#include <wowlib/formats/m2/boundaries.hpp>
#include <wowlib/formats/m2/root/record/track.hpp>

namespace wowlib::formats::m2::root::record
{
  enum class [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::doc("M2Sequence flags — the 0x20/0x40/0x130 combination decides "
                 "where the sequence's track data lives (.m2 vs .anim file).")
  ]] SequenceFlags : std::uint32_t
  {
    SetsRuntimeBlend [[=welder::doc("Sets 0x80 when loaded (M2Init).")]] = 0x1,
    TiltIn [[=welder::doc("Model tilts over X/Y by the end of the animation.")]] = 0x2,
    TiltOut [[=welder::doc("Model starts tilted and returns upright.")]] = 0x4,
    TiltFixed [[=welder::doc("Model stays tilted for the whole animation.")]] = 0x8,
    LoadedLowPriority [[=welder::doc("Set at runtime for loaded low-priority sequences.")]] = 0x10,
    DataInM2 [[=welder::doc("Primary bone sequence: track data is in the .m2 "
                            "itself, not an .anim file.")]] = 0x20,
    Alias [[=welder::doc("The sequence is an alias: follow alias_next until an "
                         "entry without this flag owns the data.")]] = 0x40,
    Blended [[=welder::doc("Blend (lerp) into/out of this sequence.")]] = 0x80,
    StoredInModel [[=welder::doc("Sequence stored in the model (0x100).")]] = 0x100,
    SplitBlendTimes [[=welder::doc("blend_time is the in/out pair, not one u32.")]] = 0x200,
    Legion0x800 [[=welder::doc("Seen in Legion 24500 models.")]] = 0x800
  };

  /** Whether a WotLK+ sequence keeps its track data in an external .anim
      file — the client loads one iff none of 0x10/0x20/0x100 are set.
      @param flags the sequence's flags value.
      @return true when the data lives in "%s%04d-%02d.anim". */
  constexpr bool sequence_data_external(std::uint32_t flags)
  {
    return (flags & 0x130u) == 0;
  }

  namespace detail
  {
    // The annotated era layouts; instantiate through the canonicalizing
    // aliases below, never directly.
    template <ClientVersion V>
    struct M2Sequence;

    template <ClientVersion V>
      requires (V < m2_per_sequence_timelines)
    struct [[
      =welder::weld(welder::lang::py, welder::lang::lua),
      =welder::doc("An animation sequence, pre-WotLK layout: global start/end timestamps, "
                   "one u32 blend time.")
    ]] M2Sequence<V>
    {
      [[=welder::doc("Animation id in AnimationData.dbc.")]]
      std::uint16_t id = 0;
      [[=welder::doc("Which variation of the animation this is.")]]
      std::uint16_t variation_index = 0;
      [[=welder::doc("Slice start on the global timeline.")]]
      std::uint32_t start_timestamp = 0;
      [[=welder::doc("Slice end on the global timeline.")]]
      std::uint32_t end_timestamp = 0;
      [[=welder::doc("Character move speed during the sequence.")]]
      float movespeed = 0;
      [[=welder::doc("See SequenceFlags.")]]
      std::uint32_t flags = 0;
      [[=welder::doc("Playback probability weight (sums to 0x7FFF per id).")]]
      std::int16_t frequency = 0;
      [[=welder::doc("Unused alignment padding.")]]
      std::uint16_t padding = 0;
      [[=welder::doc("Random repetition bounds; both 0 = no repeat.")]]
      M2Range replay{};
      [[=welder::doc("Transition blend duration, milliseconds.")]]
      std::uint32_t blend_time = 0;
      [[=welder::doc("Bounds while the sequence plays.")]]
      M2Bounds bounds{};
      [[=welder::doc("Next variation index, -1 if none.")]]
      std::int16_t variation_next = -1;
      [[=welder::doc("Alias target (flags & 0x40 chains).")]]
      std::uint16_t alias_next = 0;

      [[=welder::doc("Whether this sequence is an alias (flag 0x40): it owns "
                     "NO track data — resolve it by following alias_next. "
                     "Files still carry stale array records for aliases "
                     "(dead offsets, junk values), so they are never "
                     "chased.")]]
      constexpr bool is_alias() const { return (flags & 0x40u) != 0; }

      [[=welder::doc("Whether this sequence owns an external .anim file: its "
                     "data is low-priority (0x10/0x20/0x100 all clear) and "
                     "it is no alias.")]]
      constexpr bool owns_anim_file() const
      {
        return sequence_data_external(flags) && !is_alias();
      }

      bool operator==(const M2Sequence&) const = default;
    };

    template <ClientVersion V>
      requires (V >= m2_per_sequence_timelines && V < m2_split_blend_times)
    struct [[
      =welder::weld(welder::lang::py, welder::lang::lua),
      =welder::doc("An animation sequence, WotLK through MoP layout: an own duration, one "
                   "u32 blend time.")
    ]] M2Sequence<V>
    {
      [[=welder::doc("Animation id in AnimationData.dbc.")]]
      std::uint16_t id = 0;
      [[=welder::doc("Which variation of the animation this is.")]]
      std::uint16_t variation_index = 0;
      [[=welder::doc("Sequence length, milliseconds.")]]
      std::uint32_t duration = 0;
      [[=welder::doc("Character move speed during the sequence.")]]
      float movespeed = 0;
      [[=welder::doc("See SequenceFlags.")]]
      std::uint32_t flags = 0;
      [[=welder::doc("Playback probability weight (sums to 0x7FFF per id).")]]
      std::int16_t frequency = 0;
      [[=welder::doc("Unused alignment padding.")]]
      std::uint16_t padding = 0;
      [[=welder::doc("Random repetition bounds; both 0 = no repeat.")]]
      M2Range replay{};
      [[=welder::doc("Transition blend duration, milliseconds.")]]
      std::uint32_t blend_time = 0;
      [[=welder::doc("Bounds while the sequence plays.")]]
      M2Bounds bounds{};
      [[=welder::doc("Next variation index, -1 if none.")]]
      std::int16_t variation_next = -1;
      [[=welder::doc("Alias target (flags & 0x40 chains).")]]
      std::uint16_t alias_next = 0;

      [[=welder::doc("Whether this sequence is an alias (flag 0x40): it owns "
                     "NO track data — resolve it by following alias_next. "
                     "Files still carry stale array records for aliases "
                     "(dead offsets, junk values), so they are never "
                     "chased.")]]
      constexpr bool is_alias() const { return (flags & 0x40u) != 0; }

      [[=welder::doc("Whether this sequence owns an external .anim file: its "
                     "data is low-priority (0x10/0x20/0x100 all clear) and "
                     "it is no alias.")]]
      constexpr bool owns_anim_file() const
      {
        return sequence_data_external(flags) && !is_alias();
      }

      bool operator==(const M2Sequence&) const = default;
    };

    template <ClientVersion V>
      requires (V >= m2_split_blend_times)
    struct [[
      =welder::weld(welder::lang::py, welder::lang::lua),
      =welder::doc("An animation sequence, WoD+ layout: an own duration, split "
                   "blend-in/out times.")
    ]] M2Sequence<V>
    {
      [[=welder::doc("Animation id in AnimationData.dbc.")]]
      std::uint16_t id = 0;
      [[=welder::doc("Which variation of the animation this is.")]]
      std::uint16_t variation_index = 0;
      [[=welder::doc("Sequence length, milliseconds.")]]
      std::uint32_t duration = 0;
      [[=welder::doc("Character move speed during the sequence.")]]
      float movespeed = 0;
      [[=welder::doc("See SequenceFlags.")]]
      std::uint32_t flags = 0;
      [[=welder::doc("Playback probability weight (sums to 0x7FFF per id).")]]
      std::int16_t frequency = 0;
      [[=welder::doc("Unused alignment padding.")]]
      std::uint16_t padding = 0;
      [[=welder::doc("Random repetition bounds; both 0 = no repeat.")]]
      M2Range replay{};
      [[=welder::doc("Blend-in duration, milliseconds.")]]
      std::uint16_t blend_time_in = 0;
      [[=welder::doc("Blend-out duration, milliseconds.")]]
      std::uint16_t blend_time_out = 0;
      [[=welder::doc("Bounds while the sequence plays.")]]
      M2Bounds bounds{};
      [[=welder::doc("Next variation index, -1 if none.")]]
      std::int16_t variation_next = -1;
      [[=welder::doc("Alias target (flags & 0x40 chains).")]]
      std::uint16_t alias_next = 0;

      [[=welder::doc("Whether this sequence is an alias (flag 0x40): it owns "
                     "NO track data — resolve it by following alias_next. "
                     "Files still carry stale array records for aliases "
                     "(dead offsets, junk values), so they are never "
                     "chased.")]]
      constexpr bool is_alias() const { return (flags & 0x40u) != 0; }

      [[=welder::doc("Whether this sequence owns an external .anim file: its "
                     "data is low-priority (0x10/0x20/0x100 all clear) and "
                     "it is no alias.")]]
      constexpr bool owns_anim_file() const
      {
        return sequence_data_external(flags) && !is_alias();
      }

      bool operator==(const M2Sequence&) const = default;
    };
  }

  /** An animation sequence — the canonicalizing face of detail::M2Sequence:
      every client version maps to its range's first grid version
      (m2_sequence_pivots: WotLK's duration layout, WoD's split blend times),
      so one instantiation serves the whole range. */
  template <ClientVersion V>
  using M2Sequence =
    detail::M2Sequence<canonical_version(V, m2_sequence_pivots, m2_versions)>;

  struct [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::doc("A pre-WotLK playable-animation fallback: the substitute "
                 "animation id plus how to play it (0 normal, 1 backwards, "
                 "2 frame-by-frame, 3 freeze).")
  ]] M2SequenceFallback
  {
    [[=welder::doc("Substitute animation id in AnimationData.dbc.")]]
    std::int16_t fallback_animation_id = 0;
    [[=welder::doc("Playback mode: 0 normal, 1 backwards, 2 frame-by-frame, "
                   "3 freeze.")]]
    std::int16_t flags = 0;

    bool operator==(const M2SequenceFallback&) const = default;
  };

  static_assert(sizeof(M2Sequence<versions::vanilla>) == 68);
  static_assert(sizeof(M2Sequence<versions::wotlk>) == 64);
  static_assert(sizeof(M2Sequence<versions::wod>) == 64);
  static_assert(sizeof(M2SequenceFallback) == 4);
}
