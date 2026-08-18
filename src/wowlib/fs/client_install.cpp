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

namespace wowlib::fs
{
  namespace
  {
    namespace fsys = std::filesystem;

    /** Split @a line on '|' — the column separator of Blizzard's .build.info
        and .flavor.info tables.
        @param line the raw line.
        @return the fields, in order, empty fields included. */
    std::vector<std::string_view> split_fields(std::string_view line)
    {
      std::vector<std::string_view> fields;
      while (true)
      {
        const auto bar = line.find('|');
        if (bar == std::string_view::npos)
          break;
        fields.push_back(line.substr(0, bar));
        line.remove_prefix(bar + 1);
      }
      fields.push_back(line);
      return fields;
    }

    /** The column name of a .build.info header field: "Version!STRING:0" names
        the column "Version".
        @param header_field one header cell.
        @return the name before the '!' type suffix. */
    std::string_view column_name(std::string_view header_field)
    {
      return header_field.substr(0, header_field.find('!'));
    }

    /** Read a whole text file into lines, dropping a trailing empty one and any
        stray carriage returns.
        @param path the file to read.
        @return the lines, or nullopt if the file cannot be opened. */
    std::optional<std::vector<std::string>> read_lines(const fsys::path& path)
    {
      std::ifstream file{path};
      if (!file)
        return std::nullopt;

      std::vector<std::string> lines;
      for (std::string line; std::getline(file, line);)
      {
        if (line.ends_with('\r'))
          line.pop_back();
        if (!line.empty())
          lines.push_back(std::move(line));
      }
      return lines;
    }

    /** The product code a flavor directory records in its .flavor.info: a
        "## product-install-script-name!STRING:0" header followed by the code.
        @param client_path the directory holding Data/.
        @return the code, or nullopt when the file is absent or empty. */
    std::optional<std::string> read_flavor(const fsys::path& client_path)
    {
      const auto lines = read_lines(client_path / ".flavor.info");
      if (!lines)
        return std::nullopt;
      for (const std::string& line : *lines)
        if (!line.starts_with("##"))
          return line;
      return std::nullopt;
    }

    /** Where the install's .build.info lives: beside Data/ for a single-flavor
        install, one level up for a multi-flavor one (the flavor directories
        share the parent's table).
        @param client_path the directory holding Data/.
        @return the path, or nullopt when neither location has one. */
    std::optional<fsys::path> find_build_info(const fsys::path& client_path)
    {
      std::error_code ec;
      for (const fsys::path& candidate :
           {client_path / ".build.info", client_path.parent_path() / ".build.info"})
        if (fsys::is_regular_file(candidate, ec))
          return candidate;
      return std::nullopt;
    }

    /** Parse a "major.minor.patch.build" version string.
        @param text the Version column's contents.
        @return the version (flavor left at its default), or nullopt if the
                string is not four dot-separated numbers. */
    std::optional<ClientVersion> parse_version(std::string_view text)
    {
      std::array<std::uint32_t, 4> parts{};
      for (std::uint32_t& part : parts)
      {
        const auto dot = text.find('.');
        const std::string_view field = text.substr(0, dot);
        const auto [end, ec] =
          std::from_chars(field.data(), field.data() + field.size(), part);
        if (ec != std::errc{} || end != field.data() + field.size())
          return std::nullopt;
        text = dot == std::string_view::npos ? std::string_view{}
                                             : text.substr(dot + 1);
      }
      return ClientVersion{static_cast<std::uint16_t>(parts[0]),
                           static_cast<std::uint16_t>(parts[1]),
                           static_cast<std::uint16_t>(parts[2]), parts[3]};
    }

    /** The flavor a TACT product code belongs to. Unknown codes are Retail:
        the retail channels ('wow', 'wowt', 'wowxptr', 'wow_beta', ...) share no
        common prefix, while every Classic-family code does.
        @param product the product code.
        @return the flavor. */
    ClientFlavor flavor_of(std::string_view product)
    {
      if (product.starts_with("wow_classic_era"))
        return ClientFlavor::ClassicEra;
      if (product.starts_with("wow_anniversary"))
        return ClientFlavor::Anniversary;
      if (product.starts_with("wow_classic"))
        return ClientFlavor::Classic;
      return ClientFlavor::Retail;
    }

