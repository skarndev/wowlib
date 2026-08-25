/** @file
    SchemaCatalog::from_dbd_dir — the runtime WoWDBDefs loader: parse a
    checkout's `definitions` directory of `.dbd` files, resolve every
    targeted era's member list
    with EXACTLY dbdgen's rules, assemble a WDBS blob in memory and hand it
    to from_blob. The parity contract (tested): built from the same
    definitions, this catalog and the embedded one agree schema for schema —
    which means every mangling rule here (snake_case, `_lang` stripping and
    its collision escape, keyword escaping, LocString locale counts, range
    collapsing, the Item-sparse rename) mirrors `tools/dbdgen` move for
    move. When dbdgen's rules change, this file and its parity test are the
    fence that changes with them. */

#include <wowlib/db/schema_catalog.hpp>

#include <wowlib/db/schema_blob.hpp>

#if WOWLIB_DB_SCHEMA_RUNTIME

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace wowlib::db {
  namespace {
    using Build = std::array<std::uint32_t, 4>;

    /** dbdgen's era targets (tools/dbdgen/dbdgen/targets.py), same order —
        the blob's era table and mask bits. */
    constexpr std::array<ClientVersion, 11> targets{
      versions::vanilla,
      versions::tbc,
      versions::wotlk,
      versions::cata,
      versions::mop,
      versions::wod,
      versions::legion,
      versions::bfa,
      versions::shadowlands,
      versions::dragonflight,
      versions::tww
    };

    Build build_of(ClientVersion v) {
      return {v.major, v.minor, v.patch, v.build};
    }

    /** locstring_langs (targets.py): 8 slots before TBC 2.1.0.6692 added
        ruRU, 16 until Cataclysm collapsed to a single localized string. */
    std::optional<int> locstring_langs(Build build) {
      if (build < Build{2, 1, 0, 6692}) return 8;
      if (build < Build{4, 0, 0, 0}) return 16;
      return std::nullopt;
    }

    /** emit.py's _RESERVED: C++ keywords + the record statics. */
    bool reserved_name(std::string_view name) {
      static const std::set<std::string_view> reserved{
        "alignas",
        "alignof",
        "and",
        "asm",
        "auto",
        "bool",
        "break",
        "case",
        "catch",
        "char",
        "class",
        "concept",
        "const",
        "consteval",
        "constexpr",
        "constinit",
        "continue",
        "decltype",
        "default",
        "delete",
        "do",
        "double",
        "else",
        "enum",
        "explicit",
        "export",
        "extern",
        "false",
        "float",
        "for",
        "friend",
        "goto",
        "if",
        "inline",
        "int",
        "long",
        "mutable",
        "namespace",
        "new",
        "noexcept",
        "not",
        "nullptr",
        "operator",
        "or",
        "private",
        "protected",
        "public",
        "register",
        "requires",
        "return",
        "short",
        "signed",
        "sizeof",
        "static",
        "static_assert",
        "struct",
        "switch",
        "template",
        "this",
        "throw",
        "true",
        "try",
        "typedef",
        "typeid",
        "typename",
        "union",
        "unsigned",
        "using",
        "virtual",
        "void",
        "volatile",
        "while",
        "xor",
        "version",
        "table_name"
      };
      return reserved.contains(name);
    }

    /** emit.py's snake(): CamelCase -> snake_case with acronym runs kept
        together — the exact two-regex semantics, hand-rolled:
        1. `(.)([A-Z][a-z]+)` -> `\1_\2`
        2. `([a-z0-9])([A-Z])` -> `\1_\2`
        then lowercase and collapse `__+`. */
    std::string snake(std::string_view name) {
      const auto upper = [](char c) { return c >= 'A' && c <= 'Z'; };
      const auto lower = [](char c) { return c >= 'a' && c <= 'z'; };
      const auto digit = [](char c) { return c >= '0' && c <= '9'; };

      // Pass 1: an underscore before every [A-Z][a-z]+ run that has ANY
      // predecessor (the regex needs one char of lookbehind).
      std::string pass1;
      for (std::size_t i = 0; i < name.size(); ++i) {
        if (i > 0 && upper(name[i]) && i + 1 < name.size() && lower(name[i + 1])) pass1 += '_';
        pass1 += name[i];
      }
      // Pass 2: an underscore between [a-z0-9] and [A-Z].
      std::string pass2;
      for (std::size_t i = 0; i < pass1.size(); ++i) {
        if (i > 0 && upper(pass1[i]) && (lower(pass1[i - 1]) || digit(pass1[i - 1]))) pass2 += '_';
        pass2 += pass1[i];
      }
      // Lowercase + collapse "__".
      std::string out;
      for (char c : pass2) {
        const char l = upper(c) ? static_cast<char>(c - 'A' + 'a') : c;
        if (l == '_' && !out.empty() && out.back() == '_') continue;
        out += l;
      }
      return out;
    }

    /** emit.py's member_name(): strip the `_lang` locstring marker, snake,
        escape keywords/statics with a trailing underscore, digit prefix. */
    std::string member_name(std::string_view dbd_name) {
      std::string_view base = dbd_name;
      if (base.ends_with("_lang")) base.remove_suffix(5);
      std::string name = snake(base);
      if (reserved_name(name)) name += '_';
      if (!name.empty() && name.front() >= '0' && name.front() <= '9') name.insert(name.begin(), '_');
      return name;
    }

    // --- the .dbd grammar (dbd.py) ------------------------------------------

    struct ColumnDecl {
      std::string type; /**< int / float / string / locstring. */
    };

    struct Entry {
      std::string name;
      bool is_id = false;
      bool is_relation = false;
      bool noninline = false;
      std::optional<int> bits;
      bool is_unsigned = false;
      std::optional<int> array_len;
    };

    struct VersionBlock {
      std::vector<std::pair<Build, Build>> builds; /**< Inclusive ranges. */
      bool has_layouts = false;
      std::vector<Entry> entries;

      bool matches(Build build) const {
        return std::ranges::any_of(builds, [&](const auto& range) {
          return range.first <= build && build <= range.second;
        });
      }
    };

    struct Definition {
      std::map<std::string, ColumnDecl, std::less<>> columns;
      std::vector<VersionBlock> blocks;

      const VersionBlock* block_for(Build build) const {
        for (const VersionBlock& block : blocks)
          if (block.matches(build)) return &block;
        return nullptr;
      }
    };

    /** Strip a trailing `// comment` and surrounding whitespace. */
    std::string_view strip_line(std::string_view line) {
      if (const auto at = line.find("//"); at != std::string_view::npos) line = line.substr(0, at);
      while (!line.empty() && (line.front() == ' ' || line.front() == '\t' || line.front() == '\r')) line.
        remove_prefix(1);
      while (!line.empty() && (line.back() == ' ' || line.back() == '\t' || line.back() == '\r')) line.remove_suffix(1);
      return line;
    }

    bool ident_char(char c) {
      return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
    }

    std::optional<Build> parse_build(std::string_view text) {
      Build out{};
      std::size_t part = 0;
      const char* p = text.data();
      const char* end = p + text.size();
      while (part < 4) {
        auto [next, ec] = std::from_chars(p, end, out[part]);
        if (ec != std::errc{}) return std::nullopt;
        p = next;
        ++part;
        if (part < 4) {
          if (p == end || *p != '.') return std::nullopt;
          ++p;
        }
      }
      return p == end ? std::optional{out} : std::nullopt;
    }

    /** `int<Map::ID> ParentMapID?` -> decl; nullopt on anything else. */
    bool parse_column_line(std::string_view body, Definition& def) {
      static constexpr std::array<std::string_view, 4> types{"int", "float", "string", "locstring"};
      std::string_view type;
      for (const std::string_view t : types)
        if (body.starts_with(t) && (body.size() == t.size() || body[t.size()] == '<' || body[t.size()] == ' '))
          if (t.size() > type.size()) type = t;
      if (type.empty()) return false;
      body.remove_prefix(type.size());
      if (body.starts_with('<')) {
        const auto close = body.find('>');
        if (close == std::string_view::npos) return false;
        body.remove_prefix(close + 1);
      }
      while (body.starts_with(' ')) body.remove_prefix(1);
      std::string_view name = body;
      if (name.ends_with('?')) name.remove_suffix(1);
      if (name.empty() || !std::ranges::all_of(name, ident_char)) return false;
      def.columns.emplace(std::string{name}, ColumnDecl{std::string{type}});
      return true;
    }

    /** `$id,noninline$ID<u32>[2]` -> entry; nullopt on mismatch. */
    std::optional<Entry> parse_entry(std::string_view body) {
      Entry entry{};
      if (body.starts_with('$')) {
        const auto close = body.find('$', 1);
        if (close == std::string_view::npos) return std::nullopt;
        std::string_view anns = body.substr(1, close - 1);
        body.remove_prefix(close + 1);
        while (!anns.empty()) {
          const auto comma = anns.find(',');
          const std::string_view ann = anns.substr(0, comma);
          if (ann == "id") entry.is_id = true;
          else if (ann == "relation") entry.is_relation = true;
          else if (ann == "noninline") entry.noninline = true;
          if (comma == std::string_view::npos) break;
          anns.remove_prefix(comma + 1);
        }
      }
      std::size_t at = 0;
      while (at < body.size() && ident_char(body[at])) ++at;
      if (at == 0) return std::nullopt;
      entry.name = std::string{body.substr(0, at)};
      body.remove_prefix(at);
      if (body.starts_with('<')) {
        body.remove_prefix(1);
        if (body.starts_with('u')) {
          entry.is_unsigned = true;
          body.remove_prefix(1);
        }
        int bits = 0;
        auto [next, ec] = std::from_chars(body.data(), body.data() + body.size(), bits);
        if (ec != std::errc{} || next == body.data() || *next != '>') return std::nullopt;
        entry.bits = bits;
        body.remove_prefix(static_cast<std::size_t>(next - body.data()) + 1);
      }
      if (body.starts_with('[')) {
        body.remove_prefix(1);
        int len = 0;
        auto [next, ec] = std::from_chars(body.data(), body.data() + body.size(), len);
        if (ec != std::errc{} || next == body.data() || *next != ']') return std::nullopt;
        entry.array_len = len;
        body.remove_prefix(static_cast<std::size_t>(next - body.data()) + 1);
      }
      return body.empty() ? std::optional{entry} : std::nullopt;
    }

    /** Parse one whole .dbd (dbd.py's paragraph model). Nullopt = skip the
        table, exactly as dbdgen skips on a warning. */
    std::optional<Definition> parse_dbd(const std::string& text) {
      std::vector<std::vector<std::string_view>> paragraphs;
      std::vector<std::string_view> current;
      std::string_view rest = text;
      // Skip a UTF-8 BOM (dbdgen reads utf-8-sig).
      if (rest.starts_with("\xEF\xBB\xBF")) rest.remove_prefix(3);
      while (!rest.empty()) {
        const auto nl = rest.find('\n');
        std::string_view raw = rest.substr(0, nl);
        rest = nl == std::string_view::npos ? std::string_view{} : rest.substr(nl + 1);
        std::string_view line = raw;
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.remove_suffix(1);
        while (!line.empty() && line.front() == ' ') line.remove_prefix(1);
        if (line.empty()) {
          if (!current.empty()) {
            paragraphs.push_back(std::move(current));
            current.clear();
          }
        }
        else {
          current.push_back(line);
        }
      }
      if (!current.empty()) paragraphs.push_back(std::move(current));
      if (paragraphs.empty() || paragraphs.front().front() != "COLUMNS") return std::nullopt;

      Definition def{};
      for (std::size_t i = 1; i < paragraphs.front().size(); ++i) {
        const std::string_view body = strip_line(paragraphs.front()[i]);
        if (body.empty()) continue;
        if (!parse_column_line(body, def)) return std::nullopt;
      }
      for (std::size_t p = 1; p < paragraphs.size(); ++p) {
        VersionBlock block{};
        for (const std::string_view raw : paragraphs[p]) {
          const std::string_view body = strip_line(raw);
          if (body.empty()) continue;
          if (body.starts_with("LAYOUT ")) {
            block.has_layouts = true;
          }
          else if (body.starts_with("BUILD ")) {
            std::string_view list = body.substr(6);
            while (!list.empty()) {
              const auto comma = list.find(',');
              std::string_view part = strip_line(list.substr(0, comma));
              if (const auto dash = part.find('-'); dash != std::string_view::npos) {
                const auto lo = parse_build(strip_line(part.substr(0, dash)));
                const auto hi = parse_build(strip_line(part.substr(dash + 1)));
                if (!lo || !hi) return std::nullopt;
                block.builds.emplace_back(*lo, *hi);
              }
              else {
                const auto build = parse_build(part);
                if (!build) return std::nullopt;
                block.builds.emplace_back(*build, *build);
              }
              if (comma == std::string_view::npos) break;
              list.remove_prefix(comma + 1);
            }
          }
          else if (body.starts_with("COMMENT")) {
            // Doc text only — the schema does not carry it.
          }
          else {
            auto entry = parse_entry(body);
            if (!entry) return std::nullopt;
            block.entries.push_back(std::move(*entry));
          }
        }
        if (block.builds.empty() && !block.has_layouts) return std::nullopt;
        def.blocks.push_back(std::move(block));
      }
      return def;
    }

    // --- member resolution (emit.py's build_members/_member_of) -------------

    /** One resolved member — exactly the facts the blob column carries. */
    struct Member {
      std::string name;
      ColumnType type = ColumnType::Int;
      std::uint8_t bits = 32;
      bool is_signed = false;
      std::uint16_t array_len = 1;
      std::uint8_t locale_count = 0;
      bool is_id = false;
      bool is_relation = false;
      bool noninline = false;

      bool same_shape(const Member& other) const {
        return name == other.name && type == other.type && bits == other.bits && is_signed == other.is_signed &&
          array_len == other.array_len && locale_count == other.locale_count && is_id == other.is_id && is_relation ==
          other.is_relation && noninline == other.noninline;
      }
    };

    /** build_members for one target; nullopt = the table skips this era
        (dbdgen warns and drops the whole table for that target). */
    std::optional<std::vector<Member>> members_for(const Definition& def, const VersionBlock& block, Build build) {
      const std::optional<int> langs = locstring_langs(build);

      // The `_lang` strip may collide with a sibling column; colliding
      // locstrings keep their suffix (build_members' first pass).
      std::vector<std::string> names;
      names.reserve(block.entries.size());
      for (const Entry& entry : block.entries) names.push_back(member_name(entry.name));
      for (std::size_t i = 0; i < block.entries.size(); ++i) {
        if (std::ranges::count(names, names[i]) > 1 && block.entries[i].name.ends_with("_lang")) {
          std::string kept = snake(block.entries[i].name);
          if (reserved_name(kept)) kept += '_';
          names[i] = std::move(kept);
        }
      }

      // Uniqueness up front (names is stable; storing views into the
      // growing members vector would dangle on reallocation).
      if (std::set<std::string_view>(names.begin(), names.end()).size() != names.size()) return std::nullopt;
      // member name collision
      std::vector<Member> members;
      for (std::size_t i = 0; i < block.entries.size(); ++i) {
        const Entry& entry = block.entries[i];
        const auto decl = def.columns.find(entry.name);
        if (decl == def.columns.end()) return std::nullopt; // no COLUMNS declaration
        const std::string& type = decl->second.type;

        Member m{};
        m.name = names[i];
        m.array_len = static_cast<std::uint16_t>(entry.array_len.value_or(1));
        m.is_id = entry.is_id;
        m.is_relation = entry.is_relation;
        m.noninline = entry.noninline;

        if (entry.bits) {
          if (type != "int") return std::nullopt; // sized entry on a non-int column
          if (*entry.bits != 8 && *entry.bits != 16 && *entry.bits != 32 && *entry.bits != 64) return std::nullopt;
          // unsupported width
          m.type = ColumnType::Int;
          m.bits = static_cast<std::uint8_t>(*entry.bits);
          m.is_signed = !entry.is_unsigned;
        }
        else if (type == "float") {
          m.type = ColumnType::Float;
          m.bits = 32;
        }
        else if (type == "string") {
          m.type = ColumnType::String;
          m.bits = 32;
        }
        else if (type == "locstring") {
          if (entry.array_len) return std::nullopt; // locstring arrays unsupported
          if (langs) {
            m.type = ColumnType::LocString;
            m.bits = 32;
            m.locale_count = static_cast<std::uint8_t>(*langs);
          }
          else {
            m.type = ColumnType::String;
            m.bits = 32;
          }
        }
        else if (type == "int" && entry.noninline) {
          // ids/relations delivered by satellite blocks: u32.
          m.type = ColumnType::Int;
          m.bits = 32;
          m.is_signed = false;
        }
        else {
          return std::nullopt; // int entry without a <size>
        }

        members.push_back(std::move(m));
      }
      return members;
    }

    // --- WDBS assembly (emit.py's emit_schema_blob) -------------------------

    struct Range {
      std::uint16_t era_mask = 0;
      std::vector<Member> members;
    };

    class BlobWriter {
    public:
      void add_table(const std::string& name, const std::string& disk, const std::vector<Range>& ranges) {
        append_u32(tables_, intern(name));
        append_u32(tables_, intern(disk));
        append_u32(tables_, range_count_);
        append_u32(tables_, static_cast<std::uint32_t>(ranges.size()));
        for (const Range& range : ranges) {
          append_u32(ranges_, column_count_);
          append_u16(ranges_, static_cast<std::uint16_t>(range.members.size()));
          append_u16(ranges_, range.era_mask);
          ++range_count_;
          for (const Member& m : range.members) {
            append_u32(columns_, intern(m.name));
            columns_.push_back(static_cast<unsigned char>(m.type));
            columns_.push_back(m.bits);
            columns_.push_back(
              static_cast<unsigned char>((m.is_signed ? 1 : 0) | (m.is_id ? 2 : 0) | (m.is_relation ? 4 : 0) | (
                m.noninline ? 8 : 0)));
            columns_.push_back(m.locale_count);
            append_u16(columns_, m.array_len);
            append_u16(columns_, 0);
            ++column_count_;
          }
        }
        ++table_count_;
      }

      std::vector<unsigned char> finish() const {
        std::vector<unsigned char> out;
        append_u32(out, blob::magic);
        append_u32(out, blob::format_version);
        append_u32(out, table_count_);
        append_u32(out, range_count_);
        append_u32(out, column_count_);
        append_u32(out, static_cast<std::uint32_t>(pool_.size()));
        out.push_back(static_cast<unsigned char>(targets.size()));
        out.insert(out.end(), 3, 0);
        for (const ClientVersion v : targets) {
          append_u16(out, v.major);
          append_u16(out, v.minor);
          append_u16(out, v.patch);
          append_u16(out, 0);
          append_u32(out, v.build);
        }
        out.insert(out.end(), tables_.begin(), tables_.end());
        out.insert(out.end(), ranges_.begin(), ranges_.end());
        out.insert(out.end(), columns_.begin(), columns_.end());
        out.insert(out.end(), pool_.begin(), pool_.end());
        return out;
      }

    private:
      static void append_u16(std::vector<unsigned char>& out, std::uint16_t v) {
        out.push_back(static_cast<unsigned char>(v & 0xFF));
        out.push_back(static_cast<unsigned char>(v >> 8));
      }

      static void append_u32(std::vector<unsigned char>& out, std::uint32_t v) {
        for (int shift = 0; shift < 32; shift += 8) out.push_back(static_cast<unsigned char>((v >> shift) & 0xFF));
      }

      std::uint32_t intern(const std::string& text) {
        const auto [at, fresh] = interned_.try_emplace(text, static_cast<std::uint32_t>(pool_.size()));
        if (fresh) {
          pool_.insert(pool_.end(), text.begin(), text.end());
          pool_.push_back(0);
        }
        return at->second;
      }

      std::vector<unsigned char> pool_{0}; /**< Offset 0 = "". */
      std::map<std::string, std::uint32_t> interned_{{"", 0}};
      std::vector<unsigned char> tables_;
      std::vector<unsigned char> ranges_;
      std::vector<unsigned char> columns_;
      std::uint32_t table_count_ = 0;
      std::uint32_t range_count_ = 0;
      std::uint32_t column_count_ = 0;
    };
  }

  Result<SchemaCatalog> SchemaCatalog::from_dbd_dir(const std::filesystem::path& definitions) {
    std::error_code ec;
    std::filesystem::directory_iterator it{definitions, ec};
    if (ec)
      return make_error(ErrorCode::IoError, "cannot read WoWDBDefs definitions directory: " + definitions.string());

    // dbdgen walks sorted(*.dbd); the blob writer wants name-sorted tables
    // anyway, so collect and sort by IDENTIFIER name.
    std::vector<std::pair<std::string, std::filesystem::path>> files;
    for (const auto& entry : it) {
      if (!entry.is_regular_file() || entry.path().extension() != ".dbd") continue;
      std::string table = entry.path().stem().string();
      if (table == "Item-sparse") // dbdgen's IDENT_RENAMES
        table = "ItemSparseLegacy";
      const bool identifier = !table.empty() && !(table.front() >= '0' && table.front() <= '9') && std::ranges::all_of(
        table, ident_char);
      if (!identifier) continue;
      files.emplace_back(std::move(table), entry.path());
    }
    std::ranges::sort(files);

    BlobWriter writer;
    for (const auto& [table, path] : files) {
      std::ifstream in{path, std::ios::binary};
      if (!in) continue;
      std::ostringstream buffer;
      buffer << in.rdbuf();
      const auto def = parse_dbd(buffer.str());
      if (!def) continue; // dbdgen warns and skips; the loader skips silently

      // Per-target member lists, then collapse() consecutive identical ones.
      std::vector<Range> ranges;
      for (std::size_t era = 0; era < targets.size(); ++era) {
        const Build build = build_of(targets[era]);
        const VersionBlock* block = def->block_for(build);
        if (!block) continue;
        auto members = members_for(*def, *block, build);
        if (!members) {
          ranges.clear(); // any era error drops the table, like dbdgen
          break;
        }
        const auto same = [&](const Range& range) {
          return range.members.size() == members->size() && std::ranges::equal(
            range.members, *members, [](const Member& a, const Member& b) {
              return a.same_shape(b);
            });
        };
        if (!ranges.empty() && same(ranges.back()))
          ranges.back().era_mask |= static_cast<std::uint16_t>(1u << era);
        else
          ranges.push_back(Range{static_cast<std::uint16_t>(1u << era), std::move(*members)});
      }
      if (ranges.empty()) continue;
      const std::string disk = table == "ItemSparseLegacy" ? "Item-sparse" : table;
      writer.add_table(table, disk, ranges);
    }

    auto bytes = writer.finish();
    const blob::View view{bytes};
    if (!view.valid() || view.table_count() == 0)
      return make_error(ErrorCode::SchemaBlobInvalid,
                        "no WoWDBDefs definition in " + definitions.string() + " produced a usable schema");
    return from_blob(std::move(bytes));
  }
}

#endif // WOWLIB_DB_SCHEMA_RUNTIME
