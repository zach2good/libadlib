// The 1999 ADLIB compiler (Baseball 2000, CPlanCompiler at 0x60EC90..0x6143E0) and the 1998
// dialect, which is the same compiler without SET_AGENDA_ITEM_PARAM (docs/dialects.md).
//
// Compile (0x60FB00) runs the prescan (0x610B60), then calls the braced emitter (0x60FB80) until
// the source ends. The braced emitter compiles one `{KEYWORD ...}` block and computes its counts;
// the first time it scans to the end of the source without meeting a brace it switches for good
// to the flat emitter (0x6110D0), which is the 1996 translator with case-insensitive lookups. The
// addresses and the evidence are in docs/research/binary-evidence-1999.md.
//
// This is a model of the compiler's behaviour, not of its memory. Only the name tables, whose
// overflow changes output, are kept as bytes. Input on which the original crashes, hangs, writes
// past its object or reads memory nothing in the compile wrote is an error (a Hazard).
#include <algorithm>
#include <array>
#include <cctype>
#include <functional>
#include <optional>
#include <string_view>
#include <cstring>
#include <cstdlib>
#include <map>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

#include "adlib/vocabulary.h"
#include "internal.h"
#include "machine.h"

namespace adlib {

using namespace cc;

namespace {

constexpr int32_t kLaterMaxSource = 399998; // ReadFile(400000); 399999 or more is "too large"
constexpr int32_t kLaterOutCap = 300000;    // new(1200000), unchecked
constexpr int32_t kTokenCap = 256;          // every token buffer is char[256]
constexpr int32_t kNameRegion = 70400;      // behaviour, local-real and interactor slots, 32 bytes each
constexpr int32_t kLocalBase = 16000;
constexpr int32_t kIntBase = 32000;
constexpr int32_t kDevTableBytes = 500 * 20 * 32;
constexpr int32_t kMainSymbols = int32_t(kSymbolLimit1999);
// The braced emitter recurses once per block level and once per comment between statements; its
// 1 MB stack holds about 1240 frames, less what the game has used (binary-evidence-1999.md §12).
constexpr int kMaxBraceDepth = kMaxNesting1999;

constexpr const char *kMsgAbort = "Aborting Plan Parse.\n";
constexpr const char *kMsgBadNumber1999 = "Bad number in plan: ";
constexpr const char *kMsgArgEnum1999 = "Expected an argument enumerator.";
constexpr const char *kMsgBehName1999 = "Expected a string name in Behavior Declaration.\n";
constexpr const char *kMsgLocalName1999 = "Expected a string name in LocalVar Declaration.\n";
constexpr const char *kMsgArgEnumLocals1999 =
    "Expected an argument enumerator.  First two args are id and string name.\n";
constexpr const char *kMsgLocalsEnum1999 = "expected arg enumeration of localvars.";
constexpr const char *kMsgNoBehavior1999 = "Could not find behavior named: ";
constexpr const char *kMsgNoInteractor1999 = "Could not find interactor named: ";
constexpr const char *kMsgCannotOpen1999 = "This Plan file could not be opened: ";
constexpr const char *kPlansDir = "Data\\Plans\\"; // the path the original's message shows

const CompilerKeyword kKeywords1999[] = {
    {"SET_COLLISION_INTERACTOR", 0x30000}, {"SET_MESSAGE_INTERACTOR", 0x50000},
    {"ADD_AGENDA_ITEM", 0x1a0001}, {"REMOVE_AGENDA_ITEM", 0x1a0003}, {"SET_AGENDA_ITEM_PARAM", 0x1a0004},
    {"SET_TIMEOUTMSG", 0x1a0002}, {"GLOBAL_INTERACTORS", 0x1b0000}, {"CHANGE_OF_PLAN", 0x160000},
    {"CHANGE_TO_BEHAVIOR", 0x70002}, {"CHANGE_TO_NEW_BEHAVIOR", 0x70005}, {"DECLARE_BEHAVIOR", 0x70000},
    {"DECLARE_LOCAL_REALS", 0x160001}, {"SET_BEHAVIOR_PARAM", 0x70003}, {"SET_INTERACTOR_PARAM", 0x180001},
    {"DO_ACTION", 0x180000}, {"DECIDE_BY_AMONG", 0x170000}, {"SET_DEBUG", 0x60001}, {"SET_TRACE", 0x60002},
    {"Enable", 0x80000000u}, {"Block", 0x40000000}, {"DataBlock", 0x20000000}, {"END_PLAN", 0xffff0000u},
};

bool prefixed(const std::string &s, const char *p) { return s.compare(0, std::strlen(p), p) == 0; }
bool isPRN(const std::string &s) { return prefixed(s, "id_PRN_"); }
bool isPRT(const std::string &s) { return prefixed(s, "id_PRT_"); }
bool isMSG(const std::string &s) { return prefixed(s, "id_MSG_"); }
bool isIdentifier1999(const std::string &b) { // 0x610F20
    auto c = [&](size_t i) { return i < b.size() ? b[i] : '\0'; };
    return (c(0) == 'i' && c(1) == 'd') || c(0) == '_' ||
           (c(0) == 'A' && (c(1) == '_' || (c(1) == 'A' && c(2) == '_'))) ||
           (c(0) == 'B' && c(1) == 'C' && c(2) == '_');
}
bool ieq(const std::string &a, const char *b) { return msvcStricmp(a.c_str(), b) == 0; }
// 0x6143E0: drop the last character, then the first.
std::string strip(const std::string &s) {
    if (s.empty()) throw Hazard{"undefined", "an empty token is stripped of its quotes; the original writes before its buffer"};
    return s.size() >= 2 ? s.substr(1, s.size() - 2) : std::string();
}

// The source buffer and the tokenizer shared by the compiler and the symbol loader.
struct Reader {
    std::string src; // the file; byteAt(size) is the NUL ReadSource writes
    int32_t size = 0, pos = 0;
    bool ranPastEnd = false;

