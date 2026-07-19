#pragma once

/** @file
    WMO wire structs — the exact on-disk record layouts of the v17 root and
    group chunks, transcribed from wowdev.wiki/WMO. Trivially copyable by
    design: array chunks memcpy straight into vectors of these.

    Client bitfields are flattened to plain integers plus mask constants so the
    layouts stay memcpy-exact and weld cleanly. Version-dependent layouts are
    constrained partial specializations on the ClientVersion NTTP; everything
    stable across our supported range stays a plain struct. */

#include <cstdint>

#include <wowlib/core/client_version.hpp>
#include <wowlib/formats/common/types.hpp>

namespace wowlib::formats::wmo
{
  /** 8.1.0.28186: MOTX/MODN string blocks give way to FileDataIDs in
      MOMT/MODI/MOSI. Presence of MOTX in a file signals the legacy mode. */
  inline constexpr ClientVersion wmo_fdid_refs{8, 1, 0, 28186};

  /** 9.2.0: the unused u32 at MOGP+0x40 becomes the two split-group indices. */
  inline constexpr ClientVersion wmo_split_groups{9, 2, 0, 42423};

  /** 7.0: SMOBatch's culling box gives way to a large material id. */
  inline constexpr ClientVersion wmo_batch_large_material{7, 0, 1, 20740};

  // --- MOHD -------------------------------------------------------------------

  /** Root header flags (SMOHeader::flags). */
  namespace header_flags
  {
    inline constexpr std::uint16_t do_not_attenuate_vertices = 0x1;
    inline constexpr std::uint16_t use_unified_render_path = 0x2;
    inline constexpr std::uint16_t use_liquid_type_dbc_id = 0x4;
    inline constexpr std::uint16_t do_not_fix_vertex_color_alpha = 0x8;
    inline constexpr std::uint16_t lod = 0x10;
    inline constexpr std::uint16_t default_max_lod = 0x20;
  }

  struct SMOHeader
  {
    std::uint32_t n_textures = 0;
    std::uint32_t n_groups = 0;
    std::uint32_t n_portals = 0;
    std::uint32_t n_lights = 0;
    std::uint32_t n_doodad_names = 0;
    std::uint32_t n_doodad_defs = 0;
    std::uint32_t n_doodad_sets = 0;
    CArgb ambient_color{};
    std::uint32_t wmo_id = 0;      /**< Foreign key into WMOAreaTable::m_WMOID. */
    CAaBox bounding_box{};
    std::uint16_t flags = 0;       /**< header_flags:: masks. */
    std::uint16_t num_lod = 0;     /**< Legion+; zero before. */
  };
  static_assert(sizeof(SMOHeader) == 0x40);

  // --- MOMT -------------------------------------------------------------------

  /** Material flags (SMOMaterial::flags). */
  namespace material_flags
  {
    inline constexpr std::uint32_t unlit = 0x1;
    inline constexpr std::uint32_t unfogged = 0x2;
    inline constexpr std::uint32_t two_sided = 0x4;
    inline constexpr std::uint32_t ext_light = 0x8;
    inline constexpr std::uint32_t sidn = 0x10;
    inline constexpr std::uint32_t window = 0x20;
    inline constexpr std::uint32_t clamp_s = 0x40;
    inline constexpr std::uint32_t clamp_t = 0x80;
  }

