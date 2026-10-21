# ADLIB compiler: binary evidence from Baseball2000.exe

This file records what Baseball 2000's executable proves about its ADLIB plan compiler, measured
against the 1996 HyperBlade compiler described in [binary-evidence.md](binary-evidence.md) (called
"1996" below). Every item says **SAME AS 1996**, **CHANGED** or **NEW**, with an address. The
language it describes is specified in [../language-1999.md](../language-1999.md), and
[../dialects.md](../dialects.md) says which game uses which dialect.

Evidence levels:

- **[oracle]**: run through the original code under x86 emulation, with its file I/O
  redirected (a maintainer harness, not part of this package). The experiment sources are quoted
  inline; `fixtures/compiler1999` freezes the same experiments.
- **[decomp]**: read from a Ghidra decompilation of the executable and, where the decompiler was
  unclear, from a disassembly.
- **UNVERIFIED**: inferred, not checked either way.

The headline: the 1999 compiler is two compilers sharing one tokenizer. A **bracketed** form
`{KEYWORD args {child} ...}` computes every child count itself. The 1996 **flat** form (explicit
counts, positional counters) still exists as a fallback, and the compiler switches to it the
first time it scans to the end of the file without meeting a brace.

## 0. The binary

| | |
|---|---|
| File | `Baseball2000.exe` (Microsoft Baseball 2000) |
| PE TimeDateStamp | `0x36FAF3DD` (1999-03-26 by the stamp; the brief says linked 1999-04-14) |
| Image base | 0x400000 |
| Sections (VA / raw) | `.text` 0x401000/0x1000, `.rdata` 0x62C000/0x22C000, `.data` 0x65F000/0x25F000 (raw 0x16000, virtual 0x5C728), `_BSS` 0x6BC000, `.rsrc` 0x6BD000 |

File offset = VA - 0x400000 for every section (raw offset equals RVA).

## 1. Errors and exceptions

- **CHANGED.** There is no `AdlibError` RTTI any more. A search of every section finds only the
  `.PAD` (`char*`, TypeDescriptor 0x661018) and `.PAX` descriptors.
- **SAME AS 1996.** Every diagnostic is `throw (char*)`:
  `__CxxThrowException@8` at **0x5F66E5**, ThrowInfo **0x64CA90** (catchable-type array 0x64CA80).
- **CHANGED.** Message buffers are now stack locals, not `.data` buffers. "Bad number in plan: "
  is thrown as the bare literal, with no token appended [oracle].
- The scene compile path (0x609790, 0x609F60) has no `catch (char*)` of its own. Where the throw
  is finally caught is UNVERIFIED. The animation-assembly loader catches at 0x411E81
  ("Error in reading assembly table").

## 2. Compiler object (no RTTI, no vtable)

Allocated with `operator new(0xCDABA)` (1996: 0x2289C) at five sites: the scene compile driver
0x609F60, the plan-message collector 0x6093C0, the nested `CHANGE_OF_PLAN` compile (in both
emitters), the animation-assembly loader 0x4119C0, and plan instantiation 0x614A30 (a lazily
built global at `DAT_006B841C`, used for symbol lookups when it logs argument-count errors).

| offset | size | field | vs 1996 |
|---|---|---|---|
| +0x00 | i32 | source length | CHANGED (was +0x04; +0x00 held the object index) |
| +0x04 | i32 | cursor | CHANGED (was +0x08) |
| +0x08 | i32 | dwords emitted (PBN header) | CHANGED (was +0x0C) |
| +0x0C | char* | source buffer, `new(400000)` | CHANGED (was +0x10, 80000) |
| +0x10 | u32* | output buffer, `new(1200000)` = 300,000 dwords, no bounds check | CHANGED (was +0x14, 120000) |
| +0x14 | i16 | brace depth (reset by Compile) | NEW |
| +0x18 | u16 | object index (`OpenPlanSource` arg 1) | CHANGED (was +0x00) |
| +0x1A | char[500][32] | behaviour names | CHANGED (was 60 at +0x18) |
| +0x3E9A | char[500][32] | local-real names | CHANGED (was 60 at +0x798) |
| +0x7D1A | char[1200][32] | interactor names | CHANGED (was 150 at +0xF18) |
| +0x1131A | 1000 x 0x28 | keyword table (§4), 22 entries used | CHANGED layout and capacity |
| +0x1AF5A | 300 x 0x28 | special-constant table (§5), 58 used | CHANGED capacity |
| +0x1DE3A | 5000 x 0x28 | main symbol table (§6) | CHANGED (was 2101 at +0x4A7C) |
| +0x4EB7A | 5000 x 0x28 | per-object message table (§6.7) | NEW |
| +0x7F8BA | char[500][20][32] | message-interactor ids per behaviour (dev UI) | CHANGED (was [61][20][32]) |
| +0xCDABA | | end | |

Evidence [decomp]: the constructor writes +4..+0x14 and the two buffers; `ReadSource` 0x60F580
clears the first byte of every slot in the four name tables (`iVar11 = 500`, inner 0x14,
`0x4B0`); the lookups bound the tables at 999, 299, 4999, 4999, 499 and 0x4AF.

```c
// 0x60F580 ReadSource: after a successful read
puVar7 = (undefined1 *)((int)param_1 + 0x3e9a);   // local reals; puVar7[-16000] = +0x1A behaviours
puVar4 = (undefined1 *)((int)param_1 + 0x7f8ba);  // 500 rows x 20 message interactors
puVar7 = (undefined1 *)((int)param_1 + 0x7d1a); iVar3 = 0x4b0;  // 1200 interactors
```

The constructor does **not** initialise the message table at +0x4EB7A. Compile reads it on every
identifier lookup, so a harness must call 0x612940 (or write -1 at +0x4EB9E) before compiling.

## 3. Function table

Calling conventions are from the `ret` instruction. "this" is ECX.

