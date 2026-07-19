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

  // --- miniature annotation vocabulary (mirrors formats/chunk/annotations.hpp)
  struct chunk_spec
  {
    std::uint32_t magic;
  };
  struct since_spec
  {
    ClientVersion v;
  };
  struct until_spec
  {
    ClientVersion v;
  };
  struct optional_spec
  {
  };

  consteval chunk_spec chunk(const char (&cc)[5])
  {
    return {static_cast<std::uint32_t>(cc[0]) | static_cast<std::uint32_t>(cc[1]) << 8
            | static_cast<std::uint32_t>(cc[2]) << 16 | static_cast<std::uint32_t>(cc[3]) << 24};
  }
  consteval since_spec since(ClientVersion v) { return {v}; }
  consteval until_spec until(ClientVersion v) { return {v}; }
  inline constexpr optional_spec optional{};

  // --- constrained partial specialization on a ClientVersion NTTP
  inline constexpr ClientVersion split_boundary{9, 1, 5, 40772};

  template <ClientVersion V>
  struct GroupHeader;

  template <ClientVersion V>
    requires(V < split_boundary)
  struct GroupHeader<V>
  {
    std::uint32_t name_ofs;
    std::uint32_t unk;
  };

  template <ClientVersion V>
    requires(V >= split_boundary)
  struct GroupHeader<V>
  {
    std::uint32_t name_ofs;
    std::int16_t parent_split;
    std::int16_t next_split;
  };

  static_assert(sizeof(GroupHeader<versions::wotlk>) == 8);
  static_assert(sizeof(GroupHeader<versions::shadowlands>) == 8);
  static_assert(std::is_trivially_copyable_v<GroupHeader<versions::shadowlands>>);

  // --- annotated members of a ClientVersion-NTTP template
  inline constexpr ClientVersion fdid_refs{8, 1, 0, 28186};

  template <ClientVersion V>
  struct Entity
  {
    static constexpr ClientVersion version = V;

    [[=chunk("MVER")]] std::uint32_t mver = 17;
    [[=chunk("MOHD")]] GroupHeader<V> header{};
    [[=chunk("MOTX"), =until(fdid_refs), =optional]] std::vector<char> textures;
    [[=chunk("GFID"), =since(versions::legion), =optional]] std::vector<std::uint32_t> fdids;
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
  consteval bool version_active()
  {
    if (auto s = annotation<since_spec, M>(); s && V < s->v)
      return false;
    if (auto u = annotation<until_spec, M>(); u && V >= u->v)
      return false;
    return true;
  }

  struct member_row
  {
    std::uint32_t magic;  // 0 == no chunk annotation
    bool active;
    bool optional;
  };

  template <typename E>
  consteval auto rows()
  {
    static constexpr auto members = std::define_static_array(
      std::meta::nonstatic_data_members_of(^^E, std::meta::access_context::current()));
    std::array<member_row, members.size()> out{};
    std::size_t i = 0;
    template for (constexpr auto m : members)
    {
      constexpr auto c = annotation<chunk_spec, m>();
      out[i] = {c ? c->magic : 0u, version_active<E::version, m>(),
                annotation<optional_spec, m>().has_value()};
      ++i;
    }
    return out;
  }

  /** Runtime dispatch shape: match a scanned fourcc against annotated members
      and mutate the spliced member — exactly what read_entity does. */
  template <typename E>
  bool poke(E& e, std::uint32_t fourcc)
  {
    bool matched = false;
    static constexpr auto members = std::define_static_array(
      std::meta::nonstatic_data_members_of(^^E, std::meta::access_context::current()));
    template for (constexpr auto m : members)
    {
      if constexpr (constexpr auto c = annotation<chunk_spec, m>(); c.has_value())
      {
        if (!matched && fourcc == c->magic && version_active<E::version, m>())
        {
          matched = true;
          e.[:m:] = {};
        }
      }
    }
    return matched;
  }

  constexpr auto wotlk_rows = rows<Entity<versions::wotlk>>();
  constexpr auto sl_rows = rows<Entity<versions::shadowlands>>();

  static_assert(wotlk_rows.size() == 5);
  static_assert(wotlk_rows[0].magic == chunk("MVER").magic);
  static_assert(wotlk_rows[2].active);   // MOTX active pre-8.1
  static_assert(!wotlk_rows[3].active);  // GFID inactive on wotlk
  static_assert(!sl_rows[2].active);     // MOTX inactive on shadowlands
  static_assert(sl_rows[3].active);      // GFID active on shadowlands
  static_assert(sl_rows[3].optional);
  static_assert(sl_rows[4].magic == 0);  // unannotated member has no chunk
}

// --- explicit instantiation spelled with versions:: constants
template struct Entity<versions::wotlk>;
template struct Entity<versions::shadowlands>;

TEST_CASE("reflection dispatch matches active annotated members only", "[formats][reflect]")
{
  Entity<versions::wotlk> e;
  e.mver = 99;
  CHECK(poke(e, chunk("MVER").magic));
  CHECK(e.mver == 0);                          // spliced member was written
  CHECK_FALSE(poke(e, chunk("GFID").magic));   // inactive on wotlk
  CHECK_FALSE(poke(e, chunk("XXXX").magic));   // unknown fourcc

  Entity<versions::shadowlands> sl;
  CHECK(poke(sl, chunk("GFID").magic));        // active on shadowlands
  CHECK_FALSE(poke(sl, chunk("MOTX").magic));  // inactive on shadowlands
}
