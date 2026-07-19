#pragma once

/** @file
    WMO wire structs — the exact on-disk record layouts of the v17 root and
    group chunks, transcribed from wowdev.wiki/WMO. Trivially copyable by
    design: array chunks memcpy straight into vectors of these.

    Client bitfields are flattened to plain integers plus scoped flag enums
    (see common/flags.hpp `has_flag`) so the layouts stay memcpy-exact and weld
    cleanly; fixed-size sequences are std::array for the same reason (layouts
    guarded by the size asserts). Version-dependent layouts are constrained
    partial specializations on the ClientVersion NTTP; everything stable across
    the supported range stays a plain struct.

    Flag enumerators are documented with plain Doxygen comments: welder's doc()
    annotation covers classes/functions/members, not enumerators, so there is
    no duplication — the Doxygen comment is the single documentation slot. */

#include <algorithm>
#include <array>
#include <cstdint>
#include <string_view>
#include <utility>

#include <welder/vocabulary.hpp>

#include <wowlib/core/client_version.hpp>
#include <wowlib/formats/common/flags.hpp>
#include <wowlib/formats/common/types.hpp>

namespace wowlib::formats::wmo
{
  // --- version boundaries (chunk-set and layout changes) ----------------------

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

  // --- MOHD -------------------------------------------------------------------

