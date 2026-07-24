#pragma once

/** @file
    The M2 entity (namespace wowlib::formats::m2): a whole model with its
    satellite files baked in, versioned on the client it is laid out for. The
    assembly unifies the MD20 body (M2Data) with the external .skin LOD views
    and re-splits low-priority sequence data back out to .anim files on write
    (driven by the sequence flags). Pre-WotLK everything is one file; Legion+
    adds the chunked wrapper and .skel/.bone/.phys satellites (stage 3). */

#include <array>
#include <vector>

#include <wowlib/core/client_version.hpp>
#include <wowlib/core/error.hpp>
#include <wowlib/core/file_key.hpp>
#include <wowlib/formats/common/version_slot.hpp>
#include <wowlib/formats/m2/chunked.hpp>
#include <wowlib/formats/m2/data.hpp>
#include <wowlib/formats/m2/skin.hpp>

namespace wowlib::fs
{
  class FileSystem;
}

namespace wowlib::formats::m2
{
  /** The versions M2 is instantiated (and welded) for: every targeted
      last-minor-of-major release, in release order. Kept in sync with the
      alias/instantiation X-macro below (checked by static_assert). */
  inline constexpr std::array m2_versions{
    versions::vanilla, versions::tbc,         versions::wotlk,
    versions::cata,    versions::mop,         versions::wod,
    versions::legion,  versions::bfa,         versions::shadowlands,
    versions::dragonflight, versions::tww};

  /** The version-agnostic base of every M2<V> (welded as "M2").

      This empty base exists ENTIRELY for the language bindings (Python, Lua):
      it gives the per-version M2* classes a common welded supertype, and the
      module glue attaches the for_version/read/write/convert surface to it.
      It has no role in the C++ API, where you use the concrete M2<V>
      directly.

      @see https://wowdev.wiki/M2 */
  struct [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::weld_as("M2"),
    =welder::doc(R"(
        A whole model, abstract over the client version — the MD20 body with
        its satellite files (.skin, .anim) baked in. Construct the concrete
        version with M2.for_version(expansion), then read()/write(); the
        per-version M2* classes are subclasses. See https://wowdev.wiki/M2.)")
  ]] M2Base
  {
    bool operator==(const M2Base&) const = default;
  };

  namespace detail
  {
    /** WotLK+ assembly members: the external .skin LOD views baked in. */
    template <ClientVersion V>
    struct AssemblySkins
    {
      [[
        =welder::mark::no_reassign,
        =welder::doc(R"(The model's LOD views, in view order ("{model}0N.skin"
                        files, WotLK+); num_skin_profiles on the body must
                        match — write() validates.)")]]
      std::vector<Skin<V>> skins;

      bool operator==(const AssemblySkins&) const = default;
    };

    /** Legion+ assembly members: the chunked shell and the satellites it
        references. */
    template <ClientVersion V>
    struct AssemblyLegion
    {
      [[
        =welder::doc(R"(The chunked .m2 shell: satellite chunks (FileDataIDs,
                        extended particles, parent overrides) plus preserved
                        unknown chunks. Its MD21 blob is the on-disk image;
                        `data` is the decoded form — write() re-encodes data
                        into it and refreshes the FileDataID chunks.)")]]
      M2File<V> shell{};

      [[
        =welder::mark::no_reassign,
        =welder::doc("The LOD-band skins (the SFID entries beyond "
                     "num_skin_profiles), in band order.")]]
      std::vector<Skin<V>> lod_skins;

      [[
        =welder::doc("The referenced .phys file bytes (PFID), baked in "
                     "verbatim — structured physics is a follow-up "
                     "milestone. Inline physics (PFDC) stays on the shell.")]]
      ChunkBlob phys;

      bool operator==(const AssemblyLegion&) const = default;
    };
  }

