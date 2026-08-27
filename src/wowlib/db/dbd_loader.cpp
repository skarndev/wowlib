/** @file
    SchemaCatalog::fromDbdDir — the runtime WoWDBDefs loader: parse a
    checkout's `definitions` directory of `.dbd` files, resolve every
    targeted era's member list
    with EXACTLY dbdgen's rules, assemble a WDBS blob in memory and hand it
    to fromBlob. The parity contract (tested): built from the same
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
    constexpr std::array<ClientVersion, 11> Targets{
      versions::Vanilla,
      versions::Tbc,
      versions::Wotlk,
      versions::Cata,
      versions::Mop,
      versions::Wod,
      versions::Legion,
      versions::Bfa,
      versions::Shadowlands,
      versions::Dragonflight,
      versions::Tww
    };

    Build buildOf(ClientVersion v) {
      return {v.major, v.minor, v.patch, v.build};
    }

    /** locstringLangs (targets.py): 8 slots before TBC 2.1.0.6692 added
        ruRU, 16 until Cataclysm collapsed to a single localized string. */
    std::optional<int> locstringLangs(Build build) {
      if (build < Build{2, 1, 0, 6692}) return 8;
      if (build < Build{4, 0, 0, 0}) return 16;
      return std::nullopt;
    }

    /** emit.py's _RESERVED: C++ keywords + the record statics. */
    bool reservedName(std::string_view name) {
      static const std::set<std::string_view> Reserved{
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
      return Reserved.contains(name);
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

    /** emit.py's memberName(): strip the `_lang` locstring marker, snake,
        escape keywords/statics with a trailing underscore, digit prefix. */
    std::string memberName(std::string_view dbdName) {
      std::string_view base = dbdName;
      if (base.ends_with("_lang")) base.remove_suffix(5);
      std::string name = snake(base);
      if (reservedName(name)) name += '_';
      if (!name.empty() && name.front() >= '0' && name.front() <= '9') name.insert(name.begin(), '_');
      return name;
    }

    // --- the .dbd grammar (dbd.py) ------------------------------------------

    struct ColumnDecl {
      std::string type; /**< int / float / string / locstring. */
    };

    struct Entry {
      std::string name;
      bool isId = false;
      bool isRelation = false;
      bool noninline = false;
      std::optional<int> bits;
      bool isUnsigned = false;
      std::optional<int> arrayLen;
    };

    struct VersionBlock {
      std::vector<std::pair<Build, Build>> builds; /**< Inclusive ranges. */
      bool hasLayouts = false;
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

      const VersionBlock* blockFor(Build build) const {
        for (const VersionBlock& block : blocks)
          if (block.matches(build)) return &block;
        return nullptr;
      }
    };

    /** Strip a trailing `// comment` and surrounding whitespace. */
    std::string_view stripLine(std::string_view line) {
      if (const auto at = line.find("//"); at != std::string_view::npos) line = line.substr(0, at);
      while (!line.empty() && (line.front() == ' ' || line.front() == '\t' || line.front() == '\r')) line.
        remove_prefix(1);
      while (!line.empty() && (line.back() == ' ' || line.back() == '\t' || line.back() == '\r')) line.remove_suffix(1);
      return line;
    }

    bool identChar(char c) {
      return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
    }

    std::optional<Build> parseBuild(std::string_view text) {
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
    bool parseColumnLine(std::string_view body, Definition& def) {
      static constexpr std::array<std::string_view, 4> Types{"int", "float", "string", "locstring"};
      std::string_view type;
      for (const std::string_view t : Types)
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
      if (name.empty() || !std::ranges::all_of(name, identChar)) return false;
      def.columns.emplace(std::string{name}, ColumnDecl{std::string{type}});
      return true;
    }

    /** `$id,noninline$ID<u32>[2]` -> entry; nullopt on mismatch. */
    std::optional<Entry> parseEntry(std::string_view body) {
      Entry entry{};
      if (body.starts_with('$')) {
        const auto close = body.find('$', 1);
        if (close == std::string_view::npos) return std::nullopt;
        std::string_view anns = body.substr(1, close - 1);
        body.remove_prefix(close + 1);
        while (!anns.empty()) {
          const auto comma = anns.find(',');
          const std::string_view ann = anns.substr(0, comma);
          if (ann == "id") entry.isId = true;
          else if (ann == "relation") entry.isRelation = true;
          else if (ann == "noninline") entry.noninline = true;
          if (comma == std::string_view::npos) break;
          anns.remove_prefix(comma + 1);
        }
      }
      std::size_t at = 0;
      while (at < body.size() && identChar(body[at])) ++at;
      if (at == 0) return std::nullopt;
      entry.name = std::string{body.substr(0, at)};
      body.remove_prefix(at);
      if (body.starts_with('<')) {
        body.remove_prefix(1);
        if (body.starts_with('u')) {
          entry.isUnsigned = true;
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
        entry.arrayLen = len;
        body.remove_prefix(static_cast<std::size_t>(next - body.data()) + 1);
      }
      return body.empty() ? std::optional{entry} : std::nullopt;
    }

    /** Parse one whole .dbd (dbd.py's paragraph model). Nullopt = skip the
        table, exactly as dbdgen skips on a warning. */
    std::optional<Definition> parseDbd(const std::string& text) {
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
        const std::string_view body = stripLine(paragraphs.front()[i]);
        if (body.empty()) continue;
        if (!parseColumnLine(body, def)) return std::nullopt;
      }
      for (std::size_t p = 1; p < paragraphs.size(); ++p) {
        VersionBlock block{};
        for (const std::string_view raw : paragraphs[p]) {
          const std::string_view body = stripLine(raw);
          if (body.empty()) continue;
          if (body.starts_with("LAYOUT ")) {
            block.hasLayouts = true;
          }
          else if (body.starts_with("BUILD ")) {
            std::string_view list = body.substr(6);
            while (!list.empty()) {
              const auto comma = list.find(',');
              std::string_view part = stripLine(list.substr(0, comma));
              if (const auto dash = part.find('-'); dash != std::string_view::npos) {
                const auto lo = parseBuild(stripLine(part.substr(0, dash)));
                const auto hi = parseBuild(stripLine(part.substr(dash + 1)));
                if (!lo || !hi) return std::nullopt;
                block.builds.emplace_back(*lo, *hi);
              }
              else {
                const auto build = parseBuild(part);
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
            auto entry = parseEntry(body);
            if (!entry) return std::nullopt;
            block.entries.push_back(std::move(*entry));
          }
        }
        if (block.builds.empty() && !block.hasLayouts) return std::nullopt;
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
      bool isSigned = false;
      std::uint16_t arrayLen = 1;
      std::uint8_t localeCount = 0;
      bool isId = false;
      bool isRelation = false;
      bool noninline = false;

      bool sameShape(const Member& other) const {
        return name == other.name && type == other.type && bits == other.bits && isSigned == other.isSigned &&
          arrayLen == other.arrayLen && localeCount == other.localeCount && isId == other.isId && isRelation ==
          other.isRelation && noninline == other.noninline;
      }
    };

    /** build_members for one target; nullopt = the table skips this era
        (dbdgen warns and drops the whole table for that target). */
    std::optional<std::vector<Member>> membersFor(const Definition& def, const VersionBlock& block, Build build) {
      const std::optional<int> langs = locstringLangs(build);

      // The `_lang` strip may collide with a sibling column; colliding
      // locstrings keep their suffix (build_members' first pass).
      std::vector<std::string> names;
      names.reserve(block.entries.size());
      for (const Entry& entry : block.entries) names.push_back(memberName(entry.name));
      for (std::size_t i = 0; i < block.entries.size(); ++i) {
        if (std::ranges::count(names, names[i]) > 1 && block.entries[i].name.ends_with("_lang")) {
          std::string kept = snake(block.entries[i].name);
          if (reservedName(kept)) kept += '_';
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
        m.arrayLen = static_cast<std::uint16_t>(entry.arrayLen.value_or(1));
        m.isId = entry.isId;
        m.isRelation = entry.isRelation;
        m.noninline = entry.noninline;

        if (entry.bits) {
          if (type != "int") return std::nullopt; // sized entry on a non-int column
          if (*entry.bits != 8 && *entry.bits != 16 && *entry.bits != 32 && *entry.bits != 64) return std::nullopt;
          // unsupported width
          m.type = ColumnType::Int;
          m.bits = static_cast<std::uint8_t>(*entry.bits);
          m.isSigned = !entry.isUnsigned;
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
          if (entry.arrayLen) return std::nullopt; // locstring arrays unsupported
          if (langs) {
            m.type = ColumnType::LocString;
            m.bits = 32;
            m.localeCount = static_cast<std::uint8_t>(*langs);
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
          m.isSigned = false;
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
      std::uint16_t eraMask = 0;
      std::vector<Member> members;
    };

    class BlobWriter {
    public:
      void addTable(const std::string& name, const std::string& disk, const std::vector<Range>& ranges) {
        _appendU32(_tables, _intern(name));
        _appendU32(_tables, _intern(disk));
        _appendU32(_tables, _rangeCount);
        _appendU32(_tables, static_cast<std::uint32_t>(ranges.size()));
        for (const Range& range : ranges) {
          _appendU32(_ranges, _columnCount);
          _appendU16(_ranges, static_cast<std::uint16_t>(range.members.size()));
          _appendU16(_ranges, range.eraMask);
          ++_rangeCount;
          for (const Member& m : range.members) {
            _appendU32(_columns, _intern(m.name));
            _columns.push_back(static_cast<unsigned char>(m.type));
            _columns.push_back(m.bits);
            _columns.push_back(
              static_cast<unsigned char>((m.isSigned ? 1 : 0) | (m.isId ? 2 : 0) | (m.isRelation ? 4 : 0) | (
                m.noninline ? 8 : 0)));
            _columns.push_back(m.localeCount);
            _appendU16(_columns, m.arrayLen);
            _appendU16(_columns, 0);
            ++_columnCount;
          }
        }
        ++_tableCount;
      }

      std::vector<unsigned char> finish() const {
        std::vector<unsigned char> out;
        _appendU32(out, blob::Magic);
        _appendU32(out, blob::FormatVersion);
        _appendU32(out, _tableCount);
        _appendU32(out, _rangeCount);
        _appendU32(out, _columnCount);
        _appendU32(out, static_cast<std::uint32_t>(_pool.size()));
        out.push_back(static_cast<unsigned char>(Targets.size()));
        out.insert(out.end(), 3, 0);
        for (const ClientVersion v : Targets) {
          _appendU16(out, v.major);
          _appendU16(out, v.minor);
          _appendU16(out, v.patch);
          _appendU16(out, 0);
          _appendU32(out, v.build);
        }
        out.insert(out.end(), _tables.begin(), _tables.end());
        out.insert(out.end(), _ranges.begin(), _ranges.end());
        out.insert(out.end(), _columns.begin(), _columns.end());
        out.insert(out.end(), _pool.begin(), _pool.end());
        return out;
      }

    private:
      static void _appendU16(std::vector<unsigned char>& out, std::uint16_t v) {
        out.push_back(static_cast<unsigned char>(v & 0xFF));
        out.push_back(static_cast<unsigned char>(v >> 8));
      }

      static void _appendU32(std::vector<unsigned char>& out, std::uint32_t v) {
        for (int shift = 0; shift < 32; shift += 8) out.push_back(static_cast<unsigned char>((v >> shift) & 0xFF));
      }

      std::uint32_t _intern(const std::string& text) {
        const auto [at, fresh] = _interned.try_emplace(text, static_cast<std::uint32_t>(_pool.size()));
        if (fresh) {
          _pool.insert(_pool.end(), text.begin(), text.end());
          _pool.push_back(0);
        }
        return at->second;
      }

      std::vector<unsigned char> _pool{0}; /**< Offset 0 = "". */
      std::map<std::string, std::uint32_t> _interned{{"", 0}};
      std::vector<unsigned char> _tables;
      std::vector<unsigned char> _ranges;
      std::vector<unsigned char> _columns;
      std::uint32_t _tableCount = 0;
      std::uint32_t _rangeCount = 0;
      std::uint32_t _columnCount = 0;
    };
  }

  Result<SchemaCatalog> SchemaCatalog::fromDbdDir(const std::filesystem::path& definitions) {
    std::error_code ec;
    std::filesystem::directory_iterator it{definitions, ec};
    if (ec)
      return makeError(ErrorCode::IoError, "cannot read WoWDBDefs definitions directory: " + definitions.string());

    // dbdgen walks sorted(*.dbd); the blob writer wants name-sorted tables
    // anyway, so collect and sort by IDENTIFIER name.
    std::vector<std::pair<std::string, std::filesystem::path>> files;
    for (const auto& entry : it) {
      if (!entry.is_regular_file() || entry.path().extension() != ".dbd") continue;
      std::string table = entry.path().stem().string();
      if (table == "Item-sparse") // dbdgen's IDENT_RENAMES
        table = "ItemSparseLegacy";
      const bool identifier = !table.empty() && !(table.front() >= '0' && table.front() <= '9') && std::ranges::all_of(
        table, identChar);
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
      const auto def = parseDbd(buffer.str());
      if (!def) continue; // dbdgen warns and skips; the loader skips silently

      // Per-target member lists, then collapse() consecutive identical ones.
      std::vector<Range> ranges;
      for (std::size_t era = 0; era < Targets.size(); ++era) {
        const Build build = buildOf(Targets[era]);
        const VersionBlock* block = def->blockFor(build);
        if (!block) continue;
        auto members = membersFor(*def, *block, build);
        if (!members) {
          ranges.clear(); // any era error drops the table, like dbdgen
          break;
        }
        const auto same = [&](const Range& range) {
          return range.members.size() == members->size() && std::ranges::equal(
            range.members, *members, [](const Member& a, const Member& b) {
              return a.sameShape(b);
            });
        };
        if (!ranges.empty() && same(ranges.back()))
          ranges.back().eraMask |= static_cast<std::uint16_t>(1u << era);
        else
          ranges.push_back(Range{static_cast<std::uint16_t>(1u << era), std::move(*members)});
      }
      if (ranges.empty()) continue;
      const std::string disk = table == "ItemSparseLegacy" ? "Item-sparse" : table;
      writer.addTable(table, disk, ranges);
    }

    auto bytes = writer.finish();
    const blob::View view{bytes};
    if (!view.valid() || view.tableCount() == 0)
      return makeError(ErrorCode::SchemaBlobInvalid,
                        "no WoWDBDefs definition in " + definitions.string() + " produced a usable schema");
    return fromBlob(std::move(bytes));
  }
}

#endif // WOWLIB_DB_SCHEMA_RUNTIME
