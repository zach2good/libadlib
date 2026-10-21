// adlibc: compile ADLIB plan source (.txt) to a .PBN, byte-identical to the 1996 ADLIB compiler.
// See docs/adlibc.md.
//
//   adlibc [--symbols DIR | [--reserved F] --enumids F [--anims F] [--sounds F] [--texts F]]
//          [--compat=adlib|adlib1996|adlib1998|adlib1999] [--plans-dir DIR] [--strict] [--Werror]
//          [--scene F.sbn] [-o out.pbn] [-q] file.txt
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "adlib/version.h"
#include "adlib/compiler.h"
#include "adlib/pbn.h"
#ifdef ADLIBC_LINT
#include "adlib/tools.h"
#endif

using namespace adlib;

namespace {

void usage() {
    std::fprintf(stderr,
        "usage: adlibc [symbols] [--compat=MODE] [--plans-dir DIR] [--strict] [--Werror] [-o OUT.pbn] [-q] FILE.txt\n"
        "symbols (read the way the chosen compiler read them):\n"
        "  --symbols DIR           a directory in that compiler's layout. 1996: enumIDs.h,\n"
        "                          AnimData/Animassm.txt, Sndfiles.lst, Infobtxt.lst. 1998 on:\n"
        "                          reservedEnumIDs.h, enumIDs.h, AnimData/Animassm.txt, SndFiles.lst\n"
        "                          (names case-insensitive; the lists may be missing)\n"
        "  --reserved F --enumids F --anims F --sounds F --texts F   individual files\n"
        "compatibility (docs/adlibc.md, docs/dialects.md):\n"
        "  --compat=adlib          (default) the ADLIB language, any vocabulary, with the 1996\n"
        "                          implementation limits lifted\n"
        "  --compat=adlib1996      the 1996 compiler exactly, including its fixed limits; input it\n"
        "                          would crash or hang on is an error, mangled-but-accepted input warns\n"
        "  --compat=adlib1998      the 1998 language (Baseball 3D): adlib1999 without\n"
        "                          SET_AGENDA_ITEM_PARAM\n"
        "  --compat=adlib1999      the 1999 compiler exactly (Baseball 2000 and 2001)\n"
        "options:\n"
        "  --scene F         1998 on: the scene the plan belongs to (objects, their ids and messages):\n"
        "                    a compiled .sbn, or a stadium's .cfg built with the plan sources\n"
        "  --object N        the scene object the plan is compiled for (default: the first whose type\n"
        "                    lists the plan)\n"
        "  --plans-dir DIR   where CHANGE_OF_PLAN finds sibling plans (default: FILE's directory)\n"
        "  --strict          warn about input the original accepted silently (output unchanged)\n"
        "  --Werror          strict warnings are errors (no output is written)\n"
        "  -o OUT            output file (default: <plan name>.pbn in the current directory)\n"
        "  -q                no summary line\n"
        "  --version         print the version\n");
}

bool readFile(const std::string &p, std::string &out) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

std::string lower(std::string s) {
    for (char &c : s) if (c >= 'A' && c <= 'Z') c = char(c + 32);
    return s;
}

std::optional<std::string> findPlan(const std::string &dir, const std::string &name) {
    std::string want = lower(name + ".txt"), found;
    std::error_code ec;
    for (const auto &e : std::filesystem::directory_iterator(dir.empty() ? "." : dir, ec)) {
        const std::string fileName = e.path().filename().string();
        if (lower(fileName) == want) { found = fileName; break; }
    }
    if (found.empty()) return std::nullopt;
    std::string text;
    if (!readFile((dir.empty() ? "." : dir) + "/" + found, text)) return std::nullopt;
    return text;
}

} // namespace

