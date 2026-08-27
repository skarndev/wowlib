#include <wowlib/fs/client_install.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <format>
#include <fstream>
#include <optional>
#include <ranges>
#include <string_view>
#include <vector>

namespace wowlib::fs {
  namespace {
    namespace fsys = std::filesystem;

    /** Split @a line on '|' — the column separator of Blizzard's .build.info
        and .flavor.info tables.
        @param line the raw line.
        @return the fields, in order, empty fields included. */
    std::vector<std::string_view> splitFields(std::string_view line) {
      std::vector<std::string_view> fields;
      while (true) {
        const auto bar = line.find('|');
        if (bar == std::string_view::npos) break;
        fields.push_back(line.substr(0, bar));
        line.remove_prefix(bar + 1);
      }
      fields.push_back(line);
      return fields;
    }

    /** The column name of a .build.info header field: "Version!STRING:0" names
        the column "Version".
        @param headerField one header cell.
        @return the name before the '!' type suffix. */
    std::string_view columnName(std::string_view headerField) {
      return headerField.substr(0, headerField.find('!'));
    }

    /** Read a whole text file into lines, dropping a trailing empty one and any
        stray carriage returns.
        @param path the file to read.
        @return the lines, or nullopt if the file cannot be opened. */
    std::optional<std::vector<std::string>> readLines(const fsys::path& path) {
      std::ifstream file{path};
      if (!file) return std::nullopt;

      std::vector<std::string> lines;
      for (std::string line; std::getline(file, line);) {
        if (line.ends_with('\r')) line.pop_back();
        if (!line.empty()) lines.push_back(std::move(line));
      }
      return lines;
    }

    /** The product code a flavor directory records in its .flavor.info: a
        "## product-install-script-name!STRING:0" header followed by the code.
        @param clientPath the directory holding Data/.
        @return the code, or nullopt when the file is absent or empty. */
    std::optional<std::string> readFlavor(const fsys::path& clientPath) {
      const auto lines = readLines(clientPath / ".flavor.info");
      if (!lines) return std::nullopt;
      for (const std::string& line : *lines)
        if (!line.starts_with("##")) return line;
      return std::nullopt;
    }

    /** Where the install's .build.info lives: beside Data/ for a single-flavor
        install, one level up for a multi-flavor one (the flavor directories
        share the parent's table).
        @param clientPath the directory holding Data/.
        @return the path, or nullopt when neither location has one. */
    std::optional<fsys::path> findBuildInfo(const fsys::path& clientPath) {
      std::error_code ec;
      for (const fsys::path& candidate : {clientPath / ".build.info", clientPath.parent_path() / ".build.info"})
        if (fsys::is_regular_file(candidate, ec)) return candidate;
      return std::nullopt;
    }

    /** Parse a "major.minor.patch.build" version string.
        @param text the Version column's contents.
        @return the version (flavor left at its default), or nullopt if the
                string is not four dot-separated numbers. */
    std::optional<ClientVersion> parseVersion(std::string_view text) {
      std::array<std::uint32_t, 4> parts{};
      for (std::uint32_t& part : parts) {
        const auto dot = text.find('.');
        const std::string_view field = text.substr(0, dot);
        const auto [end, ec] = std::from_chars(field.data(), field.data() + field.size(), part);
        if (ec != std::errc{} || end != field.data() + field.size()) return std::nullopt;
        text = dot == std::string_view::npos ? std::string_view{} : text.substr(dot + 1);
      }
      return ClientVersion{
        static_cast<std::uint16_t>(parts[0]),
        static_cast<std::uint16_t>(parts[1]),
        static_cast<std::uint16_t>(parts[2]),
        parts[3]
      };
    }

    /** The flavor a TACT product code belongs to. Unknown codes are Retail:
        the retail channels ('wow', 'wowt', 'wowxptr', 'wow_beta', ...) share no
        common prefix, while every Classic-family code does.
        @param product the product code.
        @return the flavor. */
    ClientFlavor flavorOf(std::string_view product) {
      if (product.starts_with("wow_classic_era")) return ClientFlavor::ClassicEra;
      if (product.starts_with("wow_anniversary")) return ClientFlavor::Anniversary;
      if (product.starts_with("wow_classic")) return ClientFlavor::Classic;
      return ClientFlavor::Retail;
    }

