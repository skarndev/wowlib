"""Griffe extension: surface all-`@overload` stub functions.

nanobind's stubgen emits every overloaded binding as a pure ``@overload`` group
with **no primary implementation** — the normal spelling in a ``.pyi`` — but
griffe only attaches overloads to a *following* primary ``def``: its visitor
stashes them in the parent's ``overloads`` mapping and, when no primary ever
arrives, never registers a member. Every overloaded method (``FileSystem.
read_file``/``exists``, the db tables' ``read``/``write``, ``ADT.convert``, the
``__init__`` constructors behind ``merge_init_into_class``) silently vanished
from the rendered API.

This extension runs after a class's/module's members are collected and
synthesizes the primary: the last overload is promoted to the member, wearing
the full overload list (so ``show_overloads`` renders each signature — pair
with ``overloads_only`` so the promoted copy's own signature is not repeated)
and the concatenated docstrings of every overload that has one (nanobind
documents each overload separately).

``for_version`` is deliberately left dropped: the format pages inject a
hand-written generic ``for_version`` entry (``_inject_for_version`` in
``format_reference_impl.py``) that reads far better than a dozen ``Literal``
overloads would.
"""

from __future__ import annotations

from griffe import Class, Docstring, Extension, Module

# Names whose overload groups stay dropped (documented by other means).
_SKIP = frozenset({"for_version"})


class SynthesizeOverloads(Extension):
    """Promote leftover all-overload groups to real members."""

    def _promote(self, obj: Class | Module) -> None:
        for name, overloads in list(obj.overloads.items()):
            if not overloads or name in _SKIP or name in obj.members:
                continue
            # Promote the FIRST overload: docstring Parameters tables resolve
            # their type/default cells against the primary's signature, and the
            # first overload is the canonical spelling its Args section (first
            # in the merged docstring) describes. Later overloads' tables leave
            # non-matching names untyped, which reads better than cross-typed.
            primary = overloads[0]
            primary.overloads = list(overloads)
            documented = [o.docstring for o in overloads if o.docstring]
            if documented:
                # Carry the loader's parser/options over — a bare Docstring has
                # parser=None and renders as one plain-text block (raw "Args:").
                primary.docstring = Docstring(
                    "\n\n".join(d.value for d in documented),
                    parent=primary,
                    parser=documented[0].parser,
                    parser_options=documented[0].parser_options,
                )
            obj.set_member(name, primary)
            del obj.overloads[name]

    def on_class_members(self, *, cls: Class, **kwargs) -> None:
        self._promote(cls)

    def on_module_members(self, *, mod: Module, **kwargs) -> None:
        self._promote(mod)
