#pragma once

/** @file
    Umbrella header: the whole public wowlib API. Include the individual headers
    instead when compile time matters. */

#include <welder/vocabulary.hpp>

namespace
[[=welder::doc(R"(
    Reading and writing World of Warcraft client files: client filesystem access
    (MPQ and CASC), listfile databases, and a project-directory overlay for
    modding.)")]]
wowlib {}

#include <wowlib/core/buffer.hpp>
#include <wowlib/core/client_version.hpp>
#include <wowlib/core/error.hpp>
#include <wowlib/core/expansion.hpp>
#include <wowlib/core/file_key.hpp>
#include <wowlib/core/path.hpp>
#include <wowlib/core/reflect.hpp>

// The fs namespace must first open AFTER the core types: welder's module walk
// binds namespace members in declaration order, and FileSystemSettings' NSDMI
// defaults (a FileDataID value) convert EAGERLY at registration — the types
// they name have to be registered before the fs submodule welds.
namespace wowlib {
  namespace
  [[=welder::doc(R"(
      Client filesystem access: storage backends, listfile databases, the
      project-directory overlay and the FileSystem gateway.)")]]
  fs {}
}

#include <wowlib/fs/casc/casc_storage.hpp>
#include <wowlib/fs/client_filesystem.hpp>
#include <wowlib/fs/client_install.hpp>
#include <wowlib/fs/csv_listfile.hpp>
#include <wowlib/fs/fdid_allocator.hpp>
#include <wowlib/fs/filesystem.hpp>
#include <wowlib/fs/listfile.hpp>
#include <wowlib/fs/mpq/mpq_chain.hpp>
#include <wowlib/fs/mpq/mpq_storage.hpp>
#include <wowlib/fs/project_directory.hpp>
#include <wowlib/fs/storage_backend.hpp>

// formats opens after fs for the same reason fs opens after core: welder binds
// namespace members in declaration order, and format entities name fs types
// (FileSystem, FileKey) in their signatures.
namespace wowlib {
  namespace
  [[=welder::doc(R"(
      Client file formats: chunked binary serialization with byte-perfect
      round-trips. Versioned formats are flat suffixed classes (WMOWotlk,
      WMOShadowlands, ...) plus for_version factories keyed on an Expansion or
      on a full ClientVersion — the latter being the only axis that can name a
      Classic client.)")]]
  formats {}
}

#include <wowlib/formats/common/chunked_file.hpp>
#include <wowlib/formats/common/flags.hpp>
#include <wowlib/formats/common/map_placements.hpp>
#include <wowlib/formats/common/string_block.hpp>
#include <wowlib/formats/common/types.hpp>
#include <wowlib/formats/convert.hpp>

// The wmo namespace must first open AFTER the common binary primitives: its
// structs carry NSDMI defaults of common types (SMOHeader's CArgb ambient
// color, CAaBox bounds), and those values convert EAGERLY when the aggregate
// field constructor registers — declaring wmo inside the formats block above
// would make it formats' FIRST member and weld it before CArgb exists.
namespace wowlib::formats {
  namespace
  [[=welder::doc(R"(
      The WMO (world map object) format: the WMO assembly and its per-version
      classes, split into submodules that mirror the C++ layout (root, root.chunks,
      group, group.chunks). The pre-declaration order is the submodule weld order —
      within each of root/group the chunks binary structs are declared first, so they
      weld before the entities that name them as NSDMI defaults.)")]]
  wmo {
    namespace
    [[=welder::doc("The WMO root-file entity: WMORoot and its per-version "
      "classes.")]]
    root {
      namespace
      [[=welder::doc("WMO root-file chunk binary structs (MOHD, MOMT, lights, "
        "doodads, fog, ambient volumes) and their flag enums.")]]
      chunks {}
    }

    namespace
    [[=welder::doc("The WMO group-file entities: WMOGroup, WMOGroupBody and "
      "their per-version classes.")]]
    group {
      namespace
      [[=welder::doc(
        "WMO group-file chunk binary structs (the MOGP header, render "
        "batches, BSP nodes, group lights) and their flag enums.")]]
      chunks {}
    }
  }
}

// The m2 namespace likewise first opens AFTER the common binary primitives (its
// records carry NSDMI defaults of common types — C3Vector pivots, CAaBox
// bounds) and after wmo, fixing the submodule weld order. Within m2 the
// sub-namespaces mirror the directory tree (root with its record structs,
// chunked with its own, skin, bone); they pre-declare before m2's own
// entities so every record welds before the entities that name them as NSDMI
// defaults.
namespace wowlib::formats {
  namespace
  [[=welder::doc(R"(
      The M2 model format: the M2 assembly (the MD20 body plus every baked
      satellite file) and the shared Skeleton entity, with the per-family
      submodules mirroring the C++ layout (root, root.record, chunked,
      chunked.record, skin, bone).)")]]
  m2 {
    namespace
    [[=welder::doc("The MD20 model body (M2Root) with its per-version "
      "classes.")]]
    root {
      namespace
      [[=welder::doc("M2 body record structs (sequences, bones, tracks, "
        "textures, cameras, emitters) and their flag enums.")]]
      record {}
    }

    namespace
    [[=welder::doc("The Legion+ chunked .m2 shell (M2ChunkedFile) with its "
      "per-version classes and the companion-chunk payload "
      "records (AFID entries, extended particles, parent-model "
      "overrides).")]]
    chunked {
      namespace
      [[=welder::doc("Companion-chunk payload records of the chunked .m2 "
        "shell.")]]
      record {}
    }

    namespace
    [[=welder::doc("The external LOD view entity (.skin file, WotLK+) and the "
      "skin profile records (submeshes, render batches).")]]
    skin {}

    namespace
    [[=welder::doc("The .bone facial-pose file entity (WoD+).")]]
    bone {}
  }
}

// The wdt namespace opens after m2 for the same submodule-order reason; its
// sub-namespaces (root, occlusion, lights, fogs, mpv — mirroring the
// directory tree) each pre-declare their chunks binary structs first, so they
// weld before the entities that name them as NSDMI defaults.
namespace wowlib::formats {
  namespace
  [[=welder::doc(R"(
      The WDT map-description format: the WDT assembly (the main .wdt file plus
      its era's _occ/_lgt/_fogs/_mpv satellite files) with the per-file
      submodules mirroring the C++ layout (root, occlusion, lights, fogs,
      mpv).)")]]
  wdt {
    namespace
    [[=welder::doc("The WDT main-file entity: WDTRoot and its per-version "
      "classes.")]]
    root {
      namespace
      [[=welder::doc(
        "WDT main-file chunk binary structs (the MPHD map header, MAIN "
        "tile table, MAID FileDataIDs) and their flag enums.")]]
      chunks {}
    }

    namespace
    [[=welder::doc("The _occ.wdt occlusion satellite entity (WoD+).")]]
    occlusion {
      namespace
      [[=welder::doc("_occ.wdt chunk binary structs (the MAOI tile index).")]]
      chunks {}
    }

    namespace
    [[=welder::doc("The _lgt.wdt lights satellite entity (WoD+).")]]
    lights {
      namespace
      [[=welder::doc(
        "_lgt.wdt chunk binary structs (point lights, spot lights, "
        "light animations).")]]
      chunks {}
    }

    namespace
    [[=welder::doc("The _fogs.wdt volumetric-fog satellite entity (Legion "
      "7.2.5+).")]]
    fogs {
      namespace
      [[=welder::doc(
        "_fogs.wdt chunk binary structs (volumetric fogs and their "
        "extensions).")]]
      chunks {}
    }

    namespace
    [[=welder::doc("The _mpv.wdt particulate-volume satellite entity (BfA+).")]]
    mpv {
      namespace
      [[=welder::doc("_mpv.wdt chunk binary structs (particulate points and "
        "bounds).")]]
      chunks {}
    }
  }
}

// The wdl namespace opens after wdt, fixing the submodule weld order; its
// chunks binary structs pre-declare before the entity that names them.
namespace wowlib::formats {
  namespace
  [[=welder::doc(R"(
      The WDL low-resolution heightmap format: the WDL entity and its
      per-version classes, with the chunk binary structs in the chunks
      submodule.)")]]
  wdl {
    namespace
    [[=welder::doc(
      "WDL chunk binary structs (per-tile heightmaps, hole and ocean "
      "masks, low-resolution placements, sky scenes).")]]
    chunks {}
  }
}

// The adt namespace opens after wdl, fixing the submodule weld order; its
// chunks binary structs pre-declare before the MapChunk/ADT entities that name
// them as NSDMI defaults.
namespace wowlib::formats {
  namespace
  [[=welder::doc(R"(
      The ADT terrain-tile format: the ADT tile entity and its 256 MapChunk
      terrain cells (both per-version classes), the structured MH2O/MCLQ liquid,
      and the chunk binary structs in the chunks submodule. One unified ADT spans
      the pre-Cataclysm single .adt and the Cataclysm+ root/_tex0/_obj0 split.)")
  ]]
  adt {
    namespace
    [[=welder::doc(
      "ADT chunk binary structs (the MCNK cell header, texture layers, "
      "liquid records, flying bounds) and their flag enums.")]]
    chunks {}
  }
}

// The blp namespace opens after adt, fixing the submodule weld order. BLP is
// version-stable across every client release, so the namespace holds one
// unversioned entity (plus the Image/EncodeSettings vocabulary) — no chunks
// submodule and no per-version classes.
namespace wowlib::formats {
  namespace
  [[=welder::doc(R"(
      The BLP texture format: the version-stable BLP2 entity with byte-perfect
      round-trips, RGBA8 decode of every shipped encoding (palettized,
      DXT1/3/5, BC5, raw BGRA) and full re-encoding (palette quantization, DXT
      compression, mip-chain generation).)")]]
  blp {}
}

#include <wowlib/formats/wmo/convert.hpp>
#include <wowlib/formats/m2/convert.hpp>
#include <wowlib/formats/wdt/convert.hpp>
#include <wowlib/formats/wdl/convert.hpp>
#include <wowlib/formats/adt/convert.hpp>

// The format entities carry their fs-level read/write definitions inline
// (implicit instantiation — the library ships no explicit instantiations).
#include <wowlib/formats/wmo/wmo.hpp>
#include <wowlib/formats/m2/m2.hpp>
#include <wowlib/formats/wdt/wdt.hpp>
#include <wowlib/formats/wdl/wdl.hpp>
#include <wowlib/formats/adt/adt.hpp>
#include <wowlib/formats/blp/blp.hpp>

// The audit namespace opens last: its entry point names fs::FileSystem and
// ClientVersion in signatures, and everything it touches is registered by now.
namespace wowlib {
  namespace
  [[=welder::doc(R"(
      Exhaustive round-trip auditing: enumerate a client's files (see
      FileSystem.enumerate_paths) and round-trip them one at a time through
      the matching format entity, collecting per-file outcome reports —
      read->write->compare only, no semantic validation.)")]]
  audit {}
}

#include <wowlib/audit/roundtrip.hpp>
