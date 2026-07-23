#pragma once

/** @file
    WMO version pivots that change a wire-struct *layout* (not just chunk
    presence). These name the client version at which a struct's field set flips,
    so the wire structs (the group/chunks headers) can select a constrained
    partial specialization with `requires (V < pivot)` / `requires (V >= pivot)`.

    Chunk *presence* boundaries are no longer named here: each chunk member's
    since()/until() annotation now carries its own exact client version inline (as
    wowdev.wiki documents it), instead of grouping chunks under a shared,
    guessed-at "feature set" constant. */

#include <cstdint>

#include <wowlib/core/client_version.hpp>

namespace wowlib::formats::wmo
{
  /** The WMO format version every supported client uses (MVER payload). */
  inline constexpr std::uint32_t wmo_version_v17 = 17;

  /** Legion 7.0.1.20740: SMOBatch's culling-box prelude gives way to a large
      (uint16) material id. A layout pivot for SMOBatch. */
  inline constexpr ClientVersion wmo_batch_large_material{7, 0, 1, 20740};

  /** 9.2.0.42423: the unused u32 at MOGP+0x40 becomes the two split-group
      indices. A layout pivot for SMOGroupHeader. */
  inline constexpr ClientVersion wmo_split_groups{9, 2, 0, 42423};
}
