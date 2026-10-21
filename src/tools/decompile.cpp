// adlib-tools: PBN -> ADLIB compiler source (port of the reconstruction's Python decompiler).
//
// The original compiler is a flat token translator (docs/research/grammar.md §1): every keyword is
// followed by the author's child count, and each token emits its dwords independently. So the
// decompiler walks the raw token stream (not the loaded tree) and prints one token per dword
// group, choosing a spelling the compiler maps back to exactly the same dwords:
//   - symbols only when the compiler's case-insensitive first-match lookup gives the same value;
//   - ints for |v| < 0x100000, otherwise the shortest round-tripping float with '.', else hex;
//   - behaviour references by name only when the name is the first one with that index;
//   - local-real and interactor names, which the PBN keeps only as slot numbers, invented where
//     the prescan needs a quoted name (their declarations) and hex everywhere else.
// The braced form (DecompileOptions::braced) is written by BracedWriter below: a statement is
// braced only when every part of it has a braced spelling, and the plan continues flat from the
// first one that does not.
// From 1998 (syms.compat) the compiler's flat form is the same translator with case-insensitive
// names, two pronoun tags and one trap: an identifier right after id_ACF_SendMessage that names
// _BROADCAST_, _MESSAGE_DATA_ or a pronoun makes it compile every later identifier as a message
// DataBlock. Such a target is written as a number, which compiles to the same dword.
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <optional>
#include <stdexcept>

#include "adlib/tools.h"

namespace adlib::tools {

namespace {

const char *specialName(uint32_t v) {
    switch (v) {
    case kBroadcast: return "_BROADCAST_";
    case kStraightAhead: return "_STRAIGHTAHEAD_";
    case kTemporarily: return "_TEMPORARILY_";
    case kMessageData: return "_MESSAGE_DATA_";
    case kNoBehChange: return "_NO_BEH_CHANGE_";
    case kPreviousBeh: return "_PREVIOUS_BEH_";
    default: return nullptr;
    }
}

std::string hex8(uint32_t v) {
    char b[16];
    std::snprintf(b, sizeof b, "0x%08x", v);
    return b;
}

// Can `s` be written as a compiler string token? ('_' is how a space is written; the tokenizer
// splits on separators and comment starts; length < 40 with quotes, 256 from 1998.)
bool expressible(const std::string &s, std::string *why, bool later = false) {
    size_t cap = later ? 253 : 37;
    if (s.size() > cap) { if (why) *why = "longer than " + std::to_string(cap) + " characters"; return false; }
    for (size_t i = 0; i < s.size(); ++i) {
        unsigned char c = uint8_t(s[i]);
        if (c == '_' || c == '"') { if (why) *why = "contains '_' or '\"'"; return false; }
        if (c < 32 && c != ' ') { if (why) *why = "contains a control character"; return false; }
        if (c == ',' || c == '&' || c == '\t' || c == '(' || c == '#' || c == '}' || (later && c == '{') ||
            (c == '/' && i + 1 < s.size() && (s[i + 1] == '/' || s[i + 1] == '*'))) {
            if (why) *why = "contains a separator or comment start";
            return false;
        }
    }
    return true;
}

std::string quote(const std::string &s) {
    std::string r = "\"";
    for (char c : s) r += c == ' ' ? '_' : c;
    return r + "\"";
}

// The deepest nesting the writers follow (no compile accepts deeper), and the deepest they indent.
constexpr int kMaxBracedNesting = kMaxNesting1999;
constexpr int kMaxNesting = kMaxNesting1999;
constexpr int kMaxIndent = 32;

struct Decompiler {
    const std::vector<uint32_t> &w;
    const SymbolTable &syms;
    const DecompileOptions &opt;
    std::vector<std::string> behs;
    std::vector<std::string> lines;
    size_t pos = 0;
    std::string copPlan; // last CHANGE_OF_PLAN target (upper case)
    int behDecl = 0;
    const bool later = isLaterCompat(syms.compat);
    int32_t send = 100; // the 1999 flat emitter's SendMessage counter, over the tokens printed
    int16_t msgObject = opt.object; // whose messages id_MSG_ names compile against right now
    uint32_t firstLeaf = 0;         // the current statement's first leaf
    std::vector<std::string> strings; // every string in the plan, lower case

    // A name for local real or interactor slot k that no string in the plan already spells.
    std::string invented(const char *base, uint32_t k) const {
        std::string n = base + std::to_string(k);
        for (int guard = 0; guard < 64; ++guard) {
            std::string l = n;
            for (char &c : l) c = char(std::tolower(uint8_t(c)));
            if (std::find(strings.begin(), strings.end(), l) == strings.end()) break;
            n += "x";
        }
        return n;
    }

