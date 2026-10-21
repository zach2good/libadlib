# Fixtures

| path | content |
|---|---|
| `compiler/{valid,quirks,errors,fuzz-regressions}/` | the compiler compatibility corpus: `<case>.txt` + `<case>.expected.json` (see [../docs/compatibility.md](../docs/compatibility.md)) |
| `compiler/plans/` | sibling plans read by `CHANGE_OF_PLAN` cases |
| `compiler1999/{valid,quirks,errors,scene}/`, `compiler1999/plans/` | the 1999 compiler's corpus, the same way, with its own `MANIFEST.json` |
| `scene1999/` | a small stadium definition (`Test.cfg`) and its plan sources, for `buildScene` |
| `vocab1999/` | its symbol files in the 1998 layout (`reservedEnumIDs.h`, `enumIDs.h`, `AnimData/Animassm.txt`, `SndFiles.lst`) and a made-up scene, `scene.sbn` |
| `vocab/` | the corpus's symbol files in the 1996 layout: `enumIDs.h`, `AnimData/Animassm.txt`, `Sndfiles.lst`, `Infobtxt.lst` (a small made-up vocabulary) |
| `MANIFEST.json` | the executable the expectations came from (hash), the vocabulary files' hashes, every fixture's hash and outcome |

Every expectation in `compiler/` was produced by the original 1996 ADLIB compiler, run with the
files in `vocab/` as its symbol files (`"expected_from": "oracle"`), except where the original crashes or
emits uninitialised memory (`"expected_from": "adlibc (...)"`). Do not edit them by hand; see
[../CONTRIBUTING.md](../CONTRIBUTING.md). The positions of the symbols in `vocab/` are part of the
expectations: append, do not reorder.

The expectations in `compiler1999/` come the same way from the 1999 compiler inside Baseball
2000's executable, run with the files in `vocab1999/`. A case whose first line is `// object N` is
compiled for object N of `vocab1999/scene.sbn`.
