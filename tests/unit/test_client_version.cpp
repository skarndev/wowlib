#include <catch2/catch_test_macros.hpp>

#include <wowlib/core/client_version.hpp>
#include <wowlib/core/error.hpp>

using namespace wowlib;

TEST_CASE("storage kind classification splits at WoD", "[version]")
{
  CHECK(versions::Vanilla.storageKind() == StorageKind::Mpq);
  CHECK(versions::Tbc.storageKind() == StorageKind::Mpq);
  CHECK(versions::Wotlk.storageKind() == StorageKind::Mpq);
  CHECK(versions::Cata.storageKind() == StorageKind::Mpq);
  CHECK(versions::Mop.storageKind() == StorageKind::Mpq);
  CHECK(versions::Wod.storageKind() == StorageKind::Casc);
  CHECK(versions::Legion.storageKind() == StorageKind::Casc);
  CHECK(versions::Bfa.storageKind() == StorageKind::Casc);
  CHECK(versions::Shadowlands.storageKind() == StorageKind::Casc);
  CHECK(versions::Dragonflight.storageKind() == StorageKind::Casc);
  CHECK(versions::Tww.storageKind() == StorageKind::Casc);
}

TEST_CASE("version constants carry the exact final builds", "[version]")
{
  STATIC_CHECK(versions::Wotlk.build == 12340);
  STATIC_CHECK(versions::Shadowlands.build == 45745);
  STATIC_CHECK(versions::Tww.build == 65299);
}

TEST_CASE("locale codes round-trip", "[version]")
{
  CHECK(localeCode(Locale::enUS) == "enUS");
  CHECK(localeCode(Locale::ruRU) == "ruRU");
  CHECK(localeFromCode("enUS") == Locale::enUS);
  CHECK(localeFromCode("ptBR") == Locale::ptBR);
  CHECK_FALSE(localeFromCode("xxYY").has_value());
}

TEST_CASE("casc locale flags are single distinct bits", "[version]")
{
  CHECK(cascLocaleFlag(Locale::enUS) == 0x2);
  CHECK(cascLocaleFlag(Locale::enUS) != cascLocaleFlag(Locale::ruRU));
}

TEST_CASE("error codes stringify via reflection", "[version][reflect]")
{
  STATIC_CHECK(toString(ErrorCode::FileNotFound) == "FileNotFound");
  CHECK(toString(ErrorCode::EncryptedContent) == "EncryptedContent");
}