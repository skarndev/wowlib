#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <filesystem>
#include <format>
#include <thread>
#include <vector>

#include <wowlib/fs/csv_listfile.hpp>

#include "unit_env.hpp"

using namespace wowlib;
using wowlib::fs::CsvListfile;

TEST_CASE("concurrent lookups and registrations keep the maps consistent",
          "[listfile][threads]")
{
  // work on a disposable copy — registrations append to the working file
  const auto sample = tests::dataRoot() /
                      "sample-listfile.csv";
  const auto csv = std::filesystem::temp_directory_path() / "wowlib-tests" /
                   "concurrency.csv";
  std::filesystem::create_directories(csv.parent_path());
  std::filesystem::copy_file(sample, csv,
                             std::filesystem::copy_options::overwrite_existing);

  auto listfile = CsvListfile::load(csv);
  REQUIRE(listfile.has_value());

  constexpr int writers = 4;
  constexpr int perWriter = 250;
  constexpr int readers = 4;

  std::atomic<bool> stop{false};
  std::atomic<int> failures{0};

  std::vector<std::jthread> threads;
  for (int w = 0; w < writers; ++w)
    threads.emplace_back([&, w] {
      for (int i = 0; i < perWriter; ++i)
        if (!listfile->registerPath(std::format("custom/writer{}/file{}.blp", w, i)))
          ++failures;
    });
  for (int r = 0; r < readers; ++r)
    threads.emplace_back([&] {
      while (!stop)
        if (listfile->pathToFdid("dbfilesclient/map.db2") != FileDataID{1349477})
          ++failures;
    });

  for (int w = 0; w < writers; ++w)
    threads[static_cast<std::size_t>(w)].join();
  stop = true;
  threads.clear();

  REQUIRE(failures == 0);
  CHECK(listfile->size() == 6 + writers * perWriter);

  // every allocated id resolves back and ids were not double-assigned
  for (int w = 0; w < writers; ++w)
    for (int i = 0; i < perWriter; ++i)
    {
      const auto id =
        listfile->pathToFdid(std::format("custom/writer{}/file{}.blp", w, i));
      REQUIRE(id.has_value());
      CHECK(listfile->fdidToPath(*id) ==
            std::format("custom\\writer{}\\file{}.blp", w, i));
    }

  // the working file absorbed every registration
  auto reloaded = CsvListfile::load(csv);
  REQUIRE(reloaded.has_value());
  CHECK(reloaded->size() == listfile->size());
}