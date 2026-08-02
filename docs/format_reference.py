"""mkdocs hook shim — reloads the real logic so `mkdocs serve` picks up edits.

mkdocs caches an imported hook module for the lifetime of the process (its
`_load_hook` is `@lru_cache`d and the option instance is created once at import),
so editing a hook's code never takes effect under `mkdocs serve` — you'd have to
restart. This shim sidesteps that: it stays trivial (so its own cached copy is
fine) and **reloads** the engine (`format_reference_impl`) plus the per-format
config modules on every build (`on_pre_build`), delegating the page events to the
fresh engine. Edit the logic there; serve will reflect it live (those files are
added to the watch list by docs/build.py). A syntax error is logged and the
previous good version is kept, so serve stays up.

Reload order matters: the engine first (the configs instantiate its dataclasses),
then each config, so the instances are rebuilt against the fresh classes; the
engine's lazy `formats()` cache resets with its reload and re-imports the
already-reloaded configs from sys.modules.
"""

import importlib
import logging
import os
import sys

_HERE = os.path.dirname(os.path.abspath(__file__))

import format_reference_impl as _impl
import wmo_reference_config as _wmo
import m2_reference_config as _m2
import wdt_reference_config as _wdt
import wdl_reference_config as _wdl
import adt_reference_config as _adt
import db_reference_config as _db
import common_reference_config as _common

log = logging.getLogger("mkdocs.hooks.format_reference")


def on_pre_build(config, **kwargs):
    global _impl, _wmo, _m2, _wdt, _wdl, _adt, _db, _common
    # mkdocs saves/restores sys.path around hook import, so this dir isn't on the
    # path by the time we reload — re-add it here (this runs after that restore).
    if _HERE not in sys.path:
        sys.path.insert(0, _HERE)
    try:
        _impl = importlib.reload(_impl)
        _wmo = importlib.reload(_wmo)
        _m2 = importlib.reload(_m2)
        _wdt = importlib.reload(_wdt)
        _wdl = importlib.reload(_wdl)
        _adt = importlib.reload(_adt)
        _db = importlib.reload(_db)
        _common = importlib.reload(_common)
    except Exception as e:  # keep serving the last good version on a bad edit
        log.warning("format_reference reload failed (%s); keeping previous version", e)


def on_page_markdown(markdown, **kwargs):
    return _impl.on_page_markdown(markdown, **kwargs)


def on_page_content(html, **kwargs):
    return _impl.on_page_content(html, **kwargs)


def on_post_page(output, **kwargs):
    return _impl.on_post_page(output, **kwargs)