    // Count a printed token the way the 1999 flat emitter counts it, and follow its message
    // table: a SendMessage target switches to the target object's, the message switches back.
    void token(const std::string &t) {
        ++send;
        if (!later || !syms.identifier(t)) return;
        const SymbolTable::Entry *e = syms.lookup(t);
        if (!e && t.compare(0, 7, "id_MSG_") != 0) return; // unknown, or a special such as _0_ (never printed here)
        if (e && isSendName(e->name)) send = 0;
        if (send == 1 && e) msgObject = int16_t(e->value);
        else if (send == 2) msgObject = opt.object;
    }
    const std::vector<std::string> *messagesOf(int16_t o) const {
        if (!opt.scene || o < 0 || size_t(o) >= opt.scene->objects.size()) return nullptr;
        int16_t t = opt.scene->objects[size_t(o)].type;
        return t >= 0 && size_t(t) < opt.scene->types.size() ? &opt.scene->types[size_t(t)].messages : nullptr;
    }
    // An id_MSG_ name that compiles to `v` here: the main table first, then the current object's
    // messages ("" when there is none).
    std::string messageName(int32_t v) const {
        std::string n = syms.roleName(Role::Message, v);
        if (!n.empty()) return n;
        const std::vector<std::string> *list = messagesOf(msgObject);
        if (v >= 4 && list && size_t(v - 4) < list->size()) {
            const std::string &m = (*list)[size_t(v - 4)];
            if (m.size() < kNameField1999 && syms.identifier(m) && !syms.lookup(m) && messageListIndex(m) == v - 4) return m;
        }
        if (v == -1) return unusedName("id_MSG_Unknown"); // a name found nowhere compiles to -1
        return {};
    }
    // The id of scene object `v`, when it compiles back to `v`.
    std::string objectName(uint32_t v) const {
        if (!opt.scene || v >= opt.scene->objects.size()) return {};
        const std::string &id = opt.scene->objects[v].id;
        if (id.size() >= kNameField1999) return {}; // stored without a terminator: no source names it
        const SymbolTable::Entry *e = syms.lookup(id);
        return syms.identifier(id) && !syms.pronounTag(id) && e && uint32_t(e->value) == v ? id : std::string();
    }
    static bool lowerEq(const std::string &a, const char *b) {
        size_t n = std::strlen(b);
        if (a.size() != n) return false;
        for (size_t i = 0; i < n; ++i)
            if (std::tolower(uint8_t(a[i])) != std::tolower(uint8_t(b[i]))) return false;
        return true;
    }
    // A number spelling of `v` (never an identifier).
    static std::string numeric(uint32_t v) {
        int32_t s = int32_t(v);
        if (-0x100000 < s && s < 0x100000) return std::to_string(s);
        return hex8(v);
    }
    static bool isSendName(const std::string &n) {
        return lowerEq(n, "id_ACF_SendMessage") || lowerEq(n, "id_ACF_SendMsgImmediate");
    }
    bool isKeyword(uint32_t id) const {
        for (const CompilerKeyword &k : compilerKeywords(syms.compat))
            if (k.id == id) return true;
        return false;
    }
    // `prefix`, or `prefix` with a number, that no symbol and no message of the current object spells.
    std::string unusedName(const char *prefix) const {
        for (int k = 0; k < 100; ++k) {
            std::string u = prefix;
            if (k) u += std::to_string(k);
            if (!syms.lookup(u) && messageListIndex(u) < 0) return u;
        }
        return {};
    }
    // The 1999 flat emitter's send counter: the next token is a SendMessage's target, or its message.
    bool nextIsTarget() const { return later && send == 0; }
    bool nextIsMessage() const { return later && send == 1; }

    uint32_t at(size_t p) const {
        if (p >= w.size()) throw std::runtime_error("truncated token stream at dword " + std::to_string(p));
        return w[p];
    }

    std::string readStr() {
        ++pos;
        std::string s;
        while (at(pos)) s += char(at(pos++) & 0xFF);
        ++pos;
        return s;
    }

