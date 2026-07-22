#pragma once

/** @file
    WMO version boundaries — the client versions at which the v17 WMO chunk set
    or a wire-struct layout changed. Shared by the chunk structs (chunks.hpp,
    group_chunks.hpp) and the entities (root.hpp, group.hpp) for their
    since()/until() annotations and constrained specializations. */

#include <cstdint>

#include <wowlib/core/client_version.hpp>

namespace wowlib::formats::wmo
{
  /** The WMO format version every supported client uses (MVER payload). */
  inline constexpr std::uint32_t wmo_version_v17 = 17;

  /** Cataclysm 4.0: triangle-strip data (MORB/MOTA) and shadow batches (MOBS).
      Expansion-level bound; the exact introducing build is not documented. */
  inline constexpr ClientVersion wmo_trans_batch_data{4, 0, 0, 0};

  /** WoD 6.0: the MDAL ambient override and MOPL terrain-cutting planes.
      Expansion-level bound; the exact introducing build is not documented. */
  inline constexpr ClientVersion wmo_ambient_override{6, 0, 0, 0};

  /** Legion 7.0: GFID group references, MOLS/MOLP group lights, MOPB. */
  inline constexpr ClientVersion wmo_legion{7, 0, 1, 20740};

  /** 7.0: SMOBatch's culling box gives way to a large material id. */
  inline constexpr ClientVersion wmo_batch_large_material = wmo_legion;

  /** 7.3.0.24473: MOUV texture-coordinate translation animations. */
  inline constexpr ClientVersion wmo_uv_animation{7, 3, 0, 24473};

  /** 8.1.0.28186: MOTX/MODN string blocks give way to FileDataIDs in
      MOMT/MODI/MOSI. Presence of MOTX in a file signals the legacy mode. */
  inline constexpr ClientVersion wmo_fdid_refs{8, 1, 0, 28186};

  /** 8.1.0.27826: the light-set chunks (MLSS/MLSP/MLSK/MOP2). */
  inline constexpr ClientVersion wmo_light_sets{8, 1, 0, 27826};

  /** 8.3.0.32044: ambient/particulate volume chunks (MAVG/MAVD/MBVD/MPVD),
      doodad color multipliers (MDDI) and their group refs (MPVR). */
  inline constexpr ClientVersion wmo_volumes{8, 3, 0, 32044};

  /** 9.0.1.33978: the Shadowlands root extensions — fog extra data (MFED),
      group info v2 (MGI2), new lights (MNLD), detail doodads (MDDL) and the
      group-side reference chunks (MAVR/MBVR/MFVR/MNLR), plus MOVX indices. */
  inline constexpr ClientVersion wmo_sl_extensions{9, 0, 1, 33978};

  /** 9.1.0.39015: MOLV light extensions to MOLT. */
  inline constexpr ClientVersion wmo_light_extensions{9, 1, 0, 39015};

  /** 9.2.0: the unused u32 at MOGP+0x40 becomes the two split-group indices. */
  inline constexpr ClientVersion wmo_split_groups{9, 2, 0, 42423};

  /** 10.0.0.46181: per-polygon ground-type queries (MOGX/MPY2/MOQG). */
  inline constexpr ClientVersion wmo_query_faces{10, 0, 0, 46181};

  /** 11.0.0.54210: M3 materials (MOM3) overriding MOMT when present. */
  inline constexpr ClientVersion wmo_m3_materials{11, 0, 0, 54210};

  /** 11.1.0.58221: portal extras (MOPE). */
  inline constexpr ClientVersion wmo_portal_extras{11, 1, 0, 58221};
}
