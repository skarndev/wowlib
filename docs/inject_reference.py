"""mkdocs hook: graft the standalone API references into the built site.

Keeps the references out of the worktree *and* out of mkdocs' ``docs_dir``:
Doxygen emits the C++ reference under ``build/docs/reference/api`` and DocFX
the C# one under ``build/docs/reference/api-cs`` (both out of source), and this
hook copies them into ``<site>/api`` / ``<site>/api-cs`` after every build —
including each ``mkdocs serve`` rebuild — so those links resolve in both
``build`` and ``serve`` while ``docs/content`` stays untouched.

The locations are passed via the ``WOWLIB_DOXYGEN_API`` / ``WOWLIB_DOCFX_API``
environment variables (set by ``docs/build.py``). When one is unset or missing
— e.g. a bare ``mkdocs build`` run by hand, or a tree without the C# surface —
that graft is a silent no-op, so the rest of the site still builds.
"""

import os
import shutil

_GRAFTS = (
    ("WOWLIB_DOXYGEN_API", "api"),
    ("WOWLIB_DOCFX_API", "api-cs"),
)


def on_post_build(config, **kwargs):
    for var, leaf in _GRAFTS:
        src = os.environ.get(var)
        if not src or not os.path.isdir(src):
            continue
        dst = os.path.join(config["site_dir"], leaf)
        shutil.rmtree(dst, ignore_errors=True)
        shutil.copytree(src, dst)
