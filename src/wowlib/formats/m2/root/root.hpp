#pragma once

/** @file
    The MD20 body entity (namespace wowlib::formats::m2::root): M2Root — the
    client's own name for the offset-addressed model payload. Pre-Legion this
    IS the .m2 file; Legion+ it is the MD21 chunk's content (offsets stay
    relative to the image either way, which is exactly what M2OffsetBlock
    serializes against).

    Version-gated members live in conditionally-inherited trait slots
    (root::detail); each carries `=offsetAfter("member")` naming the own member
    it follows, which puts it back at its interleaved layout position. A
    version's M2Root carries ONLY the members that version defines — setting
    an absent one is a compile error. */

#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <vector>

#include <welder/vocabulary.hpp>

#include <wowlib/core/client_version.hpp>
#include <wowlib/core/lang.hpp>
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

namespace wowlib::formats::m2::root {
  using namespace wowlib::formats::m2::root::record;

  /** The MD20 leading magic, as memcpy'd from disk. */
  inline constexpr std::uint32_t Md20Magic = 0x3032444D; // "MD20"

  /** M2Root::globalFlags bits. */
  enum class [[
      =welder::weld,
      =welder::doc("M2 global flags: tilt behavior, the texture-combiner-combo "
        "gate, physics participation and exporter-era markers.")
    ]] GlobalFlags : std::uint32_t {
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
      =welder::weld,
      =welder::weld_as("M2Root"),
      WOWLIB_CS_FAMILY_SURFACE
      =welder::doc(R"(
        An MD20 model body, abstract over the client version. Construct a
        concrete version with M2Root.for_version(expansion); the per-version
        M2Root* classes are subclasses. See https://wowdev.wiki/M2.)")
    ]] M2RootBase {
    bool operator==(const M2RootBase&) const = default;
  };

  namespace detail {
    // --- version-range trait bases (unwelded) ---------------------------------
    // One struct per availability range; each member's offsetAfter anchor (not
    // flatten order) fixes its interleaved position in the header.

    /** Pre-WotLK members: the embedded skin profiles and the vanilla/TBC-only
        lookup blocks. */
    template <ClientVersion V>
    struct DataPreWotlk {
      [[
        =until(M2PerSequenceTimelines),
        =offsetAfter("sequenceLookups"),
        =welder::mark::no_reassign,
        =welder::doc("Playable-animation fallbacks, one per AnimationData.dbc "
          "id (pre-WotLK).")]]
      std::vector<M2SequenceFallback> playableAnimationLookup;

      [[
        =until(M2PerSequenceTimelines),
        =offsetAfter("vertices"),
        =welder::mark::no_reassign,
        =welder::doc("The skin profiles (LOD views), embedded in the model "
          "pre-WotLK; WotLK+ moves them to .skin files.")]]
      std::vector<skin::M2SkinProfile<V>> skinProfiles;

      [[
        =until(M2PerSequenceTimelines),
        =offsetAfter("textureWeights"),
        =welder::mark::no_reassign,
        =welder::doc("Texture flipbooks (pre-WotLK; never seen engaged).")]]
      std::vector<M2TextureFlipbook<V>> textureFlipbooks;

      [[=welder::mark::exclude]]
      bool operator==(const DataPreWotlk&) const = default;
    };

    /** WotLK+ members: the external-skin count replacing the embedded
        profiles. */
    struct DataWotlk {
      [[
        =since(M2PerSequenceTimelines),
        =offsetAfter("vertices"),
        =welder::mark::exclude,
        =welder::doc("How many .skin files (LOD views) belong to the model "
          "(WotLK+). A derived layout field: the M2 assembly's "
          "skins vector is the source of truth — its write stamps "
          "this from skins.size(), and the bindings hide it.")]]
      std::uint32_t numSkinProfiles = 0;

      [[=welder::mark::exclude]]
      bool operator==(const DataWotlk&) const = default;
    };

