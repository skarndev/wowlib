#pragma once

/** @file
    Reading a client installation's own identity off disk: which product it is,
    which build, and therefore which flavor and format lineage. */

#include <filesystem>
#include <string>

#include <welder/vocabulary.hpp>

#include <wowlib/core/client_version.hpp>
#include <wowlib/core/error.hpp>

namespace wowlib::fs {
  struct [[
      =welder::weld,
      =welder::doc(R"(
        What a client installation says about itself: the exact version, the
        flavor that follows from its product code, and the TACT product code
        itself.

        Detecting beats hand-writing a ClientVersion for anything Classic. Those
        products ship new builds continuously and the build — not the 1.15 / 4.4
        version number — decides which engine's file formats the install
        carries, so the build has to be right. detect() reads it from the files
        Blizzard's installer leaves behind.

        Examples:
            ```python
            from wowlib.fs import ClientInstall, FileSystem, FileSystemSettings

            install = ClientInstall.detect("/Games/World of Warcraft/_classic_era_")
            print(install.version)         # 1.15.9.69109 (ClassicEra)
            print(install.casc_product)    # 'wow_classic_era'

            settings = FileSystemSettings(install.path, install.version,
                                          casc_product=install.casc_product)
            with FileSystem.open(settings) as fs:
                ...
            ```)")
    ]] ClientInstall {
    [[=welder::doc("The installation directory that was inspected — the one "
      "holding Data/, ready to hand to FileSystemSettings.")]]
    std::filesystem::path path;

    [[=welder::doc("The exact installed version, flavor included.")]]
    ClientVersion version;

    [[=welder::doc("The exact TACT product code the installation records "
      "('wow', 'wow_classic_era', 'wow_classic_ptr', ...) — which "
      "can be more specific than the flavor's default.")]]
    std::string cascProduct;

    [[=welder::doc(R"(
        Read a client installation's identity from the files its installer
        leaves behind: .flavor.info (the product code) beside Data/, and
        .build.info (the version table) there or one directory up, where a
        multi-flavor install keeps it.

        Only works for CASC installations. MPQ-era clients (< 6.0) record no
        such thing, and neither do repacks that ship only Data/ — construct
        their ClientVersion directly, which is unambiguous anyway since those
        versions are not shared with any Classic product.)"),
      =welder::returns("the detected installation, or NotSupported when the "
        "directory carries no CASC build information")]]
    static Result<ClientInstall> detect(
      std::filesystem::path clientPath [[=welder::doc("the installation directory holding Data/")]]);
  };
}
