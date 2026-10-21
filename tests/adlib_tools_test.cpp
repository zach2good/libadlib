// adlib-tools unit tests (ctest: adlib_tools_test FIXTURES); no external data.
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "adlib/adlib.h"
#include "adlib/tools.h"

using namespace adlib;
using namespace adlib::tools;
namespace fs = std::filesystem;

static int failures = 0;
#define CHECK(c)                                                                  \
    do {                                                                          \
        if (!(c)) {                                                               \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #c); \
            ++failures;                                                           \
        }                                                                         \
    } while (0)

static bool contains(const std::string &h, const std::string &n) { return h.find(n) != std::string::npos; }

static std::string readText(const std::string &p) {
    std::ifstream f(p, std::ios::binary);
    std::ostringstream s;
    s << f.rdbuf();
    return s.str();
}

// Non-blank, non-comment lines, CR stripped.
static std::vector<std::string> codeLines(const std::string &t) {
    std::vector<std::string> out;
    std::istringstream is(t);
    std::string l;
    while (std::getline(is, l)) {
        if (!l.empty() && l.back() == '\r') l.pop_back();
        size_t a = l.find_first_not_of(' ');
        if (a == std::string::npos || l.compare(a, 2, "//") == 0) continue;
        out.push_back(l);
    }
    return out;
}

struct Dummy : PlanHost {
    int16_t i;
    explicit Dummy(int16_t x) : i(x) {}
    int16_t objectIndex() const override { return i; }
};

static void testFloats() {
    CHECK(formatFloatLiteral(0x3F800000) == "1.0");
    CHECK(formatFloatLiteral(0x3FC00000) == "1.5");
    CHECK(formatFloatLiteral(0xBF800000) == "-1.0");
    CHECK(formatFloatLiteral(0x7FC00000) == "0x7fc00000"); // NaN: hex
}

