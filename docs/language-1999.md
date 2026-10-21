# ADLIB Plans language, 1998 and 1999

This is the ADLIB Plans source language as the 1999 compiler (inside Baseball 2000's executable)
accepts and translates it, written as the differences from the 1996 language in
[language.md](language.md). Anything not mentioned here is as in 1996. The 1998 language is the
same without `SET_AGENDA_ITEM_PARAM` ([dialects.md](dialects.md)). The implementation is
`adlibc --compat=adlib1999` (or `adlib1998`).

**How a rule is confirmed.** As for 1996: the 1999 compiler was run under emulation on test
sources, and each result is frozen as a fixture under `fixtures/compiler1999/` (`valid/`,
`quirks/`, `errors/`, `scene/`) with the test vocabulary `fixtures/vocab1999/`. A rule marked
**CONFIRMED BY ORACLE** names its fixtures. A rule read from the binary only names the section of
[research/binary-evidence-1999.md](research/binary-evidence-1999.md) instead. Scene fixtures start
with `// object N`: they are compiled for object N of `fixtures/vocab1999/scene.sbn`.

---

## 1. Source text

1. A source of 399 999 bytes or more is rejected with `This plan file: <name> is too large.  Why
   not break it up into separate plans?` (evidence §7.6).
2. A plan's source is `<Plans dir>\<plan>.txt`; `CHANGE_OF_PLAN` reads other plans' sources the
   same way. A missing one is `This Plan file could not be opened: <path>`. CONFIRMED BY ORACLE:
   `valid/change_of_plan_flat`, `valid/change_of_plan_braced`, `errors/no_such_plan`.

## 2. Lexical structure

As in 1996 (separators, line ends, `//` `/*` `(` `#` comments to the end of the line), except:

1. `{` ends the word it follows and is consumed; it is never part of a token. `}` is a one-byte
   token, as before. CONFIRMED BY ORACLE: `errors/braced_keyword_glued` (`{Block{DataBlock 1}}`
   loses the second brace).
2. A token is at most 255 bytes; a longer one overflows a 256-byte buffer and crashes the
   compiler. adlibc refuses it (kind `crash`): `errors/token_of_256_bytes`.
3. Identifiers also start with `BC_`. CONFIRMED BY ORACLE: `valid/bc_identifiers`.
   `ID_ACF_...` is still a dropped word: `quirks/upper_case_id_is_a_word`.

## 3. Two forms

The compiler reads a source as **braced blocks followed by an optional flat tail**:

```ebnf
source  = { gap block } [ flat ] ;
gap     = { byte - ( "{" | "}" ) } ;        (* skipped unread, see 3.1 *)
block   = "{" keyword ... "}" ;              (* see 3.2 *)
flat    = the 1996 language, see 4 ;
```

At each statement it scans forward byte by byte. A `{` starts a block. A `}` ends the enclosing
block, or at the top level aborts the compile with `Aborting Plan Parse.`. Reaching the end of the
source switches to the flat form for good, starting where the scan began. CONFIRMED BY ORACLE:
`valid/braced_then_flat`, `quirks/braced_unknown_unseen_between`, `errors/abort_stray_close`.

Consequences:

- A flat file must contain no brace at all, not even in a comment: `errors/brace_in_line_comment`.
- `END_PLAN` must be in the flat tail. `{END_PLAN}` aborts: `errors/abort_end_plan_braced`.
- Flat text between two blocks is skipped unread: `quirks/braced_unknown_unseen_between`.

### 3.1 Comments between blocks

Between blocks, `//` skips to the end of the line, `( ... )` skips to the closing parenthesis
across lines, and `/*` skips only itself: the scan for `{` then passes over the comment text, so a
block inside `/* ... */` is compiled. CONFIRMED BY ORACLE: `valid/braced_comments`,
`quirks/braced_brace_in_block_comment`. Each comment costs the compiler a stack frame, as each
level of nesting does; about 1240 of them fill its stack, and adlibc refuses more than 1200 (kind
`crash`). CONFIRMED BY ORACLE: `valid/many_comments_between_blocks` (1100 comments),
`valid/nesting_1200`; `errors/nesting_1300` crashes the original. A `#` there makes the compiler recurse until its stack
overflows (adlibc refuses it, kind `crash`): `errors/hash_comment_between_blocks`.

### 3.2 Blocks

The keyword follows the `{` (any case, and spaces or line ends may come between). The compiler
writes the count. A *leaf* is an identifier (looked up and tagged as in §5) or a number; a string or
other word in a leaf position counts as a token but emits nothing. A *head* is a symbol emitted
raw: an unknown name or a number there emits 0xFFFFFFFF.

| keywords | form | count written |
|---|---|---|
| `Enable`, `Block`, `GLOBAL_INTERACTORS` | `{K block*}` | blocks |
| `DECIDE_BY_AMONG`, `SET_MESSAGE_INTERACTOR` | `{K head block*}` | blocks + 1 |
| `SET_COLLISION_INTERACTOR` | `{K leaf leaf block*}` | blocks + 2 |
| `DataBlock`, `SET_TIMEOUTMSG` | `{K token*}` | tokens |
| `DO_ACTION`, `ADD_AGENDA_ITEM`, `REMOVE_AGENDA_ITEM`, `SET_AGENDA_ITEM_PARAM`, `SET_BEHAVIOR_PARAM` | `{K head token*}` | tokens + 1 |
| `DECLARE_BEHAVIOR` | `{K head "name" block*}` | blocks + 2 |
| `CHANGE_TO_BEHAVIOR`, `CHANGE_TO_NEW_BEHAVIOR` | `{K "behaviour"}`, `{K number}` or `{K symbol}` | 1 |
| `CHANGE_OF_PLAN` | `{K "plan" "behaviour"}` or `{K "plan" number}` | 2 |
| `SET_DEBUG`, `SET_TRACE`, `DECLARE_LOCAL_REALS`, `SET_INTERACTOR_PARAM` | nothing is emitted and the block is left open | |
| `END_PLAN`, anything else | aborts | |

CONFIRMED BY ORACLE: `valid/braced_forms`, `valid/change_to_new_behavior_braced`,
`valid/change_to_new_behavior_number`, `valid/change_of_plan_braced`,
`valid/set_agenda_item_param_braced`, `valid/pronoun_tags_braced`, `quirks/braced_head_number`,
`quirks/braced_counts_tokens`, `errors/abort_unknown_keyword`, `errors/abort_no_output_keyword`.

1. The count counts tokens, not dwords, so a string in a leaf list makes the count one larger
   than the children the game reads: `quirks/braced_counts_tokens`.
2. A token list ends only if `}` is the next byte after the last token and its separators, or
   starts the next line when the last token ended its line. `{DataBlock 1 2 ` + line end + `}`
   reads the `}` as a token and runs on into the next block. CONFIRMED BY ORACLE:
   `quirks/braced_close_on_next_line`, `quirks/braced_close_after_a_space`.
3. The two reference forms consume one byte after their argument, unseen; it has to be the `}`.
4. The four keywords that emit nothing leave their `}` to end the enclosing block early, which
   then aborts the file: `errors/abort_no_output_keyword`.
5. A source that ends inside a block makes the compiler loop for ever (adlibc refuses it, kind
   `hang`): `errors/unclosed_block`, `errors/unclosed_data_block`.
6. A count is the token count plus one or two in `int`, so it does not wrap at 32767:
   `quirks/count_past_32767`.
7. When the last line has no line end, the compiler's line skip runs on past the source into its
   buffer's earlier contents (in the game, the previous plan's source) and may compile blocks
   from them. adlibc refuses that in the braced form (kind `undefined`):
   `errors/comment_without_line_end_braced`. In the flat form the output is unaffected; an error
   raised after it gets kind `undefined` because its position is the stale one.
