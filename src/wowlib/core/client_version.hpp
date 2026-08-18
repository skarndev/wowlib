#pragma once

/** @file
    Client version identity, the flavor axis that separates a client's CONTENT
    version from the engine generation its files are laid out for, the
    storage-kind split both imply, locales, and the `versions` namespace of
    release constants. */

#include <array>
#include <compare>
#include <cstdint>
#include <iosfwd>
#include <optional>
#include <string_view>
#include <utility>

#include <welder/vocabulary.hpp>

namespace wowlib
{
  enum class [[
    =welder::weld,
    =welder::doc(R"(
        Which storage technology a client generation uses: Mpq for pre-WoD retail
        clients (StormLib), Casc for WoD+ and every Classic client (CascLib).)")
  ]] StorageKind
  {
    Mpq,  /**< Pre-WoD retail clients (< 6.0), StormLib. */
    Casc  /**< WoD+ retail and all Classic clients, CascLib. */
  };

  enum class [[
    =welder::weld,
    =welder::doc(R"(
        Which product line a client belongs to. Retail is the ordinary
        progression client, where the version number also tells you the engine
        generation. Every other flavor is a MODERN-engine client wearing a
        legacy version number: WoW Classic Era 1.15.9 is a Midnight-era client,
        Cataclysm Classic 4.4.2 a War Within-era one. Their file formats follow
        the build number, not the version tuple — see
        ClientVersion.format_lineage.

        The enumerator also names the client's default TACT product code
        ('wow', 'wow_classic', 'wow_classic_era', 'wow_anniversary'); products
        outside these four (PTR, beta, 'wow_classic_titan') pick the closest
        flavor and pass their exact code to the filesystem explicitly.)")
  ]] ClientFlavor
  {
    Retail,       /**< The progression client ('wow'). */
    Classic,      /**< The Classic progression line ('wow_classic'): BCC 2.5, WotLK 3.4, Cata 4.4, MoP 5.5. */
    ClassicEra,   /**< Classic Era ('wow_classic_era'): the 1.13-1.15 vanilla realms. */
    Anniversary   /**< The Anniversary realms ('wow_anniversary'). */
  };

