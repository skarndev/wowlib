"""mkdocs hook shim — reloads the real logic so `mkdocs serve` picks up edits.

mkdocs caches an imported hook module for the lifetime of the process (its
`_load_hook` is `@lru_cache`d and the option instance is created once at import),
so editing a hook's code never takes effect under `mkdocs serve` — you'd have to
restart. This shim sidesteps that: it stays trivial (so its own cached copy is
fine) and **reloads** `wmo_reference_impl` on every build (`on_pre_build`),
delegating the page events to the fresh module. Edit the logic in
`wmo_reference_impl.py`; serve will reflect it live (that file is added to the
watch list by docs/build.py). A syntax error there is logged and the previous
good version is kept, so serve stays up.
"""

import importlib
import logging
import os
import sys

_HERE = os.path.dirname(os.path.abspath(__file__))

import wmo_reference_impl as _impl

log = logging.getLogger("mkdocs.hooks.wmo_reference")


def on_pre_build(config, **kwargs):
    global _impl
    # mkdocs saves/restores sys.path around hook import, so this dir isn't on the
    # path by the time we reload — re-add it here (this runs after that restore).
    if _HERE not in sys.path:
        sys.path.insert(0, _HERE)
    try:
        _impl = importlib.reload(_impl)
    except Exception as e:  # keep serving the last good version on a bad edit
        log.warning("wmo_reference_impl reload failed (%s); keeping previous version", e)


def on_page_markdown(markdown, **kwargs):
    return _impl.on_page_markdown(markdown, **kwargs)


def on_page_content(html, **kwargs):
    return _impl.on_page_content(html, **kwargs)