static void testVending() {
    SymbolTable syms;
    std::string err;
    CHECK(loadSymbolsDir(syms, VENDING_DIR, &err));
    CHECK(syms.lookup("ID_ACF_ADDCREDIT") && syms.lookup("id_ACF_AddCredit")->value == 0);
    CHECK(syms.roleName(Role::Message, syms.vocab().id(Role::Message, "Coin")) == "id_MSG_Coin");

    // Plan built in code (independent of the example's generated files).
    const Vocabulary &V = syms.vocab();
    using namespace build;
    std::vector<std::unique_ptr<Node>> top;
    top.push_back(behavior(V.id(Role::Behavior, "Machine"), "Idle", list(
        onMessage(V.id(Role::Message, "Coin"), list(changeTo(1)), list(action(V.id(Role::Action, "AddCredit")))),
        onMessage(V.id(Role::Message, "Select"), {}, list(decide(V.id(Role::Decision, "EnoughCredit"), list(
            changeTo(kNoBehChange), changeTo(1))))))));
    top.push_back(behavior(V.id(Role::Behavior, "Machine"), "Has Credit", list(
        onMessage(V.id(Role::Message, "Refund"), {}, list(changeTo(0), timeout(100, V.id(Role::Message, "VendDone"), pronoun(0)))))));
    std::vector<uint32_t> words = encodePlan(top);

    std::string src;
    DecompileOptions o;
    o.planName = "TEST";
    CHECK(decompile(words, syms, o, src, &err));
    CHECK(contains(src, "DECLARE_BEHAVIOR 4 id_BEH_Machine \"Idle\""));
    CHECK(contains(src, "SET_MESSAGE_INTERACTOR 3 id_MSG_Coin"));
    CHECK(contains(src, "CHANGE_TO_BEHAVIOR 1 \"Has_Credit\""));
    CHECK(contains(src, "DECIDE_BY_AMONG 3 id_DCF_EnoughCredit"));
    CHECK(contains(src, "SET_TIMEOUTMSG 3 100 id_MSG_VendDone id_PRN_Me"));
    CHECK(contains(src, "END_PLAN"));
    o.annotate = true;
    CHECK(decompile(words, syms, o, src, &err) && contains(src, "\"Has_Credit\"  // #1"));

    // A string the compiler cannot express ('_') is an error, not silent damage.
    std::vector<std::unique_ptr<Node>> bad;
    bad.push_back(behavior(0, "bad_name", list(onMessage(0, list(changeTo(0)), {}))));
    CHECK(!decompile(encodePlan(bad), syms, o, src, &err) && contains(err, "not expressible"));

    PlanTree tree;
    CHECK(buildPlanTree(words, LoadHooks{}, tree, &err));
    tree.name = "TEST";
    PlanModel m = analyze(tree, &syms);
    CHECK(m.behaviours.size() == 2);
    bool coin = false, sel = false, prevBack = false;
    for (auto &t : m.transitions) {
        coin |= t.from == 0 && t.to == 1 && t.trigger == "MSG Coin" && t.viaOption;
        sel |= t.from == 0 && t.to == 1 && t.cond == "EnoughCredit=1" && !t.viaOption;
        prevBack |= t.from == 1 && t.to == 0;
    }
    CHECK(coin && sel && prevBack);
    CHECK(m.timers.size() == 1 && m.timers[0].msg == "VendDone");
    std::string dot = graphDot(m), mmd = graphMermaid(m);
    CHECK(contains(dot, "b0 -> b1") && contains(dot, "MSG Coin"));
    CHECK(contains(mmd, "flowchart LR") && contains(mmd, "b0 -->"));
    CHECK(unreachableBehaviours(m).empty());

    auto issues = lintPlan(tree, &syms, &words);
    int errs = 0;
    for (auto &i : issues) errs += i.severity == Issue::Severity::Error;
    CHECK(errs == 0);
    CHECK(checkIds(tree, syms).empty());

    // lint: a value in an action list, an unknown action id
    std::vector<std::unique_ptr<Node>> lt;
    std::vector<Arg> en;
    en.push_back(lit(5));
    en.push_back(node(action(99)));
    lt.push_back(behavior(0, "X", list(onMessage(0, {}, {}))));
    lt[0]->args[2].node->args.push_back(node(make(ENABLE, std::move(en))));
    PlanTree lt2;
    CHECK(buildPlanTree(encodePlan(lt), LoadHooks{}, lt2, &err));
    issues = lintPlan(lt2, &syms);
    bool valueErr = false;
    for (auto &i : issues) valueErr |= i.severity == Issue::Severity::Error && contains(i.message, "value in an action list");
    CHECK(valueErr);
    auto ids = checkIds(lt2, syms);
    CHECK(!ids.empty() && contains(ids[0].message, "unknown action id 99"));

    // compare
    std::string out;
    CHECK(comparePlans(tree, tree, &syms, out) == 0);
    std::vector<std::unique_ptr<Node>> top2;
    top2.push_back(behavior(V.id(Role::Behavior, "Machine"), "Idle", list(
        onMessage(V.id(Role::Message, "Coin"), list(changeTo(0)), list(action(V.id(Role::Action, "AddCredit")))))));
    PlanTree t2;
    CHECK(buildPlanTree(encodePlan(top2), LoadHooks{}, t2, &err));
    int nd = comparePlans(tree, t2, &syms, out);
    CHECK(nd >= 2 && contains(out, "0:DECLARE_BEHAVIOR\"Idle\"/2:SET_MESSAGE_INTERACTOR(Coin)/1:CHANGE_TO_BEHAVIOR/0: value"));
    CHECK(contains(out, "removed DECLARE_BEHAVIOR"));

    // tracer on a live run
    auto vocab = syms.vocabPtr();
    Registry reg(vocab);
    reg.log = [](const std::string &) {};
    int credit = 0;
    reg.action("AddCredit", [&](ActionCtx &) { ++credit; });
    reg.decision("EnoughCredit", [&](ActionCtx &) -> uint16_t { return credit >= 1 ? 1 : 0; });
    reg.behavior("Machine", [] { return std::make_unique<Behavior>(); });
    std::vector<std::string> lines;
    auto tracer = Tracer::attach(reg, "all", &err);
    CHECK(tracer);
    tracer->sink = [&](const std::string &l) { lines.push_back(l); };
    tracer->objectName = [](PlanObject &) { return std::string("machine"); };
    PlanScene scene;
    Dummy host(0);
    PlanObject obj(scene, host, reg);
    scene.add(&obj);
    CHECK(obj.loadPlanFromWords("VENDING", words));
    obj.start(0, 0);
    scene.clock = 7;
    obj.sendMessage(V.id(Role::Message, "Coin"), 0, 1);
    scene.update();
    std::string all;
    for (auto &l : lines) all += l + "\n";
    CHECK(contains(all, "[tick 7] obj=machine(0) plan=VENDING beh=Idle MSG Coin data=0 from=1"));
    CHECK(contains(all, "beh=Idle ACF AddCredit()"));
    CHECK(contains(all, "CHANGE Idle -> Has Credit"));
    // filters
    lines.clear();
    CHECK(tracer->filter.parse("kind=CHANGE|TIMER, beh=Has*", &err));
    obj.sendMessage(V.id(Role::Message, "Refund"), 0, 1);
    scene.update();
    all.clear();
    for (auto &l : lines) all += l + "\n";
    CHECK(contains(all, "TIMER set 100 VendDone -> Me") && contains(all, "CHANGE Has Credit -> Idle"));
    CHECK(!contains(all, "MSG "));
    // offline filtering of the same format
    TraceFilter f;
    std::map<std::string, bool> ctx;
    CHECK(f.parse("obj=machine,tick=5..9,kind=ACF", &err));
    CHECK(traceLineMatches(f, "[tick 7] obj=machine(0) plan=VENDING beh=Idle ACF AddCredit()", ctx));
    CHECK(!traceLineMatches(f, "[tick 10] obj=machine(0) plan=VENDING beh=Idle ACF AddCredit()", ctx));
    CHECK(!traceLineMatches(f, "[tick 7] obj=machine(0) plan=VENDING beh=Idle MSG Coin data=0 from=1", ctx));
    CHECK(f.parse("msg=Coin", &err));
    CHECK(traceLineMatches(f, "[tick 7] obj=m(0) plan=V beh=Idle MSG Coin data=0 from=1", ctx));
    CHECK(traceLineMatches(f, "[tick 7] obj=m(0) plan=V beh=Idle ACF AddCredit()", ctx));
    CHECK(!traceLineMatches(f, "[tick 8] obj=m(0) plan=V beh=Idle ACF AddCredit()", ctx));
    CHECK(!f.parse("bogus=1", &err));
}