    // ---- braced blocks (1998 on) -------------------------------------------------------------
    struct Item {
        enum Kind { Leaf, String, Node } kind = Leaf;
        uint32_t value = 0;      // Leaf
        std::string text;        // String
        uint32_t id = 0;         // Node
        uint32_t count = 0;      // Node: the stored count dword
        std::vector<Item> kids;  // Node
    };
    // The item at `p` as the game reads it; false when the stream does not hold one.
    bool readItem(size_t &p, Item &it, int depth) const {
        if (depth >= kMaxBracedNesting || p >= w.size()) return false;
        if (w[p] == kTokString) {
            it.kind = Item::String;
            for (++p; p < w.size() && w[p]; ++p) it.text += char(w[p] & 0xFF);
            if (p >= w.size()) return false;
            ++p;
            return true;
        }
        if (w[p] != kTokNode) {
            it.kind = Item::Leaf;
            it.value = w[p++];
            return true;
        }
        if (p + 2 >= w.size()) return false;
        it.kind = Item::Node;
        it.id = w[p + 1];
        if (it.id == END_PLAN) return false; // END_PLAN is never braced
        it.count = w[p + 2];
        p += 3;
        for (uint32_t k = 0; k < (it.count & 0x7FFF); ++k) {
            it.kids.emplace_back();
            if (!readItem(p, it.kids.back(), depth + 1)) return false;
        }
        return true;
    }
    static bool isIdent(const SymbolTable &s, const std::string &t) { return !t.empty() && s.identifier(t); }
    // A head: a symbol emitted raw, with no pronoun tag; an unknown name emits -1.
    std::string head(uint32_t v, uint32_t parent) {
        std::string n = parent == SET_MESSAGE_INTERACTOR ? messageName(int32_t(v)) : spell(v, parent, 0, -1, nullptr);
        if (isIdent(syms, n) && !syms.pronounTag(n)) {
            if (parent == SET_MESSAGE_INTERACTOR) return n; // messageName only names what compiles to v
            const char *sp = specialName(v);
            if (sp && n == sp) return n;
            const SymbolTable::Entry *e = syms.lookup(n);
            if (e && uint32_t(e->value) == v) return n;
        }
        if (v == 0xFFFFFFFFu && parent != SET_MESSAGE_INTERACTOR) return unusedName("id_Unknown"); // found nowhere: -1
        return {};
    }
    int32_t messageListIndex(const std::string &name) const {
        const std::vector<std::string> *list = messagesOf(msgObject);
        for (size_t i = 0; list && i < list->size(); ++i)
            if (lowerEq((*list)[i], name.c_str())) return int32_t(i);
        return -1;
    }
    // A leaf of a braced list: an identifier or a number that compiles to v.
    std::string listLeaf(uint32_t v, uint32_t parent, size_t idx) {
        std::string s = (parent == SET_TIMEOUTMSG && idx == 1) ? messageName(int32_t(v)) : std::string();
        if (s.empty()) s = spell(v, parent, idx, -1, nullptr);
        if (s.empty() || s[0] == '"') return numeric(v);
        return s;
    }
    // The broadcast DataBlock the compiler makes for message `name` here (0x614350).
    std::vector<uint32_t> messageBlock(const std::string &name) const {
        std::string found;
        if (const SymbolTable::Entry *e = syms.lookup(name)) found = e->name;
        else if (messageListIndex(name) >= 0) found = name;
        std::vector<uint32_t> out;
        if (!opt.scene) return out;
        for (const CompilerScene::ObjectType &t : opt.scene->types) {
            int32_t idx = -1;
            for (size_t i = 0; i < t.messages.size() && !found.empty(); ++i)
                if (lowerEq(t.messages[i], found.c_str())) { idx = int32_t(i + 4); break; }
            out.push_back(uint32_t(int32_t(int16_t(idx))));
        }
        return out;
    }
    // A message name whose broadcast DataBlock is `want`: the name a non-negative entry points at,
    // or, for all -1, a name found nowhere.
    std::string broadcastName(const std::vector<uint32_t> &want) const {
        std::vector<std::string> candidates;
        for (size_t t = 0; opt.scene && t < want.size() && t < opt.scene->types.size() && candidates.size() < 4; ++t) {
            int32_t idx = int32_t(want[t]);
            const auto &msgs = opt.scene->types[t].messages;
            if (idx >= 4 && size_t(idx - 4) < msgs.size()) candidates.push_back(msgs[size_t(idx - 4)]);
        }
        std::string unknown = unusedName("id_MSG_Unknown");
        if (!unknown.empty()) candidates.push_back(unknown);
        for (const std::string &c : candidates)
            if (c.size() < kNameField1999 && messageBlock(c) == want) return c;
        return {};
    }