| 1996 | 1999 | role | status |
|---|---|---|---|
| 0x487790 ctor | **0x60EC90** | fastcall(this). Builds both tables, `new(400000)` source, `new(1200000)` output | CHANGED sizes |
| 0x488220 | **0x60F190** | frees both buffers | SAME |
| 0x488490 ReadSource | **0x60F580** | thiscall(this, CDiskFile*, name), ret 8 | CHANGED limit and message |
| 0x4885E0 OpenPlanSource | **0x60F840** | thiscall(this, u16 objIdx, name), ret 8. `[0x6AA700] + name + ".txt"` | CHANGED path source |
| | **0x60F9F0** | thiscall(this, path): reads any file into the source buffer ("Object Def file could not be opened: ") | NEW |
| 0x488880 Compile | **0x60FB00** | thiscall(this, world*), ret 4 | CHANGED (§7) |
| 0x488900 PrescanDecls | **0x610B60** | fastcall(this) | CHANGED (§8) |
| 0x488EC0 IsSeparator | **0x610EA0** | space, TAB, `,`, `&` | SAME |
| | **0x610ED0** | space, TAB, `,` (no `&`) | NEW, used only by 0x611CD0 |
| 0x488EF0 IsCommentStart | **0x610EF0** | `//`, `/*`, `(`, `#` | SAME |
| 0x488F30 IsIdentifier | **0x610F20** | prefixes `id`, `_`, `A_`, `AA_`, **`BC_`** | CHANGED |
| 0x488F90 IsAnimName | **0x610F70** | `AA_` | SAME |
| 0x488FC0 IsNumber | **0x610FA0** | first char `0-9 . + -` | SAME |
| 0x488FF0 IsPronounName | **0x610FD0** | prefix `id_PRN_` | CHANGED test |
| | **0x611010** | prefix `id_PRT_` | NEW |
| | **0x611050** | prefix `id_MSG_` | NEW |
| | **0x611090** | entry name `_stricmp` `id_ACF_SendMessage` or `id_ACF_SendMsgImmediate` | NEW |
| 0x489020 EmitLeaf | **0x6110D0** | thiscall(this, world*). The flat emitter | CHANGED (§7.3) |
| | **0x60FB80** | thiscall(this, world*). The bracketed emitter, recursive | NEW (§7.2) |
| 0x489780 ParsePlanNumber | **0x611820** | stdcall(token, u32* out), ret 8 | SAME rules |
| | **0x6117A0** | integer-prefix parser (assembly trigger data) | NEW |
| 0x4898B0 EmitString | **0x611910** | cdecl(token, out, int* count) | CHANGED limit |
| | **0x611990** / **0x611A30** | line reader / last-word split (assembly loader only) | NEW |
| 0x489940 NextToken | **0x611AB0** | thiscall(this, char buf[256]), ret 4; returns "end of line followed" | CHANGED (§8) |
| 0x489AC0 SkipLine | **0x611C40** | | SAME |
| 0x489B40 SkipSeparators | **0x611CA0** | | SAME |
| | **0x611CD0** | skip space/TAB/`,` (line reader) | NEW |
| 0x489B80 SkipComment (dead) | **0x611D00** | now called, from 0x60FB80 only | CHANGED (§8) |
| 0x489C20 FindLocalReal | **0x611D70** | 256-byte copy, 500 slots, `_stricmp` | CHANGED |
| 0x489D30 FindInteractorName | **0x611E40** | 1200 slots, `_stricmp` | CHANGED |
| 0x489E40 FindKeyword | **0x611F10** | `_stricmp`, bound 999 | CHANGED |
| 0x489F00 LookupSymbol | **0x611FA0** | special, then main, then message table, all `_stricmp` | CHANGED |
| | **0x6120F0** | "is in main symbol table" | NEW |
| 0x48A030 FindBehavior | **0x612150** | `_stricmp`, throws | CHANGED case |
| 0x48A160 LookupInteractorName | **0x612210** | 1200 slots, throws | CHANGED |
| | **0x6122C0** | keyword name for an id (used by 0x614D60, plan instantiation) | NEW |
| 0x48A240 LoadSymbols | **0x612310** | thiscall(this, world*), ret 4 | CHANGED (§6) |
| | **0x612940** | thiscall(this, world*, objIdx), ret 8. Loads one object type's messages into +0x4EB7A | NEW |
| | **0x6129B0** | thiscall(this, word): counts tokens equal to `word` (`_stricmp`) | NEW |
| | **0x612A10** | object-definition parser (§11) | NEW |
| | **0x613C00**, **0x613D40** | animation-assembly version header and body (§11) | NEW |
| | **0x614350** | emits a message DataBlock (§7.4) | NEW |
| 0x48A900 StripQuotes | **0x6143E0** | in place: truncates the last char, returns `p+1` | SAME effect |
| 0x488260 WritePbn | **0x60F270** + **0x60F1D0** | thiscall(this, char** sceneRecord, name), ret 8 | CHANGED path |
| dev dialog 0x47A9B0 | **0x609F60** | scene compile driver (§10.1) | NEW shape |
| | **0x609790** | scene loader: compile the scene or load its `.sbn` | NEW |
| | **0x609360**, **0x6093C0** | collect each plan's `id_MSG_` names per object type | NEW |

`FUN_0060DB70` is the world/scene-manager setup that builds the
path globals (§10.2) and constructs the world object.

## 4. Keyword table (+0x1131A)

**CHANGED layout.** Entries are `u32 tag; u32 id; char name[32]` (1996: name first). The tag is
always 0xABCDEFFF. The terminator has name `""` (`PTR_DAT_006499B4`) and tag = id = 0xFFFFFFFF.
FindKeyword 0x611F10 returns the terminator for an unknown word, so callers see id -1.

```c
_strncpy((char *)(param_1 + 0x11322),s_SET_COLLISION_INTERACTOR_0067328c,0x20);
*(undefined4 *)(param_1 + 0x1131a) = 0xabcdefff;
*(undefined4 *)(param_1 + 0x1131e) = 0x30000;
```