  struct [[
    =welder::weld,
    =welder::doc(R"(
        A full client version tuple (major.minor.patch, build) plus the product
        flavor it came from. Determines which storage backend and archive chain
        a client uses, and — through format_lineage — which engine generation
        its files are laid out for. The versions namespace provides constants
        for the releases wowlib targets.

        The version tuple alone does NOT identify an engine: the Classic
        products reuse legacy version numbers on top of whatever retail branch
        was current when they were built (Classic 3.4.x spans three retail
        generations; 'wow_classic_titan' calls itself 3.80). The BUILD number
        does — Blizzard's build counter is global across every product — so
        every format decision keys on format_lineage, never on major/minor.)")
  ]] ClientVersion
  {
    [[=welder::doc("Expansion number, e.g. 3 for Wrath of the Lich King.")]]
    std::uint16_t major = 0;

    [[=welder::doc("Minor version within the expansion.")]]
    std::uint16_t minor = 0;

    [[=welder::doc("Patch version within the minor release.")]]
    std::uint16_t patch = 0;

    [[=welder::doc("Exact client build number, e.g. 12340 for 3.3.5a. Unique and "
                   "monotonic across ALL products, which is what makes it the "
                   "only reliable engine-generation key.")]]
    std::uint32_t build = 0;

    [[=welder::doc("Which product line this client is — Retail unless stated.")]]
    ClientFlavor flavor = ClientFlavor::Retail;

    [[=welder::getter,
      =welder::doc("Whether this is a Classic-family client: a modern engine "
                   "wearing a legacy version number.")]]
    constexpr bool is_classic() const { return flavor != ClientFlavor::Retail; }

    [[=welder::getter,
      =welder::doc("Which storage technology this client uses: Mpq for pre-WoD "
                   "(< 6.0) retail clients, Casc for everything else — every "
                   "Classic client is CASC no matter what its version says.")]]
    constexpr StorageKind storage_kind() const
    {
      return !is_classic() && major < 6 ? StorageKind::Mpq : StorageKind::Casc;
    }

    [[=welder::getter,
      =welder::doc(R"(
          The RETAIL release whose file formats this client's files follow — the
          version every format decision is actually made against.

          For a retail client this is the version itself. For a Classic client
          it is the retail branch that was the live client when this build was
          produced, looked up by build number: Classic branches fork off the
          current retail engine, so Cataclysm Classic 4.4.2 (build 60895) lays
          its files out like The War Within, not like Cataclysm. Builds newer
          than the newest release wowlib models clamp to it.)")]]
    constexpr ClientVersion format_lineage() const;

    [[=welder::getter,
      =welder::doc("The TACT product code this flavor installs under by default "
                   "('wow', 'wow_classic', 'wow_classic_era', "
                   "'wow_anniversary'). PTR, beta and one-off products carry "
                   "their own code — pass it to the filesystem explicitly.")]]
    constexpr std::string_view default_casc_product() const
    {
      switch (flavor)
      {
        case ClientFlavor::Retail:      return "wow";
        case ClientFlavor::Classic:     return "wow_classic";
        case ClientFlavor::ClassicEra:  return "wow_classic_era";
        case ClientFlavor::Anniversary: return "wow_anniversary";
      }
      std::unreachable();
    }

    constexpr auto operator<=>(const ClientVersion&) const = default;
  };

  namespace
  [[=welder::doc(R"(
      The releases wowlib targets: the last-minor-of-major of every finished
      expansion (Midnight 12.x is ongoing and has no final build yet), plus the
      newest build of each living Classic product line. Builds verified against
      wago.tools.

      The Classic constants are snapshots of a MOVING target — those products
      ship new builds continuously, and a newer build can land on a newer engine
      (Classic 4.4.0 is Dragonflight-era, 4.4.1 already War Within-era). Pin the
      exact build you have, or let ClientInstall.detect read it off the install.)")]]
  versions
  {
    [[=welder::weld,
      =welder::doc("Vanilla 1.12.1 (build 5875).")]]
    inline constexpr ClientVersion vanilla{1, 12, 1, 5875};

    [[=welder::weld,
      =welder::doc("The Burning Crusade 2.4.3 (build 8606).")]]
    inline constexpr ClientVersion tbc{2, 4, 3, 8606};

    [[=welder::weld,
      =welder::doc("Wrath of the Lich King 3.3.5a (build 12340).")]]
    inline constexpr ClientVersion wotlk{3, 3, 5, 12340};

    [[=welder::weld,
      =welder::doc("Cataclysm 4.3.4 (build 15595).")]]
    inline constexpr ClientVersion cata{4, 3, 4, 15595};

    [[=welder::weld,
      =welder::doc("Mists of Pandaria 5.4.8 (build 18414).")]]
    inline constexpr ClientVersion mop{5, 4, 8, 18414};

    [[=welder::weld,
      =welder::doc("Warlords of Draenor 6.2.4 (build 21742).")]]
    inline constexpr ClientVersion wod{6, 2, 4, 21742};

    [[=welder::weld,
      =welder::doc("Legion 7.3.5 (build 26972).")]]
    inline constexpr ClientVersion legion{7, 3, 5, 26972};

    [[=welder::weld,
      =welder::doc("Battle for Azeroth 8.3.7 (build 35662).")]]
    inline constexpr ClientVersion bfa{8, 3, 7, 35662};

    [[=welder::weld,
      =welder::doc("Shadowlands 9.2.7 (build 45745).")]]
    inline constexpr ClientVersion shadowlands{9, 2, 7, 45745};

    [[=welder::weld,
      =welder::doc("Dragonflight 10.2.7 (build 55664).")]]
    inline constexpr ClientVersion dragonflight{10, 2, 7, 55664};

    [[=welder::weld,
      =welder::doc("The War Within 11.2.7 (build 65299).")]]
    inline constexpr ClientVersion tww{11, 2, 7, 65299};

    // --- Classic: legacy version numbers on modern engines -------------------
    //
    // Each is the newest build of its product line as of 2026-08-18. The
    // format_lineage each maps onto is noted; it follows the build, so bumping
    // a constant can legitimately move it to a newer engine.

    [[=welder::weld,
      =welder::doc("Classic Era 1.15.9 (build 69109) — a Midnight-era client "
                   "('wow_classic_era').")]]
    inline constexpr ClientVersion classic_era{1, 15, 9, 69109, ClientFlavor::ClassicEra};

    [[=welder::weld,
      =welder::doc("Burning Crusade Classic 2.5.4 (build 44833) — a "
                   "Shadowlands-era client ('wow_classic').")]]
    inline constexpr ClientVersion classic_bcc{2, 5, 4, 44833, ClientFlavor::Classic};

    [[=welder::weld,
      =welder::doc("Wrath of the Lich King Classic 3.4.5 (build 63697) — a War "
                   "Within-era client ('wow_classic').")]]
    inline constexpr ClientVersion classic_wotlk{3, 4, 5, 63697, ClientFlavor::Classic};

    [[=welder::weld,
      =welder::doc("Cataclysm Classic 4.4.2 (build 60895) — a War Within-era "
                   "client ('wow_classic').")]]
    inline constexpr ClientVersion classic_cata{4, 4, 2, 60895, ClientFlavor::Classic};

    [[=welder::weld,
      =welder::doc("Mists of Pandaria Classic 5.5.4 (build 69155) — a "
                   "Midnight-era client ('wow_classic').")]]
    inline constexpr ClientVersion classic_mop{5, 5, 4, 69155, ClientFlavor::Classic};

    [[=welder::weld,
      =welder::doc("The Anniversary realms 2.5.6 (build 69110) — a Midnight-era "
                   "client ('wow_anniversary').")]]
    inline constexpr ClientVersion anniversary{2, 5, 6, 69110, ClientFlavor::Anniversary};
  }

  namespace detail
  {
    /** One retail engine generation on the global build counter: the build at
        which that major became the live/PTR client — the point from which
        concurrently built Classic clients fork off it — and the release wowlib
        models the generation with. Ascending by build. */
    struct EngineGeneration
    {
      std::uint32_t first_build;  /**< First live/PTR build of the retail major. */
      ClientVersion release;      /**< The versions constant modelling it. */
    };

    /** The retail engine timeline a Classic build is placed on. The pre-BfA
        rows are unreachable from a Classic client (the oldest Classic build is
        1.13.0.28211) and exist so the table reads as the whole CASC era.

        The newest row is the newest generation wowlib models: Midnight (12.x,
        from build 65390) has no format support yet, so 12.x-era Classic builds
        clamp onto The War Within. Adding a Midnight row here and to the format
        grids fixes retail and Classic in one move. */
    inline constexpr std::array engine_timeline{
      EngineGeneration{19034, versions::wod},           // 6.0.2 prepatch
      EngineGeneration{22248, versions::legion},        // 7.0.3 prepatch
      EngineGeneration{26926, versions::bfa},           // 8.0.1
      EngineGeneration{35917, versions::shadowlands},   // 9.0.1
      EngineGeneration{46181, versions::dragonflight},  // 10.0.0 prepatch
      EngineGeneration{55666, versions::tww},           // 11.0.0 prepatch
    };
  }

  constexpr ClientVersion ClientVersion::format_lineage() const
  {
    if (!is_classic())
      return *this;

    ClientVersion out = detail::engine_timeline.front().release;
    for (const detail::EngineGeneration& generation : detail::engine_timeline)
      if (generation.first_build <= build)
        out = generation.release;
    return out;
  }

  /** Render @a version as "major.minor.patch.build", with the flavor appended
      when it is not Retail ("1.15.9.69109 (ClassicEra)") — a Classic version is
      unreadable without it, since the tuple alone says nothing about the client.
      Welded as __str__ / __tostring.
      @param out     the stream.
      @param version the version to render.
      @return @a out. */
  std::ostream& operator<<(std::ostream& out, const ClientVersion& version);

  enum class [[
    =welder::weld,
    =welder::doc("Game client locale; enumerators use the client's own four-letter "
                 "codes.")
  ]] Locale
  {
    enUS, enGB, deDE, frFR, ruRU, esES, esMX, koKR, zhCN, zhTW, ptBR, itIT
  };

  /** The four-letter code of @a locale ("enUS", ...) as used in MPQ locale
      directory and archive names.
      @param locale the locale.
      @return a static string, never dangling. */
  std::string_view locale_code(Locale locale);

  /** Parse a four-letter locale code.
      @param code e.g. "enUS" (case-sensitive, client spelling).
      @return the locale, or nullopt if the code is unknown. */
  std::optional<Locale> locale_from_code(std::string_view code);

  /** The CASC_LOCALE_* bit of @a locale for CascOpenStorage/CascOpenFile locale
      masks. Values mirror CascLib's CascPort.h so public headers stay CascLib-free.
      @param locale the locale.
      @return a single-bit mask value. */
  std::uint32_t casc_locale_flag(Locale locale);
}