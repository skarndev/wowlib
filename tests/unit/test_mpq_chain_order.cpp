#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <vector>

#include <wowlib/fs/mpq/mpq_chain.hpp>

using namespace wowlib;
using namespace wowlib::fs::detail;
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

    // A loose-file folder standing in for an archive of that name.
    void add_dir(std::string_view relative)
    {
      fsys::create_directories(root / relative);
    }

    fsys::path root;
    static inline int counter = 0;
  };

  std::vector<std::string> names(const std::vector<ChainMember>& members,
                                 const fsys::path& root)
  {
    std::vector<std::string> out;
    for (const auto& m : members)
      out.push_back(fsys::relative(m.path, root).generic_string());
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

  const auto* spec = findChainSpec(versions::Wotlk);
  REQUIRE(spec != nullptr);

  const auto chain = expandChain(*spec, data.root, Locale::enUS);
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

TEST_CASE("base and locale patches interleave by case-insensitive filename",
          "[mpq-chain]")
{
  // One merged sort over base + locale patches (the client's wildcard pass).
  // Numbers before letters; base letters below the locale code (enUS -> 'e')
  // sort before the locale group, base letters above it sort after. So patch-4/9
  // and patch-A precede the enUS patches, while patch-Z outranks them.
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
    expandChain(*findChainSpec(versions::Wotlk), data.root, Locale::enUS);
  REQUIRE(chain.has_value());
  CHECK(names(*chain, data.root) ==
        std::vector<std::string>{"common.MPQ", "enUS/locale-enUS.MPQ", "patch.MPQ",
                                 "patch-4.MPQ", "patch-9.MPQ", "patch-A.MPQ",
                                 "enUS/patch-enUS-5.MPQ", "enUS/patch-enUS-B.MPQ",
                                 "patch-Z.MPQ"});
}

TEST_CASE("a high base letter-patch outranks the locale patches", "[mpq-chain]")
{
  // Verified against Wow.exe 3.3.5a: base and locale patches share one
  // case-insensitive filename sort, so a custom base patch whose infix sorts past
  // the locale code (patch-Z vs patch-enUS) loads LAST and overrides the locale
  // patches — the behavior custom content relies on. (This inverts the earlier
  // "locale strictly last" assumption, which rested on case-sensitive reasoning.)
  FakeDataDir data;
  data.add("common.MPQ");
  data.add("enUS/locale-enUS.MPQ");
  data.add("patch.MPQ");
  data.add("patch-3.MPQ");
  data.add("patch-Z.MPQ");            // custom base patch
  data.add("enUS/patch-enUS.MPQ");    // official locale patch
  data.add("enUS/patch-enUS-2.MPQ");

  const auto chain =
    expandChain(*findChainSpec(versions::Wotlk), data.root, Locale::enUS);
  REQUIRE(chain.has_value());
  CHECK(names(*chain, data.root) ==
        std::vector<std::string>{"common.MPQ", "enUS/locale-enUS.MPQ", "patch.MPQ",
                                 "patch-3.MPQ", "enUS/patch-enUS.MPQ",
                                 "enUS/patch-enUS-2.MPQ", "patch-Z.MPQ"});
}

TEST_CASE("directory-backed patches join the chain and sort like archives",
          "[mpq-chain]")
{
  FakeDataDir data;
  data.add("common.MPQ");
  data.add("enUS/locale-enUS.MPQ");
  data.add("patch.MPQ");
  data.add_dir("patch-4.MPQ");   // a loose-file folder in place of an archive
  data.add("patch-9.MPQ");

  const auto chain =
    expandChain(*findChainSpec(versions::Wotlk), data.root, Locale::enUS);
  REQUIRE(chain.has_value());
  CHECK(names(*chain, data.root) ==
        std::vector<std::string>{"common.MPQ", "enUS/locale-enUS.MPQ", "patch.MPQ",
                                 "patch-4.MPQ", "patch-9.MPQ"});

  const auto member = std::ranges::find_if(
    *chain, [](const ChainMember& m) { return m.path.filename() == "patch-4.MPQ"; });
  REQUIRE(member != chain->end());
  CHECK(member->isDirectory);
}

TEST_CASE("missing archives are skipped without error", "[mpq-chain]")
{
  FakeDataDir data;
  data.add("common.MPQ");
  data.add("enUS/locale-enUS.MPQ");

  const auto chain =
    expandChain(*findChainSpec(versions::Wotlk), data.root, Locale::enUS);
  REQUIRE(chain.has_value());
  CHECK(names(*chain, data.root) ==
        std::vector<std::string>{"common.MPQ", "enUS/locale-enUS.MPQ"});
}

TEST_CASE("unknown versions have no chain spec", "[mpq-chain]")
{
  CHECK(findChainSpec(ClientVersion{3, 1, 0, 9767}) == nullptr);
  // build-exact and version-triple matches both hit the 3.3.5a table
  CHECK(findChainSpec(ClientVersion{3, 3, 5, 12340}) != nullptr);
  CHECK(findChainSpec(ClientVersion{3, 3, 5, 0}) != nullptr);
}
TEST_CASE("the 4.3.4 update chain globs wow-update archives ascending by build",
          "[mpq-chain]")
{
  FakeDataDir data;
  for (const char* archive :
       {"base-Win.MPQ", "art.MPQ", "sound.MPQ", "world.MPQ", "world2.MPQ",
        "expansion1.MPQ", "expansion2.MPQ", "expansion3.MPQ",
        "wow-update-base-15354.MPQ", "wow-update-base-15211.MPQ",
        "wow-update-base-15595.MPQ"})
    data.add(archive);
  for (const char* archive :
       {"ruRU/locale-ruRU.MPQ", "ruRU/speech-ruRU.MPQ",
        "ruRU/expansion1-locale-ruRU.MPQ", "ruRU/expansion2-locale-ruRU.MPQ",
        "ruRU/expansion3-locale-ruRU.MPQ", "ruRU/wow-update-ruRU-15595.MPQ",
        "ruRU/wow-update-ruRU-15211.MPQ"})
    data.add(archive);
  // distractors: not archives of the chain
  data.add("wow-update-base-15595.MPQ.part");
  data.add("ruRU/Credits_CT.html");

  const auto* spec = findChainSpec(versions::Cata);
  REQUIRE(spec != nullptr);

  const auto chain = expandChain(*spec, data.root, Locale::ruRU);
  REQUIRE(chain.has_value());

  // Base tier: table order, only the archives on disk (no base-Mac here).
  CHECK(names(*chain, data.root) ==
        std::vector<std::string>{
          "base-Win.MPQ", "art.MPQ", "sound.MPQ", "world.MPQ", "world2.MPQ",
          "expansion1.MPQ", "expansion2.MPQ", "expansion3.MPQ",
          "ruRU/locale-ruRU.MPQ", "ruRU/speech-ruRU.MPQ",
          "ruRU/expansion1-locale-ruRU.MPQ", "ruRU/expansion2-locale-ruRU.MPQ",
          "ruRU/expansion3-locale-ruRU.MPQ",
          // The update tier: ascending build, base before locale within a build.
          "wow-update-base-15211.MPQ", "ruRU/wow-update-ruRU-15211.MPQ",
          "wow-update-base-15354.MPQ",
          "wow-update-base-15595.MPQ", "ruRU/wow-update-ruRU-15595.MPQ"});

  // Every update member is an incremental patch with its attach prefix; base
  // members are standalone archives.
  for (const auto& member : *chain)
  {
    const auto name = member.path.filename().string();
    if (name.starts_with("wow-update-base-"))
    {
      CHECK(member.incremental);
      CHECK(member.prefix == "base");
    }
    else if (name.starts_with("wow-update-ruRU-"))
    {
      CHECK(member.incremental);
      CHECK(member.prefix == "ruRU");
    }
    else
      CHECK_FALSE(member.incremental);
  }
}