    uint8_t byteAt(int64_t p) const { return p >= 0 && p < size ? uint8_t(src[size_t(p)]) : 0; }
    uint8_t cur() const { return byteAt(pos); }
    bool atEnd() const { return size < pos || cur() == 0; }
    bool isSep() const { uint8_t c = cur(); return c == ' ' || c == '\t' || c == ',' || c == '&'; }
    bool isComment() const { // 0x610EF0
        uint8_t c = cur();
        return (c == '/' && (byteAt(int64_t(pos) + 1) == '/' || byteAt(int64_t(pos) + 1) == '*')) || c == '(' || c == '#';
    }
    void skipSeps() { while (isSep()) ++pos; }
    // 0x611C40. The original scans on past the end of the source into its buffer's earlier
    // contents; that only moves the cursor further past the end, so the end stands in for it.
    void skipLine() {
        if (size < pos || cur() == 0) return;
        auto toEol = [&]() {
            while (cur() != '\r' && cur() != '\n') {
                if (pos >= size) { ranPastEnd = true; return; }
                ++pos;
            }
        };
        toEol();
        if (cur() == '\r') ++pos;
        toEol();
        if (cur() == '\n') ++pos;
    }
    // 0x611AB0: the next token into `buf`; returns it.
    std::string nextToken(std::array<uint8_t, kTokenCap> &buf) {
        std::string tok;
        auto put = [&](size_t i, uint8_t c) {
            if (i >= size_t(kTokenCap))
                throw Hazard{"crash", "a token of 256 or more bytes overflows the original's 256-byte token buffer"};
            buf[i] = c;
        };
        skipSeps();
        for (int guard = 0;; ++guard) {
            if (guard > size + 2) throw Hazard{"hang", "tokenizer made no progress"};
            bool nl = false, open = false, close = false, comment = false;
            for (;;) {
                if (isSep()) break;
                if (isComment()) { comment = true; break; }
                uint8_t c = cur();
                if (c == '\r' || c == '\n') { nl = true; break; }
                open = c == '{';
                if (!open) close = c == '}';
                if (open || close || size < pos || c == 0) break;
                put(tok.size(), c);
                tok.push_back(char(c));
                ++pos;
            }
            if (!nl && !comment && !open && !close && !(size < pos || cur() == 0)) skipSeps();
            if (nl || comment) skipLine();
            if (close && tok.empty()) {
                put(0, cur());
                ++pos;
                put(1, 0);
                return "}";
            }
            if (open) ++pos;
            if (size < pos || cur() == 0 || !tok.empty()) {
                put(tok.size(), 0);
                return tok;
            }
        }
    }
    void read(std::string_view text, const std::string &name, bool forSymbols) {
        if (int64_t(text.size()) > kLaterMaxSource) {
            std::string msg = "This plan file: " + name + " is too large.  Why not break it up into separate plans?\n";
            if (forSymbols) throw std::runtime_error(msg);
            throw Thrown{msg};
        }
        src.assign(text.data(), text.size());
        size = int32_t(text.size());
        pos = 0;
        ranPastEnd = false;
    }
};

// One main-table entry as the original stores it: strncpy(name, 36), then the value. A name of 36
// or more characters has no terminator, so lookups see it run on into the value's bytes.
struct Entry {
    std::string name; // as lookups see it
    int32_t value = -1;
};

std::string storedName(const std::vector<CompilerSymbol> &syms, size_t k) {
    std::string out;
    for (size_t i = k; i < syms.size(); ++i) { // a value with no zero byte runs into the next name
        const std::string &n = syms[i].name;
        if (n.size() < kNameField1999) return out + n;
        out += n.substr(0, kNameField1999);
        uint32_t v = uint32_t(syms[i].value);
        for (int b = 0; b < 4; ++b) {
            char c = char((v >> (8 * b)) & 0xFF);
            if (!c) return out;
            out.push_back(c);
        }
    }
    return out;
}

} // namespace

// ---- the 1999 symbol loader (0x612310) ----------------------------------------------------------

namespace cc {

std::vector<CompilerSymbol> loadSymbols1999(const SymbolFiles &files, const CompilerScene *scene,
                                           std::vector<std::string> &warnings) {
    std::vector<CompilerSymbol> out;
    Reader r;
    std::array<uint8_t, kTokenCap> buf{};
    // Entries past the table's 5000 run on through the message table (which lookups never
    // reach past its own end) and the message-interactor table, then off the object.
    auto add = [&](const std::string &name, int32_t value, const std::string &group) {
        if (int(out.size()) >= kMainSymbols + 5000 + 8000 - 1)
            throw std::runtime_error("more than 17999 symbols: the 1999 loader writes past its object");
        out.push_back({name, value, group});
    };
    auto tok = [&]() {
        try {
            return r.nextToken(buf);
        } catch (const Hazard &h) {
            throw std::runtime_error("symbol files: " + h.message);
        }
    };
    // The scene's objects first: each object's id, then its collision objects.
    if (scene) {
        for (size_t i = 0; i < scene->objects.size(); ++i) {
            const CompilerScene::Object &o = scene->objects[i];
            if (!o.id.empty()) add(o.id, int32_t(i), "ID_MOD");
            if (o.collisionObjects.size() > 8)
                throw std::runtime_error("object " + o.id + " has more than 8 collision objects");
            for (const auto &[name, id] : o.collisionObjects) add(name, id, "CollisionObjectID");
        }
    }
    const std::optional<std::string> *headers[2] = {&files.reservedEnumIds, &files.enumIds};
    int16_t idx = 0; // one counter for both files: only '}' resets it
    const char *names[2] = {"reservedEnumIDs.h", "enumIDs.h"};
    for (int f = 0; f < 2; ++f) {
        if (!*headers[f])
            throw std::runtime_error(std::string("no ") + names[f] + ": the 1999 loader would scan a stale buffer for it");
        r.read(**headers[f], "enumIDs", true);
        // Tokens up to "enum", then raw characters up to '{'. Neither loop stops at the end.
        for (;;) {
            if (r.atEnd()) throw std::runtime_error(std::string(names[f]) + " has no 'enum': the 1999 loader would hang");
            if (ieq(tok(), "enum")) break;
        }
        while (r.cur() != '{') {
            if (r.pos >= r.size) throw std::runtime_error(std::string(names[f]) + " has no '{': the 1999 loader would read past it");
            ++r.pos;
        }
        auto enumName = [&]() { // the word before '{', for libadlib's role bindings only
            int32_t e = r.pos;
            while (e > 0 && std::isspace(r.byteAt(e - 1))) --e;
            int32_t b = e;
            while (b > 0 && (std::isalnum(r.byteAt(b - 1)) || r.byteAt(b - 1) == '_')) --b;
            return r.src.substr(size_t(b), size_t(e - b));
        };
        std::string group = enumName();
        ++r.pos;
        for (int guard = 0; guard <= r.size + 1; ++guard) {
            if (r.atEnd()) break;
            std::string t = tok();
            if (buf[0] == '=') { // the value is skipped: numbering continues
                if (r.cur() != ',') {
                    for (;;) {
                        int32_t i = r.pos;
                        if (r.size < i || r.byteAt(i) == 0) break;
                        r.pos = i + 1;
                        if (r.byteAt(i + 1) == ',') break;
                    }
                }
                continue;
            }
            if (f == 1 && idx == 0) { // the reserved counts, hard-coded
                if (buf[3] == 'M') {
                    if (buf[4] == 'S' && buf[5] == 'G') idx = 4;
                } else if (buf[3] == 'A') {
                    if (buf[4] == 'G') {
                        if (buf[5] == 'D') idx = 1;
                    } else if (buf[4] == 'C' && buf[5] == 'F') {
                        idx = 2;
                    }
                }
            }
            if (buf[0] != '}') {
                add(t, idx++, group);
                continue;
            }
            idx = 0;
            if (r.cur() != '{') {
                for (;;) {
                    int32_t i = r.pos;
                    if (r.size < i || r.byteAt(i) == 0) break;
                    r.pos = i + 1;
                    if (r.byteAt(i + 1) == '{') break;
                }
            }
            if (r.cur() == '{') group = enumName();
            ++r.pos;
        }
        if (r.ranPastEnd) warnings.push_back(std::string(names[f]) + ": the last line has no line end");
    }
    if (files.animAssm) {
        r.read(*files.animAssm, "Animassm", true);
        int16_t n = 0; // its own counter, as in the original
        bool done = false;
        for (int guard = 0; !done && guard <= r.size + 1; ++guard) {
            r.skipLine();
            for (;;) {
                if (r.atEnd()) { done = true; break; }
                std::string t = tok();
                if (!prefixed(t, "AA_")) break;
                add(t, n++, "Animassm");
            }
        }
    }
    if (files.sndFiles) {
        r.read(*files.sndFiles, "SndFiles", true);
        int16_t n = 0;
        for (int guard = 0; r.pos <= r.size && r.cur() != 0 && guard <= r.size + 1; ++guard) {
            if (r.isComment()) r.skipLine();
            std::string t = tok();
            if (t.size() + 7 >= size_t(kTokenCap))
                throw std::runtime_error("SndFiles.lst: a name of 249 or more bytes overflows the loader's buffer");
            add("id_SND_" + t, n++, "Sndfiles");
            r.skipLine();
        }
    }
    if (out.size() > size_t(kMainSymbols))
        warnings.push_back(std::to_string(out.size()) + " symbols: lookups see only the first " +
                           std::to_string(kMainSymbols) + ", from " + out[size_t(kMainSymbols)].name + " on nothing compiles");
    for (size_t i = 0; i < out.size(); ++i)
        if (out[i].name.size() >= kNameField1999)
            warnings.push_back("symbol " + out[i].name + " is 36 characters or longer: the 1999 compiler stores "
                               "it without a terminator, so plans cannot name it");
    return out;
}

} // namespace cc

// ---- the compiler -------------------------------------------------------------------------------

namespace {

class Later {
public:
    Later(const CompilerSymbols &syms, const CompileOptions &opt, std::string_view source)
        : syms_(syms), opt_(opt), source_(source), has1a0004_(opt.compat == Compat::Adlib1999) {}
    CompileResult run();

private:
    using Buf = std::array<uint8_t, kTokenCap>;
    struct Names { // behaviour, local-real and interactor slots: char[500][32], [500][32], [1200][32]
        std::vector<uint8_t> bytes = std::vector<uint8_t>(size_t(kNameRegion), 0);
        void clear() { // ReadSource: the first byte of every slot
            for (int32_t i = 0; i < kNameRegion; i += 32) bytes[size_t(i)] = 0;
        }
        void put(int32_t off, const std::string &s, const char *what) {
            if (off + int32_t(s.size()) + 1 > kNameRegion)
                throw Hazard{"corrupt", std::string("the original would write past its name tables into its keyword table: ") + what};
            std::memcpy(&bytes[size_t(off)], s.c_str(), s.size() + 1);
        }
        std::string at(int32_t off) const {
            std::string s;
            for (int32_t i = off; i < kNameRegion && bytes[size_t(i)]; ++i) s.push_back(char(bytes[size_t(i)]));
            return s;
        }
        int find(int32_t base, int n, const std::string &s) const { // 0x611D70 / 0x611E40
            for (int k = 0; k < n; ++k)
                if (msvcStricmp(at(base + k * 32).c_str(), s.c_str()) == 0) return k;
            return -1;
        }
        int16_t behavior(const std::string &name) const { // 0x612150
            for (int32_t k = 0;; ++k) {
                if (k * 32 >= kNameRegion) throw Hazard{"crash", "behaviour lookup runs past the original's name tables"};
                std::string slot = at(k * 32);
                if (msvcStricmp(slot.c_str(), name.c_str()) == 0) return int16_t(k);
                if (slot.empty()) break;
            }
            if (name.size() > 32)
                throw Hazard{"corrupt", "the original builds \"" + std::string(kMsgNoBehavior1999) + name + "\" in a 64-byte stack buffer"};
            throw Thrown{std::string(kMsgNoBehavior1999) + name};
        }
        int16_t interactor(const std::string &name) const { // 0x612210
            int k = find(kIntBase, 1200, name);
            if (k >= 0) return int16_t(k);
            if (name.size() > 30)
                throw Hazard{"corrupt", "the original builds \"" + std::string(kMsgNoInteractor1999) + name + "\" in a 64-byte stack buffer"};
            throw Thrown{std::string(kMsgNoInteractor1999) + name};
        }
    };
    struct Unit { // one compiler object: its source, cursor and name tables
        Reader r;
        Names names;
    };