int main(int argc, char **argv) {
    std::string symDir, outPath, plansDir, input, scenePath, objectArg;
    bool haveFiles = false, strict = false, werror = false, quiet = false;
    Compat compat = Compat::Adlib;
    SymbolFiles files;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto val = [&](std::string &dst) {
            if (i + 1 >= argc) { usage(); std::exit(2); }
            dst = argv[++i];
        };
        auto fileArg = [&](std::optional<std::string> &dst) {
            std::string p, t;
            val(p);
            if (!readFile(p, t)) { std::fprintf(stderr, "adlibc: cannot read %s\n", p.c_str()); std::exit(2); }
            dst = t;
            haveFiles = true;
        };
        if (a.rfind("--compat=", 0) == 0) {
            if (!parseCompat(a.substr(9), compat)) {
                std::fprintf(stderr, "adlibc: unknown mode %s (adlib, adlib1996, adlib1998, adlib1999)\n", a.c_str());
                return 2;
            }
        }
        else if (a == "--symbols") val(symDir);
        else if (a == "--reserved") fileArg(files.reservedEnumIds);
        else if (a == "--enumids") fileArg(files.enumIds);
        else if (a == "--anims") fileArg(files.animAssm);
        else if (a == "--sounds") fileArg(files.sndFiles);
        else if (a == "--texts") fileArg(files.infobTxt);
        else if (a == "--plans-dir") val(plansDir);
        else if (a == "--scene") val(scenePath);
        else if (a == "--object") val(objectArg);
        else if (a == "--strict") strict = true;
        else if (a == "--Werror" || a == "-Werror") { werror = true; strict = true; }
        else if (a == "-o") val(outPath);
        else if (a == "-q") quiet = true;
        else if (a == "-h" || a == "--help") { usage(); return 0; }
        else if (a == "--version") { std::printf("adlibc %s\n", adlib::kVersion); return 0; }
        else if (!a.empty() && a[0] == '-') { std::fprintf(stderr, "adlibc: unknown option %s\n", a.c_str()); usage(); return 2; }
        else input = a;
    }
    if (input.empty() || (symDir.empty() && !haveFiles)) { usage(); return 2; }

    if ((!scenePath.empty() || !objectArg.empty()) && !isLaterCompat(compat)) {
        std::fprintf(stderr, "adlibc: --scene and --object need --compat=adlib1998 or adlib1999\n");
        return 2;
    }
    if (!objectArg.empty() && scenePath.empty()) {
        std::fprintf(stderr, "adlibc: --object needs --scene\n");
        return 2;
    }
    if (plansDir.empty()) {
        size_t s = input.find_last_of("/\\");
        plansDir = s == std::string::npos ? "." : input.substr(0, s);
    }
    CompilerScene scene;
    bool sceneSources = scenePath.size() > 4 && lower(scenePath.substr(scenePath.size() - 4)) == ".cfg";
    if (!scenePath.empty() && !sceneSources) {
        std::string text, err;
        if (!readFile(scenePath, text) || !parseScene(text, scene, &err)) {
            std::fprintf(stderr, "adlibc: %s: %s\n", scenePath.c_str(), err.empty() ? "cannot read" : err.c_str());
            return 2;
        }
    }
    if (sceneSources) { // a .cfg: build the scene from it and the plan sources, against the symbols alone
        std::string text, err;
        CompilerSymbols bare;
        try {
            if (!symDir.empty()) bare = CompilerSymbols::fromDataDir(symDir, compat, &err);
            else bare = CompilerSymbols::fromFiles(files, compat);
        } catch (const std::exception &e) {
            err = e.what();
        }
        if (err.empty() && !readFile(scenePath, text)) err = "cannot read";
        if (err.empty())
            buildScene(text, bare, [&](const std::string &p) { return findPlan(plansDir, p); }, scene, &err);
        if (!err.empty()) {
            std::fprintf(stderr, "adlibc: %s: %s\n", scenePath.c_str(), err.c_str());
            return 2;
        }
    }
    const CompilerScene *scenePtr = scenePath.empty() ? nullptr : &scene;
    // Each compiler reads the symbol files its own way. --compat=adlib1996 enforces the original
    // loader's limits; otherwise the headers are taken as written, and anything the 1996 loader
    // would have mishandled is reported under --strict.
    CompilerSymbols syms;
    try {
        if (!symDir.empty()) {
            std::string err;
            syms = CompilerSymbols::fromDataDir(symDir, compat, &err, scenePtr);
            if (!err.empty()) { std::fprintf(stderr, "adlibc: %s\n", err.c_str()); return 2; }
        } else {
            syms = CompilerSymbols::fromFiles(files, compat, scenePtr);
        }
    } catch (const std::exception &e) {
        std::fprintf(stderr, "adlibc: %s\n", e.what());
        return 2;
    }
    if (strict)
        for (const std::string &w : syms.loadWarnings()) std::fprintf(stderr, "adlibc: warning: %s [symbols]\n", w.c_str());

    std::string source;
    if (!readFile(input, source)) { std::fprintf(stderr, "adlibc: cannot read %s\n", input.c_str()); return 2; }
    std::string base = input.substr(input.find_last_of("/\\") + 1);
    std::string name = base.substr(0, base.find_last_of('.'));
    CompileOptions opt;
    opt.compat = compat;
    opt.planName = name;
    opt.scene = scenePtr;
    if (scenePtr) opt.object = scene.objectFor(name);
    if (!objectArg.empty()) {
        char *end = nullptr;
        long o = std::strtol(objectArg.c_str(), &end, 10);
        if (*end || o < 0 || size_t(o) >= scene.objects.size()) {
            std::fprintf(stderr, "adlibc: --object %s: the scene has objects 0 to %zu\n", objectArg.c_str(),
                         scene.objects.size() ? scene.objects.size() - 1 : 0);
            return 2;
        }
        opt.object = int16_t(o);
    }
    opt.strict = strict;
    opt.werror = werror;
    opt.planSource = [&](const std::string &plan) -> std::optional<std::string> {
        if (lower(plan) == lower(name)) return source; // the file being compiled, as given
        return findPlan(plansDir, plan);
    };
    CompileResult r = compilePlan(source, syms, opt);
    // Tree-level lint (arities, interactor options, action lists): the shared adlib-tools rules.
    std::vector<std::string> lint;
