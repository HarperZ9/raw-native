#!/usr/bin/env python3
"""The C++ API snapshot of ADR 0015 (roadmap M3 criterion 5): dump, write and check (stdlib only).

A snapshot is a directory api/snapshot-N/ holding manifest.json (the headers it covers) and
dump.txt: one line per public declaration, "header | scope | declaration", with comments,
whitespace, function bodies and private members removed and multi-declarator members split.
The dump is textual, not a compiler's view: it names what a user can call or read, not its ABI.

  api_dump.py --check       every declaration of the newest snapshot is still declared, with
                            the same text, in the headers it lists. Additions pass; a removal
                            or a changed signature fails and names the line. A break moves to
                            the next snapshot (--write N+1) with a migration note.
  api_dump.py --write N     write api/snapshot-N from the headers in the newest manifest (or
                            DEFAULT_HEADERS for the first snapshot).
  api_dump.py --print H     print the entries of one header.
  api_dump.py --selftest    check the parser on fixed inputs.
Exit 0 on success, 1 on a break or a parse error.
"""
import hashlib
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
API = ROOT / "api"
# Snapshot 1's surface: the scene and renderer layers, without the GPU check fixtures.
DEFAULT_HEADERS = ["raw/core/api_snapshot.hpp", "raw/scene/scene.hpp"] + [f"raw/renderer/{n}.hpp" for n in (
    "accel", "bake", "brdf", "bvh", "colour", "composite", "gbuffer", "gpu", "gpu_reconcile",
    "lighting", "lighting_cube", "motion", "outline", "pbr", "post", "raster", "ray_ao", "render",
    "sampler", "shadows", "ssao", "taa", "threads")]

KEYWORD_TYPE = re.compile(r"^(?:template\s*<.*>\s*)?(struct|class|union|enum(?:\s+class)?)\s+(\w+)\b")
FUNC_HEAD = re.compile(r"\)\s*(?:const|noexcept|override|final|&|&&|->\s*[\w:<>,\s\*&]+)*\s*$")


def strip(text):
    """Remove comments, string contents and preprocessor lines."""
    out, i, n = [], 0, len(text)
    while i < n:
        if text.startswith("//", i):
            j = text.find("\n", i)
            i = n if j < 0 else j
        elif text.startswith("/*", i):
            j = text.find("*/", i + 2)
            i = n if j < 0 else j + 2
        elif text[i] in "\"'":
            q, j = text[i], i + 1
            while j < n and text[j] != q:
                j += 2 if text[j] == "\\" else 1
            out.append(text[i] + text[i + 1:j].replace("{", "(").replace("}", ")").replace(";", ",") + q)
            i = j + 1
        else:
            out.append(text[i])
            i += 1
    lines = [l for l in "".join(out).split("\n") if not l.strip().startswith("#")]
    return "\n".join(lines)


def norm(s):
    s = re.sub(r"\s+", " ", s).strip()
    s = re.sub(r"\s*([(),\[\]{}<>=*&:;])\s*", r"\1", s)
    s = re.sub(r"(\w)\s+(\w)", r"\1 \2", s)
    return s.replace("::", "::")


def match_brace(text, i):
    depth = 0
    for j in range(i, len(text)):
        if text[j] == "{":
            depth += 1
        elif text[j] == "}":
            depth -= 1
            if depth == 0:
                return j
    raise ValueError("unbalanced braces")


def split_top(s):
    parts, depth, cur = [], 0, ""
    for ch in s:
        if ch in "(<[{":
            depth += 1
        elif ch in ")>]}":
            depth -= 1
        if ch == "," and depth == 0:
            parts.append(cur)
            cur = ""
        else:
            cur += ch
    parts.append(cur)
    return parts


def emit(decls, scope, stmt):
    stmt = norm(stmt)
    stmt = re.sub(r"^(public|protected|private):", "", stmt)
    if not stmt or stmt in ("public", "private", "protected"):
        return
    paren = 0
    for ch in stmt:   # a top-level parenthesis: a function or an initialised object, kept whole
        if ch == "(" and paren == 0 and not stmt.startswith(("using ", "typedef ")):
            paren = 1
            break
    parts = split_top(stmt) if not paren else [stmt]
    if len(parts) > 1:
        first = parts[0]
        m = re.match(r"^(.*?[\w>\*&])\s*([\*&]*\w+(?:\[[^\]]*\])?(?:\{.*\}|=.*)?)$", first)
        base = m.group(1) if m else ""
        decls.append((scope, first))
        for p in parts[1:]:
            decls.append((scope, norm(f"{base} {p}")))
    else:
        decls.append((scope, stmt))


def parse(text, scope, decls, default_public=True):
    i, buf, public = 0, "", default_public
    n = len(text)
    while i < n:
        c = text[i]
        if c == ";":
            if public:
                emit(decls, scope, buf)
            buf = ""
            i += 1
            continue
        if c == ":" and re.fullmatch(r"\s*(public|private|protected)\s*", buf) and text[i:i + 2] != "::":
            public = buf.strip() != "private"
            buf = ""
            i += 1
            continue
        if c != "{":
            buf += c
            i += 1
            continue
        head = buf.strip()
        j = match_brace(text, i)
        body = text[i + 1:j]
        ns = re.fullmatch(r"(?:inline\s+)?namespace\s*([\w:]*)", head)
        kt = KEYWORD_TYPE.match(head)
        if ns or head == 'extern "C"':
            name = ns.group(1) if ns else ""
            parse(body, f"{scope}::{name}" if name else scope, decls)
            buf, i = "", j + 1
        elif kt and not FUNC_HEAD.search(head) and "=" not in head:
            kind, name = kt.group(1), kt.group(2)
            if public:
                if kind.startswith("enum"):
                    decls.append((scope, norm(head + "{" + body + "}")))
                else:
                    decls.append((scope, norm(head)))
                    parse(body, f"{scope}::{name}", decls, default_public=kind != "class")
            k = text.find(";", j)   # trailing declarators, if any, to the semicolon
            buf, i = "", (k + 1 if k >= 0 else n)
        elif FUNC_HEAD.search(head) or re.search(r"\)\s*:\s*[\w:]+[({]", head):
            if public:
                emit(decls, scope, re.sub(r"\)\s*:\s*[\w:]+[({].*$", ")", head, flags=re.S))
            buf, i = "", j + 1
        else:   # a brace initialiser: part of the statement
            buf += text[i:j + 1]
            i = j + 1
    if buf.strip() and public:
        emit(decls, scope, buf)


