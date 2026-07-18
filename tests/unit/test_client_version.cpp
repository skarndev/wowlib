#include <catch2/catch_test_macros.hpp>

#include <wowlib/core/client_version.hpp>
#include <wowlib/core/error.hpp>

using namespace wowlib;

TEST_CASE("storage kind classification splits at WoD", "[version]")
{
  CHECK(versions::vanilla.storage_kind() == StorageKind::Mpq);
  CHECK(versions::tbc.storage_kind() == StorageKind::Mpq);
  CHECK(versions::wotlk.storage_kind() == StorageKind::Mpq);
  CHECK(versions::cata.storage_kind() == StorageKind::Mpq);
  CHECK(versions::mop.storage_kind() == StorageKind::Mpq);
  CHECK(versions::wod.storage_kind() == StorageKind::Casc);
  CHECK(versions::legion.storage_kind() == StorageKind::Casc);
  CHECK(versions::bfa.storage_kind() == StorageKind::Casc);
  CHECK(versions::shadowlands.storage_kind() == StorageKind::Casc);
  CHECK(versions::dragonflight.storage_kind() == StorageKind::Casc);
  CHECK(versions::tww.storage_kind() == StorageKind::Casc);
}

TEST_CASE("version constants carry the exact final builds", "[version]")
{
  STATIC_CHECK(versions::wotlk.build == 12340);
  STATIC_CHECK(versions::shadowlands.build == 45745);
  STATIC_CHECK(versions::tww.build == 65299);
}

TEST_CASE("locale codes round-trip", "[version]")
{
  CHECK(locale_code(Locale::enUS) == "enUS");
  CHECK(locale_code(Locale::ruRU) == "ruRU");
  CHECK(locale_from_code("enUS") == Locale::enUS);
  CHECK(locale_from_code("ptBR") == Locale::ptBR);
  CHECK_FALSE(locale_from_code("xxYY").has_value());
}

TEST_CASE("casc locale flags are single distinct bits", "[version]")
{
  CHECK(casc_locale_flag(Locale::enUS) == 0x2);
  CHECK(casc_locale_flag(Locale::enUS) != casc_locale_flag(Locale::ruRU));
}

TEST_CASE("error codes stringify via reflection", "[version][reflect]")
{
  STATIC_CHECK(to_string(ErrorCode::FileNotFound) == "FileNotFound");
  CHECK(to_string(ErrorCode::EncryptedContent) == "EncryptedContent");
}