    // One braced block for `it`, or nullopt when no braced source compiles to it.
    std::optional<std::string> braced(const Item &it, int depth) {
        if (it.kind != Item::Node) return std::nullopt;
        std::string ind;
        for (int i = 0; i < std::min(depth, kMaxIndent); ++i) ind += opt.indent;
        const char *kw = nodeIdName(it.id);
        if (!kw || !isKeyword(it.id) || it.count != it.kids.size()) return std::nullopt;
        const auto &k = it.kids;
        auto leafAt = [&](size_t i) { return i < k.size() && k[i].kind == Item::Leaf; };
        auto nodesFrom = [&](size_t i, std::string &out) {
            for (; i < k.size(); ++i) {
                std::optional<std::string> b = braced(k[i], depth + 1);
                if (!b) return false;
                out += "\n" + *b;
            }
            return true;
        };
        std::string out = ind + "{" + kw;
        switch (it.id) {
        case ENABLE: case BLOCK: case GLOBAL_INTERACTORS:
            if (!nodesFrom(0, out)) return std::nullopt;
            return out + "\n" + ind + "}";
        case DATA_BLOCK: case SET_TIMEOUTMSG:
            for (size_t i = 0; i < k.size(); ++i) {
                if (!leafAt(i)) return std::nullopt;
                out += " " + listLeaf(k[i].value, it.id, i);
            }
            return out + "}";
        case DECIDE_BY_AMONG: case SET_MESSAGE_INTERACTOR: {
            if (!leafAt(0)) return std::nullopt;
            std::string h = head(k[0].value, it.id);
            if (h.empty()) return std::nullopt;
            out += " " + h;
            if (!nodesFrom(1, out)) return std::nullopt;
            return out + "\n" + ind + "}";
        }
        case SET_COLLISION_INTERACTOR:
            if (!leafAt(0) || !leafAt(1)) return std::nullopt;
            out += " " + listLeaf(k[0].value, it.id, 0) + " " + listLeaf(k[1].value, it.id, 1);
            if (!nodesFrom(2, out)) return std::nullopt;
            return out + "\n" + ind + "}";
        case DECLARE_BEHAVIOR: {
            if (!leafAt(0) || k.size() < 2 || k[1].kind != Item::String) return std::nullopt;
            std::string h = head(k[0].value, it.id);
            if (h.empty() || !expressible(k[1].text, nullptr, true)) return std::nullopt;
            out += " " + h + " " + quote(k[1].text);
            if (!nodesFrom(2, out)) return std::nullopt;
            return out + "\n" + ind + "}";
        }
        case CHANGE_TO_BEHAVIOR: case CHANGE_TO_NEW_BEHAVIOR: {
            if (k.size() != 1 || !leafAt(0)) return std::nullopt;
            uint32_t v = k[0].value;
            std::string s;
            if (const char *sp = specialName(v)) s = sp;
            else if (firstIndex(behs, v)) s = "\"" + behs[v] + "\"";
            else s = numeric(v);
            return out + " " + s + "}"; // the closing brace is consumed unseen: nothing may come between
        }
        case CHANGE_OF_PLAN: {
            if (k.size() != 2 || k[0].kind != Item::String || !leafAt(1)) return std::nullopt;
            if (!expressible(k[0].text, nullptr, true)) return std::nullopt;
            std::string plan = k[0].text, s = numeric(k[1].value);
            for (char &c : plan) c = char(std::toupper(uint8_t(c)));
            if (opt.planBehaviours && quote(k[0].text).size() < 172) { // a longer token gains the next one
                auto pb = opt.planBehaviours->find(plan);
                if (pb != opt.planBehaviours->end() && firstIndex(pb->second, k[1].value)) s = "\"" + pb->second[k[1].value] + "\"";
            }
            return out + " " + quote(k[0].text) + " " + s + "}";
        }
        case DO_ACTION: case ADD_AGENDA_ITEM: case REMOVE_AGENDA_ITEM: case SET_AGENDA_ITEM_PARAM: case SET_BEHAVIOR_PARAM: {
            if (!leafAt(0)) return std::nullopt;
            std::string h = head(k[0].value, it.id);
            if (h.empty()) return std::nullopt;
            out += " " + h;
            const SymbolTable::Entry *he = syms.lookup(h);
            bool isSend = he && isSendName(he->name);
            int16_t saved = msgObject;
            for (size_t i = 1; i < k.size(); ++i) {
                std::string t;
                if (isSend && i == 1 && leafAt(1)) { // the target
                    uint32_t v = k[1].value;
                    bool block = k.size() > 2 && k[2].kind == Item::Node && k[2].id == DATA_BLOCK;
                    if (block && (v == kBroadcast || v == kMessageData || isPronounTag(uint16_t(v >> 16)))) {
                        t = spell(v, it.id, 1, -1, nullptr); // _BROADCAST_, _MESSAGE_DATA_ or the pronoun's name
                        if (!isIdent(syms, t)) return std::nullopt;
                        std::vector<uint32_t> want;
                        for (const Item &x : k[2].kids) {
                            if (x.kind != Item::Leaf) return std::nullopt;
                            want.push_back(x.value);
                        }
                        std::string m = broadcastName(want);
                        if (m.empty()) return std::nullopt;
                        out += " " + t + " " + m;
                        msgObject = opt.object; // the compiler restores the plan's own list at the message
                        ++i; // the DataBlock is the message's
                        continue;
                    }
                    t = objectName(v);
                    if (!t.empty() && leafAt(2)) {
                        msgObject = int16_t(v);
                        if (messageName(int32_t(k[2].value)).empty()) { msgObject = saved; t.clear(); }
                    } else {
                        t.clear();
                    }
                    if (t.empty()) t = numeric(v);
                } else if (isSend && i == 2 && leafAt(2)) { // the message
                    t = messageName(int32_t(k[2].value));
                    if (t.empty()) t = numeric(k[2].value); // the target's list stays in force
                    else msgObject = opt.object;
                } else {
                    if (!leafAt(i)) { msgObject = saved; return std::nullopt; }
                    t = listLeaf(k[i].value, it.id, i);
                }
                out += " " + t;
            }
            return out + "}";
        }
        default:
            return std::nullopt;
        }
    }