    /** One parsed .build.info row, reduced to what identifies an install. */
    struct BuildInfoRow {
      std::string product; /**< The Product column. */
      std::string version; /**< The Version column. */
      bool active = false; /**< Whether the Active column reads non-zero. */
    };

    /** Parse a .build.info table.
        @param lines the file's non-empty lines, header first.
        @return every row carrying both a Product and a Version. */
    std::vector<BuildInfoRow> parseBuildInfo(const std::vector<std::string>& lines) {
      if (lines.empty()) return {};

      const auto header = splitFields(lines.front());
      const auto column = [&](std::string_view name) -> std::size_t {
        const auto found = std::ranges::find_if(header, [&](std::string_view field) {
          return columnName(field) == name;
        });
        return static_cast<std::size_t>(found - header.begin());
      };
      const std::size_t productAt = column("Product");
      const std::size_t versionAt = column("Version");
      const std::size_t activeAt = column("Active");

      std::vector<BuildInfoRow> rows;
      for (const std::string& line : lines | std::views::drop(1)) {
        const auto fields = splitFields(line);
        if (productAt >= fields.size() || versionAt >= fields.size()) continue;
        rows.push_back({
          .product = std::string{fields[productAt]},
          .version = std::string{fields[versionAt]},
          .active = activeAt < fields.size() && fields[activeAt] != "0"
        });
      }
      return rows;
    }
  }

  Result<ClientInstall> ClientInstall::detect(fsys::path clientPath) {
    const auto infoPath = findBuildInfo(clientPath);
    if (!infoPath)
      return makeError(ErrorCode::NotSupported,
                        std::format(
                          "no .build.info in '{}' or its parent — not a CASC installation "
                          "(MPQ-era clients and bare repacks record no version; construct "
                          "the ClientVersion directly)", clientPath.string()));

    const auto lines = readLines(*infoPath);
    if (!lines)
      return makeError(ErrorCode::IoError, std::format("cannot read '{}'", infoPath->string()));

    auto rows = parseBuildInfo(*lines);
    std::erase_if(rows, [](const BuildInfoRow& row) { return !row.active; });
    if (rows.empty())
      return makeError(ErrorCode::NotSupported, std::format("'{}' lists no active installation", infoPath->string()));

    // A multi-flavor install shares one table across its flavor directories;
    // .flavor.info beside Data/ says which row is THIS one.
    const auto flavor = readFlavor(clientPath);
    if (flavor) {
      const auto match = std::ranges::find(rows, *flavor, &BuildInfoRow::product);
      if (match == rows.end())
        return makeError(ErrorCode::NotSupported, std::format("'{}' declares product '{}', which '{}' does not list",
                                                               (clientPath / ".flavor.info").string(), *flavor,
                                                               infoPath->string()));
      rows = {*match};
    }
    else if (rows.size() > 1) {
      std::string products;
      for (const BuildInfoRow& row : rows) products += (products.empty() ? "" : ", ") + row.product;
      return makeError(ErrorCode::NotSupported, std::format(
                          "'{}' lists several products ({}) and '{}' has no .flavor.info "
                          "to choose between them — point detect() at a flavor directory "
                          "(_retail_, _classic_era_, ...)", infoPath->string(), products, clientPath.string()));
    }

    const BuildInfoRow& row = rows.front();
    auto version = parseVersion(row.version);
    if (!version)
      return makeError(ErrorCode::NotSupported,
                        std::format("'{}' records an unparseable version '{}' for " "product '{}'", infoPath->string(),
                                    row.version, row.product));
    version->flavor = flavorOf(row.product);

    return ClientInstall{.path = std::move(clientPath), .version = *version, .cascProduct = row.product};
  }
}
