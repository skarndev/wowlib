#pragma once

/** @file
    WMO wire structs — the exact on-disk record layouts of the v17 root and
    group chunks, transcribed from wowdev.wiki/WMO. Trivially copyable by
    design: array chunks memcpy straight into vectors of these.

    Client bitfields are flattened to plain integers plus mask constants so the
    layouts stay memcpy-exact and weld cleanly; fixed-size sequences are
    std::array for the same reason (layouts guarded by the size asserts).
    Version-dependent layouts are constrained partial specializations on the
    ClientVersion NTTP; everything stable across our supported range stays a
    plain struct. */

#include <algorithm>
#include <array>
#include <cstdint>
#include <string_view>

#include <welder/vocabulary.hpp>

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

  struct
  [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::doc("The MOHD root header: entity counts, ambient color, bounds and "
                 "root-wide flags.")
  ]]
  SMOHeader
  {
    [[=welder::doc("Number of textures referenced by the materials.")]]
    std::uint32_t n_textures = 0;

    [[=welder::doc("Number of group files belonging to this WMO.")]]
    std::uint32_t n_groups = 0;

    [[=welder::doc("Number of portals (MOPT).")]]
    std::uint32_t n_portals = 0;

    [[=welder::doc("Number of lights (MOLT).")]]
    std::uint32_t n_lights = 0;

    [[=welder::doc("Number of doodad filenames (MODN/MODI).")]]
    std::uint32_t n_doodad_names = 0;

    [[=welder::doc("Number of doodad placements (MODD).")]]
    std::uint32_t n_doodad_defs = 0;

    [[=welder::doc("Number of doodad sets (MODS).")]]
    std::uint32_t n_doodad_sets = 0;

    [[=welder::doc("Base ambient color.")]]
    CArgb ambient_color{};

    [[=welder::doc("Foreign key into WMOAreaTable (m_WMOID).")]]
    std::uint32_t wmo_id = 0;

    [[=welder::doc("Bounding box of the whole object in model space.")]]
    CAaBox bounding_box{};

    [[=welder::doc("Root-wide flags; header_flags masks.")]]
    std::uint16_t flags = 0;

    [[=welder::doc("Number of LOD levels including the base (Legion+; zero before).")]]
    std::uint16_t num_lod = 0;
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

  struct
  [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::doc(R"(
        One MOMT material, 64 bytes. The layout is stable across v17; what
        changed in 8.1 is the meaning of the texture fields - MOTX byte offsets
        before (and in the fallback mode signalled by MOTX's presence),
        FileDataIDs after.)")
  ]]
  SMOMaterial
  {
    [[=welder::doc("Render flags; material_flags masks.")]]
    std::uint32_t flags = 0;

    [[=welder::doc("Index into the client's WMO shader table.")]]
    std::uint32_t shader = 0;

    [[=welder::doc("Blending mode (EGxBlend).")]]
    std::uint32_t blend_mode = 0;

    [[=welder::doc("First texture: MOTX offset or FileDataID.")]]
    std::uint32_t texture_1 = 0;

    [[=welder::doc("Self-illuminated (night glow) color.")]]
    CImVector sidn_color{};

    [[=welder::doc("Runtime sidn slot; zero on disk.")]]
    CImVector frame_sidn_color{};

    [[=welder::doc("Second texture: MOTX offset or FileDataID.")]]
    std::uint32_t texture_2 = 0;

    [[=welder::doc("Diffuse color.")]]
    CImVector diff_color{};

    [[=welder::doc("Foreign key into TerrainType.")]]
    std::uint32_t ground_type = 0;

    [[=welder::doc("Third texture: MOTX offset or FileDataID.")]]
    std::uint32_t texture_3 = 0;

    [[=welder::doc("Extra color slot (a texture FileDataID for shader 23).")]]
    std::uint32_t color_2 = 0;

    [[=welder::doc("Extra flags slot (a texture FileDataID for shader 23).")]]
    std::uint32_t flags_2 = 0;

    [[=welder::doc("Nulled on load; shader-23 texture FileDataIDs.")]]
    std::array<std::uint32_t, 4> run_time_data{};
  };
  static_assert(sizeof(SMOMaterial) == 0x40);

  // --- MOGI -------------------------------------------------------------------

  struct
  [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::doc("One MOGI entry: per-group flags, bounds and name, mirrored from "
                 "the group file's own header.")
  ]]
  SMOGroupInfo
  {
    [[=welder::doc("Group flags; the same group_flags values as MOGP.")]]
    std::uint32_t flags = 0;

    [[=welder::doc("Group bounding box.")]]
    CAaBox bounding_box{};

    [[=welder::doc("Byte offset of the group name in MOGN, -1 for no name.")]]
    std::int32_t name_offset = -1;
  };
  static_assert(sizeof(SMOGroupInfo) == 0x20);

  // --- MOPT / MOPR / MOVB -----------------------------------------------------

  struct
  [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::doc("One MOPT portal: a polygon plane separating two groups.")
  ]]
  SMOPortal
  {
    [[=welder::doc("First vertex in MOPV.")]]
    std::uint16_t start_vertex = 0;

    [[=welder::doc("Vertex count.")]]
    std::uint16_t count = 0;

    [[=welder::doc("The portal plane.")]]
    C4Plane plane{};
  };
  static_assert(sizeof(SMOPortal) == 0x14);

  struct
  [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::doc("One MOPR entry: a group's reference to a portal and the group "
                 "on the other side.")
  ]]
  SMOPortalRef
  {
    [[=welder::doc("Index into MOPT.")]]
    std::uint16_t portal_index = 0;

    [[=welder::doc("The group on the other side of the portal.")]]
    std::uint16_t group_index = 0;

    [[=welder::doc("Which side of the portal plane this reference looks from.")]]
    std::int16_t side = 0;

    std::uint16_t filler = 0;
  };
  static_assert(sizeof(SMOPortalRef) == 0x8);

  struct
  [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::doc("One MOVB visible block: a vertex range in MOVV.")
  ]]
  SMOVisibleBlock
  {
    [[=welder::doc("First vertex in MOVV.")]]
    std::uint16_t first_vertex = 0;

    [[=welder::doc("Vertex count.")]]
    std::uint16_t count = 0;
  };
  static_assert(sizeof(SMOVisibleBlock) == 0x4);

  // --- MOLT -------------------------------------------------------------------

  /** SMOLight::type values. */
  enum class
  [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::doc("The kind of a WMO light (SMOLight.type).")
  ]]
  LightType : std::uint8_t
  {
    Omni = 0,
    Spot = 1,
    Direct = 2,
    Ambient = 3
  };

  struct
  [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::doc("One MOLT light. Not rendered by classic-era clients, but baked "
                 "into vertex colors by the exporter.")
  ]]
  SMOLight
  {
    [[=welder::doc("The light kind; a LightType value.")]]
    std::uint8_t type = 0;

    [[=welder::doc("Whether attenuation applies.")]]
    std::uint8_t use_atten = 0;

    std::array<std::uint8_t, 2> pad{};

    [[=welder::doc("Light color.")]]
    CImVector color{};

    [[=welder::doc("Position in model space.")]]
    C3Vector position{};

    [[=welder::doc("Intensity.")]]
    float intensity = 0;

    [[=welder::doc("Orientation; spot/direct lights only.")]]
    C4Quaternion rotation{};

    [[=welder::doc("Attenuation start distance.")]]
    float atten_start = 0;

    [[=welder::doc("Attenuation end distance.")]]
    float atten_end = 0;
  };
  static_assert(sizeof(SMOLight) == 0x30);

  // --- MODS / MODD ------------------------------------------------------------

  struct
  [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::doc(R"(
        One MODS doodad set: a named range of doodad placements. Set 0
        ("Set_$DefaultGlobal") is additive and always shown; other sets are
        exclusive alternatives.)")
  ]]
  SMODoodadSet
  {
    [[=welder::mark::exclude]] std::array<char, 20> name_bytes{};

    [[=welder::doc("First doodad instance in MODD.")]]
    std::uint32_t start_index = 0;

    [[=welder::doc("Number of doodad instances in the set.")]]
    std::uint32_t count = 0;

    [[=welder::mark::exclude]] std::array<char, 4> pad{};

    [[=welder::getter("name"),
      =welder::doc("The informational set name.")]]
    std::string_view name() const
    {
      const auto end = std::find(name_bytes.begin(), name_bytes.end(), '\0');
      return {name_bytes.data(), static_cast<std::size_t>(end - name_bytes.begin())};
    }
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

  struct
  [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::doc("One MODD doodad placement: an M2 instance inside the WMO, "
                 "quaternion-oriented in model space.")
  ]]
  SMODoodadDef
  {
    [[=welder::doc("Packed 24-bit MODN offset / MODI index plus doodad flag bits "
                   "(doodad_flags masks).")]]
    std::uint32_t name_and_flags = 0;

    [[=welder::doc("Position in WMO model space (Z-up).")]]
    C3Vector position{};

    [[=welder::doc("Orientation quaternion.")]]
    C4Quaternion orientation{};

    [[=welder::doc("Uniform scale factor.")]]
    float scale = 1;

    [[=welder::doc("Color override (BGRA); alpha below 0xFF is a MOLT index.")]]
    CImVector color{};

    [[=welder::getter,
      =welder::doc("The 24-bit MODN byte offset (or MODI index) of the model name.")]]
    constexpr std::uint32_t name_index() const
    {
      return name_and_flags & doodad_flags::name_mask;
    }
  };
  static_assert(sizeof(SMODoodadDef) == 0x28);

  // --- MFOG -------------------------------------------------------------------

  struct
  [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::doc("One MFOG entry: a fog volume with regular and under-water "
                 "settings.")
  ]]
  SMOFog
  {
    struct
    [[=welder::doc("One fog band: end distance, start scalar and color.")]]
    Fog
    {
      [[=welder::doc("Distance at which visibility ceases.")]]
      float end = 0;

      [[=welder::doc("Start = end * start_scalar (0..1).")]]
      float start_scalar = 0;

      [[=welder::doc("Fog color.")]]
      CImVector color{};
    };

    [[=welder::doc("0x1: infinite radius (interior/exterior blend fog).")]]
    std::uint32_t flags = 0;

    [[=welder::doc("Fog volume center.")]]
    C3Vector position{};

    [[=welder::doc("Inner radius (full fog).")]]
    float smaller_radius = 0;

    [[=welder::doc("Outer radius (fog starts).")]]
    float larger_radius = 0;

    [[=welder::doc("The regular fog band.")]]
    Fog fog{};

    [[=welder::doc("The under-water fog band.")]]
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
  struct
  [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::doc("The MOGP group header: names, flags, bounds, portal and batch "
                 "ranges, fog and liquid.")
  ]]
  SMOGroupHeader<V>
  {
    [[=welder::doc("Byte offset of the group name in MOGN.")]]
    std::uint32_t group_name = 0;

    [[=welder::doc("Byte offset of the descriptive name in MOGN.")]]
    std::uint32_t descriptive_group_name = 0;

    [[=welder::doc("Group flags; group_flags masks.")]]
    std::uint32_t flags = 0;

    [[=welder::doc("Group bounding box.")]]
    CAaBox bounding_box{};

    [[=welder::doc("First portal reference in MOPR.")]]
    std::uint16_t portal_start = 0;

    [[=welder::doc("Portal reference count.")]]
    std::uint16_t portal_count = 0;

    [[=welder::doc("Transition batch count (MOBA prefix).")]]
    std::uint16_t trans_batch_count = 0;

    [[=welder::doc("Interior batch count.")]]
    std::uint16_t int_batch_count = 0;

    [[=welder::doc("Exterior batch count.")]]
    std::uint16_t ext_batch_count = 0;

    std::uint16_t batch_type_d = 0;

    [[=welder::doc("Fog indices into MFOG.")]]
    std::array<std::uint8_t, 4> fog_ids{};

    [[=welder::doc("Group liquid type; interpretation depends on the root "
                   "use_liquid_type_dbc_id flag.")]]
    std::uint32_t group_liquid = 0;

    [[=welder::doc("Foreign key into WMOAreaTable (m_WMOGroupID).")]]
    std::uint32_t unique_id = 0;

    [[=welder::doc("Extended flags (Cataclysm+).")]]
    std::uint32_t flags2 = 0;

    std::uint32_t unused = 0;
  };

  template <ClientVersion V>
    requires(V >= wmo_split_groups)
  struct
  [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::doc("The MOGP group header: names, flags, bounds, portal and batch "
                 "ranges, fog, liquid and split-group links (9.2+).")
  ]]
  SMOGroupHeader<V>
  {
    [[=welder::doc("Byte offset of the group name in MOGN.")]]
    std::uint32_t group_name = 0;

    [[=welder::doc("Byte offset of the descriptive name in MOGN.")]]
    std::uint32_t descriptive_group_name = 0;

    [[=welder::doc("Group flags; group_flags masks.")]]
    std::uint32_t flags = 0;

    [[=welder::doc("Group bounding box.")]]
    CAaBox bounding_box{};

    [[=welder::doc("First portal reference in MOPR.")]]
    std::uint16_t portal_start = 0;

    [[=welder::doc("Portal reference count.")]]
    std::uint16_t portal_count = 0;

    [[=welder::doc("Transition batch count (MOBA prefix).")]]
    std::uint16_t trans_batch_count = 0;

    [[=welder::doc("Interior batch count.")]]
    std::uint16_t int_batch_count = 0;

    [[=welder::doc("Exterior batch count.")]]
    std::uint16_t ext_batch_count = 0;

    std::uint16_t batch_type_d = 0;

    [[=welder::doc("Fog indices into MFOG.")]]
    std::array<std::uint8_t, 4> fog_ids{};

    [[=welder::doc("Group liquid type; interpretation depends on the root "
                   "use_liquid_type_dbc_id flag.")]]
    std::uint32_t group_liquid = 0;

    [[=welder::doc("Foreign key into WMOAreaTable (m_WMOGroupID).")]]
    std::uint32_t unique_id = 0;

    [[=welder::doc("Extended flags (Cataclysm+).")]]
    std::uint32_t flags2 = 0;

    [[=welder::doc("Parent split group, or the first child (9.2+ split groups).")]]
    std::int16_t parent_or_first_child_split_group_index = -1;

    [[=welder::doc("Next sibling in the split-group chain.")]]
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

  struct
  [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::doc("Per-triangle material info (MOPY).")
  ]]
  SMOPoly
  {
    [[=welder::doc("Triangle flags; poly_flags masks.")]]
    std::uint8_t flags = 0;

    [[=welder::doc("Index into MOMT; 0xFF for collision-only faces.")]]
    std::uint8_t material_id = 0;
  };
  static_assert(sizeof(SMOPoly) == 0x2);

  /** One render batch, 24 bytes. Before 7.0 the leading 12 bytes are an int16
      culling box; from 7.0 they are unused except for a uint16 material id
      backing the flag_use_material_id_large bit. */
  template <ClientVersion V>
  struct SMOBatch;

  template <ClientVersion V>
    requires(V < wmo_batch_large_material)
  struct
  [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::doc("One MOBA render batch: an index range sharing one material, "
                 "with its low-resolution culling box.")
  ]]
  SMOBatch<V>
  {
    [[=welder::doc("Culling box minimum corner (rounded vertex bounds).")]]
    std::array<std::int16_t, 3> box_min{};

    [[=welder::doc("Culling box maximum corner.")]]
    std::array<std::int16_t, 3> box_max{};

    [[=welder::doc("First face index in MOVI.")]]
    std::uint32_t start_index = 0;

    [[=welder::doc("Number of MOVI indices.")]]
    std::uint16_t count = 0;

    [[=welder::doc("First vertex used in MOVT.")]]
    std::uint16_t min_index = 0;

    [[=welder::doc("Last vertex used, inclusive.")]]
    std::uint16_t max_index = 0;

    [[=welder::doc("Batch flags.")]]
    std::uint8_t flags = 0;

    [[=welder::doc("Index into MOMT.")]]
    std::uint8_t material_id = 0;
  };

  template <ClientVersion V>
    requires(V >= wmo_batch_large_material)
  struct
  [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::doc("One MOBA render batch: an index range sharing one material "
                 "(Legion+ layout with the 16-bit material id).")
  ]]
  SMOBatch<V>
  {
    [[=welder::mark::exclude]] std::array<std::uint8_t, 10> unknown{};

    [[=welder::doc("16-bit material id; used when flags has 0x2.")]]
    std::uint16_t material_id_large = 0;

    [[=welder::doc("First face index in MOVI.")]]
    std::uint32_t start_index = 0;

    [[=welder::doc("Number of MOVI indices.")]]
    std::uint16_t count = 0;

    [[=welder::doc("First vertex used in MOVT.")]]
    std::uint16_t min_index = 0;

    [[=welder::doc("Last vertex used, inclusive.")]]
    std::uint16_t max_index = 0;

    [[=welder::doc("Batch flags; 0x2: use material_id_large.")]]
    std::uint8_t flags = 0;

    [[=welder::doc("Index into MOMT (when it fits 8 bits).")]]
    std::uint8_t material_id = 0;
  };

  static_assert(sizeof(SMOBatch<versions::wotlk>) == 0x18);
  static_assert(sizeof(SMOBatch<versions::shadowlands>) == 0x18);

  /** One BSP tree node (MOBN), 16 bytes. */
  struct
  [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::doc("One MOBN BSP node for collision: a split plane or a leaf "
                 "referencing faces through MOBR.")
  ]]
  CAaBspNode
  {
    [[=welder::doc("0-2: split axis, 0x4: leaf.")]]
    std::uint16_t flags = 0;

    [[=welder::doc("Negative-side child node, -1 for none.")]]
    std::int16_t neg_child = -1;

    [[=welder::doc("Positive-side child node, -1 for none.")]]
    std::int16_t pos_child = -1;

    [[=welder::doc("Face count in MOBR (leaves).")]]
    std::uint16_t n_faces = 0;

    [[=welder::doc("First face index in MOBR.")]]
    std::uint32_t face_start = 0;

    [[=welder::doc("Split plane distance from the model origin.")]]
    float plane_dist = 0;
  };
  static_assert(sizeof(CAaBspNode) == 0x10);
}