    std::string strLit(const std::string &s) {
        std::string why;
        if (!expressible(s, &why, later))
            throw std::runtime_error("string \"" + s + "\" is not expressible in compiler source: " + why);
        return quote(s);
    }

    // Is v[i] the first behaviour with its name (case-insensitively from 1998)? From 1998 the
    // names also live in 32-byte slots that the compiler searches up to the first empty one, so a
    // name is used only when it and every earlier one fit (1 to 31 characters).
    bool firstIndex(const std::vector<std::string> &v, uint32_t i) const {
        if (i >= v.size()) return false;
        if (later)
            for (size_t k = 0; k <= i; ++k)
                if (v[k].empty() || v[k].size() >= 32) return false;
        for (size_t k = 0; k < i; ++k)
            if (later ? lowerEq(v[k], v[i].c_str()) : v[k] == v[i]) return false;
        std::string raw = v[i];
        for (char &c : raw)
            if (c == '_') c = ' ';
        return expressible(raw, nullptr, later);
    }

    std::string actionShort(int acf) const {
        const char *n = syms.vocab().name(Role::Action, acf);
        return n ? Namespace::stripPrefix(n) : std::string();
    }

    std::string leaf(uint32_t v, uint32_t parent, size_t idx, int64_t acf, std::string *note) {
        if (nextIsTarget()) {
            // A SendMessage target. As an identifier, _BROADCAST_, _MESSAGE_DATA_ or a pronoun
            // would turn every later identifier into a message DataBlock, and anything but a scene
            // object would make the compiler load a missing object's messages.
            // Named, it switches the compiler to the target's messages until the next identifier,
            // so name it only when its message (the next dword) has a name there.
            std::string o = objectName(v);
            if (!o.empty() && pos + 1 < w.size() && w[pos + 1] != kTokNode && w[pos + 1] != kTokString) {
                int16_t saved = msgObject;
                msgObject = int16_t(v);
                bool named = !messageName(int32_t(w[pos + 1])).empty();
                msgObject = saved;
                if (!named) o.clear();
            }
            return o.empty() ? numeric(v) : o;
        }
        if (nextIsMessage()) {
            std::string m = messageName(int32_t(v));
            if (!m.empty()) return m;
        }
        return spell(v, parent, idx, acf, note);
    }