    const CompilerSymbols &syms_;
    const CompileOptions &opt_;
    std::string_view source_;
    bool has1a0004_;
    Unit u_;
    std::vector<uint32_t> out_;
    int32_t count_ = 0;
    bool flatMode_ = false;
    // Flat-emitter state: seven counters (initial 100), the message-DataBlock flag, and the stack
    // buffer that holds CHANGE_OF_PLAN's plan name (also every identifier's table entry).
    int32_t beh_ = 100, ctb_ = 100, ctnb_ = 100, cop_ = 100, sip_ = 100, dlr_ = 100, send_ = 100;
    bool msgFlag_ = false;
    std::optional<std::string> flatPlanName_;
    bool flatPlanFromLongName_ = false;
    Buf flatBuf_{}, braceBuf_{};
    std::vector<Entry> main_;
    std::unordered_map<std::string, size_t> mainIndex_; // lower-case stored name -> first entry
    std::unordered_map<std::string, uint32_t> specials_;
    std::vector<Entry> messages_; // the per-object table (its own type, or a send's target)
    std::unordered_map<std::string, std::optional<std::string>> planCache_;
    std::vector<Diagnostic> warnings_;

    // ---- output ----
    void put(int32_t idx, uint32_t v) {
        if (idx >= kLaterOutCap)
            throw Hazard{"corrupt", "the plan emits more than 300000 dwords; the original writes past its output buffer"};
        if (size_t(idx) >= out_.size()) out_.resize(size_t(idx) + 256, 0);
        out_[size_t(idx)] = v;
    }
    void emit(uint32_t v) { put(count_, v); ++count_; }
    int32_t reserve() { put(count_, 0); return count_++; }
    void header(const CompilerKeyword &k) {
        emit(0xABCDEFFF);
        emit(k.id);
    }
    // 0x611910: 0xABCDEFFE, the characters after the first up to the next quote ('_' is a
    // space), 0. Past the token's end the original reads stale stack bytes.
    void emitString(const std::string &tok) {
        size_t q = tok.find('"', 1);
        if (tok.size() < 1 || q == std::string::npos)
            throw Hazard{"undefined", "string " + tok + " has no closing quote; the original reads on into stale stack bytes"};
        emit(0xABCDEFFE);
        for (size_t i = 1; i < q; ++i) emit(uint32_t(int32_t(int8_t(tok[i] == '_' ? ' ' : tok[i]))));
        emit(0);
    }
    uint32_t number(const std::string &tok) {
        uint32_t v = 0;
        bool undef = false;
        if (!msvcParsePlanNumber(tok, v, &undef)) throw Thrown{kMsgBadNumber1999};
        if (undef) throw Hazard{"undefined", "'" + tok + "': sscanf(\"%x\") finds no digits and the original emits a stack address"};
        return v;
    }

