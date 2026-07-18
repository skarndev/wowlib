#include <catch2/catch_test_macros.hpp>

#include <wowlib/core/client_version.hpp>
#include <wowlib/core/error.hpp>

using namespace wowlib;

TEST_CASE("storage kind classification splits at WoD", "[version]")
{
  CHECK(ClientVersion::vanilla().storage_kind() == StorageKind::Mpq);
  CHECK(ClientVersion::tbc().storage_kind() == StorageKind::Mpq);
  CHECK(ClientVersion::wotlk().storage_kind() == StorageKind::Mpq);
  CHECK(ClientVersion::cata().storage_kind() == StorageKind::Mpq);
  CHECK(ClientVersion::mop().storage_kind() == StorageKind::Mpq);
  CHECK(ClientVersion::wod().storage_kind() == StorageKind::Casc);
  CHECK(ClientVersion::legion().storage_kind() == StorageKind::Casc);
  CHECK(ClientVersion::bfa().storage_kind() == StorageKind::Casc);
  CHECK(ClientVersion::shadowlands().storage_kind() == StorageKind::Casc);
  CHECK(ClientVersion::dragonflight().storage_kind() == StorageKind::Casc);
  CHECK(ClientVersion::tww().storage_kind() == StorageKind::Casc);
}

TEST_CASE("error codes stringify via reflection", "[version][reflect]")
{
  STATIC_CHECK(to_string(ErrorCode::FileNotFound) == "FileNotFound");
  CHECK(to_string(ErrorCode::EncryptedContent) == "EncryptedContent");
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