#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <vector>

#include <wowlib/fs/mpq/mpq_chain.hpp>

using namespace wowlib;
namespace fsys = std::filesystem;

namespace
{
  // Builds a throwaway fake Data/ tree; archives are empty files — expansion only
  // checks existence.
  struct FakeDataDir
  {
    FakeDataDir()
      : root{fsys::temp_directory_path() / "wowlib-tests" /
             ("fake-data-" + std::to_string(counter++))}
    {
      fsys::remove_all(root);
      fsys::create_directories(root);
    }

    ~FakeDataDir() { fsys::remove_all(root); }

    void add(std::string_view relative)
    {
      const auto path = root / relative;
      fsys::create_directories(path.parent_path());
      std::ofstream{path};
    }

    fsys::path root;
    static inline int counter = 0;
  };

  std::vector<std::string> names(const std::vector<fsys::path>& paths,
                                 const fsys::path& root)
  {
    std::vector<std::string> out;
    for (const auto& p : paths)
      out.push_back(fsys::relative(p, root).generic_string());
    return out;
  }
}

TEST_CASE("the full 3.3.5a chain expands in client load order", "[mpq-chain]")
{
  FakeDataDir data;
  for (const char* archive :
       {"common.MPQ", "common-2.MPQ", "expansion.MPQ", "lichking.MPQ", "patch.MPQ",
        "patch-2.MPQ", "patch-3.MPQ"})
    data.add(archive);
  for (const char* archive :
       {"enUS/locale-enUS.MPQ", "enUS/speech-enUS.MPQ", "enUS/expansion-locale-enUS.MPQ",
        "enUS/expansion-speech-enUS.MPQ", "enUS/lichking-locale-enUS.MPQ",
        "enUS/lichking-speech-enUS.MPQ", "enUS/patch-enUS.MPQ", "enUS/patch-enUS-2.MPQ",
        "enUS/patch-enUS-3.MPQ"})
    data.add(archive);
  // distractors the chain must skip
  data.add("enUS/base-enUS.MPQ");
  data.add("enUS/backup-enUS.MPQ");

  const auto* spec = find_chain_spec(ClientVersion::wotlk());
  REQUIRE(spec != nullptr);

  const auto chain = expand_chain(*spec, data.root, Locale::enUS);
  REQUIRE(chain.has_value());
  CHECK(names(*chain, data.root) ==
        std::vector<std::string>{
          "common.MPQ", "common-2.MPQ", "expansion.MPQ", "lichking.MPQ",
          "enUS/locale-enUS.MPQ", "enUS/speech-enUS.MPQ",
          "enUS/expansion-locale-enUS.MPQ", "enUS/expansion-speech-enUS.MPQ",
          "enUS/lichking-locale-enUS.MPQ", "enUS/lichking-speech-enUS.MPQ",
          "patch.MPQ", "patch-2.MPQ", "patch-3.MPQ", "enUS/patch-enUS.MPQ",
          "enUS/patch-enUS-2.MPQ", "enUS/patch-enUS-3.MPQ"});
}

TEST_CASE("custom patches load last, numbers before letters", "[mpq-chain]")
{
  FakeDataDir data;
  data.add("common.MPQ");
  data.add("patch.MPQ");
  data.add("enUS/locale-enUS.MPQ");
  // deliberately created out of order
  data.add("patch-A.MPQ");
  data.add("patch-4.MPQ");
  data.add("patch-9.MPQ");
  data.add("patch-Z.MPQ");
  data.add("enUS/patch-enUS-B.MPQ");
  data.add("enUS/patch-enUS-5.MPQ");

  const auto chain =
    expand_chain(*find_chain_spec(ClientVersion::wotlk()), data.root, Locale::enUS);
  REQUIRE(chain.has_value());
  CHECK(names(*chain, data.root) ==
        std::vector<std::string>{"common.MPQ", "enUS/locale-enUS.MPQ", "patch.MPQ",
                                 "patch-4.MPQ", "patch-9.MPQ", "patch-A.MPQ",
                                 "patch-Z.MPQ", "enUS/patch-enUS-5.MPQ",
                                 "enUS/patch-enUS-B.MPQ"});
}

TEST_CASE("missing archives are skipped without error", "[mpq-chain]")
{
  FakeDataDir data;
  data.add("common.MPQ");
  data.add("enUS/locale-enUS.MPQ");

  const auto chain =
    expand_chain(*find_chain_spec(ClientVersion::wotlk()), data.root, Locale::enUS);
  REQUIRE(chain.has_value());
  CHECK(names(*chain, data.root) ==
        std::vector<std::string>{"common.MPQ", "enUS/locale-enUS.MPQ"});
}

TEST_CASE("locale detection finds exactly one locale", "[mpq-chain]")
{
  FakeDataDir data;
  data.add("ruRU/locale-ruRU.MPQ");
  CHECK(detect_locale(data.root) == Locale::ruRU);

  data.add("enUS/locale-enUS.MPQ");
  CHECK(detect_locales(data.root).size() == 2);
  CHECK_FALSE(detect_locale(data.root).has_value());   // ambiguous -> caller decides
}

TEST_CASE("unknown versions have no chain spec", "[mpq-chain]")
{
  CHECK(find_chain_spec(ClientVersion{3, 1, 0, 9767}) == nullptr);
  // build-exact and version-triple matches both hit the 3.3.5a table
  CHECK(find_chain_spec(ClientVersion{3, 3, 5, 12340}) != nullptr);
  CHECK(find_chain_spec(ClientVersion{3, 3, 5, 0}) != nullptr);
}