  /** Root header flag bits (SMOHeader::flags); test with has_flag(). */
  enum class
  [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::doc("Root header flag bits (SMOHeader.flags).")
  ]]
  HeaderFlags : std::uint16_t
  {
    do_not_attenuate_vertices = 0x1,     /**< Do not attenuate vertices based on portal distance. */
    use_unified_render_path = 0x2,       /**< Attenuate on portal exit (unified render path). */
    use_liquid_type_dbc_id = 0x4,        /**< MLIQ ids are LiquidType foreign keys, not the legacy set. */
    do_not_fix_vertex_color_alpha = 0x8, /**< Skip the vertex-color alpha fixup (FixColorVertexAlpha). */
    lod = 0x10,                          /**< The WMO has LOD group files (Legion+). */
    default_max_lod = 0x20               /**< num_lod is defaulted, implying 3 LOD levels (BfA+). */
  };

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

    [[=welder::doc("Root-wide flags; HeaderFlags bits.")]]
    std::uint16_t flags = 0;

    [[=welder::doc("Number of LOD levels including the base (Legion+; zero before).")]]
    std::uint16_t num_lod = 0;
  };
  static_assert(sizeof(SMOHeader) == 0x40);

  // --- MOMT / MOUV ------------------------------------------------------------

  /** Material flag bits (SMOMaterial::flags); test with has_flag(). */
  enum class
  [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::doc("Material flag bits (SMOMaterial.flags).")
  ]]
  MaterialFlags : std::uint32_t
  {
    unlit = 0x1,     /**< Disable lighting. */
    unfogged = 0x2,  /**< Disable fog. */
    two_sided = 0x4, /**< Disable backface culling. */
    ext_light = 0x8, /**< Use exterior lighting on interior surfaces. */
    sidn = 0x10,     /**< Self-illuminated at day and night (window glow). */
    window = 0x20,   /**< A window (lighting special case). */
    clamp_s = 0x40,  /**< Clamp texture addressing on S. */
    clamp_t = 0x80   /**< Clamp texture addressing on T. */
  };

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
    [[=welder::doc("Render flags; MaterialFlags bits.")]]
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

  struct
  [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::doc(R"(
        One MOUV entry (7.3+): texture-coordinate translation speeds for two of
        the material's texture layers. Same count as the materials; all-zero
        entries mean no animation.)")
  ]]
  UVAnimation
  {
    [[=welder::doc("Translation speed per animated texture layer.")]]
    std::array<C2Vector, 2> translation_speed{};
  };
  static_assert(sizeof(UVAnimation) == 0x10);

  // --- MOGI / MGI2 ------------------------------------------------------------

  struct
  [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::doc("One MOGI entry: per-group flags, bounds and name, mirrored from "
                 "the group file's own header.")
  ]]
  SMOGroupInfo
  {
    [[=welder::doc("Group flags; the same GroupFlags bits as MOGP.")]]
    std::uint32_t flags = 0;

    [[=welder::doc("Group bounding box.")]]
    CAaBox bounding_box{};

    [[=welder::doc("Byte offset of the group name in MOGN, -1 for no name.")]]
    std::int32_t name_offset = -1;
  };
  static_assert(sizeof(SMOGroupInfo) == 0x20);

  struct
  [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::doc(R"(
        One MGI2 entry (9.0+): group info v2. Same count as MOGI; when present
        it overrides the older LOD-selection logic.)")
  ]]
  GroupInfo2
  {
    [[=welder::doc("A copy of the group file's flags2; GroupFlags2 bits.")]]
    std::uint32_t flags2 = 0;

    [[=welder::doc("Which LOD level this group belongs to.")]]
    std::uint32_t lod_index = 0;
  };
  static_assert(sizeof(GroupInfo2) == 0x8);

  // --- MOPT / MOPR / MOPE / MOVB ----------------------------------------------

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

    /** Alignment filler; zero in client files. */
    std::uint16_t filler = 0;
  };
  static_assert(sizeof(SMOPortalRef) == 0x8);

  struct
  [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::doc("One MOPE entry (11.1+): portal extra data; largely undeciphered.")
  ]]
  PortalExtra
  {
    [[=welder::doc("Index into MOPT; seems to match MOPR values.")]]
    std::uint32_t portal_index = 0;

    [[=welder::doc("Unknown; preserved for round-trip.")]]
    std::uint32_t unknown_1 = 0;

    [[=welder::doc("Unknown; preserved for round-trip.")]]
    std::uint32_t unknown_2 = 0;

    [[=welder::doc("Unknown; preserved for round-trip.")]]
    std::uint32_t unknown_3 = 0;
  };
  static_assert(sizeof(PortalExtra) == 0x10);

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

  // --- MOLT / MOLV / MNLD -----------------------------------------------------

  /** SMOLight::type values. */
  enum class
  [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::doc("The kind of a WMO light (SMOLight.type).")
  ]]
  LightType : std::uint8_t
  {
    Omni = 0,   /**< A point light. */
    Spot = 1,   /**< A spot light. */
    Direct = 2, /**< A directional light. */
    Ambient = 3 /**< An ambient light. */
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

    /** Alignment padding; zero in client files. */
    [[=welder::mark::exclude]] std::array<std::uint8_t, 2> pad{};

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

  struct
  [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::doc(R"(
        One MOLV entry (9.1+): a directional-gradient extension to a MOLT
        light. Multiple entries may extend the same light.)")
  ]]
  LightExtension
  {
    /** One direction/value sample of the gradient. */
    struct
    [[=welder::doc("One directional gradient sample: a direction (usually axis-"
                   "aligned) and its value.")]]
    Gradient
    {
      [[=welder::doc("Gradient direction; usually either xy or z, remainder 0.")]]
      C3Vector direction{};

      [[=welder::doc("Gradient value.")]]
      float value = 0;
    };

    [[=welder::doc("The six gradient samples.")]]
    std::array<Gradient, 6> gradients{};

    /** Alignment padding; zero in client files. */
    [[=welder::mark::exclude]] std::array<std::uint8_t, 3> pad{};

    [[=welder::doc("The MOLT light this entry extends.")]]
    std::uint8_t light_index = 0;
  };
  static_assert(sizeof(LightExtension) == 0x64);

  struct
  [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::doc(R"(
        One MNLD entry (9.0+): a Shadowlands dynamic light - point or spot -
        used for everything from torch fires to window-light projection.)")
  ]]
  NewLight
  {
    [[=welder::doc("0 = point light (sphere), 1 = spot light (cone).")]]
    std::int32_t type = 0;

    [[=welder::doc("Index of this light in the MNLD array.")]]
    std::int32_t light_index = 0;

    [[=welder::doc("0x1: blend outer and inner color; 0x2: casts shadows.")]]
    std::int32_t flags = 0;

    [[=welder::doc("The doodad set this light belongs to.")]]
    std::int32_t doodad_set = 0;

    [[=welder::doc("Inner color.")]]
    CImVector inner_color{};

    [[=welder::doc("Position in the WMO.")]]
    C3Vector position{};

    [[=welder::doc("Euler rotation in radians; rotates the light (spot) or its "
                   "cookie (point).")]]
    C3Vector rotation{};

    [[=welder::doc("Attenuation start distance.")]]
    float atten_start = 0;

    [[=welder::doc("Attenuation end distance.")]]
    float atten_end = 0;

    [[=welder::doc("Light intensity.")]]
    float intensity = 0;

    [[=welder::doc("Outer color; used with flag 0x1.")]]
    CImVector outer_color{};

    [[=welder::doc("Gradient start distance for inner/outer blending (flag 0x1).")]]
    float blend_start = 0;

    [[=welder::doc("Gradient end distance for inner/outer blending (flag 0x1).")]]
    float blend_end = 0;

    /** Empty gap in the client layout. */
    [[=welder::mark::exclude]] std::array<std::uint8_t, 4> gap_1{};

    [[=welder::doc("Flickering intensity.")]]
    float flicker_intensity = 0;

    [[=welder::doc("Flickering speed.")]]
    float flicker_speed = 0;

    [[=welder::doc("0 = off, 1 = sine curve, 2 = noise curve, 3 = noise step.")]]
    std::int32_t flicker_mode = 0;

    [[=welder::doc("Unknown; only zeros observed.")]]
    C3Vector unknown_1{};

    /** Empty gap in the client layout. */
    [[=welder::mark::exclude]] std::array<std::uint8_t, 4> gap_2{};

    [[=welder::doc("FileDataID of the light cookie texture (a cube map for "
                   "point lights).")]]
    std::uint32_t light_cookie_fdid = 0;

    /** Empty gap in the client layout. */
    [[=welder::mark::exclude]] std::array<std::uint8_t, 20> gap_3{};

    [[=welder::doc("Falloff exponent for spot lights.")]]
    float falloff = 0;

    [[=welder::doc("Start of the spot drop-off gradient, in radians.")]]
    float inner_angle = 0;

    [[=welder::doc("End of the spot drop-off gradient, in radians.")]]
    float outer_angle = 0;

    [[=welder::doc("Scale, as raw IEEE half-float bits; used with flag 0x2.")]]
    std::uint16_t scale_half = 0;

    [[=welder::doc("Intensity multiplier, as raw IEEE half-float bits; 0 is "
                   "corrected to 1 by the client.")]]
    std::uint16_t intensity_multiplier_half = 0;

    /** Trailing fields the client does not read yet; zero in files. */
    [[=welder::mark::exclude]] std::array<std::int32_t, 11> unused{};
  };
  static_assert(sizeof(NewLight) == 0xB8);

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
    /** The raw fixed-size name field; read through name(). */
    [[=welder::mark::exclude]] std::array<char, 20> name_bytes{};

    [[=welder::doc("First doodad instance in MODD.")]]
    std::uint32_t start_index = 0;

    [[=welder::doc("Number of doodad instances in the set.")]]
    std::uint32_t count = 0;

    /** Alignment padding; zero in client files. */
    [[=welder::mark::exclude]] std::array<char, 4> pad{};

    [[=welder::getter("name"),
      =welder::doc("The informational set name.")]]
    [[nodiscard]]
    std::string_view name() const
    {
      const auto end = std::find(name_bytes.begin(), name_bytes.end(), '\0');
      return {name_bytes.data(), static_cast<std::size_t>(end - name_bytes.begin())};
    }
  };
  static_assert(sizeof(SMODoodadSet) == 0x20);

  /** SMODoodadDef::name_and_flags bits: the low 24 bits are the MODN byte
      offset (or MODI index), the high byte carries the flags. */
  enum class
  [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::doc("Doodad placement flag bits, packed into the high byte of "
                 "SMODoodadDef.name_and_flags.")
  ]]
  DoodadFlags : std::uint32_t
  {
    name_mask = 0x00FF'FFFF,        /**< The low 24 bits: the MODN offset / MODI index mask. */
    accept_proj_tex = 0x0100'0000,  /**< Accept projected textures. */
    interior_lighting = 0x0200'0000 /**< Use interior lighting. */
  };

  struct
  [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::doc("One MODD doodad placement: an M2 instance inside the WMO, "
                 "quaternion-oriented in model space.")
  ]]
  SMODoodadDef
  {
    [[=welder::doc("Packed 24-bit MODN offset / MODI index plus DoodadFlags "
                   "bits in the high byte.")]]
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
    [[nodiscard]]
    constexpr std::uint32_t name_index() const
    {
      return name_and_flags & std::to_underlying(DoodadFlags::name_mask);
    }
  };
  static_assert(sizeof(SMODoodadDef) == 0x28);

  // --- MFOG / MFED ------------------------------------------------------------

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

  struct
  [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::doc("One MFED entry (9.0+): fog extra data; same count as MFOG.")
  ]]
  FogExtra
  {
    [[=welder::doc("The doodad set this fog applies to.")]]
    std::uint16_t doodad_set_id = 0;

    /** Undeciphered remainder of the record. */
    [[=welder::mark::exclude]] std::array<std::uint8_t, 14> unknown{};
  };
  static_assert(sizeof(FogExtra) == 0x10);

  // --- MAVD / MAVG / MBVD -----------------------------------------------------

  struct
  [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::doc(R"(
        One ambient volume (MAVD, 8.3+) - a spherical region overriding the
        root ambient color - or a global ambient entry (MAVG), which shares
        the layout with position/start/end zeroed.)")
  ]]
  AmbientVolume
  {
    [[=welder::doc("Volume center (zero in MAVG).")]]
    C3Vector position{};

    [[=welder::doc("Inner radius (zero in MAVG).")]]
    float start = 0;

    [[=welder::doc("Outer radius (zero in MAVG).")]]
    float end = 0;

    [[=welder::doc("Primary ambient color; overrides the MOHD ambient.")]]
    CImVector color_1{};

    [[=welder::doc("Secondary ambient color.")]]
    CImVector color_2{};

    [[=welder::doc("Tertiary ambient color.")]]
    CImVector color_3{};

    [[=welder::doc("0x1: blend color_1 and color_3.")]]
    std::uint32_t flags = 0;

    [[=welder::doc("The doodad set this volume applies to.")]]
    std::uint16_t doodad_set_id = 0;

    /** Undeciphered remainder of the record. */
    [[=welder::mark::exclude]] std::array<std::uint8_t, 10> unknown{};
  };
  static_assert(sizeof(AmbientVolume) == 0x30);

  struct
  [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::doc("One MBVD entry (8.3+): a box-shaped ambient volume bounded by "
                 "six planes. Only read when MAVG or MAVD is present.")
  ]]
  AmbientBoxVolume
  {
    [[=welder::doc("The six bounding planes (position and start combined).")]]
    std::array<C4Plane, 6> planes{};

    [[=welder::doc("Outer distance.")]]
    float end = 0;

    [[=welder::doc("Primary ambient color.")]]
    CImVector color_1{};

    [[=welder::doc("Secondary ambient color.")]]
    CImVector color_2{};

    [[=welder::doc("Tertiary ambient color.")]]
    CImVector color_3{};

    [[=welder::doc("0x1: blend color_2 and color_3.")]]
    std::uint32_t flags = 0;

    [[=welder::doc("The doodad set this volume applies to.")]]
    std::uint16_t doodad_set_id = 0;

    /** Undeciphered remainder of the record. */
    [[=welder::mark::exclude]] std::array<std::uint8_t, 10> unknown{};
  };
  static_assert(sizeof(AmbientBoxVolume) == 0x80);

  // --- MOGP header ------------------------------------------------------------

  /** Group flag bits (MOGP/MOGI flags); test with has_flag(). */
  enum class
  [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::doc("Group flag bits (MOGP and MOGI flags).")
  ]]
  GroupFlags : std::uint32_t
  {
    has_bsp = 0x1,                 /**< Has a collision BSP tree (MOBN/MOBR). */
    has_light_map = 0x2,           /**< Has a light map (v14 legacy; unused since vanilla). */
    has_vertex_colors = 0x4,       /**< Has vertex colors (MOCV). */
    exterior = 0x8,                /**< Outdoor group (SMOGroup::EXTERIOR). */
    exterior_lit = 0x40,           /**< Use exterior lighting; no local diffuse. */
    unreachable = 0x80,            /**< Unreachable (SMOGroup::UNREACHABLE). */
    show_exterior_sky = 0x100,     /**< Show the exterior sky inside this interior group. */
    has_lights = 0x200,            /**< Has light references (MOLR). */
    lod = 0x400,                   /**< Legion+: an LOD group. Pre-Cata: marked the unused MPB* chunks. */
    has_doodads = 0x800,           /**< Has doodad references (MODR). */
    has_liquid = 0x1000,           /**< Has liquid (MLIQ), or an implicitly full volume. */
    interior = 0x2000,             /**< Indoor group (SMOGroup::INTERIOR). */
    query_mount_allowed = 0x8000,  /**< Pre-WotLK: whether mount queries are allowed. */
    always_draw = 0x10000,         /**< Always draw (clears exterior after group creation). */
    has_triangle_strips = 0x20000, /**< Has triangle-strip data (MORI/MORB). */
    show_skybox = 0x40000,         /**< Show the skybox; unset automatically when MOSB is absent. */
    ocean = 0x80000,               /**< The liquid is ocean, not water (LiquidType handling). */
    mount_allowed = 0x200000,      /**< Mounting is allowed in this group. */
    has_two_mocv = 0x1000000,      /**< Has a second MOCV layer; only its alpha is used. */
    has_two_motv = 0x2000000,      /**< Has two MOTV texcoord sets (SMOGroup::TVERTS2). */
    antiportal = 0x4000000,        /**< An antiportal occluder group (SMOGroup::ANTIPORTAL). */
    exterior_cull = 0x20000000,    /**< Exterior culling (SMOGroup::EXTERIOR_CULL, Legion+). */
    has_three_motv = 0x40000000    /**< Has three MOTV texcoord sets, e.g. for shader 18. */
  };

  /** Group flags-2 bits (MOGP flags2 / MGI2); test with has_flag(). */
  enum class
  [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::doc("Extended group flag bits (MOGP.flags2 and MGI2).")
  ]]
  GroupFlags2 : std::uint32_t
  {
    can_cut_terrain = 0x1,        /**< Has terrain-cutting portal planes (MOPL, WoD+). */
    is_split_group_parent = 0x40, /**< Parent of a split group (9.2+). */
    is_split_group_child = 0x80,  /**< Child of a split group (9.2+). */
    attachment_mesh = 0x100       /**< An attachment mesh (9.2 - 11.0). */
  };

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

    [[=welder::doc("Group flags; GroupFlags bits.")]]
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

    /** The fourth batch-count slot; unused by clients. */
    std::uint16_t batch_type_d = 0;

    [[=welder::doc("Fog indices into MFOG.")]]
    std::array<std::uint8_t, 4> fog_ids{};

    [[=welder::doc("Group liquid type; interpretation depends on the root "
                   "use_liquid_type_dbc_id flag.")]]
    std::uint32_t group_liquid = 0;

    [[=welder::doc("Foreign key into WMOAreaTable (m_WMOGroupID).")]]
    std::uint32_t unique_id = 0;

    [[=welder::doc("Extended flags (Cataclysm+); GroupFlags2 bits.")]]
    std::uint32_t flags2 = 0;

    /** Unused up to 9.1.5 (becomes the split-group indices in 9.2+). */
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

    [[=welder::doc("Group flags; GroupFlags bits.")]]
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

    /** The fourth batch-count slot; unused by clients. */
    std::uint16_t batch_type_d = 0;

    [[=welder::doc("Fog indices into MFOG.")]]
    std::array<std::uint8_t, 4> fog_ids{};

    [[=welder::doc("Group liquid type; interpretation depends on the root "
                   "use_liquid_type_dbc_id flag.")]]
    std::uint32_t group_liquid = 0;

    [[=welder::doc("Foreign key into WMOAreaTable (m_WMOGroupID).")]]
    std::uint32_t unique_id = 0;

    [[=welder::doc("Extended flags (Cataclysm+); GroupFlags2 bits.")]]
    std::uint32_t flags2 = 0;

    [[=welder::doc("Parent split group, or the first child (9.2+ split groups).")]]
    std::int16_t parent_or_first_child_split_group_index = -1;

    [[=welder::doc("Next sibling in the split-group chain.")]]
    std::int16_t next_split_child_group_index = -1;
  };

  static_assert(sizeof(SMOGroupHeader<versions::wotlk>) == 0x44);
  static_assert(sizeof(SMOGroupHeader<versions::shadowlands>) == 0x44);

  // --- MOPY / MPY2 / MOBA / MORB / MOBS / MOBN --------------------------------

  /** Triangle flag bits (SMOPoly::flags); test with has_flag(). */
  enum class
  [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::doc("Per-triangle flag bits (MOPY and MPY2).")
  ]]
  PolyFlags : std::uint8_t
  {
    transition = 0x1,     /**< A transition face, blending exterior and interior lighting. */
    no_cam_collide = 0x2, /**< The camera does not collide with this face. */
    detail = 0x4,         /**< A detail face. */
    collision = 0x8,      /**< Collision-only (also turns off water ripples). */
    hint = 0x10,          /**< A hint face. */
    render = 0x20,        /**< A rendered face. */
    cull_objects = 0x40,  /**< Enables game-object culling against this face. */
    collide_hit = 0x80    /**< Collides with projectiles/hit tests. */
  };

  struct
  [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::doc("Per-triangle material info (MOPY).")
  ]]
  SMOPoly
  {
    [[=welder::doc("Triangle flags; PolyFlags bits.")]]
    std::uint8_t flags = 0;

    [[=welder::doc("Index into MOMT; 0xFF for collision-only faces.")]]
    std::uint8_t material_id = 0;
  };
  static_assert(sizeof(SMOPoly) == 0x2);

  struct
  [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::doc(R"(
        Per-triangle material info v2 (MPY2, 10.0+): the MOPY replacement with
        16-bit fields for multiple-material support.)")
  ]]
  Poly2
  {
    [[=welder::doc("Triangle flags; PolyFlags bits, plus 0x100 for MOQG "
                   "ground-type queries.")]]
    std::uint16_t flags = 0;

    [[=welder::doc("Index into MOMT.")]]
    std::uint16_t material_id = 0;
  };
  static_assert(sizeof(Poly2) == 0x4);

  /** One render batch, 24 bytes. Before 7.0 the leading 12 bytes are an int16
      culling box; from 7.0 they are unused except for a uint16 material id
      backing the large-material flag bit. */
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
    /** The nulled remains of the pre-7.0 culling box. */
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

  struct
  [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::doc(R"(
        One MORB entry (Cata+): a triangle-strip override of the matching MOBA
        batch's start and count. Same count as MOBA; ignored unless the client
        renders strips.)")
  ]]
  RenderBatchOverride
  {
    [[=welder::doc("Replacement first-index into the MORI strips.")]]
    std::uint32_t start_index = 0;

    [[=welder::doc("Replacement index count.")]]
    std::uint16_t index_count = 0;

    /** Alignment padding; zero in client files. */
    std::uint16_t padding = 0;
  };
  static_assert(sizeof(RenderBatchOverride) == 0x8);

  struct
  [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::doc("One MOBS shadow batch (Cata+): the shadow-pass counterpart of "
                 "a MOBA render batch.")
  ]]
  ShadowBatch
  {
    /** Undeciphered leading fields. */
    [[=welder::mark::exclude]] std::array<std::uint8_t, 10> unknown{};

    [[=welder::doc("16-bit material id; used when flags has 0x2.")]]
    std::uint16_t material_id_large = 0;

    [[=welder::doc("Start value; divided by 3 on use (a face index).")]]
    std::int32_t start = 0;

    [[=welder::doc("Count value; divided by 3 on use (a face count).")]]
    std::int16_t count = 0;

    /** Undeciphered middle fields. */
    [[=welder::mark::exclude]] std::array<std::uint8_t, 4> unknown_2{};

    [[=welder::doc("Batch flags; 0x2: use material_id_large.")]]
    std::uint8_t flags = 0;

    [[=welder::doc("Index into MOMT (when it fits 8 bits).")]]
    std::uint8_t material_id = 0;
  };
  static_assert(sizeof(ShadowBatch) == 0x18);

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

  // --- MOLP / MLSS / MLSP / MLSK / MOP2 ---------------------------------------

  struct
  [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::doc("One MOLP point light (Legion+).")
  ]]
  PointLight
  {
    [[=welder::doc("Unknown; preserved for round-trip.")]]
    std::uint32_t unknown_1 = 0;

    [[=welder::doc("Light color.")]]
    CImVector color{};

    [[=welder::doc("Position of the light.")]]
    C3Vector position{};

    [[=welder::doc("Intensity.")]]
    float intensity = 0;

    [[=welder::doc("Attenuation start distance.")]]
    float atten_start = 0;

    [[=welder::doc("Attenuation end distance.")]]
    float atten_end = 0;

    [[=welder::doc("Unknown; only zeros observed.")]]
    float unknown_2 = 0;

    [[=welder::doc("Unknown; preserved for round-trip.")]]
    std::uint32_t unknown_3 = 0;

    [[=welder::doc("Unknown; possibly a color.")]]
    std::uint32_t unknown_4 = 0;
  };
  static_assert(sizeof(PointLight) == 0x2C);

  struct
  [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::doc(R"(
        One light-set range (8.1+): a (first, count) window into the group's
        spot lights (MLSS -> MOLS), point lights (MLSP -> MOLP) or point-light
        animations (MLSK -> MOP2).)")
  ]]
  LightSet
  {
    [[=welder::doc("First light in the referenced chunk.")]]
    std::uint32_t offset = 0;

    [[=welder::doc("Number of lights in the set.")]]
    std::uint32_t count = 0;
  };
  static_assert(sizeof(LightSet) == 0x8);

  struct
  [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::doc("One MOP2 entry (8.1+): an animated point light with flicker "
                 "and light-cookie texture data.")
  ]]
  PointLightAnim
  {
    [[=welder::doc("Index of the light this animation belongs to.")]]
    std::uint32_t light_index = 0;

    [[=welder::doc("Light color.")]]
    CImVector color{};

    [[=welder::doc("Position of the light.")]]
    C3Vector position{};

    [[=welder::doc("Attenuation start distance.")]]
    float atten_start = 0;

    [[=welder::doc("Attenuation end distance.")]]
    float atten_end = 0;

    [[=welder::doc("Intensity.")]]
    float intensity = 0;

    [[=welder::doc("Euler rotation in radians.")]]
    C3Vector rotation{};

    [[=welder::doc("Flickering intensity.")]]
    float flicker_intensity = 0;

    [[=welder::doc("Flickering speed.")]]
    float flicker_speed = 0;

    [[=welder::doc("0 = off, 1 = sine curve, 2 = noise curve, 3 = noise step.")]]
    std::int32_t flicker_mode = 0;

    /** Undeciphered leading fields of the trailing record. */
    [[=welder::mark::exclude]] std::array<std::int32_t, 4> unknown{};

    [[=welder::doc("FileDataID of the light cookie texture.")]]
    std::uint32_t light_texture_fdid = 0;

    /** Undeciphered trailing fields; only the texture id is read. */
    [[=welder::mark::exclude]] std::array<std::int32_t, 5> unknown_2{};
  };
  static_assert(sizeof(PointLightAnim) == 0x60);

  // v14-alpha-only chunks (MOLM/MOLD lightmaps, MOIN indices, the old MOLV
  // lightmap texcoords, and the never-read MPB* set) predate every supported
  // client version and are not modeled; they would round-trip through
  // ChunkExtras::unknown if ever encountered. MLSO/MOS2 exist in client
  // binaries only, never in files.
}
