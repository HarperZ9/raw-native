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
TYPE_RE = r"(?:array<\s*atomic<\s*u32\s*>\s*>|array<\s*\w+\s*(?:,\s*\d+\s*)?>|ptr<\s*function\s*,\s*\w+\s*>|\w+)"
# WGSL builtins whose HLSL spelling differs, and the ones the translator cannot
# map faithfully (different semantics or no HLSL intrinsic): those are refused.
RENAMED = {"fract": "frac", "mix": "lerp"}
REFUSED = {"inverseSqrt", "fma", "modf", "frexp", "quantizeToF16", "bitcast", "arrayLength", "countOneBits",
           "reverseBits", "extractBits", "insertBits", "pack4x8unorm", "unpack4x8unorm", "pack2x16float",
           "unpack2x16float", "dpdx", "dpdy", "fwidth", "workgroupBarrier", "storageBarrier", "atomicLoad",
           "atomicStore", "atomicSub", "atomicMax", "atomicMin", "atomicAnd", "atomicOr", "atomicXor",
           "atomicExchange", "atomicCompareExchangeWeak", "textureSample", "textureLoad", "round"}


class TranslateError(Exception):
    pass


def htype(t):
    """A WGSL type as (HLSL element type, array suffix)."""
    t = t.strip()
    if re.fullmatch(r"atomic<\s*u32\s*>", t):
        return "uint", ""
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
    # "<" opens a bracket only after a template name (array<f32, 4>); anywhere
    # else it is a comparison, and so is a ">" with no template open. Counting
    # comparisons as brackets once hid the comma in vec2f(a, select(b, c, k >= 2)).
    out, depth, angle, cur = [], 0, 0, ""
    for ch in s:
        if ch == "<" and re.search(r"\b(array|ptr|atomic|vec[234]|mat[234]x[234])\s*$", cur):
            angle += 1
        elif ch == ">" and angle > 0 and not cur.endswith("-"):
            angle -= 1
        elif ch in "([":
            depth += 1
        elif ch in ")]":
            depth -= 1
        ch_top = depth == 0 and angle == 0
        if ch == "," and ch_top:
            out.append(cur.strip())
            cur = ""
            continue
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
    # var x: Struct;  WGSL zero-initialises it, HLSL must be told
    code = re.sub(r"\bvar\s+(\w+)\s*:\s*([A-Z]\w*)\s*;", lambda m: f"{m.group(2)} {m.group(1)} = ({m.group(2)})0;", code)

    def decl(m):
        t, suffix = htype(m.group(3))
        const = "const " if m.group(1) == "let" else ""
        return f"{const}{t} {m.group(2)}{suffix} ="
    code = re.sub(r"\b(let|var)\s+(\w+)\s*:\s*(" + TYPE_RE + r")\s*=", decl, code)
    bad = sorted(set(re.findall(r"\b(\w+)\s*\(", code)) & REFUSED)
    if bad:
        raise TranslateError(f"WGSL builtins with no faithful HLSL form: {bad} in {code.strip()!r}")
    # atomicAdd(&a[i], v) as a statement: HLSL's InterlockedAdd takes the location itself.
    code = re.sub(r"^(\s*)atomicAdd\(\s*&", r"\1InterlockedAdd(", code)
    if "atomicAdd" in code:
        raise TranslateError(f"atomicAdd is translated only as a statement: {code.strip()!r}")
    for w, h in RENAMED.items():
        code = re.sub(r"\b" + w + r"\(", h + "(", code)
    for p in ptr_params:
        code = re.sub(r"\*" + p + r"\b", p, code)
    code = re.sub(r"([(,]\s*)&(\w+)", r"\1\2", code)   # &x as a call argument
    code = rewrite_calls(code, list(VECTORS) + list(SCALARS) + ["select"], construct)
    # textureSampleLevel(t, s, uv, level): the one texture read raster passes use (an
    # explicit level, so no derivatives are involved and both APIs agree on the LOD).
    def sample_level(name, args):
        if len(args) != 4:
            raise TranslateError("textureSampleLevel needs four arguments")
        return f"{args[0]}.SampleLevel({args[1]}, {args[2]}, {args[3]})"
    code = rewrite_calls(code, ["textureSampleLevel"], sample_level)
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
    r = re.match(r"\s*(?:->\s*(@builtin\(position\)|@location\(0\))?\s*(" + TYPE_RE + r"))?\s*\{", line[i:])
    if not r:
        raise TranslateError(f"unsupported function signature: {line.strip()!r}")
    name, ret_attr, ret, rest = m.group(1), r.group(1), r.group(2), line[i + r.end():]
    out, ptrs = [], []
    for p in split_args(params):
        b = re.fullmatch(r"@builtin\(global_invocation_id\)\s*(\w+)\s*:\s*vec3u", p)
        if b:
            out.append(f"uint3 {b.group(1)} : SV_DispatchThreadID")
            continue
        b = re.fullmatch(r"@builtin\(vertex_index\)\s*(\w+)\s*:\s*u32", p)
        if b:
            out.append(f"uint {b.group(1)} : SV_VertexID")
            continue
        b = re.fullmatch(r"@builtin\(position\)\s*(\w+)\s*:\s*vec4f", p)
        if b:
            out.append(f"float4 {b.group(1)} : SV_Position")
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
    head, sem = "", ""
    if attrs in ("@vertex", "@fragment"):
        want = "@builtin(position)" if attrs == "@vertex" else "@location(0)"
        # RHI version 3: an entry point may return a struct whose fields carry the semantics
        # (a vertex output with @builtin(position); fragment targets in a struct named *Targets).
        struct_ok = ret_attr is None and ret and re.fullmatch(r"[A-Z]\w*", ret) and (attrs == "@vertex" or ret.endswith("Targets"))
        if not struct_ok and (ret_attr != want or ret != "vec4f"):
            raise TranslateError(f"{attrs} entry points return {want} vec4f or a struct: {line.strip()!r}")
        if not struct_ok:
            sem = " : SV_Position" if attrs == "@vertex" else " : SV_Target0"
    elif attrs:
        wg = re.search(r"@workgroup_size\(([^)]*)\)", attrs)
        if "@compute" not in attrs or not wg:
            raise TranslateError(f"unsupported attributes {attrs!r}")
        dims = [d.strip() for d in wg.group(1).split(",")] + ["1", "1"]
        head = f"[numthreads({dims[0]}, {dims[1]}, {dims[2]})]\n"
    if ret_attr and not sem and attrs not in ("@vertex", "@fragment"):
        raise TranslateError(f"a return attribute outside an entry point: {line.strip()!r}")
    rt = htype(ret)[0] if ret else "void"
    lead = line[:len(line) - len(line.lstrip())]
    return f"{head}{lead}{rt} {name}({', '.join(out)}){sem} {{", rest, ptrs
