# ADLIB dialects: which game speaks which

Five games compiled their plans with ADLIB: HyperBlade (1996) and four Baseball titles Wizbang!
made for Microsoft. The language changed twice. libadlib names each version after the year of
its compiler and supports all three: `--compat=adlib1996`, `--compat=adlib1998` and
`--compat=adlib1999`. `--compat=adlib`, the default, is the 1996 language with the 1996
compiler's buffer limits lifted ([adlibc.md](adlibc.md)).

| game | plans | compiler shipped | dialect |
|---|---|---|---|
| HyperBlade (1996) | 32 | inside `HYPERX.EXE` | `adlib1996` |
| Microsoft Baseball 3D trial (1998) | 32 | no | `adlib1998` |
| Microsoft Baseball 3D 1998 Edition | 32 per stadium, 30 stadiums (960), and their 34 sources | no | `adlib1998` |
| Microsoft Baseball 2000 | 30 | inside `Baseball2000.exe` | `adlib1999` |
| Microsoft Baseball 2001 | 30 | the executable is encrypted | `adlib1999` |

**Every one of these 1084 PBNs decompiles and recompiles byte for byte in its dialect.** The 960
plans of the 1998 Edition also rebuild byte for byte from the plan sources on its disc.

## 1. Why three

**1996.** HyperBlade's executable contains its compiler. That compiler is the oracle for
`adlib1996` ([compatibility.md](compatibility.md)).

**1999.** Baseball 2000's executable also contains its compiler. The game recompiles a scene
whenever a plan source is newer than the compiled scene, so the compiler is reachable code
([research/binary-evidence-1999.md](research/binary-evidence-1999.md) §10.1). It is the
oracle for `adlib1999`: [language-1999.md](language-1999.md) cites a fixture for each rule, and
the fixtures record what this compiler did.

Baseball 2001's executable is encrypted, so its compiler cannot be read. Its plans use exactly the
opcodes Baseball 2000's do. The 1999 compiler rebuilds all 30 of them from decompiled source.
Nothing separates 2001 from 1999.

**1998.** No 1998 compiler survives. The two Baseball 3D releases contain no compiler, only plans,
and the 1998 Edition also ships the plan sources (`DATA\PLANS\*.TXT`, April 1998), the headers
and the stadium definitions they compile against. Its sound and animation lists are binary; the
text the compiler read can be rebuilt from them (§3). libadlib's `adlib1998` mode rebuilds every shipped 1998 PBN from those
sources, for all 30 stadium scenes, and so does the 1999 compiler for the stadiums run through it
([compatibility.md](compatibility.md) §4). One thing the 1998 language
lacks: Baseball 3D's executable ignores node 0x001A0004 (`SET_AGENDA_ITEM_PARAM`), and no 1998
plan uses it. So `adlib1998` is the 1999 compiler without that keyword: in the flat form the word
is dropped, as any unknown word is, and a braced `{SET_AGENDA_ITEM_PARAM ...}` aborts the compile,
as any unknown keyword does.

The 1998 sources settle more than the keyword. They are written in the flat 1996 form, with the
counts written as argument enumerators (`DO_ACTION _3_ ...`), and they need everything that is new
in 1999 except `SET_AGENDA_ITEM_PARAM`: `CHANGE_TO_NEW_BEHAVIOR`, case-insensitive names, the two
pronoun tags, `reservedEnumIDs.h`, per-object messages and the scene's object ids.

## 2. What changed

| | 1996 | 1998 | 1999 |
|---|---|---|---|
| keywords | 27 | 21: `CHANGE_TO_NEW_BEHAVIOR` added; the `LOC`, `NET` and `WITH` variants (`SET_LOC_MESSAGE_INTERACTOR`, `DO_LOCAL_ACTION`, `DECIDE_BY_WITH_AMONG` ...) gone | 22: `SET_AGENDA_ITEM_PARAM` added |
| keyword and name case | exact (keywords, behaviour names) | any | any |
| syntax | flat: author writes every count | braced `{K ...}` with computed counts, then an optional flat tail | same |
| unknown identifier | error `Bad plan id value` | dropped (an unknown `id_MSG_` is -1) | same |
| `id_PRN_` | 0xFEDC tag | 0xFEDD | 0xFEDD |
| `id_PRT_` | (not an identifier class) | 0xFEDC | 0xFEDC |
| identifier prefixes | `id`, `_`, `A_`, `AA_` | also `BC_` | same |
| symbol files | `enumIDs.h`, `Animassm.txt`, `Sndfiles.lst`, `Infobtxt.lst` | `reservedEnumIDs.h` first; no `Infobtxt.lst` | same |
| `name = value` in a header | the value is a symbol | ignored; MSG/AGD/ACF enums start at 4/1/2 | same |
| messages | one `ID_MSG` enum | four reserved, then per object type, from the scene | same |
| SendMessage to `_BROADCAST_`, `_MESSAGE_DATA_`, a pronoun | the message's id | a DataBlock: the message's id in every object type | same |
| source limit | 79 998 bytes | 399 998 | 399 998 |
| string limit | 37 characters | 253 | 253 |
| name tables | 60 / 60 / 150 | 500 / 500 / 1200 | same |