    /** TBC+ members: the flag-gated combiner-combo tail. */
    struct DataTbc {
      [[
        =since(M2CompressedBones),
        =gatedBy(0x8),
        =offsetAfter("particleEmitters"),
        =welder::mark::no_reassign,
        =welder::doc("Second-texture material override combos; present in the "
          "layout only under global flag 0x8 (TBC+).")]]
      std::vector<std::uint16_t> textureCombinerCombos;

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
      =welder::weld,
      =welder::doc(R"(
        An MD20 model body for one client version: header scalars plus every
        offset-addressed block, decoded. A version's class carries ONLY the
        members that version defines. See https://wowdev.wiki/M2.)")
    ]] M2Root : M2OffsetBlock<M2Root<V>>,
                // root::detail:: (never a bare detail::) — the using-directive
                // above imports record::detail, which a bare spelling would
                // be ambiguous against
                Slot<V, ClientVersion{0, 0, 0, 0}, root::detail::DataPreWotlk<V>, M2PerSequenceTimelines>,
                Slot<V, M2PerSequenceTimelines, root::detail::DataWotlk>,
                Slot<V, M2CompressedBones, root::detail::DataTbc>,
                M2RootBase {
    static constexpr ClientVersion Version = V;

    [[=welder::mark::exclude,
      =welder::doc("The leading magic, 'MD20' — constant on every model; "
        "hidden from the bindings.")]]
    std::uint32_t magic = Md20Magic;

    [[=welder::doc("The MD20 format version (256 vanilla .. 274 Legion+).")]]
    std::uint32_t formatVersion = m2FormatVersion(V);

    [[=welder::doc("The model's internal name; empty in 9.2+ files.")]]
    std::string name;

    [[=welder::doc("Global flags, see GlobalFlags.")]]
    std::uint32_t globalFlags = 0;

    [[=welder::mark::no_reassign,
      =welder::doc("Global-sequence loop lengths (timestamps).")]]
    std::vector<M2Loop> globalLoops;

    [[=welder::mark::no_reassign,
      =welder::doc("The animation sequences.")]]
    std::vector<M2Sequence<V>> sequences;

    [[=welder::mark::no_reassign,
      =formats::indexesOptional("sequences"),
      =welder::doc("Animation-id hash table: AnimationData.dbc id -> sequence "
        "index, quadratic probing, -1 empty.")]]
    std::vector<std::int16_t> sequenceLookups;

    [[=welder::mark::no_reassign,
      =welder::doc("The bones (MAX_BONES nominally 256).")]]
    std::vector<M2CompBone<V>> bones;

    // NOT indexesOptional("bones"): a skel-based model (Legion+) keeps its
    // bones in the .skel file, leaving this body's `bones` empty while the
    // lookups still address the skeleton's. The M2 assembly's validate()
    // checks both lookups against whichever list actually supplies the bones.
    [[=welder::mark::no_reassign,
        =welder::doc(
          "Key-bone lookup: key bone slot -> bone index, -1 if none.")]
    ]
    std::vector<std::int16_t> keyBoneLookup;

    [[=welder::mark::no_reassign,
      =welder::doc("The global vertex list (Z-up model space).")]]
    std::vector<M2Vertex> vertices;

    [[=welder::mark::no_reassign,
        =welder::doc(
          "Color and alpha animations, referenced from skin batches.")]
    ]
    std::vector<M2Color<V>> colors;

    [[=welder::mark::no_reassign,
      =welder::doc("The texture definitions.")]]
    std::vector<M2Texture> textures;

    [[=welder::mark::no_reassign,
      =welder::doc("Global transparency weights.")]]
    std::vector<M2TextureWeight<V>> textureWeights;

    [[=welder::mark::no_reassign,
      =welder::doc("UV animations.")]]
    std::vector<M2TextureTransform<V>> textureTransforms;

    [[=welder::mark::no_reassign,
      =formats::indexesOptional("textures"),
      =welder::doc("Replacable-texture reverse lookup: replacable id -> "
        "texture index or -1.")]]
    std::vector<std::int16_t> replacableTextureLookup;

    [[=welder::mark::no_reassign,
      =welder::doc("Materials: render flags + blending modes.")]]
    std::vector<M2Material> materials;

    // see keyBoneLookup: the effective bone list may live in the .skel
    [[=welder::mark::no_reassign,
      =welder::doc("Bone lookup: skin sections select bone subsets through it.")
    ]]
    std::vector<std::uint16_t> boneLookupTable;

    [[=welder::mark::no_reassign,
      =formats::indexesOptional("textures"),
      =welder::doc("Texture lookup: batches select textures through it.")]]
    std::vector<std::uint16_t> textureLookupTable;

    [[=welder::mark::no_reassign,
      =welder::doc("Texture-mapping lookup: -1 environment, 0 first UV set, "
        "1 second (unused since Cata).")]]
    std::vector<std::int16_t> textureMappingLookupTable;

    [[=welder::mark::no_reassign,
      =formats::indexesOptional("textureWeights"),
      =welder::doc("Transparency lookup: batches select texture weights "
        "through it.")]]
    std::vector<std::uint16_t> transparencyLookupTable;

    [[=welder::mark::no_reassign,
      =formats::indexesOptional("textureTransforms"),
      =welder::doc("Texture-transform lookup: batches select UV animations "
        "through it, -1 static.")]]
    std::vector<std::int16_t> textureTransformsLookupTable;

    [[=welder::doc("The render bounds.")]]
    CAaBox boundingBox{};

    [[=welder::doc("The render bounding-sphere radius.")]]
    float boundingSphereRadius = 0;

    [[=welder::doc("The collision bounds.")]]
    CAaBox collisionBox{};

    [[=welder::doc("The collision bounding-sphere radius.")]]
    float collisionSphereRadius = 0;

    [[=welder::mark::no_reassign,
      =formats::countMultipleOf(3),
      =formats::indexes("collisionVertices"),
      =welder::doc("Collision-hull triangle indices (3 per face).")]]
    std::vector<std::uint16_t> collisionTriangles;

    [[=welder::mark::no_reassign,
      =welder::doc("Collision-hull vertices.")]]
    std::vector<C3Vector> collisionVertices;

    [[=welder::mark::no_reassign,
      =formats::countMatches("collisionTriangles", 3),
      =welder::doc("Collision-hull per-face normals.")]]
    std::vector<C3Vector> collisionNormals;

    [[=welder::mark::no_reassign,
      =welder::doc("Attachment points (weapons, effects, name plates).")]]
    std::vector<M2Attachment<V>> attachments;

    [[=welder::mark::no_reassign,
      =formats::indexesOptional("attachments"),
      =welder::doc("Attachment lookup: attachment id -> index.")]]
    std::vector<std::uint16_t> attachmentLookupTable;

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
      =formats::indexesOptional("cameras"),
      =welder::doc("Camera lookup: camera type -> index.")]]
    std::vector<std::uint16_t> cameraLookupTable;