| # | keyword (string VA) | id | vs 1996 | bracketed form (§7.2) |
|---|---|---|---|---|
| 0 | `SET_COLLISION_INTERACTOR` 0x67328C | 0x00030000 | SAME | two leaves, then children |
| 1 | `SET_MESSAGE_INTERACTOR` 0x673274 | 0x00050000 | SAME | head symbol, then children |
| 2 | `ADD_AGENDA_ITEM` 0x673264 | 0x001A0001 | SAME | action |
| 3 | `REMOVE_AGENDA_ITEM` 0x673250 | 0x001A0003 | SAME | action |
| 4 | `SET_AGENDA_ITEM_PARAM` 0x673238 | 0x001A0004 | **NEW** | action |
| 5 | `SET_TIMEOUTMSG` 0x673228 | 0x001A0002 | SAME | leaf list |
| 6 | `GLOBAL_INTERACTORS` 0x673214 | 0x001B0000 | SAME | group |
| 7 | `CHANGE_OF_PLAN` 0x673204 | 0x00160000 | SAME | plan reference |
| 8 | `CHANGE_TO_BEHAVIOR` 0x6731F0 | 0x00070002 | SAME | behaviour reference |
| 9 | `CHANGE_TO_NEW_BEHAVIOR` 0x6731D8 | 0x00070005 | **NEW** | behaviour reference |
| 10 | `DECLARE_BEHAVIOR` 0x6731C4 | 0x00070000 | SAME | declaration |
| 11 | `DECLARE_LOCAL_REALS` 0x6731B0 | 0x00160001 | SAME | **unsupported** |
| 12 | `SET_BEHAVIOR_PARAM` 0x67319C | 0x00070003 | SAME | action |
| 13 | `SET_INTERACTOR_PARAM` 0x673184 | 0x00180001 | SAME | **unsupported** |
| 14 | `DO_ACTION` 0x673178 | 0x00180000 | SAME | action |
| 15 | `DECIDE_BY_AMONG` 0x673168 | 0x00170000 | SAME | head symbol, then children |
| 16 | `SET_DEBUG` 0x67315C | 0x00060001 | SAME | **unsupported** |
| 17 | `SET_TRACE` 0x673150 | 0x00060002 | SAME | **unsupported** |
| 18 | `Enable` 0x673148 | 0x80000000 | SAME | group |
| 19 | `Block` 0x673140 | 0x40000000 | SAME | group |
| 20 | `DataBlock` 0x673134 | 0x20000000 | SAME | leaf list |
| 21 | `END_PLAN` 0x673128 | 0xFFFF0000 | SAME | **aborts** (flat only) |
| 22 | `""` terminator | 0xFFFFFFFF | SAME | |

**REMOVED:** `SET_LOC_COLLISION_INTERACTOR`, `SET_NET_COLLISION_INTERACTOR`,
`SET_LOC_MESSAGE_INTERACTOR`, `SET_NET_MESSAGE_INTERACTOR`, `DO_LOCAL_ACTION`, `DO_NET_ACTION`,
`DECIDE_BY_WITH_AMONG`. 22 keywords, 22 distinct ids (the 1996 duplicate id is gone with them).

**CHANGED: lookup is `_stricmp`** (0x5F6CE0 is the CRT `_stricmp`; it folds `A-Z` only).
`end_plan` compiles to `0xABCDEFFF 0xFFFF0000` [oracle, selftest "lower keyword"].

## 5. Special constants (+0x1AF5A)

**SAME AS 1996** in content: `{char name[0x24]; u32 value}`; `_BROADCAST_` 0xFFFF,
`_STRAIGHTAHEAD_` 0xABCDABCD, `_TEMPORARILY_` 0xABCDABCC, `_MESSAGE_DATA_` 0xABCDABCE,
`_NO_BEH_CHANGE_` 0xABCDABCB, `_PREVIOUS_BEH_` 0xABCDABCA, then `_0_`..`_50_` from
`sprintf("_%hd_")` (fmt 0x6730C4, loop count 0x33), then a `""`/-1 terminator.
**CHANGED:** capacity 300 and the compare is `_stricmp`, so `_broadcast_` gives 0xFFFF
[oracle: `ID_ACF_Dummy _broadcast_ id_acf_dummy` gives `ffff 2`].

## 6. Symbol loading: LoadSymbols 0x612310

`LoadSymbols(this, world)` fills the main table at +0x1DE3A in this order. Each file is read into
the compiler's own source buffer through `ReadSource`, so the 399,998-byte limit applies and the
name tables are cleared. All files use the plan tokenizer.

### 6.1 Scene objects first (NEW)

For each object type `i < *(i16*)(world+0x294)`: the type's name, value `i`; then up to 8
sub-entries per type (stride 0x48 from +0x44; present when `[+4] != 0`), name at +0, value the
i16 at +0x40. What the sub-entries are (collision objects or components) is UNVERIFIED. With an
empty world this step adds nothing.

### 6.2 reservedEnumIDs.h, then enumIDs.h (CHANGED)

Paths are the globals `[0x672FA4]` (= 0x6AA800) and `[0x672FA8]` (= 0x6AA900). Both are read with
the name "enumIDs" for the too-large message. Per file:

1. Read tokens until one `_stricmp`-equals `"enum"` (0x67358C). There is no end check: a file
   without `enum` never leaves this loop (UNVERIFIED hang, by reading).
2. Skip raw characters to the first `{`.
3. Each token becomes a symbol with the running index of the current enum.
4. A token beginning with `=` skips raw characters up to the next `,`. **The value after `=` is
   ignored, and numbering simply continues** [oracle]:
   `enum A { id_A_x = 5, id_A_y }` gives 0 and 1.
5. `}` resets the index. If the next character is not `{`, skip raw characters to the next `{`.
   Text between enums (type names, `typedef`s) is never tokenised.
6. **NEW, enumIDs.h only:** when the index is 0 and the token has `MSG`, `AGD` or `ACF` at
   characters [3..5] (case-sensitive), numbering starts at **4**, **1** or **2**. These are the
   `NUMRES` values of reservedEnumIDs.h (`id_MSG_NUMRESMSGIDS` = 4, `id_AGD_NUMRESAGDIDS` = 1,
   `id_ACF_NUMRESACFIDS` = 2), hard-coded instead of evaluated.

```c
if ((sVar10 == 1) && (sVar9 == 0)) {
  if (local_618[3] == 'M') { if ((local_614 == 'S') && (local_613 == 'G')) sVar9 = 4; }
  else if (local_618[3] == 'A') {
    if (local_614 == 'G') { if (local_613 == 'D') sVar9 = 1; }
    else if ((local_614 == 'C') && (local_613 == 'F')) sVar9 = 2;
  }
}
```