8. `CHANGE_OF_PLAN`'s plan name is copied into stack buffers that a long one overruns. In the
   braced form a plan token of 172 bytes or more is cut short by the next token; in the flat form
   a name of 40 characters or more continues with the token after it. CONFIRMED BY ORACLE:
   `errors/plan_name_of_172_braced`, `errors/plan_name_of_40_flat`, and
   `errors/plan_not_found_long_message` (a long message is still complete).

## 4. The flat form

The flat form is the 1996 language ([language.md](language.md) §3 to §7) with these changes:

1. Keywords match **case-insensitively**. CONFIRMED BY ORACLE: `valid/flat_keywords_any_case`.
2. An identifier that names nothing emits nothing (the 1996 error `Bad plan id value` is gone),
   except an unknown `id_MSG_` name, which emits 0xFFFFFFFF. CONFIRMED BY ORACLE:
   `quirks/unknown_identifier_dropped`, `quirks/unknown_message_is_minus_one`.
3. `CHANGE_TO_NEW_BEHAVIOR` resolves a string two tokens after it, like `CHANGE_TO_BEHAVIOR`.
   CONFIRMED BY ORACLE: `valid/change_to_new_behavior_flat`.
4. `Bad number in plan: ` no longer quotes the token: `errors/bad_number`, `errors/bad_number_braced`.
5. Counts may be written as argument enumerators, as the 1998 sources do:
   `valid/flat_counts_as_specials`.