  /** One material, 64 bytes. Layout is stable across v17; what changed in
      8.1.0.28186 is the meaning of texture_1/2/3 — MOTX offsets before,
      FileDataIDs after (see wmo_fdid_refs). */
  struct SMOMaterial
  {
    std::uint32_t flags = 0;            /**< material_flags:: masks. */
    std::uint32_t shader = 0;           /**< Index into the client's WMO shader table. */
    std::uint32_t blend_mode = 0;       /**< EGxBlend value. */
    std::uint32_t texture_1 = 0;        /**< MOTX offset or FileDataID. */
    CImVector sidn_color{};             /**< Self-illuminated (night glow) color. */
    CImVector frame_sidn_color{};       /**< Runtime slot; zero on disk. */
    std::uint32_t texture_2 = 0;        /**< MOTX offset or FileDataID. */
    CImVector diff_color{};
    std::uint32_t ground_type = 0;      /**< Foreign key into TerrainType. */
    std::uint32_t texture_3 = 0;        /**< MOTX offset or FileDataID. */
    std::uint32_t color_2 = 0;
    std::uint32_t flags_2 = 0;
    std::uint32_t run_time_data[4]{};   /**< Nulled on load; shader-23 texture ids. */
  };
  static_assert(sizeof(SMOMaterial) == 0x40);

  // --- MOGI -------------------------------------------------------------------

  struct SMOGroupInfo
  {
    std::uint32_t flags = 0;        /**< Same group_flags:: values as MOGP. */
    CAaBox bounding_box{};
    std::int32_t name_offset = -1;  /**< Into MOGN, -1 for no name. */
  };
  static_assert(sizeof(SMOGroupInfo) == 0x20);

  // --- MOPT / MOPR / MOVB -----------------------------------------------------

  struct SMOPortal
  {
    std::uint16_t start_vertex = 0;  /**< Into MOPV. */
    std::uint16_t count = 0;
    C4Plane plane{};
  };
  static_assert(sizeof(SMOPortal) == 0x14);

  struct SMOPortalRef
  {
    std::uint16_t portal_index = 0;  /**< Into MOPT. */
    std::uint16_t group_index = 0;   /**< The group on the other side. */
    std::int16_t side = 0;           /**< Positive or negative side of the plane. */
    std::uint16_t filler = 0;
  };
  static_assert(sizeof(SMOPortalRef) == 0x8);

  struct SMOVisibleBlock
  {
    std::uint16_t first_vertex = 0;  /**< Into MOVV. */
    std::uint16_t count = 0;
  };
  static_assert(sizeof(SMOVisibleBlock) == 0x4);

  // --- MOLT -------------------------------------------------------------------

  /** SMOLight::type values. */
  enum class LightType : std::uint8_t
  {
    Omni = 0,
    Spot = 1,
    Direct = 2,
    Ambient = 3
  };

  struct SMOLight
  {
    std::uint8_t type = 0;      /**< A LightType value. */
    std::uint8_t use_atten = 0;
    std::uint8_t pad[2]{};
    CImVector color{};
    C3Vector position{};
    float intensity = 0;
    C4Quaternion rotation{};    /**< Spot/direct lights only. */
    float atten_start = 0;
    float atten_end = 0;
  };
  static_assert(sizeof(SMOLight) == 0x30);

  // --- MODS / MODD ------------------------------------------------------------

  struct SMODoodadSet
  {
    char name[20]{};                /**< Informational set name, zero-padded. */
    std::uint32_t start_index = 0;  /**< First doodad instance in MODD. */
    std::uint32_t count = 0;
    char pad[4]{};
  };
  static_assert(sizeof(SMODoodadSet) == 0x20);

  /** SMODoodadDef::name_and_flags masks: low 24 bits are the MODN byte offset
      (or MODI index), the high byte carries the flags. */
  namespace doodad_flags
  {
    inline constexpr std::uint32_t name_mask = 0x00FF'FFFF;
    inline constexpr std::uint32_t accept_proj_tex = 0x0100'0000;
    inline constexpr std::uint32_t interior_lighting = 0x0200'0000;
  }

  struct SMODoodadDef
  {
    /** 24-bit MODN offset / MODI index plus doodad_flags:: bits. */
    std::uint32_t name_and_flags = 0;
    C3Vector position{};        /**< WMO model space (Z-up). */
    C4Quaternion orientation{};
    float scale = 1;
    CImVector color{};          /**< BGRA; A < 0xFF is a MOLT index override. */