So the Baseball header's `id_AGD_Dummy = id_AGD_NUMRESAGDIDS` gives `id_AGD_Dummy` = 1 and
`id_ACF_Dummy = id_ACF_NUMRESACFIDS` gives 2. The right-hand side is never looked up. This
matches C only because the hard-coded numbers equal the reserved counts [oracle: selftest
symbols `id_AGD_Dummy = 1`, `id_ACF_Dummy = 2`; `id_AGD_NUMRESAGDIDS` = 1,
`id_MSG_NUMRESMSGIDS` = 4]. The trigger fires on the first token of *any* enum, `=` or not:
`enum B { id_ACF_q, id_ACF_r }` gives 2, 3 [oracle].

Traps [oracle]:

- `id_A_y = 7 };` (no comma before `}`) skips to the next comma in the file, through the `}` and
  the next enum's opening: `enum A {id_A_x, id_A_y = 7}; enum B {id_B_q, id_B_r};` loses
  `id_B_q` and gives `id_B_r` = 2.
- `id_A_x=3` with no spaces is one symbol named `id_A_x=3`.
- `ENUM` matches `enum`.

### 6.3 Animassm.txt (SAME AS 1996)

Path `[0x672FAC]` (0x6AAB00, the animData directory) + `"Animassm.txt"` (0x67357C). The first line
is skipped; each `AA_` token gets a running value from 0; any other token skips the rest of its
line. The game ships only the binary `ANIMASSM.ABN`, so this file is normally absent and the step
adds nothing.

### 6.4 SndFiles.lst (CHANGED)

Path `[0x672FB0]` (0x6AAC00). For each line: if it starts with a comment, skip it; take the first
token `w` of the next line, add `id_SND_w` with a running value from 0, skip the rest of the line.
1996 skipped lines whose first token was numeric; 1999 does not, so a numeric first token becomes
`id_SND_22`. With the 2001 `SNDFILES.LST` (`#` header lines, then `Button1<TAB>id_MSG_SndDone<TAB>3`)
`id_SND_Button1` = 0 [oracle]. Baseball 2000 ships only `SNDFILES.BLS`.

### 6.5 Infobtxt.lst: REMOVED. No `id_TXT_` symbols.

### 6.6 Terminator and lookup

A `""`/-1 entry ends the main table. Lookup (0x611FA0) is the special table, then the main table,
then the per-object message table, each linear, `_stricmp`, first match wins. There is no fill
bound check on the main table (5000 entries, UNVERIFIED overflow behaviour). Names are copied
with `strncpy(dst, src, 0x24)`, so names of 36+ characters lose their terminator.

### 6.7 Per-object message table (NEW): 0x612940

```c
for (puVar2 = *(undefined4 **)(*(int *)(iVar3 + 0xc) + 8); puVar2 != 0; puVar2 = puVar2[1]) {
  _strncpy(param_1 + 0x4eb7a + sVar4 * 0x28, *puVar2, 0x24);
  *(int *)(_Dest + 0x24) = sVar4 + 4;      // message index = position + 4
}
*(undefined4 *)(param_1 + 0x4eb76 + sVar4 * 0x28) = 0xffffffff;
```

`id_MSG_` names are **per object type**. The object type's list (built by 0x6093C0 from its plan
sources: every `id_MSG_` token not in the main table, in first-seen order, appended by 0x60B5E0)
is copied in with values 4, 5, .... The four reserved messages (`id_MSG_Initiate`..`SyncFrame`,
0..3) come from reservedEnumIDs.h through the main table, which is searched first. The -1 store
lands on the list's trailing empty sentinel node (0x60B5E0 always keeps one), so every real name
stays reachable [decomp]. The exact ordering when several object types share a plan is
UNVERIFIED.

## 7. Compile 0x60FB00 and the two emitters

```c
*(undefined2 *)(param_1 + 5) = 0;   // depth
DAT_006b8400 = 0;                   // flat-mode flag
FUN_00610b60();                     // prescan
param_1[1] = 0;
while (pos < len && buf[pos] != 0)
  if (!FUN_0060fb80(world)) throw "Aborting Plan Parse.";
```

### 7.1 Mode selection (NEW)

Each call to the bracketed emitter 0x60FB80 first does this:

1. If `DAT_006B8400` is set, call the flat emitter 0x6110D0 and return its result.
2. If at CR or LF, SkipLine once. SkipSeparators. If at a comment start, run SkipComment
   0x611D00 and recurse.
3. Scan raw characters forward. At `}`: depth--, consume it, return 0 (end of a child list).
   At end of buffer: restore the cursor to where this call started, set `DAT_006B8400 = 1`, and
   run the flat emitter. Anything else is skipped.
4. At `{`: depth++, NextToken (which swallows the `{`), FindKeyword, then the form for that id.

Consequences [oracle]:

- A file is any number of bracketed blocks followed by an optional flat tail. Flat text *between*
  blocks is skipped unread.
- `END_PLAN` must be flat: `{END_PLAN}` returns 0 and throws "Aborting Plan Parse.".
  `{DataBlock 1}\r\nEND_PLAN` is the working shape.
- A purely flat file must contain **no brace at all**. The first scan starts at offset 0 and
  stops at the first brace anywhere, including inside a comment:
  `DO_ACTION 2 id_ACF_Dummy 7 // {x}` aborts; `DataBlock 1 } { 5` aborts.
- A stray `}` at top level aborts.

### 7.2 Bracketed forms (NEW), 0x60FB80