The 1998 column is the 1999 compiler's behaviour, which the 1998 data and sources match. Where
the data cannot tell (for example the size of the 1998 symbol table), libadlib applies the 1999
rule.

## 3. Compiling and decompiling each game

A 1998 or 1999 plan compiles against the symbol files and the **scene**, which lists the object
types, their plans and their messages, and every object with its id and collision objects.
Message ids and object ids come from it. The scene is either the `.sbn` the game compiled for the
stadium, or the stadium's `.cfg` with the plan sources, from which libadlib builds it the way the
game's scene compiler does (`buildScene`): objects in definition order, types in order of first
use, each type's messages the `id_MSG_` names its plans use that the symbol files lack, in order
of first use, and collision objects numbered across the scene. Built that way, all 30 scenes of
the 1998 Edition equal the shipped `.SBN` files, and the disc's sources rebuild all 960 PBNs
against them.

```
adlibc --compat=adlib1998 --symbols SYMBOLS --scene PLANS/COO/COO.SBN -o BALL.PBN PLANS/BALL.TXT
adlibc --compat=adlib1998 --symbols SYMBOLS --scene STADIUMS/COO/COO.CFG --plans-dir PLANS -o BALL.PBN PLANS/BALL.TXT
adlib --compat=adlib1999 --scene PLANS/SBN/COO.SBN --symbols SYMBOLS decompile --braced -o src PLANS
```

`decompile --braced` writes each top-level statement as a braced block when the 1999 compiler
rebuilds it from one, and the rest of the plan in the flat form. A braced head (the action,
decision, agenda item, behaviour type or message after the keyword) has to be a symbol, so a plan
comes out braced only where the vocabulary names every head. With the 1998 headers every shipped
plan does; Baseball 2000 and 2001 ship no headers, so theirs come out flat.

`SYMBOLS` is a directory in the 1998 layout. It holds `reservedEnumIDs.h` (the 1998 disc's `RENUMIDS.H`),
`enumIDs.h`, `AnimData/Animassm.txt` and `SndFiles.lst`. The games ship the last two only in
binary form; the text the compiler read can be rebuilt from them:

- `SNDFILES.BLS`: u32 count, u32 0, then 92-byte records: u32 index, u32 directory, 12 bytes, u32
  type, the name (32 bytes), the callback message (36 bytes). `id_SND_<name>` is the index.
- `ANIMASSM.ABN`: each assembly's name follows a u16 index and a u16. `AA_<name>` is the index.

Baseball 2000 and 2001 ship no headers at all, so their decompiled plans name only the reserved
ids, sounds, animations, scene objects and messages, and give numbers for the rest.

## 4. Not done

- `--strict` adds no 1998/1999 warnings beyond the tree lint.
- Each compile starts with the flat form's counters and the broadcast flag fresh. In a scene
  build the original carries them over from the previous plan ([language-1999.md](language-1999.md) §10).
- Rules the shipped data never exercises when a scene is built from sources: an `id_MSG_` name
  inside a quoted string, a plan listed by two object types, an object without `id =`.
  libadlib follows the compiler's tokenizer there, unverified against the game.
- The runtime runs every node of every dialect. It does not ignore `SET_AGENDA_ITEM_PARAM` for
  1998 plans as Baseball 3D's executable does; no 1998 plan contains one.
- A scene built from a `.cfg` reads only what the plan compiler needs (object ids and types,
  plans, collision-object ids). The rest of the object definitions (models, motion, collision
  geometry) and the `.sbn` writer are not implemented.
- Nobody knows what the 1999 sources looked like. The 1998 sources are flat; `--braced` shows
  the form the 1999 compiler introduced.
