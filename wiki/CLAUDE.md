# Phoenix Wiki — Maintenance Schema

This directory is an **LLM-maintained knowledge wiki** for the Phoenix SDR firmware
(T41-EP transceiver). It follows the "LLM Wiki" pattern: a persistent, interlinked
set of markdown pages that the LLM builds and keeps current as sources are ingested
and questions are asked. See `llm-wiki.md` for the originating idea.

**Roles.** The human curates sources, directs analysis, and asks questions. The LLM
owns this `wiki/` directory entirely — it writes and maintains every page here, the
index, and the log. The human rarely edits wiki pages by hand.

**Scope.** Four subject areas:
1. **firmware/** — the Phoenix codebase: modules, architecture, state machines, DSP
   chains, design decisions, and the multi-author code heritage.
2. **theory/** — domain knowledge independent of any code version: DSP/SDR concepts,
   the Al Peter & Jack Purdum book, calibration theory, signal-processing "why".
3. **hardware/** — the physical T41-EP platform the firmware drives: the Teensy 4.1, the
   board stack, the audio codecs/converters, the I²C bus topology, chips, connectors, power,
   and board revisions. This is the *electronics* the code talks to — distinct from the
   firmware/ module pages, which describe the *code* that drives it.
4. **roadmap/** — plans for future development: proposals, design sketches, open
   questions, things to try.

This wiki is both a **reference** and a **planning surface**.

---

## Directory layout

```
wiki/
  CLAUDE.md      # this file — the maintenance contract
  index.md       # content catalog: every page, one-line summary, grouped by category
  log.md         # append-only chronological record of ingests/queries/lints/decisions
  raw/           # immutable source documents — read from, NEVER modify
  sources/       # one summary page per ingested source
  firmware/      # entity/concept pages about the code
  theory/        # DSP/SDR/domain concept pages
  hardware/      # physical-platform pages: boards, chips, buses, connectors, power
  roadmap/       # future-development plans and proposals
```

---

## Page conventions

**Filenames.** `kebab-case.md`, descriptive, no dates in the name (dates live in
frontmatter). One concept/entity per page.

**Frontmatter.** Every page starts with YAML:

```yaml
---
title: Human Readable Title
type: source | module | concept | hardware | decision | roadmap | overview | index
status: stub | draft | stable | superseded
created: YYYY-MM-DD
updated: YYYY-MM-DD
tags: [dsp, ssb, calibration]
source_refs: [sources/some-source]   # which ingested sources back this page
related: ["[[other-page]]"]           # cross-links (also link inline in the body)
---
```

`type` values:
- `source` — a summary of one raw source (lives in `sources/`)
- `module` — a firmware module/subsystem (lives in `firmware/`)
- `concept` — a theory/domain idea (lives in `theory/`)
- `hardware` — a physical board/chip/bus/platform page (lives in `hardware/`)
- `decision` — a recorded design decision / rationale (firmware/ or roadmap/)
- `roadmap` — a future-development plan (lives in `roadmap/`)
- `overview` / `index` — top-level navigation pages

**Cross-references.** Link liberally with Obsidian-style wikilinks: `[[page-name]]`
(basename, no extension). A link to a page that doesn't exist yet is fine — it marks
a page worth writing. When you write a new page, add inbound links from related pages
so nothing is orphaned.

**Contradictions.** When a new source contradicts an existing claim, do not silently
overwrite. Note both, cite the sources, and flag which is more recent/authoritative.
Mark superseded claims with `status: superseded` and link to what replaced them.

---

## Operations

### Ingest
When the human drops a source into `raw/` and asks to process it:
1. Read the source. Discuss key takeaways with the human before writing.
2. Write a summary page in `sources/` (`type: source`) capturing the essentials and
   citing the raw file.
3. Update or create relevant pages in `firmware/`, `theory/`, `roadmap/` — integrate
   the new knowledge, don't just append. A single source may touch 5–15 pages.
4. Add/refresh cross-references across affected pages.
5. Update `index.md`.
6. Append a `log.md` entry: `## [YYYY-MM-DD] ingest | <source title>`.

### Query
When the human asks a question:
1. Read `index.md` first to locate relevant pages, then drill in.
2. Synthesize an answer with citations to wiki pages (and underlying sources).
3. **Offer to file good answers back into the wiki** — a comparison, analysis, or
   newly-discovered connection is a candidate for a new page so explorations compound.
4. If a page was created/updated, append a `log.md` entry.

### Lint
When asked to health-check the wiki, look for:
- Contradictions between pages; stale claims newer sources superseded.
- Orphan pages (no inbound links); important concepts mentioned but lacking a page.
- Missing cross-references; data gaps fillable by a source or web search.
- Suggest new questions to investigate and new sources to find.
Append a `log.md` entry: `## [YYYY-MM-DD] lint | <scope>`.

---

## index.md and log.md

**index.md** is content-oriented — a catalog of every page with a link and one-line
summary, grouped by category (firmware / theory / roadmap / sources). Updated on
every ingest and whenever pages are created/renamed.

**log.md** is chronological and append-only. Each entry starts with a consistent
prefix so it's greppable:

```
## [YYYY-MM-DD] <op> | <subject>
```

where `<op>` ∈ `ingest | query | lint | decision`. Quick history:
`grep "^## \[" log.md | tail -5`.

---

## House rules
- Never modify anything in `raw/`. It is the source of truth.
- Prefer integrating knowledge into existing pages over creating near-duplicates.
- Keep pages focused; split when a page tries to cover two distinct things.
- Cite sources on factual claims so the human can trace provenance.
- This schema co-evolves — when a convention proves awkward, propose updating this file.