"hdr" means the two dwords `0xABCDEFFF id`; "count" is a reserved dword patched afterwards.
"Children" means repeated recursive calls until one returns 0 (the call that consumes this
block's `}`). "Leaf" means: identifier, lookup and pronoun tagging (§7.5), emit; or number, emit
(else "Bad number in plan: "); strings and other words emit nothing. "Head" means: lookup the
token as a symbol and emit its raw value with no tagging and no number parse, so an unknown name
or a numeric literal emits 0xFFFFFFFF.

| form | keywords | shape | emitted count |
|---|---|---|---|
| group | `Enable`, `Block`, `GLOBAL_INTERACTORS` | `{K child*}` | children |
| head + children | `DECIDE_BY_AMONG`, `SET_MESSAGE_INTERACTOR` | `{K head child*}` | children + 1 |
| two leaves + children | `SET_COLLISION_INTERACTOR` | `{K leaf leaf child*}` | children + 2 |
| leaf list | `DataBlock`, `SET_TIMEOUTMSG` | `{K tok*}` | tokens read |
| action | `DO_ACTION`, `ADD_AGENDA_ITEM`, `REMOVE_AGENDA_ITEM`, `SET_AGENDA_ITEM_PARAM`, `SET_BEHAVIOR_PARAM` | `{K head tok*}` | tokens + 1 |
| declaration | `DECLARE_BEHAVIOR` | `{K head "name" child*}` | children + 2 |
| behaviour reference | `CHANGE_TO_BEHAVIOR`, `CHANGE_TO_NEW_BEHAVIOR` | `{K "beh"}`, `{K number}` or `{K symbol}` | literal 1 |
| plan reference | `CHANGE_OF_PLAN` | `{K "plan" "beh"}` or `{K "plan" number}` | literal 2 |
| no output, returns 1 | `SET_DEBUG`, `SET_TRACE`, `DECLARE_LOCAL_REALS`, `SET_INTERACTOR_PARAM` | | none |
| returns 0 | `END_PLAN`, any unknown word | | none |

Verified shapes [oracle]:

```
{DECLARE_BEHAVIOR id_BEH_DoNothing "A" {Enable {DO_ACTION id_ACF_Dummy 1 2}}}
  abcdefff 70000 3 0 abcdeffe 41 0 abcdefff 80000000 1 abcdefff 180000 3 2 1 2
{DECIDE_BY_AMONG id_ACF_Dummy {DataBlock 1} {DataBlock 2}}   abcdefff 170000 3 2 ...
{DECIDE_BY_AMONG 5 {DataBlock 1}}                            abcdefff 170000 2 ffffffff ...
{SET_COLLISION_INTERACTOR 5 id_ACF_Dummy {DataBlock 1}}      abcdefff 30000 3 5 2 ...
{SET_MESSAGE_INTERACTOR id_MSG_Initiate {DataBlock 1}}       abcdefff 50000 2 0 ...
{SET_TIMEOUTMSG 1 2 3}                                       abcdefff 1a0002 3 1 2 3
{CHANGE_OF_PLAN "Other" 3}                                   abcdefff 160000 2 abcdeffe 4f 74 68 65 72 0 3
{DECLARE_BEHAVIOR 6 "A"}                                     abcdefff 70000 2 ffffffff abcdeffe 41 0
```

Rules and traps, all [oracle] unless marked:

- **The count is computed, but it counts tokens, not dwords.** In leaf lists and actions every
  token raises the count, including strings and plain words, which emit nothing:
  `{DO_ACTION id_ACF_Dummy "x" 3}` gives `abcdefff 180000 3 2 3`. Strings, local-real names and
  interactor names cannot be action arguments in this form.
- **A leaf list or action ends only when `}` is the very next character** after the last token's
  trailing separators, or at the start of the next line when the last token ended the line.
  `{DataBlock 1 2\r\n}` works; `{DataBlock 1 2\r\n  }` and `{DataBlock 1 2 \r\n}` read the `}` as
  a token and run on into the next block (`abcdefff 20000000 5 1 2 3`).
- The two reference forms consume one character blindly after their argument, so the same rule
  applies to their `}`.
- **A keyword must be followed by a separator.** NextToken swallows a `{` that ends a token, so
  `{Block{DataBlock 1}}` loses the child's brace and aborts.
- The four "no output" keywords leave their block open. Their `}` then ends the *parent's* child
  list early and the file aborts: `{Block {SET_DEBUG 1} {DataBlock 2}}` emits `Block 1 ...` and
  throws "Aborting Plan Parse.". The same happens for an unknown keyword (`{Block {FOO 1} ...}`
  gives `Block 0`).
- `DECLARE_BEHAVIOR` without a quoted name throws "Inability to read string: " + token. The
  prescan runs first, though, and hangs when no `"` token follows the keyword (§8.1)
  [oracle: `{DECLARE_BEHAVIOR id_BEH_DoNothing Abcdefghij}` exhausts the instruction budget].
- `CHANGE_TO_BEHAVIOR "A"` and `CHANGE_TO_NEW_BEHAVIOR "A"` both resolve the name with
  FindBehavior [oracle, selftest "brace ctb"].
- `CHANGE_OF_PLAN` with a non-numeric second argument builds a sibling compiler:
  `new(0xCDABA)`, ctor, `LoadSymbols(world)`, `0x612940(world, this->objIdx)`,
  `OpenPlanSource(objIdx, stripped plan name)`, Prescan, `FindBehavior(stripped name)`,
  dtor, delete [decomp]. The sibling is leaked if anything throws.

### 7.3 Flat emitter 0x6110D0 (CHANGED from 1996 EmitLeaf)

For each token:

1. Increment seven global counters (initial value 100, never reset by Compile):

   | VA | reset by | name |
   |---|---|---|
   | 0x6730A8 | `DECLARE_BEHAVIOR` | beh |
   | 0x6730AC | `CHANGE_TO_BEHAVIOR` | ctb |
   | 0x6730B0 | `CHANGE_TO_NEW_BEHAVIOR` | ctnb, **NEW** |
   | 0x6730B4 | `CHANGE_OF_PLAN` | cop |
   | 0x6730B8 | `SET_INTERACTOR_PARAM` | sip |
   | 0x6730BC | `DECLARE_LOCAL_REALS` | dlr |
   | 0x6730C0 | a symbol whose entry is `id_ACF_SendMessage`/`SendMsgImmediate` | send, **NEW** |

2. Identifier: look it up. **CHANGED:** value -1 and not `id_MSG_` emits nothing and does not
   throw ("Bad plan id value" is gone); an unknown `id_MSG_` emits 0xFFFFFFFF [oracle:
   `id_NOPE 7` gives `7`, `id_MSG_Foo` gives `ffffffff`]. Pronoun tagging §7.5. `dlr == 1` needs
   a number ("expected arg enumeration of localvars.", SAME). Message handling §7.4.
3. Number: emit or throw "Bad number in plan: " (no token appended).
4. String, same order as 1996: local real `0xEDCB0000|i`; interactor name `0xDCBA0000|i`;
   `beh == 3` EmitString; `cop == 2` EmitString and remember the stripped name; `cop == 3` sibling
   compile as in §7.2; `ctb == 2` FindBehavior; **`ctnb == 2` FindBehavior (NEW)**; `sip != 3`
   EmitString; `sip == 3` LookupInteractorName [oracle: local reals, interactor names, ctnb].
5. Word: FindKeyword (`_stricmp`). Reset that keyword's counter. Unknown words emit nothing and
   still advance the counters (SAME).

The `cop == 2` plan name is kept in a **stack local** (`local_15c`) of the previous call. It
survives only because Compile always calls the flat emitter from the same stack depth
[decomp]. A harness must run real stack frames for flat `CHANGE_OF_PLAN` to work.

### 7.4 Message arguments of SendMessage (NEW)

Both emitters treat the action whose head symbol is `id_ACF_SendMessage` or
`id_ACF_SendMsgImmediate` specially. Token 1 after the head is the target and token 2 the message.

- Target value 0xFFFF (`_BROADCAST_`), 0xABCDABCE (`_MESSAGE_DATA_`), or an `id_PRN_`/`id_PRT_`
  token: the target is unknown at compile time. The next identifier is not emitted. Instead
  0x614350 emits a DataBlock: `0xABCDEFFF 0x20000000 N` and one i16 (sign-extended) per object type
  in the current world's list (`DAT_006B8378` + 8, count at +8), the index of that message in
  that type, or -1.
- Any other identifier target: `0x612940(world, targetValue)` switches the message table to the
  target object type before the message is looked up.
- After token 2, `0x612940(world, this->objIdx)` restores the compiler's own type.

The shipped trial PBNs show exactly this: `DO_ACTION 4 id_ACF_SendMessage _BROADCAST_`
followed by `DataBlock 8 -1 174 -1 -1 16 72 73 28` in the trial's `Ball.pbn`. In the flat emitter the
DataBlock flag lives in the global `DAT_006B8408` and is cleared only when an identifier arrives
at `send == 2`. A numeric message after a broadcast target leaves it set [oracle: `fixtures/compiler1999/quirks/send_broadcast_flag_outlives`].

### 7.5 Pronoun tagging (CHANGED)

The test reads the **symbol-table entry's name** and needs the exact, case-sensitive 7-character
prefix:

- `id_PRN_` gives `value | 0xFEDD0000` (1996: `PRN` at [3..5] gave 0xFEDC0000);
- `id_PRT_` gives `value | 0xFEDC0000` (NEW).

`id_PRN_Me id_PRT_ViewOwner` compiles to `fedd0000 fedc0001` [oracle].

### 7.6 Numbers, strings and limits

- **Numbers, SAME AS 1996** [oracle]: `_5_ 0x10 1.5 -1 1e3 12ab 4294967295 0X1F` gives
  `5 10 3fc00000 ffffffff 1 c ffffffff 1f`. The character set is 0x673510
  `xXABCDEFabcdef0123456789.+-_`; `_N_` uses `atol` of the inside, `0x` uses `sscanf("%x")`
  (0x67350C), a `.` (0x6499B8 points at `"."`) selects `atof` stored as a single, else `atol`.
  0x611820 stores the raw dword (`mov [ecx], edx`), despite the decompiler's `(float)` cast.
- **Strings, CHANGED limit:** EmitString 0x611910 gives up after 256 characters, but the
  tokenizer limit comes first: **content at most 253 characters** [oracle: 254 crashes]. The
  1996 crash at 40 is gone (FindLocalReal now copies into 256 bytes) [oracle: 40 works]. `_`
  still becomes a space; there are no escapes.
- **Tokens: at most 255 bytes.** NextToken has no bound and writes into callers' 256-byte buffers;
  a 256-byte word or identifier overwrites the return address [oracle].
- **Source: at most 399,998 bytes.** `ReadFile(buf, 400000)`; a result of 399,999 or more throws
  "This plan file: <name> is too large.  Why not break it up into separate plans?" [oracle].
- Output: 300,000 dwords, unchecked. Name tables: 500 behaviours, 500 local reals, 1200 interactor
  names, 20 message interactors per behaviour, 32 bytes per name, none checked on insert.
- Error buffers: "Could not find behavior named: " and "Could not find interactor named: " are
  built in 64-byte stack locals, so names longer than about 32 characters overflow them; the
  "Inability to read string: " buffer is 36 bytes (UNVERIFIED consequences; the throw follows
  at once).
- Line endings: CRLF and LF work. A bare CR skips the following line, SAME AS 1996 [oracle].

## 8. Lexical rules: NextToken 0x611AB0 and SkipComment 0x611D00

- Separators: space, TAB, `,`, `&` (SAME). CR/LF end a token and are consumed with it.
- **CHANGED:** `{` ends a token and is consumed and dropped. `}` ends a token; a lone `}` is
  returned as the token `"}"`.
- NextToken returns 1 when the token was followed by the end of a line (used by `SetPlans`).
- Comments inside NextToken: `//`, `/*`, `(`, `#` each skip to the end of the line (SAME).
- **CHANGED:** at a bracketed statement boundary the emitter calls SkipComment 0x611D00 instead:
  - `//`: to end of line;
  - `(`: up to and including the next `)`, across lines, unbounded; if the character before `(`
    is `)` nothing is skipped and the emitter recurses forever;
  - `/*`: advances one character only. The raw `{` scan then skips the comment text, but a brace
    inside the comment is compiled: `/* {DataBlock 9} */ {DataBlock 1}` emits both blocks
    [oracle];
  - `#`: nothing is skipped and the emitter recurses until the stack overflows. `# hi` before or
    between blocks crashes the compiler [oracle].
- Identifier prefixes (case-sensitive): `id`, `_`, `A_`, `AA_`, **`BC_` (NEW**, for
  `enum BALLCOUNTTYPE { BC_OUTS, ... }`). `BC_BALLS` gives 1 [oracle]. `ID_ACF_Dummy` is still a
  dropped word [oracle].

### 8.1 Prescan 0x610B60 (CHANGED)

Keywords are matched with `_stricmp` (SAME). The prescan also runs over bracketed files.

| keyword | 1999 rule | vs 1996 |
|---|---|---|
| `DECLARE_LOCAL_REALS` | next token identifier or number ("Expected an argument enumerator.  First two args are id and string name."), then n tokens starting with `"` ("Expected a string name in LocalVar Declaration."); stripped names to +0x3E9A | SAME |
| `DECLARE_BEHAVIOR` | read tokens until one starts with `"`; its stripped text is the next behaviour name. No count or type check; hangs at end of file if no `"` token follows | CHANGED: "Expected an argument enumerator." / "Expected an id_BEH_ type id." / "Expected a string name in Behavior Declaration." no longer raised here |
| `SET_MESSAGE_INTERACTOR` | read tokens until one starts with `id_MSG_`; store it raw in the dev-UI table | CHANGED: no "Expected an id_MSG_ type id." |
| `SET_INTERACTOR_PARAM` | count identifier or number ("Expected an argument enumerator."); if the next token is `id_SET_InteractorName`, the next must start with `"` ("Expected a string name in Behavior Declaration."); stripped name to +0x7D1A | SAME |

Behaviour references are now case-insensitive: `CHANGE_TO_NEW_BEHAVIOR 1 "a"` finds `"A"` [oracle].

## 9. Diagnostics catalogue

| VA | text | thrower |
|---|---|---|
| 0x6732A8 | `Cannot open binary plan file: ` + path | WritePbn 0x60F270, after one `_mkdir` retry |
| 0x673304 + 0x6732C8 | `This plan file: ` + name + ` is too large.  Why not break it up into separate plans?` | ReadSource 0x60F580 |
| 0x673318 | `This Plan file could not be opened: ` + path | OpenPlanSource 0x60F840 |
| 0x673340 | `Object Def file could not be opened: ` + path | 0x60F9F0 |
| 0x673368 | `Aborting Plan Parse.` | Compile 0x60FB00; now reachable (§7.1) |
| 0x673380 | `Inability to read string: ` + token | flat emitter, bracketed `DECLARE_BEHAVIOR` |
| 0x67339C | `Bad number in plan: ` (no token) | both emitters |
| 0x6733B4 | `Expected a string name in Behavior Declaration.` | prescan, `SET_INTERACTOR_PARAM` |
| 0x6733E8 | `Expected an argument enumerator.` | prescan, `SET_INTERACTOR_PARAM` |
| 0x67340C | `Expected a string name in LocalVar Declaration.` | prescan |
| 0x673440 | `Expected an argument enumerator.  First two args are id and string name.` | prescan |
| 0x6734D0 | `expected arg enumeration of localvars.` | flat emitter |
| 0x673530 | `Could not find behavior named: ` + name | FindBehavior 0x612150 |
| 0x673550 | `Could not find interactor named: ` + name | LookupInteractorName 0x612210 |

**REMOVED:** `Bad plan id value: `, `Expected an id_BEH_ type id.`, `Expected an id_MSG_ type id.`,
`Plan file could not be opened.`. The emitters' "FindBehavior < 0, return 0" branches are dead,
because 0x612150 throws.

Not compiler errors, but on the same path: the binary loader (0x606DE0: `Binary Plan could not be
opened!`, `Misread binary plan`, `Inability to read plan`), plan instantiation 0x614A30
(`Failed to compile plan `, `Plan argument counting error (see .log file) in plan `), and the
object-definition parser (`Expected '='`).

## 10. Harness entry points

### 10.1 How the game compiles

The scene loader **0x609790** (`thiscall(world, srcDir, sbnDir, sceneRecord*)`) builds the `.sbn`
path (`sbnDir\[scene\ or sbn\]scene.sbn`) and the scene source path (`srcDir + sceneRecord[1]`).
When the `.sbn` is missing, older than the source, or any plan's `.pbn` is missing or older than
its `.txt`, it calls the driver; otherwise it loads the `.sbn`. The runtime plan loader 0x607070
reads only `.pbn` files; its "Failed to compile plan" means instantiation, not source compilation.

The driver **0x609F60** (`thiscall(world, sceneRecord*, srcPath, sbnPath)`):

1. `new(0xCDABA)`, ctor 0x60EC90.
2. `LoadSymbols(world)` 0x612310.
3. `0x60F9F0(srcPath)` reads the object-definition file; `0x6129B0("DefineObject")` and
   `0x6129B0("SetCollisionObjs")` size the object arrays (`world+0x294`, `+0x296`).
4. `0x612A10` parses the object definitions; `SetPlans` triggers the message collection
   (0x609360 to 0x6093C0).
5. `LoadSymbols(world)` again, now with object names.
6. If `DAT_006B8375` is set, compare `_stat` (0x5F68FF) mtimes of every `[0x6AA700]name.txt` and
   `.pbn`; stop here if all are current.
7. For every object type and every plan it lists (each plan once, via 0x60B5E0):
   `OpenPlanSource(type+0x284, name)`, `0x612940(world, type+0x284)`, `Compile(world)`,
   `WritePbn(world+8, name)`.
8. dtor 0x60F190, delete, then write the `.sbn` (0x60BD70).

### 10.2 Minimal compile of one plan (what the harness does)

1. Map the image; give FS a mapped page (every function sets up SEH through `fs:[0]`).
2. Fill the path globals (256-byte buffers):

   | buffer | pointer | content | set by |
   |---|---|---|---|
   | 0x6AA700 | 0x672FA0 | Plans dir, with trailing `\` | 0x60DB70 (`base + "Plans\\"`), then game init 0x4B2D30 overwrites it |
   | 0x6AA800 | 0x672FA4 | reservedEnumIDs.h path | 0x60DB70 |
   | 0x6AA900 | 0x672FA8 | enumIDs.h path | 0x60DB70 |
   | 0x6AAB00 | 0x672FAC | animData dir, trailing `\` | 0x60DB70, then 0x4B2D30 |
   | 0x6AAC00 | 0x672FB0 | SndFiles.lst path | 0x60DB70 |
   | 0x6AAD00 | 0x672FB4 | base dir | 0x60DB70 |

   No `GetModuleFileName` is involved (CHANGED). Paths are relative or absolute exactly as stored.
3. `obj = operator new(0xCDABA)`; `ctor(ECX=obj)`.
4. `LoadSymbols(ECX=obj, world)`; a zeroed world (`+0x294` = 0) means no scene objects.
5. `0x612940(ECX=obj, world, objIdx)`, or write 0xFFFFFFFF at `obj+0x4EB9E` when there is no
   scene. Broadcast sends also need `DAT_006B8378` to point at a world with an object-type list.
6. `OpenPlanSource(ECX=obj, objIdx, name)`: opens `[0x6AA700] + name + ".txt"` in mode 1.
7. `Compile(ECX=obj, world)`. Result: `*(u32*)(obj+8)` dwords at `*(u32**)(obj+0x10)`.
8. Optional `WritePbn(ECX=obj, &sceneNamePtr, name)`: `[0x6AA700] + (DAT_006B8375 == 0 ?
   scene + "\" : "") + name + ".pbn"`, `OpenFile(path, 3)`, then 0x60F1D0 writes the header dword
   and the body up to and including the first 0xFFFF0000 (unbounded, SAME AS 1996). The game sets
   `DAT_006B8375 = 1` in 0x4B2D30, so it writes flat.

### 10.3 What to hook

| what | address | convention | note |
|---|---|---|---|
| `CDiskFile::OpenFile(char*, int)` | IAT 0x62CC8C | thiscall, ret 8 | Utility.dll. Modes seen: 1 read, 3 write. Returns nonzero on success |
| `CDiskFile::ReadFile(void*, int)` | IAT 0x62CC88 | thiscall, ret 8 | returns bytes read |
| `CDiskFile::WriteFile(void*, int)` | IAT 0x62CC90 | thiscall, ret 8 | |
| `CDiskFile::CloseFile()` | IAT 0x62CC9C | thiscall | |
| `CDiskFile::~CDiskFile()` | IAT 0x62CCA0 | thiscall | objects are 0x104-byte stack locals with the first dword zeroed |
| `operator new` | 0x5F6618 | cdecl | the probe uses a bump allocator |
| `operator delete` / `free` | 0x5F5F36 | cdecl | no-op is fine |
| `__CxxThrowException@8` | 0x5F66E5 | stdcall(&char*, ThrowInfo) | read `**arg0` as the message and stop; it would reach `RaiseException` (IAT 0x62C6B8) |
| `_mkdir` | 0x61DA46 | cdecl | WritePbn retry; calls `CreateDirectoryA` (IAT 0x62C7EC) and `GetLastError` |
| `_stat` | 0x5F68FF | cdecl | driver and scene loader only |
| `GetFileAttributesA`, `CreateDirectoryA` | KERNEL32 IAT | | scene loader 0x609790 only |
| `fopen`/`fread`/`fclose` | 0x5F65C7 / 0x61D92F / 0x5F6413 | cdecl | `.pbn` loader 0x606DE0 only |

Statically linked CRT code the compile path runs and the probe leaves alone: `_strncpy` 0x5F6090,
`_stricmp` 0x5F6CE0 (fast path while the locale flag at 0x6AA300 is zero; otherwise it calls
`_lock` 0x5FD5B5), `sprintf` 0x5F63C1 (constructor), `sscanf` 0x5F6C2A (`"%s"`, `"%x"`), `strspn`
0x5F8300, `strpbrk` or `strstr` 0x5F9AD0 (UNVERIFIED which), `atol` 0x5F8E24, `atof` 0x61DA72
(x87).

### 10.4 Global state to reset between compiles

| VA | initial | role |
|---|---|---|
| 0x6730A8..0x6730C0 (7 dwords) | 100 | flat-emitter counters (§7.3), never reset by Compile |
| 0x6B8400 | 0 | flat-mode flag, reset by Compile |
| 0x6B8408 | 0 | flat-emitter message-DataBlock flag, not reset by Compile |
| 0x6B8404 | | scratch for the `DECLARE_LOCAL_REALS` count |
| obj+0x14 | | brace depth, reset by Compile |
| obj+0x4EB7A | uninitialised | message table, see §2 |
| 0x6B8375 | 0 (BSS) | flat Plans layout and mtime-check switch; the game sets 1 |
| 0x6B8378 | | current world, read by 0x614350 |

The simplest safe policy is the 1996 one: restore `.data` and `_BSS` before each compile.

## 11. Other 1999 language and loaders

- **Object definitions (NEW, separate parser, same object).** 0x612A10 uses the compiler's
  tokenizer and buffer but is not part of plan compilation. Words it knows (all `_stricmp`):
  `DefineObject`, `EndObjectDefinition`, `type`, `modfile`, `SetMotionParams` with `pos`, `dir`,
  `accel`, `speed`, `fric`, `elas`, `SetPlans`, `SetCollisionObjs` with `type` =
  `sphere`/`plane`/`collection`, `colObjID`, `attachWithPronoun`, `attachCompID`, `normal`,
  `dimensions`, `radius`, `elementCount`. Assignments need `=` ("Expected '='", 0x67359C).
  `SetPlans` reads plan names to the end of the line (NextToken's return value) and starts the
  per-type message collection. A full grammar is UNVERIFIED.
- **Animation assemblies (NEW, separate loader).** 0x4119C0 builds a compiler object only for its
  tokenizer, symbols and buffer, reads the file with 0x60F9F0, then 0x613C00 reads an optional
  `Version N` header ("Expected a version number.", "Versions beyond version 1 are not supported
  by this parser."; no header means version 0 and the cursor rewinds), and 0x613D40 parses each
  assembly line by line (0x611990) with per-segment errors (`No segments in assembly: `,
  `Error geting start frame for segment: `, ... `trigger data error in assembly: `). Errors are
  caught at 0x411E81. This is not part of the plan compiler.
- **Binary formats.** Scenes compile to `.sbn` (in `sbn\` when `DAT_006B8375` is set), plans to
  `.pbn` with the same header-plus-dwords layout as 1996.

## 12. Since verified

The first harness covered single-plan compiles with an empty world. A later one answered the
three world accessors (0x60A8E0, 0x612940, 0x614350) from a scene file, and with it:

- `CHANGE_OF_PLAN` with a named behaviour, SendMessage message tables, broadcast DataBlocks and
  scene object ids behave as described above: `fixtures/compiler1999/valid/change_of_plan_*`,
  `scene/*`;
- the PBNs that shipped with Baseball 3D, Baseball 2000 and Baseball 2001 are rebuilt byte for
  byte by this compiler from libadlib's decompiled source (counts in [../compatibility.md](../compatibility.md) §4);
- the plan sources on the Baseball 3D 1998 disc compile, through this compiler, to the PBNs that
  disc shipped.

The braced emitter's recursion (one frame per block level or comment between statements) fills
the 1 MB stack the executable reserves: under the oracle it survives 1240 levels and crashes at
1260, before counting what the game itself has used.

Still unverified: the object-definition parser (0x612A10), the scene driver 0x609F60 itself, and
the hash lookup 0x6096A0 behind the broadcast DataBlock. Its result (a message's position + 4 in
each type's list, or -1) is confirmed by the 1998 data only.
