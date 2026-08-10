# Validating a file before you write it

wowlib will happily write a file the game cannot load. That is deliberate:
`write()` serializes exactly the entity you hand it, so a half-finished edit
round-trips instead of being rejected, and nothing silently "repairs" your data
behind your back.

Checking is therefore a **separate step you ask for**:

```python
report = wmo.validate()
if not report.ok:
    for issue in report.issues:
        print(issue.severity, issue.path, issue.message)
```

or, when you would rather it just raise:

```python
wmo.ensure_valid()   # raises InvalidEntityState listing what is wrong
wmo.write(fs, key)
```

Every format entity has both — `WMO`, `M2`, `ADT`, `WDT`, `WDL`, `BLP` and the
client-database tables — on the concrete per-version classes and on the abstract
family bases, so `def check(w: WMO)` type-checks.

## Reading a report

`validate()` never stops at the first problem: it returns a
[`ValidationReport`][wowlib.formats.ValidationReport] holding every violated
contract, each with the **member path** it was found at — `groups[3].body.indices`,
`chunks[17].layers[2]` — so a finding points at the thing to fix rather than at
the file as a whole.

Findings come in two severities, and the difference is about the *client*, not
about how tidy the data is:

| Severity | Meaning |
| --- | --- |
| `error` | The client would misread, or crash on, a file written in this state. |
| `warning` | Suspicious, but real Blizzard files ship it — the file loads. |

`report.ok` is true when there are no **errors**; warnings never fail it. That
distinction is load-bearing, and it was drawn empirically: every contract below
was run against whole client corpora, and any check that fired on shipped,
working files was either corrected or demoted. Vanilla WMOs carry float garbage
in unused material texture slots; a Battle for Azeroth group sets the
"two colour layers" flag while shipping one; textures carry junk in the alpha
depth where nothing reads it. A freshly read, unmodified client file reports
**zero errors** — that is asserted for every file in wowlib's test corpora, so a
new error means *your edit*, not a quirk of the format.

A report is capped (1000 findings) so validating a badly corrupted file cannot
become a memory problem; `report.truncated` tells you when findings were
dropped.

## What is checked

Most contracts are declared on the entities themselves as annotations, which is
what makes the table below generated rather than written — it is scanned out of
the C++ sources, so it cannot drift from what the code actually enforces.

The remaining contracts cannot be expressed that way: record-interior ranges (a
render batch's slice of the index buffer), flag/presence coherence (a header bit
promising data that must exist), and cross-file references (a skin's vertices
into the model body, a group's references into its root). Those are hand-written
per entity, and the entities carrying them are listed under each format.

<!-- validation-contracts -->

## What is deliberately not checked

Derived fields are not validated, because they are not authored: chunk offset
tables, the MOHD counts, the MCNK sub-chunk offsets and the WDL tile offsets are
all recomputed on every write, so a stale value in memory is not a defect.
Counts that real client files disagree with their own containers on — MOHD's
texture and light counts, for instance — are stored as read and left alone,
since "fixing" them would break the byte-perfect round trip.