// Every plan the 1999 corpus accepts decompiles, in its dialect, to source that recompiles to the
// same bytes: SendMessage targets, scene messages, pronoun tags, CHANGE_TO_NEW_BEHAVIOR.
static void testLaterRoundTrip(const std::string &fixtures, Compat compat, bool braced) {
    std::string vocab = fixtures + "/vocab1999", err;
    CompilerScene scene;
    CHECK(loadSceneFile(vocab + "/scene.sbn", scene, &err));
    SymbolTable plainNames, sceneNames;
    plainNames.compat = sceneNames.compat = compat;
    CHECK(loadSymbolsDir(plainNames, vocab, &err));
    CHECK(loadSymbolsDir(sceneNames, vocab, &err, &scene));
    CompilerSymbols plain = CompilerSymbols::fromDataDir(vocab, compat, &err);
    CompilerSymbols withScene = CompilerSymbols::fromDataDir(vocab, compat, &err, &scene);
    int tried = 0, same = 0, bracedBlocks = 0;
    for (const char *kind : {"valid", "quirks", "scene"}) {
        std::vector<fs::path> files;
        for (auto &de : fs::directory_iterator(fixtures + "/compiler1999/" + kind))
            if (de.path().extension() == ".txt") files.push_back(de.path());
        std::sort(files.begin(), files.end());
        for (const fs::path &f : files) {
            std::string src = readText(f.string());
            CompileOptions o;
            o.compat = compat;
            o.planSource = [&](const std::string &n) -> std::optional<std::string> {
                std::string t = readText(fixtures + "/compiler1999/plans/" + n + ".txt");
                if (t.empty()) return std::nullopt;
                return t;
            };
            bool sceneCase = src.rfind("// object ", 0) == 0;
            if (sceneCase) {
                o.scene = &scene;
                o.object = int16_t(std::atoi(src.c_str() + 10));
            }
            CompileResult a = compilePlan(src, sceneCase ? withScene : plain, o);
            if (!a.ok) continue; // e.g. SET_AGENDA_ITEM_PARAM under 1998
            PlanTree tree;
            if (!buildPlanTree(a.words, LoadHooks{}, tree, &err)) continue; // a quirk: counts that build no tree
            ++tried;
            DecompileOptions d;
            d.planName = f.stem().string();
            d.scene = o.scene;
            d.object = o.object;
            d.braced = braced;
            std::string text;
            std::vector<uint32_t> words(a.words.begin(), a.words.end());
            words.resize(size_t(std::find(words.begin(), words.end(), 0xFFFF0000u) - words.begin()) + 1);
            bool ok = decompile(words, sceneCase ? sceneNames : plainNames, d, text, &err);
            CompileResult b = ok ? compilePlan(text, sceneCase ? withScene : plain, o) : CompileResult{};
            // The header counts dwords after END_PLAN, which no source can say again.
            ok = ok && b.ok && std::vector<uint8_t>(b.pbn.begin() + 4, b.pbn.end()) == std::vector<uint8_t>(a.pbn.begin() + 4, a.pbn.end());
            if (!ok) std::fprintf(stderr, "%s round trip %s: %s\n%s\n", compatName(compat), f.string().c_str(), err.c_str(), text.c_str());
            same += ok;
            bracedBlocks += ok && text.find("\n{") != std::string::npos;
        }
    }
    CHECK(tried > 30 && same == tried);
    CHECK(!braced || bracedBlocks > 20);
    std::printf("%s%s decompile round trip: %d/%d (%d with braced blocks)\n", compatName(compat), braced ? " braced" : "",
                same, tried, bracedBlocks);

    // The decompiler's spellings: pronoun tags by name, a broadcast target as a number, a scene
    // object and its message by name.
    CompileOptions o;
    o.compat = compat;
    o.scene = &scene;
    o.object = 1;
    CompileResult r = compilePlan("DO_ACTION 4 id_ACF_SendMessage _BROADCAST_ id_MSG_Swing 3\n"
                                  "DO_ACTION 3 id_ACF_SendMessage id_MOD_Batter id_MSG_Catch\n"
                                  "DataBlock 2 id_PRN_Target id_PRT_ViewOwner END_PLAN\n", withScene, o);
    CHECK(r.ok);
    DecompileOptions d;
    d.scene = &scene;
    d.object = 1;
    std::string text;
    CHECK(decompile(r.words, sceneNames, d, text, &err));
    CHECK(contains(text, "id_ACF_SendMessage 65535"));
    CHECK(contains(text, "id_ACF_SendMessage id_MOD_Batter id_MSG_")); // the main table's names come first
    CHECK(contains(text, "id_PRN_Target id_PRT_ViewOwner"));
    // A plan the 1998 compiler cannot express.
    if (compat == Compat::Adlib1998) {
        auto bad = encodePlan(build::list(build::make(SET_AGENDA_ITEM_PARAM, build::args(build::lit(2))), build::endPlan()));
        CHECK(!decompile(bad, plainNames, DecompileOptions{}, text, &err) && contains(err, "not a keyword of adlib1998"));
    }
    CHECK(!parseScene("not a scene", scene, &err) && !err.empty());
}