#ifdef ADLIBC_LINT
    if (strict && !r.pbn.empty()) {
        std::vector<uint32_t> words;
        std::string err;
        PlanTree tree;
        if (readPbnWords(r.pbn, words, &err) && buildPlanTree(words, LoadHooks{}, tree, &err)) {
            tools::SymbolTable st;
            for (const CompilerSymbol &s : syms.symbols()) st.add(s.name, s.value, s.group);
            for (const tools::Issue &i : tools::lintPlan(tree, &st, &r.words))
                if (i.severity != tools::Issue::Severity::Info)
                    lint.push_back(input + ": warning: " + i.path + ": " + i.message + " [lint]");
        }
        if (werror && !lint.empty()) r.ok = false;
    }
#endif
    for (const std::string &l : lint) std::fprintf(stderr, "%s\n", l.c_str());
    for (const Diagnostic &d : r.diagnostics) {
        std::fprintf(stderr, "%s\n", d.format(input).c_str());
        if (d.severity == Diagnostic::Severity::Error && (d.kind == "crash" || d.kind == "corrupt" ||
                                                          d.kind == "hang" || d.kind == "undefined"))
            std::fprintf(stderr, "%s: note: adlibc rejects this input because the original compiler "
                                 "would %s on it\n", input.c_str(),
                         d.kind == "crash" ? "crash" : d.kind == "hang" ? "hang" :
                         d.kind == "corrupt" ? "corrupt its memory" : "produce an undefined value");
    }
    if (!r.ok) {
        if (r.error() == nullptr && werror) std::fprintf(stderr, "adlibc: warnings treated as errors (--Werror)\n");
        return 1;
    }
    if (outPath.empty()) outPath = name + ".pbn";
    std::ofstream f(outPath, std::ios::binary);
    if (!f || !f.write(reinterpret_cast<const char *>(r.pbn.data()), std::streamsize(r.pbn.size()))) {
        std::fprintf(stderr, "adlibc: cannot write %s\n", outPath.c_str());
        return 1;
    }
    if (!quiet) std::fprintf(stderr, "wrote %s (%zu bytes)\n", outPath.c_str(), r.pbn.size());
    return 0;
}
