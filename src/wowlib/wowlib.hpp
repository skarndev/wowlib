#pragma once

/** @file
    Umbrella header: the whole public wowlib API. Include the individual headers
    instead when compile time matters. */

#include <wowlib/core/buffer.hpp>
#include <wowlib/core/client_version.hpp>
#include <wowlib/core/error.hpp>
#include <wowlib/core/file_key.hpp>
#include <wowlib/core/path.hpp>
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