// The checks decompile makes on its own output (DecompileOptions::verify), and the limits it keeps.
static void testVerify(const std::string &fixtures) {
    std::string vocab = fixtures + "/vocab1999";
    std::string err;
    std::string text;
    SymbolTable names;
    names.compat = Compat::Adlib1999;
    CompilerSymbols compiler;
    CHECK(loadSymbolsDir(names, vocab, &err, nullptr, &compiler));
    CompileOptions co;
    co.compat = Compat::Adlib1999;
    DecompileOptions d;
    d.verify = &compiler;

    // Every dword is compared, those after END_PLAN too.
    CompileResult plan = compilePlan("DataBlock 2 1 5\nEND_PLAN\n", compiler, co);
    CHECK(plan.ok);
    for (uint32_t extra : {5u, 0xFFFFFFFFu, 0x10000u, 0xFEDD0000u}) {
        std::vector<uint32_t> w = plan.words;
        w.push_back(extra);
        if (!decompile(w, names, d, text, &err)) {
            CHECK(contains(err, "not expressible"));
            continue;
        }
        CompileResult again = compilePlan(text, compiler, co);
        CHECK(again.ok && again.words == w);
    }

    // The header comment is added after the check, so it cannot push the source over the limit.
    auto statements = [](int k) {
        std::string s;
        for (int i = 0; i < k; ++i) s += "DataBlock 2 1 5\n";
        return s + "END_PLAN\n";
    };
    auto bodySize = [&](int k) {
        DecompileOptions bare;
        bare.header = false;
        std::string t;
        decompile(compilePlan(statements(k), compiler, co).words, names, bare, t, &err);
        return t.size();
    };
    const size_t line = bodySize(2) - bodySize(1);
    const int k = int((kMaxCompilerSource1999 - 100 - bodySize(0)) / line);
    CompileResult big = compilePlan(statements(k), compiler, co);
    CHECK(big.ok && decompile(big.words, names, d, text, &err));
    CHECK(text.size() > kMaxCompilerSource1999 && contains(err, "warning:"));

    // From 1998 a declaration without its name shifts the later ones, so none are named.
    const std::vector<uint32_t> unnamed = {kTokNode, DECLARE_BEHAVIOR, 2, 0, 7,
                                           kTokNode, DECLARE_BEHAVIOR, 2, 0, kTokString, 'B', 0,
                                           kTokNode, END_PLAN};
    CHECK(behaviourNames(unnamed) == std::vector<std::string>{"B"});
    CHECK(behaviourNames(unnamed, Compat::Adlib1999).empty());

    // A CHANGE_OF_PLAN name of 40 characters or more reaches the sibling lookup changed: no name.
    const std::string longName(45, 'L');
    const std::map<std::string, std::vector<std::string>> siblings = {{longName, {"A", "B"}}, {"SHORT", {"A", "B"}}};
    DecompileOptions named;
    named.planBehaviours = &siblings;
    for (const std::string &plan : {longName, std::string("SHORT")}) {
        CompileResult c = compilePlan("CHANGE_OF_PLAN 2 \"" + plan + "\" 1\nEND_PLAN\n", compiler, co);
        CHECK(c.ok && decompile(c.words, names, named, text, &err));
        CHECK(contains(text, "\"B\"") == (plan == "SHORT"));
    }

    // A count claiming more statements than the stream holds is an error, not the plan's end.
    const std::vector<uint32_t> cut = {kTokNode, BLOCK, 2, kTokNode, DATA_BLOCK, 1, 5};
    CHECK(!decompile(cut, names, DecompileOptions{}, text, &err) && contains(err, "truncated"));
}

int main(int argc, char **argv) {
    testFloats();
    testVending();
    CHECK(argc > 1); // ctest passes the fixtures directory
    if (argc > 1) {
        for (bool braced : {false, true}) {
            testLaterRoundTrip(argv[1], Compat::Adlib1999, braced);
            testLaterRoundTrip(argv[1], Compat::Adlib1998, braced);
        }
        testVerify(argv[1]);
    }
    if (failures) std::fprintf(stderr, "%d failure(s)\n", failures);
    else std::printf("adlib_tools_test: ok\n");
    return failures ? 1 : 0;
}
