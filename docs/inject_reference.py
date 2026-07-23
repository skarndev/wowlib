"""mkdocs hook: graft the Doxygen C++ reference into the built site.

Keeps the reference out of the worktree *and* out of mkdocs' ``docs_dir``: Doxygen
emits it under ``build/docs/reference/api`` (out of source), and this hook copies
that into ``<site>/api`` after every build — including each ``mkdocs serve``
rebuild — so the ``/api`` link resolves in both ``build`` and ``serve`` while
``docs/content`` stays untouched.

The reference location is passed via the ``WOWLIB_DOXYGEN_API`` environment
variable (set by ``docs/build.py``). When it is unset or missing — e.g. a bare
``mkdocs build`` run by hand — the hook is a silent no-op, so the guide + Python
API still build (just without the C++ reference).
"""

import os
import shutil


def on_post_build(config, **kwargs):
    src = os.environ.get("WOWLIB_DOXYGEN_API")
    if not src or not os.path.isdir(src):
        return
    dst = os.path.join(config["site_dir"], "api")
    shutil.rmtree(dst, ignore_errors=True)
    shutil.copytree(src, dst)
