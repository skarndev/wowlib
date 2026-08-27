#include <catch2/catch_test_macros.hpp>

/** @file
    Canary for the gcc-16 reflection corners the formats chunk framework stands
    on. If this file stops compiling after a toolchain bump, the serializer
    engine is broken too — fix here first, with a minimal reproduction in hand.

    Probes:
     - structural annotation specs carrying a ClientVersion payload;
     - annotations on members of a ClientVersion-NTTP class template, read
       through an instantiation via std::meta::annotations_of_with_type;
     - `template for` over nonstatic_data_members_of of such an instantiation,
       extracting annotations and splicing members (the serializer's core move);
     - constrained partial specialization on a ClientVersion NTTP;
     - explicit instantiation directives spelled with versions:: constants.

    Idiom note (gcc 16.1): a define_static_array local used as a `template for`
    range inside a function template must be `static constexpr` — a plain
    constexpr local is rejected as "address may differ per invocation". */

#include <meta>

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

#include <wowlib/core/client_version.hpp>

namespace
{
  using wowlib::ClientVersion;
  namespace versions = wowlib::versions;

  // --- miniature annotation vocabulary (mirrors formats/common/annotations.hpp)
  struct ChunkSpec
  {
    std::uint32_t magic;
  };
  struct SinceSpec
  {
    ClientVersion v;
  };
  struct UntilSpec
  {
    ClientVersion v;
  };
  struct OptionalSpec
  {
  };

  consteval ChunkSpec chunk(const char (&cc)[5])
  {
    return {static_cast<std::uint32_t>(cc[0]) | static_cast<std::uint32_t>(cc[1]) << 8
            | static_cast<std::uint32_t>(cc[2]) << 16 | static_cast<std::uint32_t>(cc[3]) << 24};
  }
  consteval SinceSpec since(ClientVersion v) { return {v}; }
  consteval UntilSpec until(ClientVersion v) { return {v}; }
  inline constexpr OptionalSpec Optional{};

  // --- constrained partial specialization on a ClientVersion NTTP
  inline constexpr ClientVersion SplitBoundary{9, 1, 5, 40772};

  template <ClientVersion V>
  struct GroupHeader;

  template <ClientVersion V>
    requires(V < SplitBoundary)
  struct GroupHeader<V>
  {
    std::uint32_t nameOfs;
    std::uint32_t unk;
  };

  template <ClientVersion V>
    requires(V >= SplitBoundary)
  struct GroupHeader<V>
  {
    std::uint32_t nameOfs;
    std::int16_t parentSplit;
    std::int16_t nextSplit;
  };

  static_assert(sizeof(GroupHeader<versions::Wotlk>) == 8);
  static_assert(sizeof(GroupHeader<versions::Shadowlands>) == 8);
  static_assert(std::is_trivially_copyable_v<GroupHeader<versions::Shadowlands>>);

  // --- annotated members of a ClientVersion-NTTP template
  inline constexpr ClientVersion FdidRefs{8, 1, 0, 28186};

  template <ClientVersion V>
  struct Entity
  {
    static constexpr ClientVersion Version = V;

    [[=chunk("MVER")]] std::uint32_t mver = 17;
    [[=chunk("MOHD")]] GroupHeader<V> header{};
    [[=chunk("MOTX"), =until(FdidRefs), =Optional]] std::vector<char> textures;
    [[=chunk("GFID"), =since(versions::Legion), =Optional]] std::vector<std::uint32_t> fdids;
    std::vector<std::byte> unannotated;  // must be seen as chunk-less
  };

  // --- template-for walk + annotation extraction
  template <typename Spec, std::meta::info M>
  consteval std::optional<Spec> annotation()
  {
    auto anns = std::meta::annotations_of_with_type(M, ^^Spec);
    if (anns.empty())
      return std::nullopt;
    return std::meta::extract<Spec>(anns[0]);
  }

  template <ClientVersion V, std::meta::info M>
  consteval bool versionActive()
  {
    if (auto s = annotation<SinceSpec, M>(); s && V < s->v)
      return false;
    if (auto u = annotation<UntilSpec, M>(); u && V >= u->v)
      return false;
    return true;
  }

  struct MemberRow
  {
    std::uint32_t magic;  // 0 == no chunk annotation
    bool active;
    bool optional;
  };

  template <typename E>
  consteval auto rows()
  {
    static constexpr auto Members = std::define_static_array(
      std::meta::nonstatic_data_members_of(^^E, std::meta::access_context::current()));
    std::array<MemberRow, Members.size()> out{};
    std::size_t i = 0;
    template for (constexpr auto m : Members)
    {
      constexpr auto c = annotation<ChunkSpec, m>();
      out[i] = {c ? c->magic : 0u, versionActive<E::Version, m>(),
                annotation<OptionalSpec, m>().has_value()};
      ++i;
    }
    return out;
  }

  /** Runtime dispatch shape: match a scanned fourcc against annotated members
      and mutate the spliced member — exactly what readEntity does. */
  template <typename E>
  bool poke(E& e, std::uint32_t fourcc)
  {
    bool matched = false;
    static constexpr auto Members = std::define_static_array(
      std::meta::nonstatic_data_members_of(^^E, std::meta::access_context::current()));
    template for (constexpr auto m : Members)
    {
      if constexpr (constexpr auto c = annotation<ChunkSpec, m>(); c.has_value())
      {
        if (!matched && fourcc == c->magic && versionActive<E::Version, m>())
        {
          matched = true;
          e.[:m:] = {};
        }
      }
    }
    return matched;
  }

  constexpr auto WotlkRows = rows<Entity<versions::Wotlk>>();
  constexpr auto SlRows = rows<Entity<versions::Shadowlands>>();

  static_assert(WotlkRows.size() == 5);
  static_assert(WotlkRows[0].magic == chunk("MVER").magic);
  static_assert(WotlkRows[2].active);   // MOTX active pre-8.1
  static_assert(!WotlkRows[3].active);  // GFID inactive on wotlk
  static_assert(!SlRows[2].active);     // MOTX inactive on shadowlands
  static_assert(SlRows[3].active);      // GFID active on shadowlands
  static_assert(SlRows[3].optional);
  static_assert(SlRows[4].magic == 0);  // unannotated member has no chunk
}

// --- explicit instantiation spelled with versions:: constants (inside the
// anonymous namespace: pedantically, an instantiation outside it would need a
// nested-name-specifier it cannot have)
namespace
{
  template struct Entity<versions::Wotlk>;
  template struct Entity<versions::Shadowlands>;
}

TEST_CASE("reflection dispatch matches active annotated members only", "[formats][reflect]")
{
  Entity<versions::Wotlk> e;
  e.mver = 99;
  CHECK(poke(e, chunk("MVER").magic));
  CHECK(e.mver == 0);                          // spliced member was written
  CHECK_FALSE(poke(e, chunk("GFID").magic));   // inactive on wotlk
  CHECK_FALSE(poke(e, chunk("XXXX").magic));   // unknown fourcc

  Entity<versions::Shadowlands> sl;
  CHECK(poke(sl, chunk("GFID").magic));        // active on shadowlands
  CHECK_FALSE(poke(sl, chunk("MOTX").magic));  // inactive on shadowlands
}
