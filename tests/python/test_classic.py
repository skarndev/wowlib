"""The Classic client axis on the Python surface: the flavor field, the format
lineage a build resolves to, the ClientVersion overload of for_version(), and
ClientInstall.detect() reading an installation's identity off disk.

The C++ side (tests/unit/test_client_flavor.cpp) owns the lineage table itself;
these cases guard that the binding actually exposes it and that the facade
dispatches a Classic version to the same class its retail counterpart gets.
"""

import pytest

import wowlib
from wowlib.formats import adt as adt_mod
from wowlib.formats import wmo as wmo_mod

versions = wowlib.versions


def test_classic_constants_carry_their_flavor():
    assert versions.classic_era.flavor == wowlib.ClientFlavor.ClassicEra
    assert versions.classic_cata.flavor == wowlib.ClientFlavor.Classic
    assert versions.anniversary.flavor == wowlib.ClientFlavor.Anniversary
    assert versions.wotlk.flavor == wowlib.ClientFlavor.Retail
    assert versions.classic_era.is_classic
    assert not versions.wotlk.is_classic


def test_classic_clients_are_casc_whatever_their_version_says():
    # 1.15 and 4.4 would both be MPQ if the major decided it.
    assert versions.classic_era.storage_kind == wowlib.StorageKind.Casc
    assert versions.classic_cata.storage_kind == wowlib.StorageKind.Casc
    assert versions.mop.storage_kind == wowlib.StorageKind.Mpq


def test_format_lineage_places_a_build_on_the_retail_timeline():
    assert versions.classic_bcc.format_lineage == versions.shadowlands
    assert versions.classic_cata.format_lineage == versions.tww
    assert versions.wotlk.format_lineage == versions.wotlk


def test_str_spells_the_build_and_the_flavor():
    assert str(versions.classic_era) == "1.15.9.69109 (ClassicEra)"
    assert str(versions.wotlk) == "3.3.5.12340"


def test_flavor_names_its_default_product():
    assert versions.classic_era.default_casc_product == "wow_classic_era"
    assert versions.wotlk.default_casc_product == "wow"


def test_expansion_stays_the_content_axis():
    assert wowlib.expansion_of(versions.classic_cata) == wowlib.Expansion.Cata
    assert wowlib.to_expansion(versions.classic_cata) is None
    assert (wowlib.to_expansion(versions.classic_cata.format_lineage)
            == wowlib.Expansion.TheWarWithin)


@pytest.mark.parametrize("version, expected", [
    (versions.classic_bcc, "WMOShadowlands"),
    (versions.classic_cata, "WMOTheWarWithin"),
    (versions.classic_era, "WMOTheWarWithin"),
    (versions.wotlk, "WMOVanillaToWotlk"),
])
def test_for_version_accepts_a_client_version(version, expected):
    assert type(wmo_mod.WMO.for_version(version)).__name__ == expected


def test_for_version_still_accepts_an_expansion():
    assert type(wmo_mod.WMO.for_version(wowlib.Expansion.Wotlk)).__name__ \
        == "WMOVanillaToWotlk"


def test_a_classic_version_lands_on_the_retail_counterpart():
    # The point of the whole exercise: no separate Classic instantiation exists,
    # because a Classic client IS its engine's client.
    assert type(adt_mod.ADT.for_version(versions.classic_era)) \
        is type(adt_mod.ADT.for_version(versions.tww))


def test_a_hand_built_version_resolves_by_build():
    # Cataclysm Classic 4.4.0 shipped on Dragonflight and later on TWW; the
    # version number is identical, so only the build can tell them apart.
    early = wowlib.ClientVersion(4, 4, 0, 54481, wowlib.ClientFlavor.Classic)
    late = wowlib.ClientVersion(4, 4, 0, 57244, wowlib.ClientFlavor.Classic)
    assert type(wmo_mod.WMO.for_version(early)).__name__ == "WMODragonflight"
    assert type(wmo_mod.WMO.for_version(late)).__name__ == "WMOTheWarWithin"


def test_flavor_defaults_to_retail_so_old_call_sites_are_unchanged():
    assert wowlib.ClientVersion(3, 3, 5, 12340) == versions.wotlk


def test_detect_reads_an_installation(tmp_path):
    (tmp_path / "Data").mkdir()
    (tmp_path / ".flavor.info").write_text(
        "## product-install-script-name!STRING:0\nwow_classic_era\n")
    (tmp_path / ".build.info").write_text(
        "Branch!STRING:0|Active!DEC:1|Version!STRING:0|Product!STRING:0\n"
        "us|1|11.2.7.65299|wow\n"
        "us|1|1.15.9.69109|wow_classic_era\n")

    install = wowlib.fs.ClientInstall.detect(str(tmp_path))
    assert install.version == versions.classic_era
    assert install.casc_product == "wow_classic_era"
    assert install.version.flavor == wowlib.ClientFlavor.ClassicEra


def test_detect_finds_the_table_one_directory_up(tmp_path):
    # A multi-flavor install keeps one .build.info beside the flavor directories.
    flavor_dir = tmp_path / "_classic_"
    (flavor_dir / "Data").mkdir(parents=True)
    (flavor_dir / ".flavor.info").write_text(
        "## product-install-script-name!STRING:0\nwow_classic\n")
    (tmp_path / ".build.info").write_text(
        "Active!DEC:1|Version!STRING:0|Product!STRING:0\n"
        "1|5.5.4.69155|wow_classic\n")

    install = wowlib.fs.ClientInstall.detect(str(flavor_dir))
    assert install.version == versions.classic_mop


def test_detect_refuses_an_mpq_client(tmp_path):
    (tmp_path / "Data").mkdir()
    with pytest.raises(wowlib.NotSupported):
        wowlib.fs.ClientInstall.detect(str(tmp_path))


def test_settings_detect_fills_in_version_and_product(tmp_path):
    (tmp_path / "Data").mkdir()
    (tmp_path / ".flavor.info").write_text(
        "## product-install-script-name!STRING:0\nwow_classic_era\n")
    (tmp_path / ".build.info").write_text(
        "Active!DEC:1|Version!STRING:0|Product!STRING:0\n"
        "1|1.15.9.69109|wow_classic_era\n")

    settings = wowlib.fs.FileSystemSettings.detect(str(tmp_path))
    assert settings.version == versions.classic_era
    assert settings.casc_product == "wow_classic_era"