    // ---- tables ----
    const CompilerKeyword &keyword(const std::string &tok) const { // 0x611F10
        static const CompilerKeyword none{"", 0xFFFFFFFF};
        for (const CompilerKeyword &k : kKeywords1999) {
            if (k.id == 0x1a0004 && !has1a0004_) continue;
            if (msvcStricmp(tok.c_str(), k.name) == 0) return k;
        }
        return none;
    }
    Entry lookup(const std::string &tok) const { // 0x611FA0: specials, main table, messages
        std::string key = lowerAscii(tok);
        for (const auto &s : compilerSpecials())
            if (msvcStricmp(tok.c_str(), s.first.c_str()) == 0) return {s.first, int32_t(s.second)};
        auto it = mainIndex_.find(key);
        if (it != mainIndex_.end()) return main_[it->second];
        for (const Entry &e : messages_)
            if (msvcStricmp(tok.c_str(), e.name.c_str()) == 0) return e;
        return {"", -1}; // the message table's terminator
    }
    static uint32_t tagged(const Entry &e) {
        if (isPRN(e.name)) return uint32_t(e.value) | 0xFEDD0000u;
        if (isPRT(e.name)) return uint32_t(e.value) | 0xFEDC0000u;
        return uint32_t(e.value);
    }
    static bool isSendEntry(const Entry &e) { // 0x611090
        return ieq(e.name, "id_ACF_SendMessage") || ieq(e.name, "id_ACF_SendMsgImmediate");
    }
    // 0x612940: the message table of object `object`'s type (a send's target, or the plan's own
    // object, where -1 means none).
    void loadMessages(int32_t object, bool target) {
        messages_.clear();
        if (!opt_.scene || (!target && object < 0)) return; // no scene, no objects: an empty table
        int16_t o = int16_t(object);
        if (o < 0 || size_t(o) >= opt_.scene->objects.size())
            throw Hazard{"crash", "no object " + std::to_string(o) + " in the scene: the original reads a null object"};
        int16_t t = opt_.scene->objects[size_t(o)].type;
        if (t < 0 || size_t(t) >= opt_.scene->types.size())
            throw Hazard{"crash", "object " + std::to_string(o) + " has no type in the scene"};
        const auto &msgs = opt_.scene->types[size_t(t)].messages;
        for (size_t i = 0; i < msgs.size(); ++i) {
            std::string n = msgs[i];
            if (n.size() >= kNameField1999) { // strncpy(36): no terminator, the value's bytes follow up to a zero
                n = n.substr(0, kNameField1999);
                for (uint32_t v = uint32_t(i + 4); v & 0xFF; v >>= 8) n.push_back(char(v & 0xFF));
            }
            messages_.push_back({n, int32_t(i + 4)});
        }
    }
    // 0x614350: a DataBlock with the message's index in every object type, or -1.
    void messageBlock(const Entry &e) {
        emit(0xABCDEFFF);
        emit(0x20000000);
        size_t n = opt_.scene ? opt_.scene->types.size() : 0;
        emit(uint32_t(int32_t(int16_t(n))));
        for (size_t t = 0; t < n; ++t) {
            int32_t idx = -1;
            const auto &msgs = opt_.scene->types[t].messages;
            for (size_t i = 0; i < msgs.size(); ++i)
                if (!e.name.empty() && msvcStricmp(msgs[i].c_str(), e.name.c_str()) == 0) { idx = int32_t(i + 4); break; }
            emit(uint32_t(int32_t(int16_t(idx))));
        }
    }

    // ---- prescan (0x610B60) ----
    void prescan(Unit &u) {
        Buf buf{};
        int16_t nb = 0, col = 0, nint = 0;
        std::optional<int16_t> localCount;
        auto next = [&]() { return u.r.nextToken(buf); };
        auto hang = [&](const char *what) {
            if (u.r.atEnd()) throw Hazard{"hang", std::string("the source ends while the prescan looks for ") + what};
        };
        for (int guard = 0; u.r.pos < u.r.size && u.r.cur() != 0; ++guard) {
            if (guard > u.r.size + 1) throw Hazard{"hang", "prescan made no progress"};
            std::string t = next();
            if (ieq(t, "DECLARE_LOCAL_REALS")) {
                t = next();
                if (!isIdentifier1999(t) && !isNumber(t)) throw Thrown{kMsgArgEnumLocals1999};
                uint32_t v = 0;
                bool undef = false;
                if (msvcParsePlanNumber(t, v, &undef)) {
                    if (undef) throw Hazard{"undefined", "local-real count '" + t + "': the original reads a stack address"};
                    localCount = int16_t(v & 0xFFFF);
                } else if (!localCount) {
                    throw Hazard{"undefined", "local-real count '" + t + "' is not a number: the original reads an uninitialised count"};
                }
                for (int16_t i = 0; i < *localCount; ++i) {
                    t = next();
                    if (t.empty() || t[0] != '"') throw Thrown{kMsgLocalName1999};
                    u.names.put(kLocalBase + i * 32, strip(t), "local-real names");
                }
            }
            if (ieq(t, "DECLARE_BEHAVIOR")) {
                col = 0;
                while (t.empty() || t[0] != '"') {
                    hang("a behaviour's quoted name");
                    t = next();
                }
                u.names.put(nb * 32, strip(t), "behaviour names");
                ++nb;
            }
            if (ieq(t, "SET_MESSAGE_INTERACTOR")) {
                while (!isMSG(t)) {
                    hang("an id_MSG_ name");
                    t = next();
                }
                if ((int32_t(nb) * 20 + col) * 32 + int32_t(t.size()) + 1 > kDevTableBytes)
                    throw Hazard{"corrupt", "the original writes past its object recording message interactors"};
                ++col;
            }
            if (ieq(t, "SET_INTERACTOR_PARAM")) {
                t = next();
                if (!isIdentifier1999(t) && !isNumber(t)) throw Thrown{kMsgArgEnum1999};
                t = next();
                if (ieq(t, "id_SET_InteractorName")) {
                    t = next();
                    if (t.empty() || t[0] != '"') throw Thrown{kMsgBehName1999};
                    u.names.put(kIntBase + nint * 32, strip(t), "interactor names");
                    ++nint;
                }
            }
        }
    }

    // CHANGE_OF_PLAN to a behaviour by name: a second compiler object reads Data\Plans\<plan>.txt
    // and prescans it.
    int16_t siblingBehavior(const std::string &plan, const std::string &behName) {
        std::string key = lowerAscii(plan);
        auto it = planCache_.find(key);
        std::optional<std::string> text;
        if (it != planCache_.end()) {
            text = it->second;
        } else {
            if (opt_.planSource) text = opt_.planSource(plan);
            planCache_[key] = text;
        }
        if (!text) {
            std::string msg = std::string(kMsgCannotOpen1999) + kPlansDir + plan + ".txt";
            throw Thrown{msg};
        }
        Unit sib;
        sib.r.read(*text, plan, false);
        try {
            prescan(sib);
        } catch (Hazard &h) {
            h.message = "in plan " + plan + ": " + h.message;
            throw;
        }
        return sib.names.behavior(behName);
    }

