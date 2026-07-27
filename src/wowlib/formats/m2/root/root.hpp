#pragma once

/** @file
    The MD20 body entity (namespace wowlib::formats::m2::root): M2Root — the
    client's own name for the offset-addressed model payload. Pre-Legion this
    IS the .m2 file; Legion+ it is the MD21 chunk's content (offsets stay
    relative to the image either way, which is exactly what M2OffsetBlock
    serializes against).

    Version-gated members live in conditionally-inherited trait slots
    (root::detail); each carries `=offset_after("member")` naming the own member
    it follows, which puts it back at its interleaved layout position. A
    version's M2Root carries ONLY the members that version defines — setting
    an absent one is a compile error. */

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <welder/vocabulary.hpp>

#include <wowlib/core/client_version.hpp>
#include <wowlib/formats/common/annotations.hpp>
#include <wowlib/formats/common/types.hpp>
#include <wowlib/formats/common/version_slot.hpp>
#include <wowlib/formats/m2/boundaries.hpp>
#include <wowlib/formats/m2/offset_block.hpp>
#include <wowlib/formats/m2/root/record/bone.hpp>
#include <wowlib/formats/m2/root/record/effects.hpp>
#include <wowlib/formats/m2/root/record/material.hpp>
#include <wowlib/formats/m2/root/record/scene.hpp>
#include <wowlib/formats/m2/root/record/sequence.hpp>
#include <wowlib/formats/m2/skin/records.hpp>
#include <wowlib/formats/m2/root/record/track.hpp>

namespace wowlib::formats::m2::root
{
  using namespace wowlib::formats::m2::root::record;

  /** The MD20 leading magic, as memcpy'd from disk. */
  inline constexpr std::uint32_t md20_magic = 0x3032444D;  // "MD20"

