"""The expression and statement layer of scripts/wgsl_to_hlsl.py: WGSL types,
calls, literals, declarations and function signatures as HLSL. Everything it
does not recognise raises TranslateError; nothing is passed through unchecked."""
import re

TYPES = {"f32": "float", "u32": "uint", "i32": "int", "bool": "bool",
         "vec2f": "float2", "vec3f": "float3", "vec4f": "float4",
         "vec2u": "uint2", "vec3u": "uint3", "vec4u": "uint4",
         "vec2i": "int2", "vec3i": "int3", "vec4i": "int4"}
VECTORS = {k: v for k, v in TYPES.items() if k.startswith("vec")}
SCALARS = {"f32": "float", "u32": "uint", "i32": "int"}
# HLSL keywords that are legal WGSL identifiers.
RESERVED = {"line", "point", "triangle", "sample", "centroid", "linear", "precise", "shared",
            "vector", "matrix", "texture", "in", "out", "inout", "packoffset", "register", "half",
            "string", "pass", "technique", "compile", "snorm", "unorm", "static", "uniform",
            "row_major", "column_major", "groupshared", "nointerpolation", "export", "auto",
            "template", "this", "namespace", "class", "dword", "double", "min16float", "lineadj"}
TYPE_RE = r"(?:array<\s*\w+\s*(?:,\s*\d+\s*)?>|ptr<\s*function\s*,\s*\w+\s*>|\w+)"


class TranslateError(Exception):
    pass


def htype(t):
    """A WGSL type as (HLSL element type, array suffix)."""
    t = t.strip()
    m = re.fullmatch(r"array<\s*(\w+)\s*,\s*(\d+)\s*>", t)
    if m:
        return htype(m.group(1))[0], f"[{m.group(2)}]"
    if t in TYPES:
        return TYPES[t], ""
    if re.fullmatch(r"[A-Z]\w*", t):   # a struct name
        return t, ""
    raise TranslateError(f"unsupported type {t!r}")


def split_args(s):
    """Split at top-level commas, respecting (), [] and <>."""
    out, depth, cur = [], 0, ""
    for ch in s:
        if ch in "([<":
            depth += 1
        elif ch in ")]>":
            depth -= 1
        if ch == "," and depth == 0:
            out.append(cur.strip())
            cur = ""
        else:
            cur += ch
    if cur.strip():
        out.append(cur.strip())
    return out


def rewrite_calls(code, names, fn):
    """Replace every call `name(args)` for name in names by fn(name, args), innermost first."""
    pat = re.compile(r"\b(" + "|".join(map(re.escape, names)) + r")\(")
    while True:
        m = None
        for cand in pat.finditer(code):
            m = cand   # the last match is the innermost-or-rightmost; rewrite from the right
        if m is None:
            return code
        start, i, depth = m.start(), m.end(), 1
        while depth:
            if i >= len(code):
                raise TranslateError(f"unbalanced call in {code!r}")
            depth += {"(": 1, ")": -1}.get(code[i], 0)
            i += 1
        args = split_args(code[m.end():i - 1])
        code = code[:start] + fn(m.group(1), args) + code[i:]


def construct(name, args):
    if name in SCALARS:
        return f"{SCALARS[name]}({', '.join(args)})"
    if name == "select":   # select(false_value, true_value, condition)
        if len(args) != 3:
            raise TranslateError("select needs three arguments")
        return f"(({args[2]}) ? ({args[1]}) : ({args[0]}))"
    h = VECTORS[name]
    if len(args) == 1:     # a splat: HLSL needs a cast, not a one-argument constructor
        return f"(({h})({args[0]}))"
    return f"{h}({', '.join(args)})"


def literals(code):
    def hexfloat(m):
        v = float.fromhex(m.group(1))
        return f"{v:.9g}f"
    code = re.sub(r"\b(0[xX][0-9a-fA-F]*\.?[0-9a-fA-F]*[pP][+-]?\d+)f?", hexfloat, code)
    # Decimal float literals get an f suffix so no expression is ever evaluated in double.
    return re.sub(r"(?<![\w.])(\d+\.\d*(?:[eE][+-]?\d+)?|\d+[eE][+-]?\d+)(?![\w.])", r"\1f", code)