    std::string spell(uint32_t v, uint32_t parent, size_t idx, int64_t acf, std::string *note) {
        if ((parent == CHANGE_TO_BEHAVIOR || parent == CHANGE_TO_NEW_BEHAVIOR) && idx == 0 && !specialName(v)) {
            if (firstIndex(behs, v)) return "\"" + behs[v] + "\"";
            if (note && v < behs.size()) *note = "behaviour \"" + behs[v] + "\"";
            return std::to_string(v);
        }
        if (parent == CHANGE_OF_PLAN && idx == 1 && opt.planBehaviours && (!later || copPlan.size() + 2 < 40)) {
            auto it = opt.planBehaviours->find(copPlan);
            if (it != opt.planBehaviours->end() && firstIndex(it->second, v)) return "\"" + it->second[v] + "\"";
        }
        if (const char *s = specialName(v)) return s;
        uint16_t hi = uint16_t(v >> 16);
        if (hi == kTagPronoun || (later && hi == kTagPronoun1998)) {
            const char *n = syms.vocab().name(Role::Pronoun, int(v & 0xFFFF));
            if (n && syms.pronounTag(n) == hi) {
                const SymbolTable::Entry *e = syms.lookup(n);
                if (e && e->value == int(v & 0xFFFF)) return n;
            }
            return hex8(v);
        }
        // The prescan reads these declarations' names as quoted strings; elsewhere hex is exact.
        if (hi == kTagLocalReal && parent == DECLARE_LOCAL_REALS) return quote(invented("LocalReal", v & 0xFFFF));
        if ((hi == kTagNameRef || hi == kTagLocalReal) && parent == SET_INTERACTOR_PARAM && idx == 1) {
            // A declared name the compiler found among the local reals first keeps that reference.
            const SymbolTable::Entry *n = syms.lookup("id_SET_InteractorName");
            if (n && uint32_t(n->value) == firstLeaf)
                return quote(invented(hi == kTagNameRef ? "Interactor" : "LocalReal", v & 0xFFFF));
        }
        if (hi == kTagLocalReal || hi == kTagNameRef) return hex8(v); // exact; names unknown
        int32_t s = int32_t(v);
        if (!(-0x100000 < s && s < 0x100000)) return formatFloatLiteral(v);
        std::string n;
        if (idx == 0) {
            switch (nodeClass(parent)) {
            case 0x00180000: // DO_*, SET_INTERACTOR_PARAM
                n = parent == SET_INTERACTOR_PARAM ? syms.roleName(Role::InteractorParam, s) : syms.roleName(Role::Action, s);
                break;
            case 0x00170000: n = syms.roleName(Role::Decision, s); break;
            case 0x00050000:
                n = later ? messageName(s) : syms.roleName(Role::Message, s);
                // The 1999 prescan reads on to the next id_MSG_ token after this keyword.
                if (later && n.empty())
                    throw std::runtime_error("message " + std::to_string(s) + " at dword " + std::to_string(pos) +
                                             " has no id_MSG_ name in object " + std::to_string(msgObject) +
                                             "'s messages, and the 1999 prescan needs one (give the scene)");
                break;
            case 0x00030000: n = syms.roleName(Role::CollisionObject, s); break;
            case 0x001A0000:
                if (parent != SET_TIMEOUTMSG) n = syms.roleName(Role::Agenda, s);
                break;
            default:
                if (parent == SET_BEHAVIOR_PARAM) n = syms.roleName(Role::BehaviorParam, s);
                else if (parent == DECLARE_BEHAVIOR) n = syms.roleName(Role::Behavior, s);
            }
        } else if (idx == 1 && nodeClass(parent) == 0x00030000) {
            n = syms.roleName(Role::CollisionObject, s);
        } else if (idx == 1 && parent == SET_TIMEOUTMSG) {
            n = later ? messageName(s) : syms.roleName(Role::Message, s);
        } else if (nodeClass(parent) == 0x00180000 && parent != SET_INTERACTOR_PARAM && acf >= 0) {
            auto it = syms.argHints.find({actionShort(int(acf)), int(idx)});
            if (it != syms.argHints.end()) n = syms.nameIn(it->second, s);
        }
        return n.empty() ? std::to_string(s) : n;
    }

