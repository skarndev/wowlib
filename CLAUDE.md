# wowlib

This project is a multi-purpose library for reading and writing various World of Warcraft client files to facilitate
building community software around the game to create artwork and mods, as well as exploration projects.
It is available as a C++-26 library (currently only gcc-16 toolchain is target, as the sole implementation supporting 
C++26 reflection). The library is automatically bound to Python and Lua by the welder library (we also own it, 
obtain here https://github.com/skarndev/welder).

# Python specifics
The main target version for Python so far is 3.13, matching the version used by Blender 5.1 release. welder is able to 
produce MSVC ABI-compatible builds using the STABLE ABI mode with nanobind, that's what we should use.

# World of Warcraft game client compatibility 
We aim to support all the last-release of expansion (last minor of a major) versions of the game clients released.
Testing in the initial stages of development will happen against the most popular 3.3.5a (Wrath of the Lich Kind) 
and 9.2.7 (Shadowlands) versions of the game client. The clients are avaiable locally at (/Users/Skarn/WoWModding/Clients/).

# The information sources on World of Warcraft files
[https://wowdev](https://wowdev.wiki/Main_Page) - the main source for information on file format specs and other details.
https://github.com/Deamon87/WebWowViewerCpp - the codebase featuring an online model viewer, best rendering precision in 
replicating the game's renderer.
https://github.com/wowdev/pywowlib - My earlier attempt at a fileformat library of the same theme. This library will be 
a replace (no need to keep interface compatibility or anything though).
https://github.com/skarndev/WoWLib - My earlier attempt to replicate the same in C++ with static polymorphism.
https://www.ownedcore.com - The forum where you may search for client-specific details.
https://github.com/wowdev/wow-listfile - The listfile mapping known filepaths 

There are other sources like renderers, map editors and model viewers. Ask me, if we are missing some information, I will
tell you where to look. Only consult external sources when there is the need to understand relevant specficis, wowdev.wiki 
will be the main one you consult first.

# Supported formats and features
These are presented roughly in order:
- Handling of loading of game files from the client. This involves wrapping https://github.com/ladislav-zezula/StormLib
  (pre-WoD era clients) and https://github.com/ladislav-zezula/CascLib (WoD+ clients) in one common file management gateway.
- Serialization/deserialization of world clientside data files. WMO, M2, ADT, WDT, WDL, etc.
- Serialization/deserialization of clientside database file (DBFilesClient/) which involves DBC and multiple versions of the
.db2 file formats.

# Context routing
| Topic | File |
|---|---|
| C++ conventions (no free functions for domain ops, ranges + monadic expected, no C-style casts, full Doxygen) | .claude/context/cpp-conventions.md |
| C++ naming convention (camelCase sweep 2026-08-27, exemptions, protocol names, Python-surface coupling) | .claude/context/naming-convention.md |
| Classic clients (flavor axis, build-keyed format lineage, install detection) | .claude/context/classic-versioning.md |
| Filesystem gateway architecture, thread-safety, doc/annotation policy | .claude/context/filesystem-architecture.md |
| Formats subsystem (chunk framework, versioned entities, round-trip guarantee, bindings, new-format recipe) | .claude/context/formats-architecture.md |
| M2 subsystem (offset engine, external-file baking, staged plan, user decisions) | .claude/context/m2-architecture.md |
| ADT subsystem (unified entity, split-file routing, alphamap decode, liquids, survey findings) | .claude/context/adt-architecture.md |
| ClientDB subsystem (DBC/DB2, dbdgen codegen from WoWDBDefs, round-trip policy, encryption) | .claude/context/db-architecture.md |
| BLP subsystem (unversioned entity, layout replay, codecs, stb_dxt, corpus findings) | .claude/context/blp-notes.md |
| Round-trip audit (welded Auditor + enumeration APIs + pytest driver) | .claude/context/audit-notes.md |
| MPQ load order & chain tables | .claude/context/mpq-load-order.md |
| CASC storage specifics (repack shim, error codes, test FDIDs) | .claude/context/casc-notes.md |
| Listfile & custom FileDataID policy | .claude/context/listfile-notes.md |
| Dependencies, pins & build quirks | .claude/context/deps-build-notes.md |
| CLion Nova formatter (codeStyles scheme, clang-format ban, annotation/doc-comment pitfalls, re-flow procedure) | .claude/context/clion-formatter-notes.md |
| CI server, self-hosted runner & client installs layout | .claude/context/ci-server-notes.md |
| Releasing (tag-driven PyPI wheels + multi-RID NuGet, version from git tag) | .claude/context/release-process.md |
| Python/Lua bindings (welder rods, casters, naming) | .claude/context/bindings-notes.md |
| C#/.NET bindings (welder-csharp extension, wowlib::lang::cs identity, shim sharding, build shape) | .claude/context/csharp-bindings.md |
| Documentation site (mkdocs-material guide + Python API via mkdocstrings + Doxygen C++ reference) | .claude/context/docs-site.md |

# Important notes
- We are writing this library in C++, exposing it to Python and Lua, but it also remains the C++ library itsef.
- Performance is important the library is used for realtime renderers such as map viewers, model viewers, Blender addons.
- We have C++26 reflection available, using modern features to make clear code is encouraged. We do not care about ANY
compatibility with older C++ standards.
- Save notes for context in .claude/context when we encounter new knowledge. Route yourself by adding a table to CLAUDE.md
and briefly point to these files, CLAUDE.md itself should be kept minimal.

