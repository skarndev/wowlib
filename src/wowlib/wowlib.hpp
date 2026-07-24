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
wowlib
{
}

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
namespace wowlib
{
  namespace
  [[=welder::doc(R"(
      Client filesystem access: storage backends, listfile databases, the
      project-directory overlay and the FileSystem gateway.)")]]
  fs
  {
  }
}

#include <wowlib/fs/casc/casc_storage.hpp>
#include <wowlib/fs/client_filesystem.hpp>
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
namespace wowlib
{
  namespace
  [[=welder::doc(R"(
      Client file formats: chunked binary serialization with byte-perfect
      round-trips. Versioned formats are flat suffixed classes (WMOWotlk,
      WMOShadowlands, ...) plus load_* factories keyed on Expansion.)")]]
  formats
  {
  }
}

#include <wowlib/formats/common/chunk.hpp>
#include <wowlib/formats/common/flags.hpp>
#include <wowlib/formats/common/string_block.hpp>
#include <wowlib/formats/common/types.hpp>
#include <wowlib/formats/convert.hpp>

// The wmo namespace must first open AFTER the common wire primitives: its
// structs carry NSDMI defaults of common types (SMOHeader's CArgb ambient
// color, CAaBox bounds), and those values convert EAGERLY when the aggregate
// field constructor registers — declaring wmo inside the formats block above
// would make it formats' FIRST member and weld it before CArgb exists.
namespace wowlib::formats
{
  namespace
  [[=welder::doc(R"(
      The WMO (world map object) format: the WMO assembly and its per-version
      classes, split into submodules that mirror the C++ layout (root, root.chunks,
      group, group.chunks). The pre-declaration order is the submodule weld order —
      within each of root/group the chunks wire structs are declared first, so they
      weld before the entities that name them as NSDMI defaults.)")]]
  wmo
  {
    namespace
    [[=welder::doc("The WMO root-file entity: WMORoot and its per-version "
                   "classes.")]]
    root
    {
      namespace
      [[=welder::doc("WMO root-file chunk wire structs (MOHD, MOMT, lights, "
                     "doodads, fog, ambient volumes) and their flag enums.")]]
      chunks
      {
      }
    }

    namespace
    [[=welder::doc("The WMO group-file entities: WMOGroup, WMOGroupBody and "
                   "their per-version classes.")]]
    group
    {
      namespace
      [[=welder::doc("WMO group-file chunk wire structs (the MOGP header, render "
                     "batches, BSP nodes, group lights) and their flag enums.")]]
      chunks
      {
      }
    }
  }
}

// The m2 namespace likewise first opens AFTER the common wire primitives (its
// records carry NSDMI defaults of common types — C3Vector pivots, CAaBox
// bounds) and after wmo, fixing the submodule weld order. Within m2 the
// sub-namespaces mirror the directory tree (body with its records, skin,
// bone); they pre-declare before m2's own entities so every record welds
// before the entities that name them as NSDMI defaults.
namespace wowlib::formats
{
  namespace
  [[=welder::doc(R"(
      The M2 model format: the M2 assembly (the MD20 body plus every baked
      satellite file) and the shared Skeleton entity, with the per-family
      submodules mirroring the C++ layout (body, body.records, skin, bone).)")]]
  m2
  {
    namespace
    [[=welder::doc("The MD20 model body (M2Data) and the Legion+ chunked .m2 "
                   "shell (M2File) with their per-version classes.")]]
    body
    {
      namespace
      [[=welder::doc("M2 body record structs (sequences, bones, tracks, "
                     "textures, cameras, emitters, shell chunk payloads) and "
                     "their flag enums.")]]
      records
      {
      }
    }

    namespace
    [[=welder::doc("The external LOD view entity (.skin file, WotLK+) and the "
                   "skin profile records (submeshes, render batches).")]]
    skin
    {
    }

    namespace
    [[=welder::doc("The .bone facial-pose file entity (WoD+).")]]
    bone
    {
    }
  }
}

#include <wowlib/formats/wmo/convert.hpp>
#include <wowlib/formats/m2/convert.hpp>

// The fs-level read/write definitions (implicit instantiation — the library
// ships no explicit instantiations; see formats/*/io.hpp).
#include <wowlib/formats/wmo/io.hpp>
#include <wowlib/formats/m2/io.hpp>