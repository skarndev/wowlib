#pragma once

/** @file
    @brief The client-database (DBFilesClient) binding surface (proof: Map only).

    The db namespaces are declared+doc'd here; the generated table headers put
    each table's welded supertypes in db::tables (table supertype {table}_) and
    db::rowbase ({table} row supertype). The per-range concrete classes weld
    through the aliases below (welder names a class-template instantiation by its
    namespace-scope alias). */

#include <welder/vocabulary.hpp>

namespace wowlib
{
  namespace
  [[=welder::doc("Client-side database files (DBFilesClient): typed table rows.")]]
  db
  {
    namespace [[=welder::doc("Row supertypes, one per table.")]] rowbase {}
    namespace [[=welder::doc("The generated table classes and row/table "
                             "supertypes.")]] tables {}
  }
}

#include <wowlib/db/locstring.hpp>
#include <wowlib/db/table.hpp>
#include <wowlib/db/tables/map.hpp>

namespace wowlib::db::tables
{
  using MapWotlk = Map<versions::wotlk>;
  using MapRecordWotlk = MapRecord<versions::wotlk>;
}
