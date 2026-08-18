#include <catch2/catch_test_macros.hpp>

#include <wowlib/core/client_version.hpp>
#include <wowlib/core/expansion.hpp>
#include <wowlib/formats/adt/adt.hpp>
#include <wowlib/formats/m2/boundaries.hpp>
#include <wowlib/formats/wmo/boundaries.hpp>
#include <wowlib/formats/wmo/wmo.hpp>

using namespace wowlib;
using namespace wowlib::formats;

// The Classic products reuse legacy version numbers on top of whatever retail
// engine was current when a build was cut, so nothing about their file layout
// follows from major/minor. These tests pin the one thing that does: the build
// number's placement on the retail engine timeline.

TEST_CASE("retail versions are their own format lineage", "[version][flavor]")
{
  STATIC_CHECK(versions::wotlk.format_lineage() == versions::wotlk);
  STATIC_CHECK(versions::tww.format_lineage() == versions::tww);
  STATIC_CHECK_FALSE(versions::wotlk.is_classic());
}

TEST_CASE("classic builds map onto the retail engine that produced them",
          "[version][flavor]")
{
  // BCC 2.5.4 (build 44833, July 2022) was built on the Shadowlands engine.
  STATIC_CHECK(versions::classic_bcc.format_lineage() == versions::shadowlands);
  // WotLK Classic 3.4.5 and Cata Classic 4.4.2 are War Within-era clients.
  STATIC_CHECK(versions::classic_wotlk.format_lineage() == versions::tww);
  STATIC_CHECK(versions::classic_cata.format_lineage() == versions::tww);
  // Classic Era 1.15.9, MoP Classic 5.5.4 and the Anniversary realms are on the
  // Midnight engine, which wowlib does not model yet — they clamp to TWW.
  STATIC_CHECK(versions::classic_era.format_lineage() == versions::tww);
  STATIC_CHECK(versions::classic_mop.format_lineage() == versions::tww);
  STATIC_CHECK(versions::anniversary.format_lineage() == versions::tww);
}

TEST_CASE("one classic version number can straddle two engines", "[version][flavor]")
{
  // Cataclysm Classic 4.4.0 opened 2024-04 on the Dragonflight engine and was
  // still shipping builds in 2024-10, by which time The War Within had gone
  // live and the branch had moved with it. Identical version number, different
  // file layouts — which is exactly why the build is the key and the tuple is
  // not.
  constexpr ClientVersion cata_classic_first{4, 4, 0, 54481, ClientFlavor::Classic};
  constexpr ClientVersion cata_classic_last{4, 4, 0, 57244, ClientFlavor::Classic};
  STATIC_CHECK(cata_classic_first.format_lineage() == versions::dragonflight);
  STATIC_CHECK(cata_classic_last.format_lineage() == versions::tww);
}

TEST_CASE("a version tuple no expansion ever had still resolves", "[version][flavor]")
{
  // 'wow_classic_titan' calls itself 3.80.x. Nothing can be read off that
  // tuple; the build places it on the TWW engine all the same.
  constexpr ClientVersion titan{3, 80, 2, 69137, ClientFlavor::Classic};
  STATIC_CHECK(titan.format_lineage() == versions::tww);
}

TEST_CASE("every classic client is CASC whatever its version says",
          "[version][flavor]")
{
  STATIC_CHECK(versions::classic_era.storage_kind() == StorageKind::Casc);
  STATIC_CHECK(versions::classic_bcc.storage_kind() == StorageKind::Casc);
  STATIC_CHECK(versions::classic_wotlk.storage_kind() == StorageKind::Casc);
  STATIC_CHECK(versions::classic_cata.storage_kind() == StorageKind::Casc);
  STATIC_CHECK(versions::classic_mop.storage_kind() == StorageKind::Casc);
  STATIC_CHECK(versions::anniversary.storage_kind() == StorageKind::Casc);
  // The retail split is untouched.
  STATIC_CHECK(versions::mop.storage_kind() == StorageKind::Mpq);
}

TEST_CASE("flavors name their default TACT product", "[version][flavor]")
{
  STATIC_CHECK(versions::wotlk.default_casc_product() == "wow");
  STATIC_CHECK(versions::classic_era.default_casc_product() == "wow_classic_era");
  STATIC_CHECK(versions::classic_cata.default_casc_product() == "wow_classic");
  STATIC_CHECK(versions::anniversary.default_casc_product() == "wow_anniversary");
}

TEST_CASE("expansion_of stays the content axis", "[version][flavor][expansion]")
{
  // Cata Classic IS Cataclysm, content-wise — and writes War Within files.
  STATIC_CHECK(expansion_of(versions::classic_cata) == Expansion::Cata);
  STATIC_CHECK(to_expansion(versions::classic_cata.format_lineage())
               == Expansion::TheWarWithin);
  // A Classic constant is not an expansion release, so nothing matches it.
  STATIC_CHECK_FALSE(to_expansion(versions::classic_cata).has_value());
}

TEST_CASE("classic versions canonicalize onto existing retail instantiations",
          "[version][flavor][ranges]")
{
  // The payoff: every Classic version collapses to a grid version that already
  // exists, so support costs no new template instantiation or welded class.
  STATIC_CHECK(canonical_version(versions::classic_cata, wmo::wmo_assembly_pivots,
                                 wmo::wmo_versions)
               == versions::tww);
  STATIC_CHECK(canonical_version(versions::classic_bcc, wmo::wmo_assembly_pivots,
                                 wmo::wmo_versions)
               == versions::shadowlands);
  STATIC_CHECK(canonical_version(versions::classic_era, adt::adt_pivots,
                                 adt::adt_versions)
               == canonical_version(versions::tww, adt::adt_pivots,
                                    adt::adt_versions));
  // Same type, not merely the same version value.
  STATIC_CHECK(std::is_same_v<wmo::WMO<versions::classic_cata>,
                              wmo::WMO<versions::tww>>);
  STATIC_CHECK(std::is_same_v<wmo::WMO<versions::classic_bcc>,
                              wmo::WMO<versions::shadowlands>>);
}

TEST_CASE("a classic entity gets its engine's chunk set, not its era's",
          "[version][flavor][ranges]")
{
  // WMOGroupBody<cata_classic> must be the modern group body: if the 4.4 tuple
  // leaked into the version math it would land on the Cataclysm layout.
  STATIC_CHECK_FALSE(std::is_same_v<wmo::group::WMOGroupBody<versions::classic_cata>,
                                    wmo::group::WMOGroupBody<versions::cata>>);
  STATIC_CHECK(std::is_same_v<wmo::group::WMOGroupBody<versions::classic_cata>,
                              wmo::group::WMOGroupBody<versions::tww>>);
}
