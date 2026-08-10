"""mkdocs hook shim — reloads the validation reference so `serve` sees edits.

Same reasoning as ``format_reference.py``: mkdocs caches a hook module for the
process's lifetime, so the real logic lives in ``validation_reference_impl`` and
is reloaded on every build. Keep this file trivial.
"""

import importlib
import logging
import os
import sys

_HERE = os.path.dirname(os.path.abspath(__file__))
if _HERE not in sys.path:
    sys.path.insert(0, _HERE)

import validation_reference_impl as _impl

_log = logging.getLogger("mkdocs.hooks.validation_reference")


def on_pre_build(config, **kwargs):
    """Reload the engine so `mkdocs serve` picks up edits without a restart.

    A syntax error keeps the previous good version, so serve stays up.
    """
    global _impl
    try:
        _impl = importlib.reload(_impl)
    except Exception:                                   # noqa: BLE001 - serve must survive
        _log.exception("validation_reference_impl reload failed; keeping the last good copy")


def on_page_markdown(markdown, page, config, files, **kwargs):
    """Delegate to the freshly reloaded engine."""
    return _impl.on_page_markdown(markdown, page, config, files, **kwargs)
