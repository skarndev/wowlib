#pragma once

/** @file
    @brief The client-database (DBFilesClient) shared binding surface.

    This unit is part of the MAIN welder walk (included by wowlib_module.cpp).
    It welds the GENERIC table surface: the runtime-schema @c Table (DynTable),
    its @c Column / @c ColumnType metadata, the @c LocString value types and
    the encrypted-section types — into @c namespace @c db, which makes welder
    create the @c wowlib.db submodule.

    There are NO per-table classes anymore: ~1200 tables x 11 eras used to
    compile as generated welds (96 shard TUs, the single largest cost of the
    whole build); the schema is data now (the WDBS blob), and one welded
    class serves every table of every era. db_dyn.hpp adds the hand-written
    ergonomics on top (row proxies, zero-copy numpy columns). */

#include <welder/vocabulary.hpp>

namespace wowlib
{
  namespace
  [[=welder::doc("Client-side database files (DBFilesClient): the "
                 "runtime-schema Table and the value types it shares.")]]
  db
  {
  }
}

#include <wowlib/db/dyn_table.hpp>
#include <wowlib/db/locstring.hpp>
#include <wowlib/db/table.hpp>