    constexpr std::uint32_t name_index() const
    {
      return name_and_flags & doodad_flags::name_mask;
    }
  };
  static_assert(sizeof(SMODoodadDef) == 0x28);

  // --- MFOG -------------------------------------------------------------------

  struct SMOFog
  {
    struct Fog
    {
      float end = 0;
      float start_scalar = 0;  /**< Start = end * start_scalar. */
      CImVector color{};
    };

    std::uint32_t flags = 0;        /**< 0x1: infinite radius (interior/exterior blend). */
    C3Vector position{};
    float smaller_radius = 0;
    float larger_radius = 0;
    Fog fog{};        /**< Regular fog. */
    Fog under_water_fog{};
  };
  static_assert(sizeof(SMOFog) == 0x30);

  // --- MOGP header ------------------------------------------------------------

  /** Group flags (MOGP/MOGI flags). */
  namespace group_flags
  {
    inline constexpr std::uint32_t has_bsp = 0x1;
    inline constexpr std::uint32_t has_light_map = 0x2;
    inline constexpr std::uint32_t has_vertex_colors = 0x4;
    inline constexpr std::uint32_t exterior = 0x8;
    inline constexpr std::uint32_t exterior_lit = 0x40;
    inline constexpr std::uint32_t unreachable = 0x80;
    inline constexpr std::uint32_t show_exterior_sky = 0x100;
    inline constexpr std::uint32_t has_lights = 0x200;
    inline constexpr std::uint32_t lod = 0x400;
    inline constexpr std::uint32_t has_doodads = 0x800;
    inline constexpr std::uint32_t has_liquid = 0x1000;
    inline constexpr std::uint32_t interior = 0x2000;
    inline constexpr std::uint32_t always_draw = 0x10000;
    inline constexpr std::uint32_t show_skybox = 0x40000;
    inline constexpr std::uint32_t ocean = 0x80000;
    inline constexpr std::uint32_t mount_allowed = 0x200000;
    inline constexpr std::uint32_t has_two_mocv = 0x1000000;
    inline constexpr std::uint32_t has_two_motv = 0x2000000;
    inline constexpr std::uint32_t antiportal = 0x4000000;
    inline constexpr std::uint32_t has_three_motv = 0x40000000;
  }

  /** The 0x44-byte header leading the MOGP container payload. The final u32
      slot is unused up to 9.1.5 and becomes the split-group indices after. */
  template <ClientVersion V>
  struct SMOGroupHeader;

  template <ClientVersion V>
    requires(V < wmo_split_groups)
  struct SMOGroupHeader<V>
  {
    std::uint32_t group_name = 0;              /**< Offset into MOGN. */
    std::uint32_t descriptive_group_name = 0;  /**< Offset into MOGN. */
    std::uint32_t flags = 0;                   /**< group_flags:: masks. */
    CAaBox bounding_box{};
    std::uint16_t portal_start = 0;            /**< Into MOPR. */
    std::uint16_t portal_count = 0;
    std::uint16_t trans_batch_count = 0;
    std::uint16_t int_batch_count = 0;
    std::uint16_t ext_batch_count = 0;
    std::uint16_t batch_type_d = 0;            /**< Probably padding. */
    std::uint8_t fog_ids[4]{};                 /**< Into MFOG. */
    std::uint32_t group_liquid = 0;            /**< See MLIQ. */
    std::uint32_t unique_id = 0;               /**< Foreign key into WMOAreaTable. */
    std::uint32_t flags2 = 0;
    std::uint32_t unused = 0;
  };