def statement(code, ptr_params):
    """Translate one line of function-body code (comments already removed)."""
    if re.search(r"\b(let|var)\s+\w+\s*(=|;)", code):
        raise TranslateError(f"declaration without an explicit type: {code.strip()!r}")
    if re.search(r"\bvar\s*<", code):
        raise TranslateError(f"address space inside a function: {code.strip()!r}")

    def array_var(m):   # var x: array<T, N>;  WGSL zero-initialises, HLSL must be told
        et, n = htype(m.group(2))[0], int(m.group(3))
        zero = "0.0f" if et == "float" else "0"
        return f"{et} {m.group(1)}[{n}] = {{{', '.join([zero] * n)}}};"
    code = re.sub(r"\bvar\s+(\w+)\s*:\s*array<\s*(\w+)\s*,\s*(\d+)\s*>\s*;", array_var, code)

    def decl(m):
        t, suffix = htype(m.group(3))
        const = "const " if m.group(1) == "let" else ""
        return f"{const}{t} {m.group(2)}{suffix} ="
    code = re.sub(r"\b(let|var)\s+(\w+)\s*:\s*(" + TYPE_RE + r")\s*=", decl, code)
    for p in ptr_params:
        code = re.sub(r"\*" + p + r"\b", p, code)
    code = re.sub(r"([(,]\s*)&(\w+)", r"\1\2", code)   # &x as a call argument
    code = rewrite_calls(code, list(VECTORS) + list(SCALARS) + ["select"], construct)
    return literals(code)


def signature(line, attrs):
    """`fn name(params) -> T {` with an optional one-line body after the brace.
    Returns (HLSL head, rest of the line after the brace, pointer parameter names)."""
    m = re.match(r"\s*fn\s+(\w+)\s*\(", line)
    if not m:
        raise TranslateError(f"unsupported function signature: {line.strip()!r}")
    i, depth = m.end(), 1
    while depth:
        if i >= len(line):
            raise TranslateError(f"unbalanced signature: {line.strip()!r}")
        depth += {"(": 1, ")": -1}.get(line[i], 0)
        i += 1
    params = line[m.end():i - 1]
    r = re.match(r"\s*(?:->\s*(" + TYPE_RE + r"))?\s*\{", line[i:])
    if not r:
        raise TranslateError(f"unsupported function signature: {line.strip()!r}")
    name, ret, rest = m.group(1), r.group(1), line[i + r.end():]
    out, ptrs = [], []
    for p in split_args(params):
        b = re.fullmatch(r"@builtin\(global_invocation_id\)\s*(\w+)\s*:\s*vec3u", p)
        if b:
            out.append(f"uint3 {b.group(1)} : SV_DispatchThreadID")
            continue
        pm = re.fullmatch(r"(\w+)\s*:\s*(" + TYPE_RE + r")", p)
        if not pm:
            raise TranslateError(f"unsupported parameter {p!r}")
        ptr = re.fullmatch(r"ptr<\s*function\s*,\s*(\w+)\s*>", pm.group(2))
        if ptr:
            ptrs.append(pm.group(1))
            out.append(f"inout {htype(ptr.group(1))[0]} {pm.group(1)}")
        else:
            t, suffix = htype(pm.group(2))
            out.append(f"{t} {pm.group(1)}{suffix}")
    head = ""
    if attrs:
        wg = re.search(r"@workgroup_size\(([^)]*)\)", attrs)
        if "@compute" not in attrs or not wg:
            raise TranslateError(f"unsupported attributes {attrs!r}")
        dims = [d.strip() for d in wg.group(1).split(",")] + ["1", "1"]
        head = f"[numthreads({dims[0]}, {dims[1]}, {dims[2]})]\n"
    rt = htype(ret)[0] if ret else "void"
    lead = line[:len(line) - len(line.lstrip())]
    return f"{head}{lead}{rt} {name}({', '.join(out)}) {{", rest, ptrs