def entries(header):
    text = strip((ROOT / header).read_text(encoding="utf-8"))
    decls = []
    parse(text, "", decls)
    return [f"{header} | {s or '::'} | {d}" for s, d in decls]


def newest():
    snaps = sorted((int(p.name.split("-")[1]), p) for p in API.glob("snapshot-*") if p.name.split("-")[1].isdigit())
    return snaps[-1] if snaps else (0, None)


def write(n):
    _, prev = newest()
    headers = json.loads((prev / "manifest.json").read_text())["headers"] if prev else DEFAULT_HEADERS
    lines = sorted(set(e for h in headers for e in entries(h)))
    d = API / f"snapshot-{n}"
    d.mkdir(parents=True, exist_ok=True)
    text = "\n".join(lines) + "\n"
    (d / "dump.txt").write_text(text, encoding="utf-8", newline="\n")
    man = {"schema": "raw-native.api-snapshot/1", "snapshot": n, "headers": headers, "entries": len(lines),
           "dump_sha256": hashlib.sha256(text.encode("utf-8")).hexdigest()}
    (d / "manifest.json").write_text(json.dumps(man, indent=1) + "\n", encoding="utf-8", newline="\n")
    print(f"api/snapshot-{n}: {len(lines)} entries from {len(headers)} headers")
    return 0


def check():
    n, d = newest()
    if not d:
        print("no snapshot under api/")
        return 1
    man = json.loads((d / "manifest.json").read_text())
    want = [l for l in (d / "dump.txt").read_text(encoding="utf-8").split("\n") if l]
    missing_headers = [h for h in man["headers"] if not (ROOT / h).exists()]
    have = set(e for h in man["headers"] if (ROOT / h).exists() for e in entries(h))
    gone = [l for l in want if l not in have]
    added = len(have - set(want))
    for h in missing_headers:
        print(f"BREAK: header removed: {h}")
    for l in gone:
        print(f"BREAK: {l}")
    print(f"api snapshot {n}: {len(want)} entries, {len(gone)} missing or changed, {added} added since; "
          + ("compatible" if not gone and not missing_headers else "a break: write the next snapshot with a migration note (ADR 0015)"))
    return 0 if not gone and not missing_headers else 1


def selftest():
    src = """
    // c
    #pragma once
    namespace raw::x {
    struct S { double a{0}, b{1}; int f(int q) const { return q; } private: int hidden_; };
    class C { int p_; public: C(); void g(); };
    enum class E : int { A = 1, B };
    inline double h(double v) { return v * 2.0; }
    inline constexpr int kN = 3;
    template<class F> void each(F f) { f(); }
    std::vector<int> make(int n, double s = {});
    }
    """
    decls = []
    parse(strip(src), "", decls)
    got = [f"{s}|{d}" for s, d in decls]
    want = ["::raw::x|struct S", "::raw::x::S|double a{0}", "::raw::x::S|double b{1}", "::raw::x::S|int f(int q)const",
            "::raw::x|class C", "::raw::x::C|C()", "::raw::x::C|void g()", "::raw::x|enum class E:int{A=1,B}",
            "::raw::x|inline double h(double v)", "::raw::x|inline constexpr int kN=3", "::raw::x|template<class F>void each(F f)",
            "::raw::x|std::vector<int>make(int n,double s={})"]
    ok = got == want
    if not ok:
        print("selftest FAIL")
        for g in got:
            print("  got ", g)
        for w in want:
            print("  want", w)
    else:
        print(f"selftest: {len(want)} declarations parsed as expected")
    # Controls that must register as breaks: a changed parameter type, a removed member, and a
    # function body edit that must not (a body is not part of the API).
    edits = [("int f(int q) const", "int f(long q) const", True), ("double a{0}, b{1};", "double a{0};", True),
             ("return v * 2.0;", "return v * 3.0;", False)]
    for old, new, breaks in edits:
        d2 = []
        parse(strip(src.replace(old, new)), "", d2)
        gone = [g for g in got if g not in [f"{s}|{d}" for s, d in d2]]
        if bool(gone) != breaks:
            print(f"selftest control FAIL: editing '{old}' {'should' if breaks else 'should not'} break the snapshot")
            ok = False
    if ok:
        print("selftest controls: a changed signature and a removed member break the snapshot; a body edit does not")
    return 0 if ok else 1


def main(argv):
    if "--selftest" in argv:
        return selftest()
    if "--write" in argv:
        return write(int(argv[argv.index("--write") + 1]))
    if "--print" in argv:
        for e in entries(argv[argv.index("--print") + 1]):
            print(e)
        return 0
    if "--check" in argv:
        return check()
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