  template <ClientVersion V>
    requires(V >= wmo_split_groups)
  struct SMOGroupHeader<V>
  {
    std::uint32_t group_name = 0;
    std::uint32_t descriptive_group_name = 0;
    std::uint32_t flags = 0;
    CAaBox bounding_box{};
    std::uint16_t portal_start = 0;
    std::uint16_t portal_count = 0;
    std::uint16_t trans_batch_count = 0;
    std::uint16_t int_batch_count = 0;
    std::uint16_t ext_batch_count = 0;
    std::uint16_t batch_type_d = 0;
    std::uint8_t fog_ids[4]{};
    std::uint32_t group_liquid = 0;
    std::uint32_t unique_id = 0;
    std::uint32_t flags2 = 0;
    std::int16_t parent_or_first_child_split_group_index = -1;
    std::int16_t next_split_child_group_index = -1;
  };

  static_assert(sizeof(SMOGroupHeader<versions::wotlk>) == 0x44);
  static_assert(sizeof(SMOGroupHeader<versions::shadowlands>) == 0x44);

  // --- MOPY / MOBA / MOBN -----------------------------------------------------

  /** Triangle flags (SMOPoly::flags). */
  namespace poly_flags
  {
    inline constexpr std::uint8_t transition = 0x1;
    inline constexpr std::uint8_t no_cam_collide = 0x2;
    inline constexpr std::uint8_t detail = 0x4;
    inline constexpr std::uint8_t collision = 0x8;
    inline constexpr std::uint8_t hint = 0x10;
    inline constexpr std::uint8_t render = 0x20;
    inline constexpr std::uint8_t cull_objects = 0x40;
    inline constexpr std::uint8_t collide_hit = 0x80;
  }

  struct SMOPoly
  {
    std::uint8_t flags = 0;        /**< poly_flags:: masks. */
    std::uint8_t material_id = 0;  /**< Into MOMT; 0xFF for collision-only faces. */
  };
  static_assert(sizeof(SMOPoly) == 0x2);

  /** One render batch, 24 bytes. Before 7.0 the leading 12 bytes are an int16
      culling box; from 7.0 they are unused except for a uint16 material id
      backing the flag_use_material_id_large bit. */
  template <ClientVersion V>
  struct SMOBatch;

  template <ClientVersion V>
    requires(V < wmo_batch_large_material)
  struct SMOBatch<V>
  {
    std::int16_t box_min[3]{};       /**< Low-resolution culling box. */
    std::int16_t box_max[3]{};
    std::uint32_t start_index = 0;   /**< First face index in MOVI. */
    std::uint16_t count = 0;         /**< MOVI indices used. */
    std::uint16_t min_index = 0;     /**< First vertex used in MOVT. */
    std::uint16_t max_index = 0;     /**< Last vertex used, inclusive. */
    std::uint8_t flags = 0;
    std::uint8_t material_id = 0;    /**< Into MOMT. */
  };

  template <ClientVersion V>
    requires(V >= wmo_batch_large_material)
  struct SMOBatch<V>
  {
    std::uint8_t unknown[10]{};              /**< The nulled remains of the culling box. */
    std::uint16_t material_id_large = 0;     /**< Used when flags has 0x2. */
    std::uint32_t start_index = 0;
    std::uint16_t count = 0;
    std::uint16_t min_index = 0;
    std::uint16_t max_index = 0;
    std::uint8_t flags = 0;                  /**< 0x2: use material_id_large. */
    std::uint8_t material_id = 0;
  };

  static_assert(sizeof(SMOBatch<versions::wotlk>) == 0x18);
  static_assert(sizeof(SMOBatch<versions::shadowlands>) == 0x18);

  /** One BSP tree node (MOBN), 16 bytes. */
  struct CAaBspNode
  {
    std::uint16_t flags = 0;      /**< 0-2: split axis, 0x4: leaf. */
    std::int16_t neg_child = -1;  /**< Node index, -1 for none. */
    std::int16_t pos_child = -1;
    std::uint16_t n_faces = 0;    /**< Face count in MOBR. */
    std::uint32_t face_start = 0; /**< First face index in MOBR. */
    float plane_dist = 0;
  };
  static_assert(sizeof(CAaBspNode) == 0x10);
}