## 5. Symbols

1. The symbol table is built, in order, from the scene's objects (§7), `reservedEnumIDs.h`,
   `enumIDs.h`, `AnimData\Animassm.txt` and `SndFiles.lst`. There is no `Infobtxt.lst`.
   CONFIRMED BY ORACLE: `valid/reserved_ids`, `valid/symbol_lists`, `scene/object_ids`.
2. In each header the loader looks for the word `enum`, then reads every enum body. A token
   starting with `=` skips to the next comma: **the value is ignored** and numbering continues.
   CONFIRMED BY ORACLE: `valid/enum_value_skipped` (`id_TV_x = 5, id_TV_y` gives 0 and 1).
   Instead, in `enumIDs.h`, an enum whose first name has `MSG`, `AGD` or `ACF` at characters 3..5
   starts at 4, 1 or 2 (the reserved counts). CONFIRMED BY ORACLE: `valid/reserved_ids`
   (`id_AGD_Dummy` = 1, `id_ACF_Dummy` = 2, `id_MSG_Ping` = 4), `quirks/enum_value_ignored`.
   One counter serves both headers, and only a `}` resets it, so an enum `reservedEnumIDs.h`
   leaves open carries its numbering into `enumIDs.h` (evidence §6.2).
3. `SndFiles.lst`: the first word of every line that is not a comment is `id_SND_<word>`,
   numbered from 0, numeric or not (1996 skipped numeric words). CONFIRMED BY ORACLE:
   `valid/symbol_lists`, `valid/sound_named_by_number`.
4. Names are compared case-insensitively everywhere, and the first match wins. Only the first 5000
   entries are searched, and the search stops at an entry whose value is -1 (evidence §2, §6.6).
5. Special constants also match case-insensitively: `valid/specials_any_case`.
6. A symbol whose stored name starts with `id_PRN_` emits `0xFEDD0000 | value`; `id_PRT_` emits
   `0xFEDC0000 | value`. CONFIRMED BY ORACLE: `valid/pronoun_tags`, `valid/pronoun_tags_braced`.

Numbers are read as in 1996. CONFIRMED BY ORACLE: `valid/numbers`. A lone `0x` emits a stack
address; adlibc refuses it (kind `undefined`): `errors/hex_without_digits`.

## 6. Strings and names

1. A string's content may be up to 253 characters; the 1996 crash at 40 bytes is gone. CONFIRMED
   BY ORACLE: `valid/long_string`, `valid/string_of_253`. 254 overflows the token buffer (adlibc
   refuses it, kind `crash`): `errors/string_of_254`.
2. Behaviour, local-real and interactor names are matched case-insensitively. CONFIRMED BY ORACLE:
   `valid/behaviour_names_any_case`, `valid/local_reals_and_names`.
3. A string with no closing quote makes the compiler read the stack bytes after the token;
   adlibc refuses it (kind `undefined`): `errors/string_without_closing_quote`.
4. Names are still stored in 32-byte slots without a bounds check; a longer one spills into the
   next slot, and the next declaration then cuts it short. CONFIRMED BY ORACLE:
   `quirks/behaviour_name_overflow`. In `errors/behaviour_name_spill` the 40-character name can
   no longer be found, and the original crashes building the message that says so, in a 64-byte
   buffer (adlibc refuses it, kind `corrupt`).

### 6.1 Declarations

The scan before translation (language.md §7.2) changes:

| keyword | 1999 rule | CONFIRMED BY ORACLE |
|---|---|---|
| `DECLARE_BEHAVIOR` | the next token that starts with `"` is the name; nothing is checked | `valid/flat_minimal`, `errors/behaviour_without_name` (the source ends first: kind `hang`) |
| `SET_MESSAGE_INTERACTOR` | tokens are skipped up to the next `id_MSG_` name, whatever lies between | `quirks/prescan_skips_to_message` |
| `DECLARE_LOCAL_REALS`, `SET_INTERACTOR_PARAM` | as in 1996 | `valid/local_reals_and_names`, `errors/local_reals_need_names`, `errors/local_reals_need_count`, `errors/interactor_name_needs_string`, `errors/interactor_param_needs_id` |

So a `SET_MESSAGE_INTERACTOR` whose message is a number hides every declaration up to the next
`id_MSG_` token. A local-real count that is not a number reads an uninitialised count; adlibc
refuses it (kind `undefined`): `errors/locals_count_not_symbol`. Table sizes are 500 behaviours,
500 local reals and 1200 interactor names.

## 7. Messages and the scene

A scene is what the game's scene compiler records in its `.sbn`: object types in order, each
with its plans and its messages, and every object with its id (`id_MOD_...`), its type and its
collision objects (`id_COB_...` with their ids).