  /** M2Root::global_flags bits. */
  enum class [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::doc("M2 global flags: tilt behavior, the texture-combiner-combo "
                 "gate, physics participation and exporter-era markers.")
  ]] GlobalFlags : std::uint32_t
  {
    TiltX [[=welder::doc("Tilt the model over X (flying mounts).")]] = 0x1,
    TiltY [[=welder::doc("Tilt the model over Y.")]] = 0x2,
    UseTextureCombinerCombos [[=welder::doc("The texture_combiner_combos "
                                            "block trails the header (TBC+).")]] = 0x8,
    LoadPhysData [[=welder::doc("Request the .phys file (MoP+).")]] = 0x20,
    Unk0x80 [[=welder::doc("Unset stops demon-hunter tattoos glowing (WoD+).")]] = 0x80,
    CameraRelated [[=welder::doc("Camera related (WoD+).")]] = 0x100,
    NewParticleRecord [[=welder::doc("Cata: particle records are the 492-byte "
                                     "layout even below v272.")]] = 0x200,
    TextureTransformsUseBoneSequences
      [[=welder::doc("Texture transforms animate on the bone's sequence "
                     "(Legion+).")]] = 0x800,
    ChunkedAnimFiles [[=welder::doc("The .anim files are chunked (Legion+).")]] = 0x2000
  };

  /** The version-agnostic base of every M2Root<V> (welded as "M2Root").

      This empty base exists ENTIRELY for the language bindings: it gives the
      per-version M2Root* classes a common welded supertype so binding users
      can write version-agnostic code (isinstance,
      M2Root.for_version(expansion)). It has no role in the C++ API, where you
      use the concrete M2Root<V>.

      @see https://wowdev.wiki/M2 */
  struct [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::weld_as("M2Root"),
    =welder::doc(R"(
        An MD20 model body, abstract over the client version. Construct a
        concrete version with M2Root.for_version(expansion); the per-version
        M2Root* classes are subclasses. See https://wowdev.wiki/M2.)")
  ]] M2RootBase
  {
    bool operator==(const M2RootBase&) const = default;
  };

  namespace detail
  {
    // --- version-range trait bases (unwelded) ---------------------------------
    // One struct per availability range; each member's offset_after anchor (not
    // flatten order) fixes its interleaved position in the header.

    /** Pre-WotLK members: the embedded skin profiles and the vanilla/TBC-only
        lookup blocks. */
    template <ClientVersion V>
    struct DataPreWotlk
    {
      [[
        =until(m2_per_sequence_timelines),
        =offset_after("sequence_lookups"),
        =welder::mark::no_reassign,
        =welder::doc("Playable-animation fallbacks, one per AnimationData.dbc "
                     "id (pre-WotLK).")]]
      std::vector<M2SequenceFallback> playable_animation_lookup;

      [[
        =until(m2_per_sequence_timelines),
        =offset_after("vertices"),
        =welder::mark::no_reassign,
        =welder::doc("The skin profiles (LOD views), embedded in the model "
                     "pre-WotLK; WotLK+ moves them to .skin files.")]]
      std::vector<skin::M2SkinProfile<V>> skin_profiles;

      [[
        =until(m2_per_sequence_timelines),
        =offset_after("texture_weights"),
        =welder::mark::no_reassign,
        =welder::doc("Texture flipbooks (pre-WotLK; never seen engaged).")]]
      std::vector<M2TextureFlipbook<V>> texture_flipbooks;

      [[=welder::mark::exclude]]
      bool operator==(const DataPreWotlk&) const = default;
    };

    /** WotLK+ members: the external-skin count replacing the embedded
        profiles. */
    struct DataWotlk
    {
      [[
        =since(m2_per_sequence_timelines),
        =offset_after("vertices"),
        =welder::mark::exclude,
        =welder::doc("How many .skin files (LOD views) belong to the model "
                     "(WotLK+). A derived layout field: the M2 assembly's "
                     "skins vector is the source of truth — its write stamps "
                     "this from skins.size(), and the bindings hide it.")]]
      std::uint32_t num_skin_profiles = 0;

      [[=welder::mark::exclude]]
      bool operator==(const DataWotlk&) const = default;
    };

    /** TBC+ members: the flag-gated combiner-combo tail. */
    struct DataTbc
    {
      [[
        =since(m2_compressed_bones),
        =gated_by(0x8),
        =offset_after("particle_emitters"),
        =welder::mark::no_reassign,
        =welder::doc("Second-texture material override combos; present in the "
                     "layout only under global flag 0x8 (TBC+).")]]
      std::vector<std::uint16_t> texture_combiner_combos;

      [[=welder::mark::exclude]]
      bool operator==(const DataTbc&) const = default;
    };
  }

  /** The MD20 model body for one client version: header scalars plus every
      offset-addressed block, decoded into vectors. This entity alone is a
      complete pre-WotLK model; later clients pull skins, low-priority
      sequence data, skeleton and physics from satellite files — the M2
      assembly bakes those back in. Instantiate through the canonicalizing
      m2::M2Root alias, never directly.
      @tparam V the canonical client version this body targets.
      @see https://wowdev.wiki/M2 */
  template <ClientVersion V>
  struct [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::doc(R"(
        An MD20 model body for one client version: header scalars plus every
        offset-addressed block, decoded. A version's class carries ONLY the
        members that version defines. See https://wowdev.wiki/M2.)")
  ]] M2Root : M2OffsetBlock<M2Root<V>>,
                  // root::detail:: (never a bare detail::) — the using-directive
                  // above imports record::detail, which a bare spelling would
                  // be ambiguous against
                  slot<V, ClientVersion{0, 0, 0, 0}, root::detail::DataPreWotlk<V>,
                       m2_per_sequence_timelines>,
                  slot<V, m2_per_sequence_timelines, root::detail::DataWotlk>,
                  slot<V, m2_compressed_bones, root::detail::DataTbc>,
                  M2RootBase
  {
    static constexpr ClientVersion version = V;

    [[=welder::mark::exclude,
      =welder::doc("The leading magic, 'MD20' — constant on every model; "
                   "hidden from the bindings.")]]
    std::uint32_t magic = md20_magic;

    [[=welder::doc("The MD20 format version (256 vanilla .. 274 Legion+).")]]
    std::uint32_t format_version = m2_format_version(V);

    [[=welder::doc("The model's internal name; empty in 9.2+ files.")]]
    std::string name;

    [[=welder::doc("Global flags, see GlobalFlags.")]]
    std::uint32_t global_flags = 0;

    [[=welder::mark::no_reassign,
      =welder::doc("Global-sequence loop lengths (timestamps).")]]
    std::vector<M2Loop> global_loops;

    [[=welder::mark::no_reassign,
      =welder::doc("The animation sequences.")]]
    std::vector<M2Sequence<V>> sequences;

    [[=welder::mark::no_reassign,
      =welder::doc("Animation-id hash table: AnimationData.dbc id -> sequence "
                   "index, quadratic probing, -1 empty.")]]
    std::vector<std::int16_t> sequence_lookups;

    [[=welder::mark::no_reassign,
      =welder::doc("The bones (MAX_BONES nominally 256).")]]
    std::vector<M2CompBone<V>> bones;

    [[=welder::mark::no_reassign,
      =welder::doc("Key-bone lookup: key bone slot -> bone index, -1 if none.")]]
    std::vector<std::int16_t> key_bone_lookup;

    [[=welder::mark::no_reassign,
      =welder::doc("The global vertex list (Z-up model space).")]]
    std::vector<M2Vertex> vertices;

    [[=welder::mark::no_reassign,
      =welder::doc("Color and alpha animations, referenced from skin batches.")]]
    std::vector<M2Color<V>> colors;

    [[=welder::mark::no_reassign,
      =welder::doc("The texture definitions.")]]
    std::vector<M2Texture> textures;

    [[=welder::mark::no_reassign,
      =welder::doc("Global transparency weights.")]]
    std::vector<M2TextureWeight<V>> texture_weights;

    [[=welder::mark::no_reassign,
      =welder::doc("UV animations.")]]
    std::vector<M2TextureTransform<V>> texture_transforms;

    [[=welder::mark::no_reassign,
      =welder::doc("Replacable-texture reverse lookup: replacable id -> "
                   "texture index or -1.")]]
    std::vector<std::int16_t> replacable_texture_lookup;

    [[=welder::mark::no_reassign,
      =welder::doc("Materials: render flags + blending modes.")]]
    std::vector<M2Material> materials;

    [[=welder::mark::no_reassign,
      =welder::doc("Bone lookup: skin sections select bone subsets through it.")]]
    std::vector<std::uint16_t> bone_lookup_table;

    [[=welder::mark::no_reassign,
      =welder::doc("Texture lookup: batches select textures through it.")]]
    std::vector<std::uint16_t> texture_lookup_table;

    [[=welder::mark::no_reassign,
      =welder::doc("Texture-mapping lookup: -1 environment, 0 first UV set, "
                   "1 second (unused since Cata).")]]
    std::vector<std::int16_t> texture_mapping_lookup_table;

    [[=welder::mark::no_reassign,
      =welder::doc("Transparency lookup: batches select texture weights "
                   "through it.")]]
    std::vector<std::uint16_t> transparency_lookup_table;

    [[=welder::mark::no_reassign,
      =welder::doc("Texture-transform lookup: batches select UV animations "
                   "through it, -1 static.")]]
    std::vector<std::int16_t> texture_transforms_lookup_table;

    [[=welder::doc("The render bounds.")]]
    CAaBox bounding_box{};

    [[=welder::doc("The render bounding-sphere radius.")]]
    float bounding_sphere_radius = 0;

    [[=welder::doc("The collision bounds.")]]
    CAaBox collision_box{};

    [[=welder::doc("The collision bounding-sphere radius.")]]
    float collision_sphere_radius = 0;

    [[=welder::mark::no_reassign,
      =welder::doc("Collision-hull triangle indices (3 per face).")]]
    std::vector<std::uint16_t> collision_triangles;

    [[=welder::mark::no_reassign,
      =welder::doc("Collision-hull vertices.")]]
    std::vector<C3Vector> collision_vertices;

    [[=welder::mark::no_reassign,
      =welder::doc("Collision-hull per-face normals.")]]
    std::vector<C3Vector> collision_normals;

    [[=welder::mark::no_reassign,
      =welder::doc("Attachment points (weapons, effects, name plates).")]]
    std::vector<M2Attachment<V>> attachments;

    [[=welder::mark::no_reassign,
      =welder::doc("Attachment lookup: attachment id -> index.")]]
    std::vector<std::uint16_t> attachment_lookup_table;

    [[=welder::mark::no_reassign,
      =welder::doc("Timed events (sounds, footsteps, death thud).")]]
    std::vector<M2Event<V>> events;

    [[=welder::mark::no_reassign,
      =welder::doc("Model lights.")]]
    std::vector<M2Light<V>> lights;

    [[=welder::mark::no_reassign,
      =welder::doc("Cameras (portrait, character info, flyby).")]]
    std::vector<M2Camera<V>> cameras;

    [[=welder::mark::no_reassign,
      =welder::doc("Camera lookup: camera type -> index.")]]
    std::vector<std::uint16_t> camera_lookup_table;

    [[=welder::mark::no_reassign,
      =welder::doc("Ribbon (trail) emitters.")]]
    std::vector<M2Ribbon<V>> ribbon_emitters;

    [[=welder::mark::no_reassign,
      =welder::doc("Particle emitters.")]]
    std::vector<M2Particle<V>> particle_emitters;

    bool operator==(const M2Root&) const = default;
  };
}

namespace wowlib::formats::m2
{
  /** The MD20 body — the canonicalizing face of root::M2Root: every client
      version maps to its range's first grid version (m2_data_pivots), so one
      instantiation serves e.g. both Cata and MoP. */
  template <ClientVersion V>
  using M2Root = root::M2Root<canonical_version(V, m2_data_pivots, m2_versions)>;
}