  /** A whole M2 model for one client version: the MD20 body plus every
      baked-in satellite. Reading resolves external sequence data (.anim) and
      the LOD views (.skin) into the entity; writing re-derives the satellite
      set — sequences with `(flags & 0x130) == 0` split back into
      "{model}AAAA-SS.anim" files, the skins into "{model}0N.skin".

      There is no byte-perfect round-trip for offset formats: writes lay data
      out canonically, and a written model re-reads equal instead.
      @tparam V the client version this assembly targets.
      @see https://wowdev.wiki/M2 */
  template <ClientVersion V>
  struct [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::doc(R"(
        A whole model for one client version: the MD20 body with skins and
        external sequence data baked in. A written model is canonical-layout
        and re-reads equal (no byte-perfect guarantee for offset formats).
        See https://wowdev.wiki/M2.)")
  ]] M2 : M2Base,
          slot<V, m2_per_sequence_timelines, detail::AssemblySkins<V>>,
          slot<V, m2_chunked_container, detail::AssemblyLegion<V>>
  {
    static constexpr ClientVersion version = V;

    [[=welder::doc("The MD20 body.")]]
    M2Data<V> data{};

    // read()/write() weld the (FileSystem, FileKey) load/save on LUA ONLY —
    // mirroring WMO: on Python the module glue attaches the richer
    // read/write/convert/for_version surface to M2Base instead (stage 4).

    [[=welder::mark::only(welder::lang::lua),
      =welder::doc("Load the model and all its satellite files from a client "
                   "filesystem, replacing this entity's contents.")]]
    Result<void> read(fs::FileSystem& fs [[=welder::doc("the filesystem gateway")]],
                      const FileKey& key
                      [[=welder::doc("the .m2 file identity (path and/or FileDataID)")]]);

    [[=welder::mark::only(welder::lang::lua),
      =welder::doc("Serialize and store the model and every satellite file "
                   "through the filesystem's project overlay; satellite names "
                   "derive from the key, which must resolve to a path.")]]
    Result<void> write(fs::FileSystem& fs [[=welder::doc("the filesystem gateway")]],
                       const FileKey& key
                       [[=welder::doc("the .m2 file identity; must resolve to a path")]]) const;

    bool operator==(const M2&) const = default;
  };
}

/** Per-version expansion of the M2 template surface. X(Suffix, version) is
    invoked once per targeted release, in release order — the single spot that
    couples m2::m2_versions, the welded aliases and the explicit
    instantiations. Extending the version list means adding one row here. */
#define WOWLIB_M2_FOR_EACH_VERSION(X)                                                              \
  X(Vanilla, vanilla)                                                                              \
  X(Tbc, tbc)                                                                                      \
  X(Wotlk, wotlk)                                                                                  \
  X(Cata, cata)                                                                                    \
  X(Mop, mop)                                                                                      \
  X(Wod, wod)                                                                                      \
  X(Legion, legion)                                                                                \
  X(Bfa, bfa)                                                                                      \
  X(Shadowlands, shadowlands)                                                                      \
  X(Dragonflight, dragonflight)                                                                    \
  X(TheWarWithin, tww)

/** The subset with external .skin files (Skin<V> exists WotLK+ only). */
#define WOWLIB_M2_FOR_EACH_SKIN_VERSION(X)                                                         \
  X(Wotlk, wotlk)                                                                                  \
  X(Cata, cata)                                                                                    \
  X(Mop, mop)                                                                                      \
  X(Wod, wod)                                                                                      \
  X(Legion, legion)                                                                                \
  X(Bfa, bfa)                                                                                      \
  X(Shadowlands, shadowlands)                                                                      \
  X(Dragonflight, dragonflight)                                                                    \
  X(TheWarWithin, tww)

/** The subset with the chunked shell (M2File<V> exists Legion+ only). */
#define WOWLIB_M2_FOR_EACH_CHUNKED_VERSION(X)                                                      \
  X(Legion, legion)                                                                                \
  X(Bfa, bfa)                                                                                      \
  X(Shadowlands, shadowlands)                                                                      \
  X(Dragonflight, dragonflight)                                                                    \
  X(TheWarWithin, tww)

namespace wowlib::formats::m2
{
#define WOWLIB_M2_ALIAS(Suffix, version_)                                                          \
  using M2Data##Suffix = M2Data<versions::version_>;                                               \
  using M2##Suffix = M2<versions::version_>;
  WOWLIB_M2_FOR_EACH_VERSION(WOWLIB_M2_ALIAS)
#undef WOWLIB_M2_ALIAS

#define WOWLIB_M2_SKIN_ALIAS(Suffix, version_) using Skin##Suffix = Skin<versions::version_>;
  WOWLIB_M2_FOR_EACH_SKIN_VERSION(WOWLIB_M2_SKIN_ALIAS)
#undef WOWLIB_M2_SKIN_ALIAS

#define WOWLIB_M2_FILE_ALIAS(Suffix, version_) using M2File##Suffix = M2File<versions::version_>;
  WOWLIB_M2_FOR_EACH_CHUNKED_VERSION(WOWLIB_M2_FILE_ALIAS)
#undef WOWLIB_M2_FILE_ALIAS

