// adlibc: the ADLIB Plans compiler (text source -> .PBN), reproducing the 1996 ADLIB compiler
// (as shipped inside HyperBlade's HYPERX.EXE, CPlanCompiler 0x487790..0x48a900) byte for byte.
//
// The original is not a parser but a flat token translator: every token is translated on its own
// (keyword -> 0xABCDEFFF id, number -> dword, string -> 0xABCDEFFE chars 0, identifier -> symbol
// value), the author writes every child count, and five positional counters decide which strings
// are behaviour references. adlibc emulates that machine, including its fixed-size buffers, so
// that malformed input produces the same bytes or the same diagnostic as the original. Inputs on
// which the original crashes or corrupts memory are rejected with a clear error instead.
// See docs/adlibc.md (usage, quirks policy) and docs/language.md (language).
#pragma once
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace adlib {

class Vocabulary;
enum class Compat : uint8_t;
struct CompilerScene;

// ---- symbol table ----------------------------------------------------------------------------
// The original's symbol table is one flat list of (name, value) entries searched front to back
// with _stricmp; the first match wins. It is built from four text files in a fixed order:
//   enumIDs.h (every token inside `{ }` is a symbol, value = position in its enum),
//   AnimData\Animassm.txt (`AA_*` tokens), Sndfiles.lst (`id_SND_<word>`), Infobtxt.lst
//   (`id_TXT_<word>`).
struct SymbolFiles {
    std::optional<std::string> reservedEnumIds; // Data\reservedEnumIDs.h (1998 on, read first)
    std::optional<std::string> enumIds;  // Data\enumIDs.h
    std::optional<std::string> animAssm; // Data\AnimData\Animassm.txt
    std::optional<std::string> sndFiles; // Data\Sndfiles.lst
    std::optional<std::string> infobTxt; // Data\Infobtxt.lst
};

struct CompilerSymbol {
    std::string name;  // as stored (the original truncates nothing: long names overflow, see below)
    int32_t value = 0;
    std::string group; // enum name ("ID_MSG", ...) or "Animassm", "Sndfiles", "Infobtxt"
};

class CompilerSymbols {
public:
    CompilerSymbols();
    ~CompilerSymbols();
    CompilerSymbols(const CompilerSymbols &);
    CompilerSymbols &operator=(const CompilerSymbols &);
    CompilerSymbols(CompilerSymbols &&) noexcept;
    CompilerSymbols &operator=(CompilerSymbols &&) noexcept;

    // Emulation of CPlanCompiler_LoadSymbols (0x48a240), quirks included. A missing file is
    // skipped (treated as an empty list).
    // originalLimits = true (--compat=adlib1996): input on which the original loader
    // misbehaves (a token of 40+ bytes overwrites its counters) throws std::runtime_error, and
    // names longer than 35 characters are mangled exactly as the original's 36-byte name field
    // mangles them. false (new games): such input is accepted as written and reported in
    // loadWarnings().
    static CompilerSymbols fromFiles(const SymbolFiles &files, bool originalLimits = false);
    // The loader of `compat`'s compiler. Adlib1998/Adlib1999: CPlanCompiler LoadSymbols 0x612310
    // of 1999 (reservedEnumIDs.h then enumIDs.h, '=' values skipped, MSG/AGD/ACF enums starting at
    // 4/1/2, then Animassm.txt and SndFiles.lst; no Infobtxt.lst). Both headers are required.
    // Input the 1999 loader would hang or crash on throws std::runtime_error.
    // A scene's object and collision-object ids come first (the 1999 loader's order).
    static CompilerSymbols fromFiles(const SymbolFiles &files, Compat compat, const CompilerScene *scene = nullptr);
    // Reads the four files from a data directory in the 1996 layout (case-insensitive names):
    // <dir>/enumIDs.h, <dir>/AnimData/Animassm.txt, <dir>/Sndfiles.lst, <dir>/Infobtxt.lst.
    static CompilerSymbols fromDataDir(const std::string &dir, std::string *err = nullptr,
                                       bool originalLimits = false);
    // The same in `compat`'s layout. 1998 on: <dir>/reservedEnumIDs.h, <dir>/enumIDs.h,
    // <dir>/AnimData/Animassm.txt, <dir>/SndFiles.lst. Missing headers are reported in `err`
    // (or thrown when it is null); input the 1999 loader cannot survive throws std::runtime_error.
    static CompilerSymbols fromDataDir(const std::string &dir, Compat compat, std::string *err = nullptr,
                                       const CompilerScene *scene = nullptr);
    // Symbols from a libadlib Vocabulary (new games): every namespace in creation order, each in
    // id order, with the namespace name as the group. Equivalent to fromFiles() when the
    // vocabulary was loaded from the same well-formed headers and lists.
    static CompilerSymbols fromVocabulary(const Vocabulary &vocab);

    const std::vector<CompilerSymbol> &symbols() const { return syms_; }
    // Things in the symbol files the original compiler would have mishandled (new-game mode).
    const std::vector<std::string> &loadWarnings() const { return warnings_; }
    // _stricmp first match, like CPlanCompiler_LookupSymbol (symbol table part only).
    const CompilerSymbol *find(std::string_view name) const;

    struct Image; // emulated compiler object after LoadSymbols (opaque)
    const Image &image() const { return *img_; }

private:
    std::vector<CompilerSymbol> syms_;
    Image *img_ = nullptr;
    std::vector<std::string> roleGroups_; // per adlib::Role: the group (enum) it expects
    std::vector<std::string> warnings_;
    friend struct SymbolsBuilder;
public:
    // Group name expected for a role (Role::Message -> "ID_MSG" ...), used by --strict.
    const std::string &roleGroup(int role) const;
};

// ---- diagnostics -------------------------------------------------------------------------------
struct Diagnostic {
    enum class Severity : uint8_t { Error, Warning, Note };
    Severity severity = Severity::Error;
    // Errors in the original's terms: stage = read | compile | write, kind = throw | no-end, or
    // (adlibc only) kind = crash | corrupt | undefined for inputs the original cannot survive.
    std::string stage, kind;
    std::string message;          // the original's text verbatim (may end in '\n')
    std::optional<int32_t> offset; // the original's cursor (just after the failing token)
    std::optional<int32_t> line;   // 1 + number of '\n' before offset
    std::string code;              // strict warnings: a short id ("dropped-word", ...)
    int32_t tokenOffset = -1;      // strict warnings: start of the token concerned

    // `file:line: error [stage/kind] (byte N): message` (the oracle's format), or for warnings
    // `file:line: warning: message [code]`.
    std::string format(const std::string &file, bool stripMessage = true) const;
};

// ---- compiling ---------------------------------------------------------------------------------
// Compatibility mode (docs/adlibc.md):
//   Adlib       generic ADLIB with any vocabulary: the 1996 language exactly (same tokenizer,
//               translation, positional rules and diagnostics), with the 1996 implementation's
//               buffer and table limits lifted. Identical bytes for every input within them.
//   Adlib1996   the 1996 ADLIB compiler exactly, including its fixed limits (table sizes, 40/44-
//               byte token buffers, source and output caps). Input on which it would crash, hang,
//               write past its memory or read uninitialised memory is an error; input it accepts
//               but mangles is reproduced byte for byte with a warning. Any vocabulary.
//   Adlib1999   the 1999 ADLIB compiler (inside Baseball 2000's executable) exactly: braced
//               blocks with computed counts, the flat 1996 form as a fallback, case-insensitive
//               keywords and names, id_PRN_ 0xFEDD and id_PRT_ 0xFEDC, reservedEnumIDs.h. Input
//               on which it crashes, hangs or reads uninitialised memory is an error.
//   Adlib1998   the language of the 1998 games (Baseball 3D), whose compiler did not survive:
//               Adlib1999 without SET_AGENDA_ITEM_PARAM (docs/dialects.md).
enum class Compat : uint8_t { Adlib, Adlib1996, Adlib1998, Adlib1999 };
// "adlib", "adlib1996", "adlib1998", "adlib1999" <-> Compat. False for an unknown name.
bool parseCompat(std::string_view name, Compat &out);
const char *compatName(Compat c);
// The 1998 and 1999 compilers (one implementation, docs/dialects.md).
constexpr bool isLaterCompat(Compat c) { return c == Compat::Adlib1998 || c == Compat::Adlib1999; }
// The 1999 compiler's lookups search this many main-table symbols, and it stores each name in a
// field this wide: a name that fills it has no terminator, and no source can name it.
constexpr size_t kSymbolLimit1999 = 5000;
constexpr size_t kNameField1999 = 36;
// The deepest nesting adlibc accepts from 1998 on: the 1999 compiler's stack holds about 1240
// levels. Anything a compile accepts, the decompiler writes back.
constexpr int kMaxNesting1999 = 1200;

// The scene a 1998/1999 plan is compiled for (what the game's .sbn records). Object types in
// scene order, each with the messages its plans use: an id_MSG_ name compiles to its position + 4
// in the list of the object whose plan it is, or of the object a SendMessage targets. A
// SendMessage to _BROADCAST_, _MESSAGE_DATA_ or a pronoun compiles its message as a DataBlock
// with the message's index in every type. The objects also name symbols, ahead of enumIDs.h:
// each object's id (value: its index) and its collision objects (value: their id).
// Without a scene there are no types and no objects.
struct CompilerScene {
    struct ObjectType {
        std::string name;
        std::vector<std::string> plans;
        std::vector<std::string> messages;
    };
    struct Object {
        std::string id;      // id_MOD_...
        int16_t type = 0;    // index into types
        std::vector<std::pair<std::string, int16_t>> collisionObjects; // at most 8
    };
    std::vector<ObjectType> types;
    std::vector<Object> objects; // index = the object's number
    // The object the scene compiles `plan` for: the first one whose type lists it, or -1.
    int16_t objectFor(std::string_view plan) const;
};
// Reads the text of a .sbn (the game's compiled scene, written by its scene compiler 0x609F60):
// the object types with their plans and messages, then every object with its id, type and
// collision objects. False and `err` on text that is not a scene.
bool parseScene(std::string_view sbn, CompilerScene &out, std::string *err = nullptr);
// Builds a scene from its sources, as the game's scene compiler (0x609F60) does before it writes
// the .sbn: the objects of a stadium's .cfg (`DefineObject ... type = T ... id = I`, `SetPlans`,
// `SetCollisionObjs ... colObjID = C`) in order, collision objects numbered across the scene;
// object types in order of first use, each with its objects' plans in order of first listing;
// each type's messages the id_MSG_ names its plans' sources use that `symbols` lacks, in order
// of first use. `planSource` returns a plan's source text. False and `err` when a plan's source
// is missing or the .cfg is malformed.
bool buildScene(std::string_view cfg, const CompilerSymbols &symbols,
                const std::function<std::optional<std::string>(const std::string &plan)> &planSource,
                CompilerScene &out, std::string *err = nullptr);

struct CompileOptions {
    Compat compat = Compat::Adlib;
    // CHANGE_OF_PLAN "X" "Beh" makes the original compile Data\Plans\X.txt to look the behaviour
    // up. Return the source text of plan `name` (match the name case-insensitively, like the
    // original's file system), or nullopt when it does not exist.
    std::function<std::optional<std::string>(const std::string &name)> planSource;
    bool strict = false; // add warnings for historically accepted hazards (output unchanged)
    bool werror = false; // strict warnings fail the compile
    // Adlib1998/Adlib1999 only.
    const CompilerScene *scene = nullptr;
    int16_t object = -1;        // the object `scene` compiles this plan for (objectFor); -1: none
    std::string planName = "plan"; // named by the original's "too large" message
};

struct CompileResult {
    bool ok = false;               // a PBN was produced (and no -Werror warning)
    std::vector<uint8_t> pbn;      // the file the original would write
    std::vector<uint32_t> words;   // every dword emitted (pbn header = words.size())
    std::vector<Diagnostic> diagnostics;
    const Diagnostic *error() const; // first Error, or nullptr
};

CompileResult compilePlan(std::string_view source, const CompilerSymbols &symbols,
                          const CompileOptions &opt = {});

// Built-in tables of the original (for documentation and tools).
struct CompilerKeyword { const char *name; uint32_t id; };
const std::vector<CompilerKeyword> &compilerKeywords();        // 27 entries, table order
const std::vector<CompilerKeyword> &compilerKeywords(Compat c); // that compiler's table
const std::vector<std::pair<std::string, uint32_t>> &compilerSpecials(); // _BROADCAST_ ... _50_

} // namespace adlib