1. The objects' ids (value: the object's number) and collision objects (value: their id) are the
   first symbols. CONFIRMED BY ORACLE: `scene/object_ids`.
2. Message names `id_MSG_Initiate`, `id_MSG_ExitBehavior`, `id_MSG_CellAnimationDone` and
   `id_MSG_SyncFrame` (0..3) come from `reservedEnumIDs.h`. Other `id_MSG_` names come from the
   message list of the type of the object whose plan this is: position + 4. The main table is
   searched first. CONFIRMED BY ORACLE: `scene/own_messages`, `scene/other_object`.
3. `DO_ACTION` with `id_ACF_SendMessage` or `id_ACF_SendMsgImmediate` (in either form): when the
   next token, the target, is an identifier naming an object, the message after it is looked up in
   that object's type's list. The plan's own list comes back only when the message is an
   identifier; after a numeric message the target's list stays in force. CONFIRMED BY ORACLE:
   `scene/send_switches_table`, `scene/send_braced`. With a scene, a target that names no object
   makes the original read a null object; adlibc refuses it (kind `crash`):
   `scene/send_to_missing_object`. Without one, every target leaves an empty list (item 7).
4. When the target is `_BROADCAST_`, `_MESSAGE_DATA_` or an `id_PRN_`/`id_PRT_` name, the next
   identifier is not emitted. In its place goes `DataBlock` with one value per object type: the
   message's index in that type's list, or -1. The name is first looked up as usual, so one that
   is not in the main table or the current object's list is -1 in every type. CONFIRMED BY
   ORACLE: `scene/broadcast_indices` (a DataBlock of 4 and 5), `scene/broadcast`,
   `quirks/send_broadcast_flat`, `quirks/send_pronoun_braced`. The lookup the original uses for
   each type (0x6096A0) is a hash table; the emulation replaces it, so the 1998 data, whose
   broadcasts all rebuild, is its evidence.
5. In the flat form that replacement stays on until an identifier arrives as the send's second
   token. When the message is a number, every later identifier becomes a DataBlock. CONFIRMED BY
   ORACLE: `quirks/send_broadcast_flag_outlives`.
6. The game builds the scene from a stadium's object definitions and the plan sources
   ([dialects.md](dialects.md) §3); `buildScene` does the same, checked against the 30 scenes of
   Baseball 3D 1998.
7. Without a scene there are no objects or types: the only messages are those in the symbol
   files (the reserved four, and any `enumIDs.h` names), and a broadcast DataBlock has no values:
   `quirks/send_broadcast_flat`.
8. With a scene, a plan must belong to one of its objects; otherwise the original would take its
   message list from a null object (adlibc refuses it, kind `crash`).

## 8. The PBN produced

As in 1996: `u32 n`, then the dwords up to and including the first 0xFFFF0000, where `n` counts
every dword emitted. CONFIRMED BY ORACLE: `quirks/dwords_after_end`, `errors/no_end_plan`.

## 9. Diagnostics

The 1999 compiler's messages, verbatim (several end in a newline):

| message | CONFIRMED BY ORACLE |
|---|---|
| `This plan file: <name> is too large.  Why not break it up into separate plans?` | (evidence §7.6) |
| `This Plan file could not be opened: <path>` | `errors/no_such_plan` |
| `Aborting Plan Parse.` | `errors/abort_*` |
| `Expected an argument enumerator.` | `errors/interactor_param_needs_id` |
| `Expected an argument enumerator.  First two args are id and string name.` | `errors/local_reals_need_count` |
| `Expected a string name in Behavior Declaration.` | `errors/interactor_name_needs_string` |
| `Expected a string name in LocalVar Declaration.` | `errors/local_reals_need_names` |
| `expected arg enumeration of localvars.` | (evidence §7.3) |
| `Bad number in plan: ` | `errors/bad_number` |
| `Inability to read string: <token>` | (reached only through stale stack bytes, §6, which adlibc refuses) |
| `Could not find behavior named: <name>` | `errors/no_such_behavior`, `errors/no_such_behavior_braced` |
| `Could not find interactor named: <name>` | `errors/no_such_interactor` |

The position reported is the compiler's cursor, as in 1996. For a source that is too large the
cursor has not been reset yet: the original reports where it stopped in the symbol files, adlibc
byte 0.

## 10. Between compiles

Each compile starts as the first plan of a scene build does. In a real scene build the original
keeps the flat form's counters and the broadcast flag from the plan before, so a plan ending in
`... id_ACF_SendMessage _BROADCAST_ END_PLAN` changes the next plan; adlibc does not model that.
