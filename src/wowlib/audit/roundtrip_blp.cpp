/** @file
    The BLP round-trip driver: byte-perfect rewrite of one texture, modeled on
    tests/integration/test_blp_roundtrip.cpp with returned outcomes in place
    of Catch2 assertions. No decode checks — that is validation, not
    round-tripping. BLP is version-stable, so there is no version dispatch. */

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <string>

#include <wowlib/audit/detail.hpp>
#include <wowlib/formats/blp/blp.hpp>

namespace
{
  using namespace wowlib;
  using namespace wowlib::audit;
  using namespace wowlib::formats;

  /** The first byte divergence between an original BLP and its rewrite,
      located within the file region (header / palette / mip level) it falls
      into — the byte-perfect guarantee's debugging lens.
      @param original  the reference bytes.
      @param rewritten the bytes to compare.
      @param entity    the parsed texture (for the stored mip layout).
      @return the description, or nullopt when identical. */
  std::optional<std::string> first_divergence_blp(const FileBuffer& original,
                                                  const FileBuffer& rewritten,
                                                  const blp::BLP& entity)
  {
    if (original == rewritten)
      return std::nullopt;

    const std::size_t common = std::min(original.size(), rewritten.size());
    std::size_t at = 0;
    while (at < common && original[at] == rewritten[at])
      ++at;

    std::string inside = "gap/tail";
    if (at < 0x94)
      inside = "header";
    else if (at < blp::blp_header_bytes)
      inside = "palette";
    else
      for (std::size_t level = 0; level < blp::blp_max_mips; ++level)
      {
        const std::uint32_t offset = entity.stored_layout.offsets[level];
        const std::uint32_t size = entity.stored_layout.sizes[level];
        if (offset != 0 && size != 0 && at >= offset && at < std::uint64_t{offset} + size)
        {
          inside = std::format("mip {}", level);
          break;
        }
      }
    return std::format("first divergence at {:#x} inside {} (sizes {} vs {})", at, inside,
                       original.size(), rewritten.size());
  }

  /** Round-trip one BLP byte-for-byte.
      @param fs   the client filesystem.
      @param path the canonical .blp path.
      @return the outcome. */
  RoundtripReport roundtrip_blp(fs::FileSystem& fs, const std::string& path)
  {
    RoundtripReport report;
    const auto raw = fs.read_file(FileKey{path});
    if (!raw)
      return audit::detail::fail_with(report, "read", raw.error().message);

    blp::BLP entity;
    if (auto r = entity.read(*raw); !r)
      return audit::detail::fail_with(report, "parse", r.error().message);

    const auto rewritten = entity.write();
    if (!rewritten)
      return audit::detail::fail_with(report, "write", rewritten.error().message);
    if (auto divergence = first_divergence_blp(*raw, *rewritten, entity))
      return audit::detail::fail_with(report, "compare", *divergence);
    return report;
  }
}

namespace wowlib::audit::detail
{
  RoundtripReport FormatDrivers::blp(fs::FileSystem& fs, const std::string& path)
  {
    return guarded([&] { return roundtrip_blp(fs, path); });
  }
}
