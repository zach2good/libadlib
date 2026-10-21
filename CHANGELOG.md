# Changelog

All notable changes to this project are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and the project uses
[semantic versioning](https://semver.org/).

## [Unreleased]

The Baseball games: every ADLIB plan that shipped, in the language of its year.

### Added
- **Dialects** ([docs/dialects.md](docs/dialects.md)): `--compat=adlib1999` is the 1999 compiler
  inside Baseball 2000's executable, reproduced byte for byte. It covers braced blocks with
  computed counts, the flat fallback, case-insensitive keywords and names, `CHANGE_TO_NEW_BEHAVIOR`,
  `SET_AGENDA_ITEM_PARAM`, `id_PRN_` 0xFEDD and `id_PRT_` 0xFEDC, `BC_` identifiers and its
  diagnostics. `--compat=adlib1998` is the language of Baseball 3D: the same compiler without
  `SET_AGENDA_ITEM_PARAM`. Specification: [docs/language-1999.md](docs/language-1999.md).
- **Scenes**: `CompilerScene` and `parseScene` read a game's compiled scene (`.sbn`): object
  types with their plans and messages, objects with their ids and collision objects. They supply
  per-object message ids, SendMessage message tables, broadcast DataBlocks and scene symbols.
  `--scene` and `--object` on adlibc, `--scene` on adlib.
- **Scenes from their sources**: `buildScene` builds a scene from a stadium's `.cfg` and the plan
  sources as the game's scene compiler does; `adlibc --scene` takes a `.cfg`. Checked against all
  30 scenes of Baseball 3D 1998.
- **Checked decompiler output**: with the compiler's symbol files loaded, `decompile` compiles
  its output again and fails ("not expressible") rather than return source that would not
  rebuild the plan (`DecompileOptions::verify`, `--no-verify`).
- **Braced output**: `adlib decompile --braced` (`DecompileOptions::braced`) writes the 1999
  braced form wherever the compiler rebuilds a statement from it, then the rest flat.
- **Symbol files in the 1998 layout**: `reservedEnumIDs.h` (or `RENUMIDS.H`), `enumIDs.h`,
  `AnimData/Animassm.txt`, `SndFiles.lst`, read the 1999 way (`CompilerSymbols::fromFiles` and
  `fromDataDir` with a `Compat`); `--reserved` on both CLIs; `parseCompat`, `compatName`,
  `compilerKeywords(Compat)`.
- **Decompiler** for the later dialects: case-insensitive behaviour names, both pronoun tags,
  dialect keywords, scene message names, SendMessage targets written so the 1999 compiler
  reproduces them. `--compat` on the `adlib` CLI.
- **Fixtures**: `fixtures/compiler1999/`, 87 cases whose expectations are the 1999 compiler's
  results with `fixtures/vocab1999/`; `adlibc_test` replays them, `adlib_tools_test` round-trips
  every accepted one that builds a plan tree through the decompiler, under both later dialects, flat
  and braced.
- **Runtime**: `CHANGE_TO_NEW_BEHAVIOR` (no change when the plan is already in that behaviour) and
  `SET_AGENDA_ITEM_PARAM` (the first agenda item with that id gets `AgendaItem::setParam`), as
  Baseball 2000's executor runs them.
- Research: [docs/research/binary-evidence-1999.md](docs/research/binary-evidence-1999.md).

### Changed
- `adlib decompile` now checks its output whenever the compiler's symbol files are loaded, and
  reports a plan as not expressible instead of writing source that would not rebuild it
  (`--no-verify` turns the check off). Names from `--enum` or `--list` turn it off, with a note.
- `isPronounTag` treats 0xFEDC and 0xFEDD alike in the loader hook, the runtime and the
  display tools. This reaches 1996 plans too: a leaf written as `0xFEDDxxxx` is now resolved as a
  pronoun. HyperBlade's plans hold none; a 1996 plan gets one only from a hex literal in its source.
- `adlibc` finds the default output and plans directories after a `\` as well as a `/`, so a
  Windows path such as `C:\x\p.txt` writes beside its input.

### Fixed
- The decompiler wrote `DECLARE_LOCAL_REALS` and interactor-name declarations as hex slot
  numbers, which the prescan of either compiler rejects. It now invents names for them.

## [0.1.0] - 2026-10-03

First standalone release.

### Added
- **libadlib**: the ADLIB runtime (behaviours, interactors, agenda, message queue, timers,
  decision-path record/replay for lockstep networking), PBN reader/writer and `adlib::build`
  plan builder, `Vocabulary` (enum headers and list files, roles bound by enum name), primitives
  registered by name, `PlanHost` host interface, structured trace events. Builds natively and
  with Emscripten. Interpreter ids come only from the vocabulary (unbound features are disabled).
- **adlibc**: the ADLIB compiler, byte-identical to the 1996 ADLIB compiler; modes
  `--compat=adlib` (default, 1996 limits lifted) and `--compat=adlib1996` (the 1996 compiler
  exactly, fixed limits included); `--strict` warnings and `--Werror`; symbols from the four
  1996 symbol files (`--symbols DIR`, `--enumids ...`) or from a `Vocabulary`; library API
  `adlib/compiler.h`.
- **adlib** CLI and `adlib_tools` library: `dump`, `decompile` (byte-identical recompile),
  `graph` (DOT, Mermaid; single plan or all plans), `inspect`/`lint`, `compare`, `trace`
  (with `adlib::tools::Tracer` as the run-time sink); action-argument hints (`--hint`).
- **Example**: the vending machine host, its plans compiled from source by adlibc at build time.
- **Fixtures**: 185 compiler fixtures whose expectations were produced by the original 1996
  compiler with the package's own symbol files (`fixtures/vocab/`), so the whole test suite runs
  with no external data; `MANIFEST.json` with the executable's hash and every outcome.
- CMake package (`find_package(adlib 0.1 CONFIG)`, targets `adlib::adlib`, `adlib::tools`),
  version header, `--version` on both CLIs, CI workflow.
- Documentation: README, getting started, plan-writing tutorial, host API, language
  specification (confirmed rules only), adlibc, tools, compatibility, provenance, research notes.

### Verified (against the original executable, outside this package)
- Every fixture expectation re-derived from the original compiler: unchanged.
- Differential fuzzing, final campaign: 17,547/17,547 compared cases identical, 0 mismatches.
- The 32 plans shipped with HyperBlade: rebuilt byte-identically from source; decompile round
  trip 32/32 through adlibc and the original compiler.
- A source port of HyperBlade runs on this package unchanged (plan traces, headless matches,
  lockstep determinism hash and network tests identical).
