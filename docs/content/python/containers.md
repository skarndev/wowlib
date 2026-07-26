# Containers

wowlib binds the C++ `std::vector` members of its formats as **opaque container
types** — the `Vector*` classes on the top-level `wowlib` module (`VectorC3Vector`,
`VectorSMOMaterial`, …). They are handed back **by reference**, not copied, so:

- mutating one (append, assign, clear) mutates the underlying C++ object;
- numeric vectors expose a **zero-copy NumPy view** of their backing storage.

There is one `Vector*` type per element type welder found in the welded surface;
they share a uniform, list-like interface. `VectorC3Vector` below is
representative.

!!! info "Reference pages spell these as `list[...]` — they are **not** Python lists"
    Throughout the [format field references](index.md) (the WMO and M2 pages), a
    member that is really a
    `Vector*` is displayed as `list[Element]` (e.g. `list[C3Vector]`) purely for
    readability — a `Vector*` wraps `std::vector<Element>`, so the spelling reads
    truthfully at a glance. But these objects are **not** `list`: they are opaque
    handles onto live C++ storage, not owning Python sequences. They implement a
    list-*like* interface — indexing (`v[i]`, `v[i] = x`), iteration, `len(v)`,
    membership, and the usual mutators (`append`, `insert`, `pop`, `extend`,
    `clear`) — but every operation reads or writes the underlying C++ vector directly
    rather than a Python-side copy. `isinstance(v, list)` is `False`; if you need a
    detached snapshot, build one explicitly with `list(v)`.

## Constructing elements in place: `new()`

Every `Vector*` of a **struct** element also carries a `new()` method not found on
`list`: it default-constructs an element in place at the end of the vector and
returns a **live reference** to it.

```python
mat = wmo.root.materials.new()   # a fresh, default SMOMaterial, appended in place
mat.shader = 3                    # writes straight through to the C++ vector
mat.blend_mode = 1
```

The point is generic, import-free authoring: growing a container no longer forces
you to import the element type just to construct one and hand it to `append()` — the
container mints the right type for you. It exists only where it is unambiguous and
safe:

- **struct elements only.** A scalar vector (`VectorFloat`) has no `new()` — a
  scalar would come back *by value*, so writes to it would be lost; use
  `append(value)` there.
- the element must be **default-constructible** (all welded wire structs are).

!!! warning "Held element references are invalidated by resizing — this is undefined behavior, not an exception"
    A reference into one of these containers — whatever `new()`, `v[i]`, or iteration
    hands you — aliases the C++ vector's backing storage directly. Any operation that
    **resizes** the vector (`append`/`new`, `insert`, `pop`, `clear`, assigning a
    whole slice) may reallocate that storage and move every element, exactly as in
    C++. References you obtained *before* such an operation then dangle.

    Because the binding hands out a raw pointer with no liveness check, using a
    stale reference is **undefined behavior** — you may read garbage, silently
    corrupt unrelated memory on write, or crash the interpreter with a segfault.
    It is **not** reported as a catchable Python exception, and it will not reproduce
    reliably. (The container object itself is kept alive while a reference lives — it
    is only the *buffer address* that is unstable.)

    Safe pattern: use the reference immediately, before growing the container.

    ```python
    e = v.new()          # OK
    e.field = 1          # OK — no resize has happened
    v.new()              # may reallocate: `e` is now dangling
    e.field = 2          # ⚠️ undefined behavior — do not touch `e` after this
    ```

    If you must keep working with an element across growth, re-fetch it by index
    (`v[i]`) afterwards, or size the container up front so no reallocation occurs.

::: wowlib.VectorC3Vector
    options:
      show_root_heading: true
      show_root_toc_entry: true
      show_if_no_docstring: true
