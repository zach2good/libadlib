# Provenance

Where ADLIB comes from, and how each part of this package was derived.

## History

ADLIB (*Authoring and Design Language for Interactive Behavior*) was created by Stuart Rosen and
Robert Duisberg, the principals of Wizbang!, and used to build Activision's **HyperBlade** (1996).
They described it in *Game Developer*, Aug/Sep 1996 ("Bringing Life to HyperBlade") as a language
for behaviours and interactions, an authoring environment and a run-time system. HyperBlade
shipped the compiler inside the game's executable and its plans as compiled `.PBN` files; the
language's source form and the compiler's exact behaviour were never published. This package is a
reconstruction of that system from the shipped game, plus new tooling.

## Sources

| source | used for | in this package |
|---|---|---|
| HyperBlade's executable, `HYPERX.EXE` (PE timestamp 1996-10-21) | the compiler (`CPlanCompiler`, 0x487790..0x48a900) and the runtime were read from it by static analysis; its compiler routine, run under emulation, produced every compatibility expectation | no: identified by hash only |
| HyperBlade's data (symbol files, 32 compiled plans) | the vocabulary format; the shipped plans as compatibility targets | no |
| Microsoft Baseball 2000's executable, `Baseball2000.exe` (PE timestamp 1999-03-26) | the 1999 compiler (0x60EC90..0x6143E0) and runtime, read by static analysis; the compiler, run under emulation, produced every expectation in `fixtures/compiler1999/` | no: identified by hash only |
| Microsoft Baseball 3D (1998 trial and 1998 Edition), Baseball 2000 and 2001: compiled plans, scenes, symbol files; the 1998 Edition's plan sources and stadium definitions | the 1998 and 1999 dialects' compatibility targets ([dialects.md](dialects.md)) | no |
| Rosen & Duisberg's 1996 article | the system's name and structure, the vending-machine example, the networking model | short quotations in [research/article-1996.md](research/article-1996.md) |

The executable all evidence refers to:

| | |
|---|---|
| File | `HYPERX.EXE`, 1,359,872 bytes, CD file date 1996-10-21 |
| SHA-256 | `08c7b7a462847089d41a4e3ac0d61df3409ec8c9107cb97deff08364c4c0de60` |
| PE TimeDateStamp | `0x326BCBDE` = 1996-10-21 19:15:42 UTC |

and, for the 1998 and 1999 dialects:

| | |
|---|---|
| File | `Baseball2000.exe` (Microsoft Baseball 2000), 2,629,632 bytes |
| SHA-256 | `40fde7a85ce680b41d60b9250bb0fcaafa360d669d533dc4024de85f74a3a7cd` |
| PE TimeDateStamp | `0x36FAF3DD` = 1999-03-26 02:41:33 UTC |

## Methodology

1. **Static analysis** of the compiler in the executable: its keyword and special-constant
   tables, the symbol loader, the tokenizer, the prescan and the emitter. Every claim with its
   address: [research/binary-evidence.md](research/binary-evidence.md).
2. **Running the original.** The compiler routine was run under x86 emulation (unicorn) with the
   C runtime's file I/O and heap replaced by hooks, so that any source and any symbol files could
   be compiled by the 1996 code. 159 grammar probes tested each hypothesis; the working notes,
   with evidence tags and what remains inference, are in [research/grammar.md](research/grammar.md).
   This harness needs the game and is maintained outside this package.
3. **The specification** ([language.md](language.md)) keeps only rules the original compiler
   confirmed, each citing the fixtures that prove it.
4. **adlibc** was written from the specification and the analysed machine (its fixed-size
   buffers included), then held to the original by the fixture corpus and differential fuzzing
   ([compatibility.md](compatibility.md)). The fixtures' expectations were produced by the
   original compiler with this package's own symbol files (`fixtures/vocab/`), so they need no
   game data; `fixtures/MANIFEST.json` records the executable's hash and every fixture's outcome.
5. **The runtime** was reconstructed from the executable's interpreter (the comments in
   `include/adlib/runtime.h` give the original function addresses), verified by running the 32
   shipped plans in a source port of the game, then made independent of the game.
6. **The tools** (decompiler, graphs, lint, trace) are new; the decompiler was verified by
   recompiling its output of the 32 shipped plans through both compilers.
7. **The 1998 and 1999 dialects** followed the same method with Baseball 2000's compiler: static
   analysis ([research/binary-evidence-1999.md](research/binary-evidence-1999.md)), the same kind
   of emulation harness, a fixture corpus and differential fuzzing
   ([compatibility.md](compatibility.md) §4). The Baseball 3D 1998 disc's own plan sources tested
   the 1998 dialect against real source text.

## The 1996 article versus the shipped compiler

The article prints one code listing in a labelled syntax (`DECLARE_BEHAVIOR Named: ...`) that the
shipped compiler rejects; the shipped form is prefix notation with explicit child counts. Every
construct of the listing has a counterpart in the shipped language. Whether the labelled form was
presentation pseudocode or an earlier dialect is unresolved:
[research/article-1996.md](research/article-1996.md).

## What the package contains

No files, code, vocabulary or plans of the games. The research notes (`docs/research/`) discuss the
executable and cite some of the game's symbol names as evidence; the compiler emulates the 1996
machine's table layout, buffer sizes and diagnostic texts, which is what byte-exact compatibility
means.
