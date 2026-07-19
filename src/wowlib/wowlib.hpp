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
      round-trips. Versioned formats are flat suffixed classes (WmoWotlk,
      WmoShadowlands, ...) plus load_* factories keyed on Expansion.)")]]
  formats
  {
    namespace
    [[=welder::doc(R"(
        The WMO (world map object) format: wire structs shared across the
        supported client versions.)")]]
    wmo
    {
    }
  }
}

#include <wowlib/formats/chunk/types.hpp>
#include <wowlib/formats/common/types.hpp>
#include <wowlib/formats/convert.hpp>
#include <wowlib/formats/wmo/wmo.hpp>