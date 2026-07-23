# C++ API Reference

The full **C++ reference** is a standalone Doxygen site — generated from the real
headers under `src/wowlib/` through welder's annotation filter, with a source
browser and class graphs throughout.

<a class="md-button md-button--primary" href="../api/index.html">Open the C++ reference →</a>

It opens in this same site under `/api`. If the link 404s, the reference has not
been generated yet — run:

```bash
.venv/bin/python docs/build.py build
```

which builds the Doxygen reference and grafts it into `<site>/api` before the
mkdocs site is assembled. For the *why* behind the filter (how `[[=welder::doc]]`
annotations become Doxygen comments, and its fail-safety contract), see welder's
own "Generating C++ docs" guide.
