# Compatibility: how adlibc is held to the original compilers

adlibc's claim ([adlibc.md](adlibc.md)) is that, in `--compat=adlib1996` mode, it produces the
same bytes as the 1996 ADLIB compiler for every input that compiler accepts, and the same
diagnostic (message, line, byte offset) for every input it rejects; inputs the original crashes or
hangs on, or answers with uninitialised memory, are refused with a clear error. `--compat=adlib`
is the same language with the 1996 buffer limits lifted: identical output wherever those limits
are not hit.

The evidence was produced against the **original executable**: the compiler routine inside
HyperBlade's `HYPERX.EXE` (1996; sha256 `08c7b7a4…c0de60`, see [provenance.md](provenance.md)),
run under x86 emulation with its file I/O redirected, so that any source text and any symbol
files could be fed to it. That harness needs a copy of the game and is not part of this package;
what the package contains is its results, frozen as fixtures that run anywhere.

## 1. The fixture corpus (in `ctest`, no external data)

`fixtures/compiler/` holds 185 cases, each a source file and `<case>.expected.json`:

| directory | cases | content |
|---|---|---|
| `valid/` | 28 | accepted, and clean under `--strict` |
| `quirks/` | 105 | accepted although malformed; each records the mechanism (`"mechanism"`) |
| `errors/` | 45 | rejected by the original, or crashing it |
| `fuzz-regressions/` | 7 | every disagreement the fuzzer ever found, minimised |
| `plans/` | 4 | sibling plans that `CHANGE_OF_PLAN` cases read |

An expectation records the outcome, the exact diagnostic or the PBN's sha256 and every dword, the
decoded tree, and where it came from: `"expected_from": "oracle"` (the original compiler's
result) or `"adlibc (the original compiler crashes on this input)"`. The `"oracle"` and
`"adlibc"` blocks keep both results as they were when the case was frozen; `"adlibc_agrees"`
records that they matched.

The original compiler was run with the package's own symbol files, `fixtures/vocab/`
(`enumIDs.h`, `AnimData/Animassm.txt`, `Sndfiles.lst`, `Infobtxt.lst`: a small made-up
vocabulary in the 1996 layout), so the expectations depend on nothing outside the package.
`adlibc_test` compiles every case with `--compat=adlib1996` against the same files and compares
outcome, diagnostic, sha256 and every dword.

The cases cover one rule each: the lexical rules, every keyword, every token class, counts,
positional resolution, declarations, table overflows, token-length limits, every diagnostic of
the original, and the cases found while writing adlibc. [language.md](language.md) cites them
rule by rule.

## 2. Differential fuzzing

A fuzzer generated sources and compiled each with the original compiler and with
`adlibc --compat=adlib1996`:

- grammar-built plans from the keyword table, the vocabulary's symbols and the behaviour names of
  real plans;
- token mutations (counts, numbers in every odd form, strings near the 40-byte limit, special
  constants, case changes, dropped words, separators) and byte mutations;
- generators aimed at the 1996 machine's limits: 61 behaviours or local reals, long names, stale
  string bytes, positional counters, `CHANGE_OF_PLAN` siblings.

The outcomes had to be identical: the same PBN, or the same diagnostic line. Cases on which the
original crashes or hangs were not compared, but adlibc had to reject them. Lone `0x` tokens (the
original emits uninitialised stack bytes) were counted separately. The emulator started every
compile from a fresh stack, so that results did not depend on case order.

| campaign | cases | compared | agree | original crashed (adlibc rejects) | uninitialised | mismatches |
|---|---|---|---|---|---|---|
| development (seeds 1-5) | 20,300 | 19,888 | 19,881 | 337 (337) | 145 | 7, all now fixtures, fixed |
| **v0.1 final** (seeds 21-23, release binary) | **18,000** | **17,547** | **17,547** | 288 (288) | 165 | **0** |

The fuzzer also compared `--compat=adlib` with `--compat=adlib1996` wherever the latter neither
refuses nor warns: 13,133 cases, identical.

Every mismatch became a fixture in `fuzz-regressions/` **before** adlibc was changed
([../CONTRIBUTING.md](../CONTRIBUTING.md)).

## 3. Real plans

The 32 plans that shipped with HyperBlade were reconstructed as source and each rebuilds its
shipped `.PBN` byte for byte through adlibc and through the original compiler; `adlib decompile`
output for all 32 recompiles byte-identically through both. Those sources and the game's files are
not part of this package.

