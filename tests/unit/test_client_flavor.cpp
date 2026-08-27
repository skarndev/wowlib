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
  STATIC_CHECK(versions::Wotlk.formatLineage() == versions::Wotlk);
  STATIC_CHECK(versions::Tww.formatLineage() == versions::Tww);
  STATIC_CHECK_FALSE(versions::Wotlk.isClassic());
}

TEST_CASE("classic builds map onto the retail engine that produced them",
          "[version][flavor]")
{
  // BCC 2.5.4 (build 44833, July 2022) was built on the Shadowlands engine.
  STATIC_CHECK(versions::ClassicBcc.formatLineage() == versions::Shadowlands);
  // WotLK Classic 3.4.5 and Cata Classic 4.4.2 are War Within-era clients.
  STATIC_CHECK(versions::ClassicWotlk.formatLineage() == versions::Tww);
  STATIC_CHECK(versions::ClassicCata.formatLineage() == versions::Tww);
  // Classic Era 1.15.9, MoP Classic 5.5.4 and the Anniversary realms are on the
  // Midnight engine, which wowlib does not model yet — they clamp to TWW.
  STATIC_CHECK(versions::ClassicEra.formatLineage() == versions::Tww);
  STATIC_CHECK(versions::ClassicMop.formatLineage() == versions::Tww);
  STATIC_CHECK(versions::Anniversary.formatLineage() == versions::Tww);
}

TEST_CASE("one classic version number can straddle two engines", "[version][flavor]")
{
  // Cataclysm Classic 4.4.0 opened 2024-04 on the Dragonflight engine and was
  // still shipping builds in 2024-10, by which time The War Within had gone
  // live and the branch had moved with it. Identical version number, different
  // file layouts — which is exactly why the build is the key and the tuple is
  // not.
  constexpr ClientVersion cataClassicFirst{4, 4, 0, 54481, ClientFlavor::Classic};
  constexpr ClientVersion cataClassicLast{4, 4, 0, 57244, ClientFlavor::Classic};
  STATIC_CHECK(cataClassicFirst.formatLineage() == versions::Dragonflight);
  STATIC_CHECK(cataClassicLast.formatLineage() == versions::Tww);
}

TEST_CASE("a version tuple no expansion ever had still resolves", "[version][flavor]")
{
  // 'wow_classic_titan' calls itself 3.80.x. Nothing can be read off that
  // tuple; the build places it on the TWW engine all the same.
  constexpr ClientVersion titan{3, 80, 2, 69137, ClientFlavor::Classic};
  STATIC_CHECK(titan.formatLineage() == versions::Tww);
}

TEST_CASE("every classic client is CASC whatever its version says",
          "[version][flavor]")
{
  STATIC_CHECK(versions::ClassicEra.storageKind() == StorageKind::Casc);
  STATIC_CHECK(versions::ClassicBcc.storageKind() == StorageKind::Casc);
  STATIC_CHECK(versions::ClassicWotlk.storageKind() == StorageKind::Casc);
  STATIC_CHECK(versions::ClassicCata.storageKind() == StorageKind::Casc);
  STATIC_CHECK(versions::ClassicMop.storageKind() == StorageKind::Casc);
  STATIC_CHECK(versions::Anniversary.storageKind() == StorageKind::Casc);
  // The retail split is untouched.
  STATIC_CHECK(versions::Mop.storageKind() == StorageKind::Mpq);
}

TEST_CASE("flavors name their default TACT product", "[version][flavor]")
{
  STATIC_CHECK(versions::Wotlk.defaultCascProduct() == "wow");
  STATIC_CHECK(versions::ClassicEra.defaultCascProduct() == "wow_classic_era");
  STATIC_CHECK(versions::ClassicCata.defaultCascProduct() == "wow_classic");
  STATIC_CHECK(versions::Anniversary.defaultCascProduct() == "wow_anniversary");
}

TEST_CASE("expansion_of stays the content axis", "[version][flavor][expansion]")
{
  // Cata Classic IS Cataclysm, content-wise — and writes War Within files.
  STATIC_CHECK(expansionOf(versions::ClassicCata) == Expansion::Cata);
  STATIC_CHECK(toExpansion(versions::ClassicCata.formatLineage())
               == Expansion::TheWarWithin);
  // A Classic constant is not an expansion release, so nothing matches it.
  STATIC_CHECK_FALSE(toExpansion(versions::ClassicCata).has_value());
}

TEST_CASE("classic versions canonicalize onto existing retail instantiations",
          "[version][flavor][ranges]")
{
  // The payoff: every Classic version collapses to a grid version that already
  // exists, so support costs no new template instantiation or welded class.
  STATIC_CHECK(canonicalVersion(versions::ClassicCata, wmo::WmoAssemblyPivots,
                                 wmo::WmoVersions)
               == versions::Tww);
  STATIC_CHECK(canonicalVersion(versions::ClassicBcc, wmo::WmoAssemblyPivots,
                                 wmo::WmoVersions)
               == versions::Shadowlands);
  STATIC_CHECK(canonicalVersion(versions::ClassicEra, adt::AdtPivots,
                                 adt::AdtVersions)
               == canonicalVersion(versions::Tww, adt::AdtPivots,
                                    adt::AdtVersions));
  // Same type, not merely the same version value.
  STATIC_CHECK(std::is_same_v<wmo::WMO<versions::ClassicCata>,
                              wmo::WMO<versions::Tww>>);
  STATIC_CHECK(std::is_same_v<wmo::WMO<versions::ClassicBcc>,
                              wmo::WMO<versions::Shadowlands>>);
}

TEST_CASE("a classic entity gets its engine's chunk set, not its era's",
          "[version][flavor][ranges]")
{
  // WMOGroupBody<cata_classic> must be the modern group body: if the 4.4 tuple
  // leaked into the version math it would land on the Cataclysm layout.
  STATIC_CHECK_FALSE(std::is_same_v<wmo::group::WMOGroupBody<versions::ClassicCata>,
                                    wmo::group::WMOGroupBody<versions::Cata>>);
  STATIC_CHECK(std::is_same_v<wmo::group::WMOGroupBody<versions::ClassicCata>,
                              wmo::group::WMOGroupBody<versions::Tww>>);
}