    [[=welder::mark::no_reassign,
      =welder::doc("Ribbon (trail) emitters.")]]
    std::vector<M2Ribbon<V>> ribbonEmitters;

    [[=welder::mark::no_reassign,
      =welder::doc("Particle emitters.")]]
    std::vector<M2Particle<V>> particleEmitters;

    /** Validation hook (see formats::detail::validateValue): the model-body
        contracts the annotations cannot express — the bone hierarchy, the
        alias-sequence chains the client follows to find track data, and the
        global-sequence references tracks carry. Contracts spanning the
        satellite files (skins into this body, .anim coverage) belong to the
        M2 assembly's validate().
        @param report the report findings land in. */
    [[=welder::mark::exclude]]
    void validateExtra(ValidationReport& report) const {
      // the bone hierarchy: parents exist, precede their children (the client
      // resolves transforms in one forward pass) and never form a cycle
      for (std::size_t i = 0; i < bones.size(); ++i) {
        const std::int16_t parent = bones[i].parentBone;
        if (parent < 0)
          continue;
        if (static_cast<std::size_t>(parent) >= bones.size())
          report.addError(std::format("bones[{}]", i),
                          std::format("parent_bone {} out of range: {} bones",
                                      parent,
                                      bones.size()));
        else if (static_cast<std::size_t>(parent) >= i)
          report.addError(std::format("bones[{}]", i),
                          std::format(
                            "parent_bone {} does not precede the child",
                            parent));
      }

      // alias sequences own no track data: the client follows aliasNext until
      // it reaches a non-alias, so a dangling or self-referential link hangs it
      for (std::size_t i = 0; i < sequences.size(); ++i) {
        if (!sequences[i].isAlias())
          continue;
        std::size_t at = i;
        std::size_t steps = 0;
        while (steps++ <= sequences.size()) {
          const std::size_t next = sequences[at].aliasNext;
          if (next >= sequences.size()) {
            report.addError(std::format("sequences[{}]", at),
                            std::format(
                              "alias_next {} out of range: {} sequences", next,
                              sequences.size()));
            break;
          }
          if (next == at) {
            report.addError(std::format("sequences[{}]", at),
                            "alias_next points at itself");
            break;
          }
          at = next;
          if (!sequences[at].isAlias())
            break;
        }
        if (steps > sequences.size())
          report.addError(std::format("sequences[{}]", i),
                          "alias chain does not reach a non-alias sequence (cycle)");
      }
    }

    bool operator==(const M2Root&) const = default;
  };
}

namespace wowlib::formats::m2 {
  /** The MD20 body — the canonicalizing face of root::M2Root: every client
      version maps to its range's first grid version (M2DataPivots), so one
      instantiation serves e.g. both Cata and MoP. */
  template <ClientVersion V>
  using M2Root = root::M2Root<canonicalVersion(V, M2DataPivots, M2Versions)>;
}
