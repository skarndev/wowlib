#pragma once

/** @file
    @brief The client-database (DBFilesClient) shared binding surface.

    This unit is part of the MAIN welder walk (included by wowlib_module.cpp). It
    welds only what is shared across every generated table — the @c LocString
    value types and the encrypted-section types — into @c namespace @c db, which
    makes welder create the @c wowlib.db submodule.

    The tables themselves are NOT here: there are ~1200 of them, so they are
    sharded across the generated @c db_shard_N.cpp translation units (dbdgen
    output) for parallel compilation. Each shard welds its slice into the
    @c db.rowbase / @c db.tables submodules that @c db_shards.hpp's
    @c register_all() creates after this walk. */

#include <welder/vocabulary.hpp>

namespace wowlib
{
  namespace
  [[=welder::doc("Client-side database files (DBFilesClient): typed table rows "
                 "and the value types they share.")]]
  db
  {
  }
}

#include <wowlib/db/locstring.hpp>
#include <wowlib/db/table.hpp>