    // ---- the braced emitter (0x60FB80) ----
    // `depth` counts the emitter's recursion (blocks and comments), `level` the open blocks.
    bool braced(int depth, int level);
    bool flat(int level);
    void hangAtEnd(int level) {
        if (level > 0 && u_.r.ranPastEnd)
            throw Hazard{"undefined", "the source ends inside a block, on a line with no line end: the original reads "
                                      "on into stale buffer contents"};
        if (level > 0 && u_.r.atEnd()) throw Hazard{"hang", "the source ends inside a block"};
    }
};

bool Later::braced(int depth, int level) {
    Reader &r = u_.r;
    int32_t start = 0;
    // A comment makes the original call itself again (0x611D00, then recursion): a loop here, so
    // only the count of frames, not this function's own stack, models it.
    for (;; ++depth) {
        if (depth > kMaxBraceDepth)
            throw Hazard{"crash", "blocks or comments nested more than " + std::to_string(kMaxBraceDepth) +
                                  " deep overflow the original's stack (the braced emitter recurses for each)"};
        if (flatMode_) return flat(level);
        // Past the end of the source the original reads its buffer's stale contents, which may hold
        // braces of an earlier plan.
        if (r.ranPastEnd)
            throw Hazard{"undefined", "a line at the end of the source has no line end, and the original reads on "
                                      "into stale buffer contents looking for the next block"};
        start = r.pos;
        if (r.cur() == '\r' || r.cur() == '\n') r.skipLine();
        r.skipSeps();
        if (r.ranPastEnd) continue;
        if (r.isComment()) {
            uint8_t c = r.cur();
            if (c == '/' && r.byteAt(int64_t(r.pos) + 1) == '/') {
                r.skipLine();
            } else if (c == '(') {
                if (r.byteAt(int64_t(r.pos) - 1) != ')') {
                    for (;;) {
                        int32_t old = r.pos;
                        if (old >= r.size) throw Hazard{"undefined", "a '(' comment between blocks has no ')': the original scans on past the source"};
                        r.pos = old + 1;
                        if (r.byteAt(old) == ')') break;
                    }
                }
            } else if (r.byteAt(int64_t(r.pos) + 1) == '*') {
                uint8_t c2 = r.byteAt(int64_t(r.pos) - 2);
                for (int i = 0; i < 4 && c2 != '*' && r.byteAt(int64_t(r.pos) - 1) != '/'; ++i) {
                    int32_t old = r.pos;
                    r.pos = old + 1;
                    c2 = r.byteAt(int64_t(old) - 1);
                }
            }
            continue;
        }
        break;
    }
    for (;;) { // raw characters up to a brace
        uint8_t c = r.cur();
        if (c == '{') break;
        if (c == '}') {
            ++r.pos;
            return false;
        }
        if (r.size < r.pos || c == 0) {
            r.pos = start;
            flatMode_ = true;
            return flat(level);
        }
        ++r.pos;
    }
    std::string tok = r.nextToken(braceBuf_);
    const CompilerKeyword &k = keyword(tok);
    auto children = [&]() {
        int16_t n = 0;
        for (;;) {
            if (!braced(depth + 1, level + 1)) return n;
            ++n;
        }
    };
    auto leafValue = [&](const std::string &t) { // identifier: lookup and tag; number: parse
        if (isIdentifier1999(t)) emit(tagged(lookup(t)));
        if (isNumber(t)) {
            int32_t idx = count_++;
            put(idx, number(t));
        }
    };
    auto closeHere = [&]() {
        if (r.cur() != '}') return false;
        ++r.pos;
        return true;
    };
    switch (k.id) {
    case 0x80000000u: // Enable
    case 0x40000000:  // Block
    case 0x1b0000: {  // GLOBAL_INTERACTORS
        header(k);
        int32_t c = reserve();
        put(c, uint32_t(int32_t(children())));
        return true;
    }
    case 0x20000000: // DataBlock
    case 0x1a0002: { // SET_TIMEOUTMSG
        header(k);
        int32_t c = reserve();
        int16_t n = 0;
        for (;;) {
            if (closeHere()) break;
            hangAtEnd(1);
            std::string t = r.nextToken(braceBuf_);
            ++n;
            leafValue(t);
        }
        put(c, uint32_t(int32_t(n)));
        return true;
    }
    case 0x180000:   // DO_ACTION
    case 0x1a0001:   // ADD_AGENDA_ITEM
    case 0x1a0003:   // REMOVE_AGENDA_ITEM
    case 0x1a0004:   // SET_AGENDA_ITEM_PARAM
    case 0x70003: {  // SET_BEHAVIOR_PARAM
        header(k);
        int32_t c = reserve();
        Entry head = lookup(r.nextToken(braceBuf_));
        emit(uint32_t(head.value));
        bool send = isSendEntry(head), block = false;
        int16_t n = 0;
        for (;;) {
            if (closeHere()) break;
            hangAtEnd(1);
            std::string t = r.nextToken(braceBuf_);
            ++n;
            if (isIdentifier1999(t)) {
                Entry e = lookup(t);
                uint32_t v = tagged(e);
                if (block) messageBlock(e);
                else emit(v);
                if (send && n == 1) {
                    if (v == 0xFFFF || v == 0xABCDABCE || isPRN(t) || isPRT(t)) block = true;
                    else loadMessages(int32_t(v), true);
                } else if (send && n == 2) {
                    loadMessages(opt_.object, false);
                    block = false;
                }
            }
            if (isNumber(t)) {
                int32_t idx = count_++;
                put(idx, number(t));
            }
        }
        put(c, uint32_t(int32_t(n) + 1)); // (int)short + 1: no wrap at 32767
        return true;
    }
    case 0x170000: // DECIDE_BY_AMONG
    case 0x50000: { // SET_MESSAGE_INTERACTOR
        header(k);
        int32_t c = reserve();
        emit(uint32_t(lookup(r.nextToken(braceBuf_)).value));
        put(c, uint32_t(int32_t(children()) + 1));
        return true;
    }
    case 0x30000: { // SET_COLLISION_INTERACTOR
        header(k);
        int32_t c = reserve();
        leafValue(r.nextToken(braceBuf_));
        leafValue(r.nextToken(braceBuf_));
        put(c, uint32_t(int32_t(children()) + 2));
        return true;
    }
    case 0x70000: { // DECLARE_BEHAVIOR
        header(k);
        int32_t c = reserve();
        emit(uint32_t(lookup(r.nextToken(braceBuf_)).value));
        emitString(r.nextToken(braceBuf_));
        put(c, uint32_t(int32_t(children()) + 2));
        return true;
    }
    case 0x70002:   // CHANGE_TO_BEHAVIOR
    case 0x70005: { // CHANGE_TO_NEW_BEHAVIOR
        header(k);
        emit(1);
        std::string t = r.nextToken(braceBuf_);
        if (!t.empty() && t[0] == '"') emit(uint32_t(int32_t(u_.names.behavior(strip(t)))));
        else if (isNumber(t)) emit(number(t));
        else emit(uint32_t(lookup(t).value));
        ++r.pos; // the closing brace, unseen
        return true;
    }
    case 0x160000: { // CHANGE_OF_PLAN
        header(k);
        emit(2);
        std::string planTok = r.nextToken(braceBuf_);
        emitString(planTok);
        std::string t = r.nextToken(braceBuf_);
        if (planTok.size() >= 172) planTok = planTok.substr(0, 172) + t; // overwritten by the next token
        if (isNumber(t)) {
            uint32_t v = 0;
            bool undef = false;
            if (!msvcParsePlanNumber(t, v, &undef) || undef)
                throw Hazard{"undefined", "CHANGE_OF_PLAN's behaviour number '" + t + "' does not parse; the original emits a stale stack value"};
            emit(v);
        } else {
            emit(uint32_t(int32_t(siblingBehavior(strip(planTok), strip(t)))));
        }
        ++r.pos; // the closing brace, unseen
        return true;
    }
    case 0x60001: case 0x60002: case 0x160001: case 0x180001: // no output, the block stays open
        return true;
    default: // END_PLAN or not a keyword
        return false;
    }
}

// ---- the flat emitter (0x6110D0) ----------------------------------------------------------------
bool Later::flat(int level) {
    Reader &r = u_.r;
    hangAtEnd(level);
    std::string tok = r.nextToken(flatBuf_);
    ++beh_; ++ctb_; ++ctnb_; ++cop_; ++sip_; ++dlr_; ++send_;
    if (isIdentifier1999(tok)) {
        Entry e = lookup(tok);
        flatPlanName_ = e.name;
        flatPlanFromLongName_ = e.name.size() >= kNameField1999;
        if (e.value == -1 && !isMSG(tok)) return true;
        uint32_t v = tagged(e);
        if (isSendEntry(e)) send_ = 0;
        if (dlr_ == 1) {
            uint32_t scratch = 0;
            bool undef = false;
            if (!msvcParsePlanNumber(tok, scratch, &undef)) throw Thrown{kMsgLocalsEnum1999};
        }
        if (msgFlag_) messageBlock(e);
        else emit(v);
        if (send_ == 1) {
            if (uint32_t(e.value) == 0xFFFF || uint32_t(e.value) == 0xABCDABCE || isPRN(tok) || isPRT(tok)) msgFlag_ = true;
            else loadMessages(e.value, true);
        } else if (send_ == 2) {
            loadMessages(opt_.object, false);
            msgFlag_ = false;
        }
    }
    if (isNumber(tok)) {
        int32_t idx = count_++;
        put(idx, number(tok));
    }
    if (!tok.empty() && tok[0] == '"') {
        std::string s = strip(tok);
        int k = u_.names.find(kLocalBase, 500, s);
        if (k >= 0) { emit(0xEDCB0000u | uint32_t(k & 0xFFFF)); return true; }
        k = u_.names.find(kIntBase, 1200, s);
        if (k >= 0) { emit(0xDCBA0000u | uint32_t(k & 0xFFFF)); return true; }
        if (beh_ == 3) { emitString(tok); return true; }
        if (cop_ == 2) {
            emitString(tok);
            flatPlanName_ = s;
            flatPlanFromLongName_ = false;
            return true;
        }
        if (cop_ == 3) {
            if (!flatPlanName_) throw Hazard{"undefined", "CHANGE_OF_PLAN names no plan; the original reads a stale stack buffer"};
            if (flatPlanFromLongName_)
                throw Hazard{"undefined", "CHANGE_OF_PLAN's plan name comes from a symbol of 36+ characters, stored without "
                                          "a terminator: the original reads on into its stack"};
            // The name's buffer sits 40 bytes below the token buffer: a longer name continues with
            // the token now in it.
            std::string plan = *flatPlanName_;
            if (plan.size() >= 40) plan = plan.substr(0, 40) + tok;
            emit(uint32_t(int32_t(siblingBehavior(plan, s))));
            return true;
        }
        if (ctb_ == 2 || ctnb_ == 2) { emit(uint32_t(int32_t(u_.names.behavior(s)))); return true; }
        if (sip_ != 3) { emitString(tok); return true; }
        emit(uint32_t(int32_t(u_.names.interactor(s))));
        return true;
    }
    const CompilerKeyword &k = keyword(tok);
    switch (k.id) {
    case 0x160000: cop_ = 0; break;
    case 0x70000: beh_ = 0; break;
    case 0x70002: ctb_ = 0; break;
    case 0x70005: ctnb_ = 0; break;
    case 0x160001: dlr_ = 0; break;
    case 0x180001: sip_ = 0; break;
    default: break;
    }
    if (k.name[0] == 0) return true;
    header(k);
    return true;
}

CompileResult Later::run() {
    CompileResult res;
    std::string stage = "read";
    auto lineOf = [&](int32_t off) {
        size_t lim = std::min<size_t>(size_t(std::max(off, 0)), source_.size());
        return int32_t(std::count(source_.begin(), source_.begin() + long(lim), '\n') + 1);
    };
    auto fail = [&](const std::string &kind, const std::string &msg) {
        res.diagnostics = warnings_;
        Diagnostic d;
        d.stage = stage;
        d.kind = kind;
        d.message = msg;
        d.offset = u_.r.pos;
        d.line = lineOf(u_.r.pos);
        res.diagnostics.push_back(d);
        res.ok = false;
        return res;
    };
    // The main table as lookups see it, first match per name.
    const auto &syms = syms_.symbols();
    for (size_t k = 0; k < syms.size() && k < size_t(kMainSymbols) && syms[k].value != -1; ++k) {
        main_.push_back({storedName(syms, k), syms[k].value});
        mainIndex_.emplace(lowerAscii(main_.back().name), main_.size() - 1);
    }
    bool prescanPastEnd = false;
    try {
        u_.r.read(source_, opt_.planName, false);
        stage = "compile";
        if (opt_.scene && opt_.object < 0)
            throw Hazard{"crash", "the plan belongs to no object of the scene (the original's message table would "
                                  "come from a null object): list it in the scene or name its object"};
        loadMessages(opt_.object, false);
        prescan(u_);
        prescanPastEnd = u_.r.ranPastEnd;
        u_.r.pos = 0;
        u_.r.ranPastEnd = false;
        for (int guard = 0; u_.r.pos < u_.r.size && u_.r.cur() != 0; ++guard) {
            if (guard > u_.r.size + 1) throw Hazard{"hang", "the compile made no progress"};
            if (!braced(0, 0)) throw Thrown{kMsgAbort};
        }
    } catch (const Thrown &t) {
        if (u_.r.ranPastEnd) // the cursor has been in stale buffer bytes: its position is not the original's
            return fail("undefined", t.message + " (after the cursor ran past the end of the source on a line with no "
                                                 "line end: the original's position comes from stale buffer bytes)");
        return fail("throw", t.message);
    } catch (const Hazard &h) {
        std::string msg = h.message;
        if (msg.find("original") == std::string::npos) msg += " (the original compiler would " + h.kind + ")";
        return fail(h.kind, msg);
    } catch (const std::exception &e) { // a planSource callback that threw
        return fail("callback", e.what());
    }
    res.words.assign(out_.begin(), out_.begin() + count_);
    if (prescanPastEnd || u_.r.ranPastEnd) {
        Diagnostic d;
        d.severity = Diagnostic::Severity::Warning;
        d.code = "adlib1999-read-past-end";
        d.message = "the last line has no line end (a comment or bare CR on it): the original reads past the end of "
                    "the source into its buffer's stale contents (output unaffected)";
        d.offset = int32_t(source_.size());
        warnings_.push_back(d);
    }
    auto endIt = std::find(res.words.begin(), res.words.end(), 0xFFFF0000u);
    if (endIt != res.words.end() && endIt + 1 != res.words.end()) {
        Diagnostic d;
        d.severity = Diagnostic::Severity::Warning;
        d.code = "adlib1999-after-end";
        d.message = std::to_string(res.words.end() - endIt - 1) + " dwords after END_PLAN are counted in the PBN "
                    "header but not written: the game's loader would read past the end of the file";
        warnings_.push_back(d);
    }
    res.diagnostics = warnings_;
    if (endIt == res.words.end()) {
        Diagnostic d;
        d.stage = "write";
        d.kind = "no-end";
        d.message = kMsgNoEnd;
        res.diagnostics.insert(res.diagnostics.begin(), d);
        res.ok = false;
        return res;
    }
    auto putLE = [&](uint32_t v) { for (int i = 0; i < 4; ++i) res.pbn.push_back(uint8_t(v >> (8 * i))); };
    putLE(uint32_t(res.words.size()));
    for (auto w = res.words.begin(); w != endIt + 1; ++w) putLE(*w);
    res.ok = true;
    if (opt_.werror)
        for (const Diagnostic &d : res.diagnostics)
            if (d.severity == Diagnostic::Severity::Warning) { res.ok = false; break; }
    return res;
}

} // namespace

namespace cc {
CompileResult compileLater(std::string_view source, const CompilerSymbols &symbols, const CompileOptions &opt) {
    Later l(symbols, opt, source);
    return l.run();
}
} // namespace cc

bool parseScene(std::string_view data, CompilerScene &out, std::string *err) {
    std::vector<std::string> lines;
    std::istringstream in{std::string(data)};
    for (std::string l; std::getline(in, l);) {
        size_t b = l.find_first_not_of(" \t\r"), e = l.find_last_not_of(" \t\r");
        if (b != std::string::npos) lines.push_back(l.substr(b, e - b + 1)); // blank lines separate records
    }
    size_t i = 0;
    auto fail = [&](const std::string &why) {
        if (err) *err = "non-blank line " + std::to_string(std::min(i, lines.size())) + ": " + why;
        return false;
    };
    auto words = [&](std::vector<std::string> &w) {
        if (i >= lines.size()) return false;
        w.clear();
        std::istringstream ls(lines[i++]);
        for (std::string x; ls >> x;) w.push_back(x);
        return true;
    };
    auto number = [&](const std::string &s, long &v) { // counts and indices: 0 to 999 999 999
        if (s.empty() || s.size() > 9 || s.find_first_not_of("0123456789") != std::string::npos) return false;
        v = std::strtol(s.c_str(), nullptr, 10);
        return true;
    };
    auto signedNumber = [&](const std::string &s, long &v) { // collision-object ids: a short
        size_t d = s.size() > 0 && s[0] == '-' ? 1 : 0;
        if (s.size() == d || s.size() > d + 6 || s.find_first_not_of("0123456789", d) != std::string::npos) return false;
        v = std::strtol(s.c_str(), nullptr, 10);
        return v >= -32768 && v <= 32767;
    };
    std::vector<std::string> w;
    long nTypes = 0, nObjects = 0, a = 0, b = 0;
    // "<scene> <config file> <plans> <types> <objects>"
    if (!words(w) || w.size() < 3 || !number(w[w.size() - 2], nTypes) || !number(w.back(), nObjects))
        return fail("not a scene header");
    if (nTypes > 32767 || nObjects > 32767) return fail("more than 32767 types or objects");
    out = CompilerScene();
    std::map<std::string, int16_t> typeIndex;
    for (long t = 0; t < nTypes; ++t) { // "<type> <index> <plans> <messages>", the plans, the messages
        if (!words(w) || w.size() != 4 || !number(w[1], a) || a != t || !number(w[2], a) || !number(w[3], b) ||
            size_t(a) > lines.size() - i || size_t(b) > lines.size() - i - size_t(a))
            return fail("not an object type");
        if (typeIndex.count(w[0])) return fail("object type " + w[0] + " twice");
        CompilerScene::ObjectType ty;
        ty.name = w[0];
        ty.plans.assign(lines.begin() + long(i), lines.begin() + long(i) + a);
        ty.messages.assign(lines.begin() + long(i) + a, lines.begin() + long(i) + a + b);
        i += size_t(a + b);
        typeIndex[ty.name] = int16_t(t);
        out.types.push_back(ty);
    }
    for (long o = 0; o < nObjects; ++o) { // "<name> <id> <type> <index>"
        if (!words(w) || w.size() != 4 || !number(w[3], a) || a != o || !typeIndex.count(w[2]))
            return fail("not an object");
        CompilerScene::Object ob;
        ob.id = w[1];
        ob.type = typeIndex[w[2]];
        if (!words(w) || w.size() != 1 || !number(w[0], a)) return fail("no model count");
        if (size_t(a) > lines.size() - i) return fail("model files past the end");
        i += size_t(a);           // model files
        if (i >= lines.size() || lines[i++][0] != '|') return fail("no motion record");
        for (int s = 0; s < 8; ++s) { // collision-object slots: kind, then "|" or "<id> <number> ..."
            if (!words(w) || w.size() != 1 || !number(w[0], a)) return fail("no collision-object kind");
            if (a == 0) {
                if (i >= lines.size() || lines[i++][0] != '|') return fail("no empty collision-object record");
                continue;
            }
            if (!words(w) || w.size() < 2 || !signedNumber(w[1], b)) return fail("not a collision object");
            ob.collisionObjects.push_back({w[0], int16_t(b)});
            if (a == 4) { // a collection: its elements follow
                long n = 0;
                if (!words(w) || w.size() != 1 || !number(w[0], n)) return fail("no element count");
                if (size_t(n) > lines.size() - i) return fail("elements past the end");
                i += size_t(n);
            }
        }
        if (!words(w) || w.size() != 1 || !number(w[0], a) || size_t(a) > lines.size() - i) return fail("no plan count");
        i += size_t(a);
        out.objects.push_back(ob);
    }
    if (err) err->clear();
    return true;
}

bool buildScene(std::string_view cfg, const CompilerSymbols &symbols,
                const std::function<std::optional<std::string>(const std::string &plan)> &planSource,
                CompilerScene &out, std::string *err) {
    out = CompilerScene();
    auto fail = [&](const std::string &why) {
        if (err) *err = why;
        return false;
    };
    // The .cfg, a line at a time, with the plan compiler's separators and comment starts.
    std::vector<std::vector<std::string>> objectPlans;
    std::vector<std::string> objectTypes;
    std::vector<std::string> cfgLines(1);
    for (size_t c = 0; c < cfg.size(); ++c) { // CR, LF or CR LF
        if (cfg[c] == '\r' || cfg[c] == '\n') {
            if (cfg[c] == '\r' && c + 1 < cfg.size() && cfg[c + 1] == '\n') ++c;
            cfgLines.emplace_back();
        } else {
            cfgLines.back().push_back(cfg[c]);
        }
    }
    int16_t cob = 0;
    int lineNo = 0;
    for (std::string line : cfgLines) {
        ++lineNo;
        size_t cut = line.size();
        for (const char *c : {"#", "//", "/*", "("}) cut = std::min(cut, line.find(c));
        line = line.substr(0, cut);
        std::vector<std::string> t;
        std::string cur;
        for (char c : line + " ") {
            if (c == ' ' || c == '\t' || c == ',' || c == '&' || c == '\r' || c == '=') {
                if (!cur.empty()) t.push_back(cur);
                cur.clear();
                if (c == '=') t.push_back("=");
            } else {
                cur.push_back(c);
            }
        }
        if (t.empty()) continue;
        bool badValue = false;
        auto value = [&](const char *key) -> std::string {
            for (size_t i = 0; i + 1 < t.size(); ++i)
                if (msvcStricmp(t[i].c_str(), key) == 0 && t[i + 1] == "=") {
                    if (i + 2 >= t.size() || t[i + 2] == "=" || (i + 3 < t.size() && t[i + 3] == "=")) badValue = true;
                    else return t[i + 2];
                }
            return {};
        };
        if (msvcStricmp(t[0].c_str(), "DefineObject") == 0) {
            std::string type = value("type");
            CompilerScene::Object o;
            o.id = value("id");
            if (type.empty() || badValue) return fail("line " + std::to_string(lineNo) + ": DefineObject without a type, or a key without a value");
            out.objects.push_back(o);
            objectTypes.push_back(type);
            objectPlans.emplace_back();
        } else if (msvcStricmp(t[0].c_str(), "SetPlans") == 0) {
            if (out.objects.empty()) return fail("line " + std::to_string(lineNo) + ": SetPlans outside an object");
            objectPlans.back().insert(objectPlans.back().end(), t.begin() + 1, t.end());
        } else if (msvcStricmp(t[0].c_str(), "SetCollisionObjs") == 0) {
            if (out.objects.empty()) return fail("line " + std::to_string(lineNo) + ": SetCollisionObjs outside an object");
            std::string id = value("colObjID");
            if (badValue) return fail("line " + std::to_string(lineNo) + ": a key without a value");
            if (!id.empty()) out.objects.back().collisionObjects.push_back({id, cob++});
            if (out.objects.back().collisionObjects.size() > 8)
                return fail("line " + std::to_string(lineNo) + ": more than 8 collision objects in one object");
        }
    }
    if (out.objects.empty()) return fail("no DefineObject");
    auto same = [](const std::string &a, const std::string &b) { return msvcStricmp(a.c_str(), b.c_str()) == 0; };
    for (size_t o = 0; o < out.objects.size(); ++o) {
        size_t t = 0;
        while (t < out.types.size() && !same(out.types[t].name, objectTypes[o])) ++t;
        if (t == out.types.size()) out.types.push_back({objectTypes[o], {}, {}});
        out.objects[o].type = int16_t(t);
        for (const std::string &p : objectPlans[o])
            if (std::none_of(out.types[t].plans.begin(), out.types[t].plans.end(), [&](const std::string &q) { return same(p, q); }))
                out.types[t].plans.push_back(p);
    }
    // Each type's messages (0x609360, 0x6093C0): its plans' id_MSG_ tokens the symbols lack, as
    // the compiler's lookup sees them (the first 5000, up to a -1).
    std::unordered_set<std::string> known;
    for (size_t k = 0; k < symbols.symbols().size() && k < size_t(kMainSymbols) && symbols.symbols()[k].value != -1; ++k)
        known.insert(lowerAscii(storedName(symbols.symbols(), k)));
    for (CompilerScene::ObjectType &ty : out.types) {
        for (const std::string &p : ty.plans) {
            std::optional<std::string> text;
            try {
                if (planSource) text = planSource(p);
            } catch (const std::exception &e) {
                return fail("plan " + p + ": " + e.what());
            }
            if (!text) return fail(std::string(kMsgCannotOpen1999) + kPlansDir + p + ".txt");
            Reader r;
            std::array<uint8_t, kTokenCap> buf{};
            try {
                r.read(*text, p, false);
                for (int guard = 0; !r.atEnd() && guard <= r.size + 1; ++guard) {
                    std::string tok = r.nextToken(buf);
                    if (tok.size() >= 64) return fail("plan " + p + ": a token of 64 or more bytes overflows the scene compiler's buffer");
                    if (!isMSG(tok) || known.count(lowerAscii(tok))) continue;
                    if (std::none_of(ty.messages.begin(), ty.messages.end(), [&](const std::string &m) { return same(m, tok); }))
                        ty.messages.push_back(tok);
                }
            } catch (const Thrown &t) {
                return fail(t.message);
            } catch (const Hazard &h) {
                return fail("plan " + p + ": " + h.message);
            }
        }
    }
    if (err) err->clear();
    return true;
}

int16_t CompilerScene::objectFor(std::string_view plan) const {
    for (size_t o = 0; o < objects.size(); ++o) {
        int16_t t = objects[o].type;
        if (t < 0 || size_t(t) >= types.size()) continue;
        for (const std::string &p : types[size_t(t)].plans)
            if (p.size() == plan.size() && msvcStricmp(p.c_str(), std::string(plan).c_str()) == 0) return int16_t(o);
    }
    return -1;
}

const std::vector<CompilerKeyword> &compilerKeywords(Compat c) {
    static const std::vector<CompilerKeyword> k1999(std::begin(kKeywords1999), std::end(kKeywords1999));
    static const std::vector<CompilerKeyword> k1998 = [] {
        std::vector<CompilerKeyword> v;
        for (const CompilerKeyword &k : kKeywords1999)
            if (k.id != 0x1a0004) v.push_back(k);
        return v;
    }();
    if (c == Compat::Adlib1999) return k1999;
    if (c == Compat::Adlib1998) return k1998;
    return compilerKeywords();
}

bool parseCompat(std::string_view name, Compat &out) {
    for (Compat c : {Compat::Adlib, Compat::Adlib1996, Compat::Adlib1998, Compat::Adlib1999})
        if (name == compatName(c)) { out = c; return true; }
    return false;
}

const char *compatName(Compat c) {
    switch (c) {
    case Compat::Adlib1996: return "adlib1996";
    case Compat::Adlib1998: return "adlib1998";
    case Compat::Adlib1999: return "adlib1999";
    default: return "adlib";
    }
}

} // namespace adlib