    void node(int depth, uint32_t parent, size_t idx, int64_t acf) {
        if (depth > kMaxNesting)
            throw std::runtime_error("statements nested more than " + std::to_string(kMaxNesting) + " deep at dword " + std::to_string(pos));
        std::string ind;
        for (int i = 0; i < std::min(depth, kMaxIndent); ++i) ind += opt.indent;
        size_t start = pos;
        uint32_t t = at(pos);
        if (t == kTokNode) {
            uint32_t id = at(pos + 1);
            pos += 2;
            if (id == END_PLAN) {
                token("END_PLAN");
                lines.push_back(ind + "END_PLAN");
                return;
            }
            const char *kw = nodeIdName(id);
            if (!kw) throw std::runtime_error("unknown node id " + hex8(id) + " at dword " + std::to_string(start));
            if (!isKeyword(id))
                throw std::runtime_error(std::string(kw) + " at dword " + std::to_string(start) + " is not a keyword of " +
                                         compatName(syms.compat));
            uint32_t cnt = at(pos++);
            firstLeaf = 0xFFFFFFFF;
            token(kw);
            token(std::to_string(cnt));
            uint32_t n = cnt & 0x7FFF;
            int64_t myAcf = nodeClass(id) == 0x00180000 && id != SET_INTERACTOR_PARAM ? int64_t(int32_t(at(pos))) : -1;
            if (depth == 0 && id == DECLARE_BEHAVIOR && !lines.empty() && !lines.back().empty()) lines.push_back("");
            std::string line = ind + kw + " " + std::to_string(cnt);
            std::vector<std::string> notes;
            uint32_t k = 0;
            auto planEnds = [&]() { return pos < w.size() && w[pos] == kTokNode && pos + 1 < w.size() && w[pos + 1] == END_PLAN; };
            for (; k < n && !planEnds() && at(pos) != kTokNode; ++k) {
                if (at(pos) == kTokString) {
                    std::string s = readStr();
                    line += " " + strLit(s);
                    token("\"");
                    if (id == CHANGE_OF_PLAN && k == 0) {
                        copPlan = s;
                        for (char &c : copPlan) c = char(std::toupper(uint8_t(c)));
                    }
                } else {
                    std::string note;
                    if (k == 0) firstLeaf = at(pos);
                    std::string lt = leaf(at(pos), id, k, myAcf, opt.annotate ? &note : nullptr);
                    token(lt);
                    line += " " + lt;
                    if (!note.empty()) notes.push_back(note);
                    ++pos;
                }
            }
            if (opt.annotate) {
                if (id == DECLARE_BEHAVIOR) notes.insert(notes.begin(), "#" + std::to_string(behDecl++));
                if (cnt & 0x8000) notes.push_back("count has bit 15 set");
                if (opt.annotateOffsets) notes.push_back("@" + std::to_string(start + 1)); // dword offset incl. header
                std::string c;
                for (auto &s : notes) c += (c.empty() ? "" : "; ") + s;
                if (!c.empty()) line += "  // " + c;
            } else if (id == DECLARE_BEHAVIOR) {
                behDecl++;
            }
            lines.push_back(line);
            for (uint32_t i = k; i < n && !planEnds(); ++i) { // a count may claim more than the plan holds
                size_t first = lines.size();
                node(depth + 1, id, i, myAcf);
                // decision branch value = child index - 1 (DECIDE_BY_WITH_AMONG: child 1 is the DataBlock)
                if (opt.annotate && (id == DECIDE_BY_AMONG || id == DECIDE_BY_WITH_AMONG) && i >= (id == DECIDE_BY_AMONG ? 1u : 2u) &&
                    first < lines.size())
                    lines[first] += (lines[first].find("  // ") == std::string::npos ? "  // " : "; ") + std::string("=") + std::to_string(i - 1);
            }
            return;
        }
        if (t == kTokString) {
            lines.push_back(ind + strLit(readStr()));
            token("\"");
            return;
        }
        std::string lt = leaf(t, parent, idx, acf, nullptr); // leaf() may look at the next dword
        ++pos;
        token(lt);
        lines.push_back(ind + lt);
    }
};

} // namespace

std::string formatFloatLiteral(uint32_t bits) {
    float f;
    std::memcpy(&f, &bits, 4);
    if (std::isfinite(f)) {
        for (int p = 1; p < 12; ++p) {
            char b[64];
            std::snprintf(b, sizeof b, "%.*g", p, double(f));
            std::string s = b;
            if (s.find_first_of("eEnN") != std::string::npos) continue;
            float g = float(std::strtod(b, nullptr));
            uint32_t gb;
            std::memcpy(&gb, &g, 4);
            if (gb == bits) return s.find('.') != std::string::npos ? s : s + ".0";
        }
    }
    return hex8(bits);
}

std::vector<std::string> behaviourNames(const std::vector<uint32_t> &w, Compat compat) {
    std::vector<std::string> out;
    for (size_t i = 0; i + 4 < w.size(); ++i) {
        // From 1998 a declaration's name is the next quoted token anywhere, so a DECLARE_BEHAVIOR
        // without its string shifts every later name: refer to behaviours by number then.
        if (isLaterCompat(compat) && w[i] == kTokNode && w[i + 1] == DECLARE_BEHAVIOR && w[i + 4] != kTokString) return {};
        if (w[i] == kTokNode && w[i + 1] == DECLARE_BEHAVIOR && w[i + 4] == kTokString) {
            std::string nm;
            for (size_t j = i + 5; j < w.size() && w[j]; ++j) nm += char(w[j] & 0xFF);
            for (char &c : nm)
                if (c == ' ') c = '_';
            out.push_back(nm);
        }
    }
    return out;
}

namespace {
bool decompileOnce(const std::vector<uint32_t> &words, const SymbolTable &syms, const DecompileOptions &opt,
                   std::string &out, std::string *err);
}

bool decompile(const std::vector<uint32_t> &words, const SymbolTable &syms, const DecompileOptions &opt,
               std::string &out, std::string *err) {
    // The header is only a comment: it is added after the check, so it cannot push the source
    // past the compiler's size limit.
    DecompileOptions body = opt;
    body.header = false;
    if (!decompileOnce(words, syms, body, out, err)) return false;
    if (opt.verify) {
        // Compile the source again; braced output that does not rebuild the plan is retried flat.
        auto rebuilds = [&](const std::string &text) -> std::string {
            CompileOptions co;
            co.compat = syms.compat;
            co.scene = opt.scene;
            co.object = opt.object;
            co.planName = opt.planName.empty() ? "plan" : opt.planName;
            co.planSource = [&](const std::string &plan) -> std::optional<std::string> {
                if (!opt.planBehaviours) return std::nullopt;
                std::string key = plan;
                for (char &c : key) c = char(std::toupper(uint8_t(c)));
                auto it = opt.planBehaviours->find(key);
                if (it == opt.planBehaviours->end()) return std::nullopt;
                std::string src; // the declarations are all a sibling's prescan reads
                for (const std::string &b : it->second) src += "DECLARE_BEHAVIOR 2 _0_ \"" + b + "\"\n"; // read by either prescan
                return src + "END_PLAN\n";
            };
            CompileResult r = compilePlan(text, *opt.verify, co);
            if (!r.ok) return "it does not compile (" + (r.error() ? r.error()->message : std::string("no reason given")) + ")";
            std::vector<uint32_t> got;
            for (size_t i = 4; i + 3 < r.pbn.size(); i += 4)
                got.push_back(uint32_t(r.pbn[i]) | uint32_t(r.pbn[i + 1]) << 8 | uint32_t(r.pbn[i + 2]) << 16 | uint32_t(r.pbn[i + 3]) << 24);
            if (got == words) return {};
            size_t diff = 0;
            while (diff < words.size() && diff < got.size() && words[diff] == got[diff]) ++diff;
            return "it compiles to different dwords from dword " + std::to_string(diff);
        };
        std::string why = rebuilds(out);
        if (!why.empty() && opt.braced) {
            DecompileOptions flat = body;
            flat.braced = false;
            std::string flatWhy = "the flat form fails to decompile";
            if (decompileOnce(words, syms, flat, out, err)) flatWhy = rebuilds(out);
            why = flatWhy.empty() ? std::string() : "braced: " + why + "; flat: " + flatWhy;
        }
        if (!why.empty()) {
            if (err) *err = "not expressible: the plan holds a construct its compiler's source cannot say again; the source written for it: " + why;
            out.clear();
            return false;
        }
    }
    if (opt.header) {
        const char *nl = opt.crlf ? "\r\n" : "\n";
        std::string n = opt.planName.empty() ? "plan" : opt.planName;
        out = "// " + n + ".txt -- ADLIB compiler source decompiled from " + n + ".PBN by `adlib decompile`." + nl +
              "// Layout and symbol spelling are reconstructions; the token stream recompiles" + nl +
              "// byte-identically (docs/tools.md)." + nl + nl + out;
    }
    size_t cap = isLaterCompat(syms.compat) ? kMaxCompilerSource1999 : kMaxCompilerSource;
    if (out.size() > cap && err)
        *err = "warning: " + std::to_string(out.size()) + " bytes exceeds the original compiler's source limit (" +
               std::to_string(cap) + "); drop --annotate/--crlf or split the plan";
    return true;
}

namespace {
bool decompileOnce(const std::vector<uint32_t> &words, const SymbolTable &syms, const DecompileOptions &opt,
                   std::string &out, std::string *err) {
    Decompiler d{words, syms, opt, behaviourNames(words, syms.compat), {}, 0, {}, 0};
    std::vector<std::string> blocks;
    for (size_t i = 0; i + 1 < words.size(); ++i) { // every string, for invented names to avoid
        if (words[i] != kTokString) continue;
        std::string t;
        for (size_t j = i + 1; j < words.size() && words[j]; ++j) t += char(std::tolower(int(words[j] & 0xFF)));
        d.strings.push_back(t);
    }
    try {
        // Braced blocks while every statement has one; the message table they leave carries on.
        if (opt.braced && isLaterCompat(syms.compat) && !opt.annotate)
            while (d.pos < words.size()) {
                size_t p = d.pos;
                Decompiler::Item it;
                if (!d.readItem(p, it, 0)) break;
                int16_t savedObject = d.msgObject;
                std::optional<std::string> b = d.braced(it, 0);
                if (!b) {
                    d.msgObject = savedObject;
                    break;
                }
                blocks.push_back(*b);
                d.pos = p;
            }
        while (d.pos < words.size()) d.node(0, 0, 0, -1);
    } catch (const std::exception &e) {
        if (err) *err = e.what();
        return false;
    }
    if (err) err->clear();
    const char *nl = opt.crlf ? "\r\n" : "\n";
    out.clear();
    for (const std::string &b : blocks) {
        for (char c : b) {
            if (c == '\n') out += nl;
            else out += c;
        }
        out += nl;
    }
    for (auto &l : d.lines) out += l + nl;
    return true;
}
} // namespace

} // namespace adlib::tools