    /** One parsed .build.info row, reduced to what identifies an install. */
    struct BuildInfoRow
    {
      std::string product;  /**< The Product column. */
      std::string version;  /**< The Version column. */
      bool active = false;  /**< Whether the Active column reads non-zero. */
    };

    /** Parse a .build.info table.
        @param lines the file's non-empty lines, header first.
        @return every row carrying both a Product and a Version. */
    std::vector<BuildInfoRow> parse_build_info(const std::vector<std::string>& lines)
    {
      if (lines.empty())
        return {};

      const auto header = split_fields(lines.front());
      const auto column = [&](std::string_view name) -> std::size_t {
        const auto found = std::ranges::find_if(
          header, [&](std::string_view field) { return column_name(field) == name; });
        return static_cast<std::size_t>(found - header.begin());
      };
      const std::size_t product_at = column("Product");
      const std::size_t version_at = column("Version");
      const std::size_t active_at = column("Active");

      std::vector<BuildInfoRow> rows;
      for (const std::string& line : lines | std::views::drop(1))
      {
        const auto fields = split_fields(line);
        if (product_at >= fields.size() || version_at >= fields.size())
          continue;
        rows.push_back({.product = std::string{fields[product_at]},
                        .version = std::string{fields[version_at]},
                        .active = active_at < fields.size() && fields[active_at] != "0"});
      }
      return rows;
    }
  }

  Result<ClientInstall> ClientInstall::detect(fsys::path client_path)
  {
    const auto info_path = find_build_info(client_path);
    if (!info_path)
      return make_error(
        ErrorCode::NotSupported,
        std::format("no .build.info in '{}' or its parent — not a CASC installation "
                    "(MPQ-era clients and bare repacks record no version; construct "
                    "the ClientVersion directly)",
                    client_path.string()));

    const auto lines = read_lines(*info_path);
    if (!lines)
      return make_error(ErrorCode::IoError,
                        std::format("cannot read '{}'", info_path->string()));

    auto rows = parse_build_info(*lines);
    std::erase_if(rows, [](const BuildInfoRow& row) { return !row.active; });
    if (rows.empty())
      return make_error(ErrorCode::NotSupported,
                        std::format("'{}' lists no active installation",
                                    info_path->string()));

    // A multi-flavor install shares one table across its flavor directories;
    // .flavor.info beside Data/ says which row is THIS one.
    const auto flavor = read_flavor(client_path);
    if (flavor)
    {
      const auto match = std::ranges::find(rows, *flavor, &BuildInfoRow::product);
      if (match == rows.end())
        return make_error(
          ErrorCode::NotSupported,
          std::format("'{}' declares product '{}', which '{}' does not list",
                      (client_path / ".flavor.info").string(), *flavor,
                      info_path->string()));
      rows = {*match};
    }
    else if (rows.size() > 1)
    {
      std::string products;
      for (const BuildInfoRow& row : rows)
        products += (products.empty() ? "" : ", ") + row.product;
      return make_error(
        ErrorCode::NotSupported,
        std::format("'{}' lists several products ({}) and '{}' has no .flavor.info "
                    "to choose between them — point detect() at a flavor directory "
                    "(_retail_, _classic_era_, ...)",
                    info_path->string(), products, client_path.string()));
    }

    const BuildInfoRow& row = rows.front();
    auto version = parse_version(row.version);
    if (!version)
      return make_error(ErrorCode::NotSupported,
                        std::format("'{}' records an unparseable version '{}' for "
                                    "product '{}'",
                                    info_path->string(), row.version, row.product));
    version->flavor = flavor_of(row.product);

    return ClientInstall{.path = std::move(client_path),
                         .version = *version,
                         .casc_product = row.product};
  }
}
