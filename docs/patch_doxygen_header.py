#!/usr/bin/env python3
"""Inject wowlib's header customizations into a generated Doxygen header.

Doxygen 1.17 ships **no jQuery**, so doxygen-awesome's ``init()``-based extensions
(dark-mode toggle, copy buttons, …) silently fail. And its dark-mode *class* is a
top-level ``class`` in a classic script — a global lexical binding, not a ``window``
property — which makes it awkward to drive from another script.

So we do it ourselves, fully self-contained: doxygen-awesome's CSS keys dark mode
purely on ``html.dark-mode`` (+ ``prefers-color-scheme``), so we just manage that
class directly and persist the choice in localStorage — no doxygen-awesome JS at
all. We also place a light/dark toggle next to the search box and link the project
title back to the mkdocs guide.

Usage: patch_doxygen_header.py <generated-header.html> <patched-header.html>

Fail-safe: on any error the header is copied through unchanged and we exit 0 — a
docs build must never break here.
"""
import sys

# `$relpath^../index.html` resolves from any api/ page to the guide home. The saved
# preference is applied immediately (this runs in <head>, before first paint); the
# button + title link are built on DOMContentLoaded.
CONTROLS = """\
<!-- wowlib: self-contained dark-mode toggle + guide backlink (no jQuery / no awesome JS) -->
<script type="text/javascript">
(function () {
  var GUIDE_URL = "$relpath^../index.html";
  var KEY = "wowlib-color-scheme";
  var SUN = '<svg viewBox="0 0 24 24" width="20" height="20" fill="currentColor" aria-hidden="true"><path d="M12 7a5 5 0 100 10 5 5 0 000-10zm0-5a1 1 0 011 1v1a1 1 0 11-2 0V3a1 1 0 011-1zm0 17a1 1 0 011 1v1a1 1 0 11-2 0v-1a1 1 0 011-1zM4 12a1 1 0 01-1 1H2a1 1 0 110-2h1a1 1 0 011 1zm18 0a1 1 0 01-1 1h-1a1 1 0 110-2h1a1 1 0 011 1zM5.99 5.99a1 1 0 01-1.41 0l-.71-.71a1 1 0 011.41-1.41l.71.71a1 1 0 010 1.41zm14.02 14.02a1 1 0 01-1.41 0l-.71-.71a1 1 0 011.41-1.41l.71.71a1 1 0 010 1.41zM18.01 5.99a1 1 0 010-1.41l.71-.71a1 1 0 011.41 1.41l-.71.71a1 1 0 01-1.41 0zM3.99 20.01a1 1 0 010-1.41l.71-.71a1 1 0 011.41 1.41l-.71.71a1 1 0 01-1.41 0z"/></svg>';
  var MOON = '<svg viewBox="0 0 24 24" width="20" height="20" fill="currentColor" aria-hidden="true"><path d="M12 3a9 9 0 108.99 9.36A7.002 7.002 0 0112 3z"/></svg>';
  function prefersDark() {
    return !!(window.matchMedia && window.matchMedia("(prefers-color-scheme: dark)").matches);
  }
  function saved() {
    try { return localStorage.getItem(KEY); } catch (e) { return null; }
  }
  function isDark() {
    var c = document.documentElement.classList;
    if (c.contains("dark-mode")) { return true; }
    if (c.contains("light-mode")) { return false; }
    return prefersDark();
  }
  function apply(dark) {
    var c = document.documentElement.classList;
    c.toggle("dark-mode", dark);
    c.toggle("light-mode", !dark);
  }
  // Apply an explicit class as early as possible (this runs in <head>): the saved
  // choice, else the OS preference. Always setting a class keeps our accent
  // overrides winning (awesome's dark accent lives under a higher-specificity
  // `html:not(.light-mode)` media rule).
  var s = saved();
  apply(s === "dark" ? true : (s === "light" ? false : prefersDark()));
  function setIcon() {
    var b = document.getElementById("wowlib-dark-toggle");
    if (b) { b.innerHTML = isDark() ? MOON : SUN; }
  }
  function toggle() {
    var dark = !isDark();
    apply(dark);
    try { localStorage.setItem(KEY, dark ? "dark" : "light"); } catch (e) {}
    setIcon();
  }
  function build() {
    // Place the toggle inline next to the search box; clicks are handled by the
    // delegated listener below, so it works even inside the search container.
    if (!document.getElementById("wowlib-dark-toggle")) {
      var btn = document.createElement("button");
      btn.id = "wowlib-dark-toggle";
      btn.type = "button";
      btn.title = "Toggle light / dark mode";
      btn.setAttribute("aria-label", "Toggle light / dark mode");
      var box = document.getElementById("MSearchBox");
      var host = (box && box.parentNode) ? box.parentNode : (document.body || document.documentElement);
      host.appendChild(btn);
    }
    setIcon();
    var pn = document.getElementById("projectname");
    if (pn && !pn.querySelector("a.wowlib-guide-link")) {
      var a = document.createElement("a");
      a.href = GUIDE_URL;
      a.className = "wowlib-guide-link";
      a.title = "Back to the wowlib documentation";
      while (pn.firstChild) { a.appendChild(pn.firstChild); }
      pn.appendChild(a);
    }
  }
  // Delegated click, so it fires no matter how/when the button was added.
  document.addEventListener("click", function (e) {
    var t = e.target;
    if (t && t.closest && t.closest("#wowlib-dark-toggle")) { toggle(); }
  });
  if (document.readyState !== "loading") { build(); }
  else { document.addEventListener("DOMContentLoaded", build); }
  try { window.matchMedia("(prefers-color-scheme: dark)").addEventListener("change", setIcon); } catch (e) {}
})();
</script>
"""


def main() -> int:
    src, dst = sys.argv[1], sys.argv[2]
    try:
        with open(src, "r", encoding="utf-8") as f:
            html = f.read()
        if "wowlib-dark-toggle" not in html:
            marker = "</head>"
            if marker in html:
                html = html.replace(marker, CONTROLS + marker, 1)
        with open(dst, "w", encoding="utf-8") as f:
            f.write(html)
    except OSError as e:
        sys.stderr.write(f"patch_doxygen_header: {e}\n")
        return 0
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
