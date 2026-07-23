# Containers

wowlib binds the C++ `std::vector` members of its formats as **opaque container
types** — the `Vector*` classes on the top-level `wowlib` module (`VectorC3Vector`,
`VectorSMOMaterial`, …). They are handed back **by reference**, not copied, so:

- mutating one (append, assign, clear) mutates the underlying C++ object;
- numeric vectors expose a **zero-copy NumPy view** of their backing storage.

There is one `Vector*` type per element type welder found in the welded surface;
they share a uniform, list-like interface. `VectorC3Vector` below is
representative.

::: wowlib.VectorC3Vector
    options:
      show_root_heading: true
      show_root_toc_entry: true
      show_if_no_docstring: true