  namespace detail
  {
    /** X-macro coverage check: every alias row must be an m2_versions entry
        and vice versa. */
    consteval bool m2_macro_covers_versions()
    {
      std::size_t rows = 0;
#define WOWLIB_M2_COUNT_ROW(Suffix, version_)                                                      \
  {                                                                                                \
    ++rows;                                                                                        \
    bool found = false;                                                                            \
    for (const ClientVersion& v : m2_versions)                                                     \
      found = found || v == versions::version_;                                                    \
    if (!found)                                                                                    \
      return false;                                                                                \
  }
      WOWLIB_M2_FOR_EACH_VERSION(WOWLIB_M2_COUNT_ROW)
#undef WOWLIB_M2_COUNT_ROW
      return rows == m2_versions.size();
    }
    static_assert(m2_macro_covers_versions(),
                  "WOWLIB_M2_FOR_EACH_VERSION must list exactly the m2_versions entries");
  }

#define WOWLIB_M2_EXTERN(Suffix, version_)                                                         \
  extern template struct M2Data<versions::version_>;                                               \
  extern template struct M2<versions::version_>;
  WOWLIB_M2_FOR_EACH_VERSION(WOWLIB_M2_EXTERN)
#undef WOWLIB_M2_EXTERN

#define WOWLIB_M2_SKIN_EXTERN(Suffix, version_) extern template struct Skin<versions::version_>;
  WOWLIB_M2_FOR_EACH_SKIN_VERSION(WOWLIB_M2_SKIN_EXTERN)
#undef WOWLIB_M2_SKIN_EXTERN

#define WOWLIB_M2_FILE_EXTERN(Suffix, version_) extern template struct M2File<versions::version_>;
  WOWLIB_M2_FOR_EACH_CHUNKED_VERSION(WOWLIB_M2_FILE_EXTERN)
#undef WOWLIB_M2_FILE_EXTERN
}

namespace wowlib::formats
{
  // The OffsetFile bases carry the read()/write() definitions that expand the
  // offset serializer; extern-ing them here (an explicit instantiation must
  // sit in the template's enclosing namespace) confines that expansion to
  // m2.cpp.
#define WOWLIB_M2_EXTERN_SERIALIZER(Suffix, version_)                                              \
  extern template struct OffsetFile<m2::M2Data<versions::version_>>;
  WOWLIB_M2_FOR_EACH_VERSION(WOWLIB_M2_EXTERN_SERIALIZER)
#undef WOWLIB_M2_EXTERN_SERIALIZER

#define WOWLIB_M2_EXTERN_SKIN_SERIALIZER(Suffix, version_)                                         \
  extern template struct OffsetFile<m2::Skin<versions::version_>>;
  WOWLIB_M2_FOR_EACH_SKIN_VERSION(WOWLIB_M2_EXTERN_SKIN_SERIALIZER)
#undef WOWLIB_M2_EXTERN_SKIN_SERIALIZER

#define WOWLIB_M2_EXTERN_FILE_SERIALIZER(Suffix, version_)                                         \
  extern template struct ChunkedFile<m2::M2File<versions::version_>>;
  WOWLIB_M2_FOR_EACH_CHUNKED_VERSION(WOWLIB_M2_EXTERN_FILE_SERIALIZER)
#undef WOWLIB_M2_EXTERN_FILE_SERIALIZER
}