## 4. The 1998 and 1999 modes

`--compat=adlib1999` is held to the 1999 compiler inside Baseball 2000's executable
(`Baseball2000.exe`, sha256 in `fixtures/compiler1999/MANIFEST.json`) in the same three ways.
`--compat=adlib1998` differs from it by one keyword ([dialects.md](dialects.md)).

**Fixtures.** `fixtures/compiler1999/` holds 87 cases, each frozen from the 1999 compiler with
`fixtures/vocab1999/` (a made-up vocabulary in the 1998 layout and a made-up scene):

| directory | cases | content |
|---|---|---|
| `valid/` | 29 | accepted, one rule each |
| `quirks/` | 18 | accepted although the text suggests otherwise |
| `errors/` | 32 | rejected, or refused by adlibc because the original crashes, hangs or reads unwritten memory |
| `scene/` | 8 | compiled for an object of `scene.sbn`: messages, send targets, broadcasts, object ids |
| `plans/` | 1 | the sibling plan `CHANGE_OF_PLAN` cases read |

`adlibc_test` compiles every case with `--compat=adlib1999`; `adlib_tools_test` decompiles every
accepted case that builds a plan tree under both later dialects, flat and braced, and recompiles
it. `fixtures/scene1999/` checks `buildScene` against a small `.cfg` and its plan sources.

**Review.** A five-part review compared the compiler with the binary function by function and
attacked the decompiler and the scene readers. Each discrepancy it confirmed against the original
is now a fixture above (the `errors/plan_name_*`, `count_past_32767`, `many_comments_*`,
`comment_without_line_end_braced`, `nesting_*` cases among them).

**Differential fuzzing.** A fuzzer built flat, braced and mixed plans from the fixture vocabulary
and scene (sends, broadcasts, declarations, every keyword in any case, comments, odd counts) and
mutated a third of them byte by byte. Each was compiled by the 1999 compiler and by
`adlibc --compat=adlib1999`:

| campaign | cases | compared | agree | original crashed or hung (adlibc refuses) | uninitialised (adlibc refuses) | mismatches |
|---|---|---|---|---|---|---|
| final (seeds 41-46, final build) | **15,000** | **13,473** | **13,473** | 1,035 (1,035) | 492 | **0** |
| after the review fixes (seeds 47-48) | 5,000 | 4,500 | 4,500 | 359 (359) | 141 | **0** |

The earlier campaigns found one disagreement, an error position after the cursor had run past
the end of the source; that is now refused as `undefined` ([language-1999.md](language-1999.md) §3.2, item 7).

**Real plans.**

| corpus | adlibc round trip | the 1999 compiler |
|---|---|---|
| Baseball 3D trial (32 PBNs) | 32/32 (`adlib1998`) | 32/32 from the decompiled source |
| Baseball 3D 1998, 30 stadiums (960 PBNs) | 960/960 (`adlib1998`) | 960/960 from the decompiled source |
| the 1998 disc's 34 plan sources, compiled for each stadium | 960/960 shipped PBNs rebuilt (`adlib1998`), with the scene read from the `.SBN` or built from the `.CFG` | 960/960 (the two sources no stadium uses, `BALLINIT` and `MINALT`, have no PBN to compare) |
| Baseball 2000 (30 PBNs) | 30/30 (`adlib1999`) | 30/30 from the decompiled source |
| Baseball 2001 (30 PBNs) | 30/30 (`adlib1999`) | 30/30 from the decompiled source |

The 1998 symbol lists were rebuilt from the games' binary `SNDFILES.BLS` and `ANIMASSM.ABN`
([dialects.md](dialects.md) §3). Baseball 2000 and 2001 ship no headers, so their plans were
decompiled with the reserved ids, sounds, animations and scene names only. None of these files
is part of this package.

## Release criterion

A release requires:

1. the test suite passes on Linux with GCC and Clang, and on Windows with MSVC (CI);
2. every fixture expectation still matches the original compiler when re-derived from it, and
   the real-plan checks above pass;
3. a fresh fuzz campaign of at least 15,000 compared cases on the release binary with **zero**
   mismatches.

v0.1.0 met all three (see the table above and [../CHANGELOG.md](../CHANGELOG.md)).
