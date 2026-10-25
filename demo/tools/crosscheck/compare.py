"""Compares the cross-check's two printouts. Called by crosscheck.sh.

    compare.py exact A B          every line identical; exit 1 and show the first difference
    compare.py ulps A B [--tables] how far apart, field by field, in ulps: a report, plus
                                  (--tables) a failure unless every float the GPU reads from
                                  the blackbody and orbit tables is the same
    compare.py frames CPP JS      the frame check: every C line identical; every uniform the
                                  plugin's GL programs hold equal to the port's, or GL's
                                  initial zero where the port has not set it yet
    compare.py frames-report CPP JS   the same pair measured, in ulps: a report, never a failure
    compare.py tables A B         two raw dumps of the tables' floats: how many differ, a report

Fields are hex words: 16 digits a double, 8 a float (or an int, in a U line).
"""
import re
import struct
import sys
from collections import defaultdict

HEX = re.compile(r"[0-9a-f]{16}|[0-9a-f]{8}")


def read(path):
    with open(path) as handle:
        return handle.read().splitlines()


def exact(a_path, b_path):
    a, b = read(a_path), read(b_path)
    for i in range(max(len(a), len(b))):
        x = a[i] if i < len(a) else "<missing>"
        y = b[i] if i < len(b) else "<missing>"
        if x != y:
            print(f"first difference at line {i + 1}:")
            print(f"  C++: {x[:220]}")
            print(f"  js : {y[:220]}")
            return 1
    print(f"{len(a)} lines identical")
    return 0


def ulps_between(x, y):
    if len(x) == 16:
        ix, iy = (struct.unpack(">q", bytes.fromhex(h))[0] for h in (x, y))
        mask = 0x7FFFFFFFFFFFFFFF
    else:
        ix, iy = (struct.unpack(">i", bytes.fromhex(h))[0] for h in (x, y))
        mask = 0x7FFFFFFF
    ix = -(ix & mask) if ix < 0 else ix
    iy = -(iy & mask) if iy < 0 else iy
    return abs(ix - iy)


def ulps(a_path, b_path, tables):
    a, b = read(a_path), read(b_path)
    if len(a) != len(b):
        print(f"FAIL  {len(a)} lines against {len(b)}")
        return 1
    stats = defaultdict(lambda: [0, 0, 0, 0.0, 0])
    problems = []
    for x, y in zip(a, b):
        tag = x.split()[0]
        s = stats[tag]
        s[0] += 1
        if x == y:
            s[1] += 1
            continue
        tx, ty = re.split(r"[ ,=]", x), re.split(r"[ ,=]", y)
        if len(tx) != len(ty):
            problems.append(f"{tag}: the lines differ in shape: {x[:80]}")
            continue
        previous = ""
        for p, q in zip(tx, ty):
            if p != q:
                if previous in ("row", "h") or tag == "geoall":
                    s[4] += 1  # a digest of floats: compared by tables(), not here
                elif HEX.fullmatch(p) and HEX.fullmatch(q) and len(p) == len(q):
                    s[2] = max(s[2], ulps_between(p, q))
                    fmt = ">d" if len(p) == 16 else ">f"
                    fp, fq = (struct.unpack(fmt, bytes.fromhex(h))[0] for h in (p, q))
                    s[3] = max(s[3], abs(fp - fq))
                else:
                    problems.append(f"{tag}: {p} against {q} in a field that is not a number")
            previous = p
        # What the GPU reads: the blackbody texels, the orbit table's sampled
        # texels and its rows' digests, and the ends as floats.
        if tables and tag in ("bb", "orb", "geoall"):
            problems.append(f"{tag}: a texel the GPU reads differs: {x[:70]}")
        if tables and tag == "geo":
            if re.search(r"ends=(\S+) row=(\S+)", x).groups() != re.search(r"ends=(\S+) row=(\S+)", y).groups():
                problems.append(f"geo row {x.split()[1]}: the floats the GPU reads differ")
    width = max(len(t) for t in stats)
    for tag, (n, same, worst, absolute, digests) in stats.items():
        if n == same:
            note = "identical"
        else:
            note = f"{same} identical, the rest within {worst} ulp{'s' if worst != 1 else ''} (largest difference {absolute:.2g})"
            if digests:
                note += f"; {digests} table digest{'s' if digests != 1 else ''} differ"
        print(f"   {tag:<{width}} {n:6} lines: {note}")
    if problems:
        for p in problems[:8]:
            print(f"FAIL  {p}")
        return 1
    return 0


def tables(a_path, b_path):
    """The raw table floats: how many of them differ, and by how many ulps."""
    a, b = open(a_path, "rb").read(), open(b_path, "rb").read()
    if len(a) != len(b):
        print(f"   tables: {len(a)} bytes against {len(b)}")
        return 1
    n = len(a) // 4
    ia, ib = struct.unpack(f"<{n}i", a), struct.unpack(f"<{n}i", b)
    differ = worst = 0
    for x, y in zip(ia, ib):
        if x != y:
            differ += 1
            x = -(x & 0x7FFFFFFF) if x < 0 else x
            y = -(y & 0x7FFFFFFF) if y < 0 else y
            worst = max(worst, abs(x - y))
    print(f"   tables: {n - differ:,} of {n:,} floats the GPU reads identical"
          + (f", {differ:,} within {worst} ulp{'s' if worst != 1 else ''}" if differ else ""))
    return 0


def parse_u(line):
    words = line.split()
    out = {}
    for field in words[3:]:
        name, value = field.split("=", 1)
        out[name] = value
    return words[1], words[2], out


def frames(cpp_path, js_path):
    a, b = read(cpp_path), read(js_path)
    if len(a) != len(b):
        print(f"FAIL  {len(a)} lines from the plugin against {len(b)} from the port")
        return 1
    cpu = uniforms = 0
    inactive = defaultdict(set)
    seen = defaultdict(set)
    for i, (x, y) in enumerate(zip(a, b)):
        if x.startswith("U "):
            frame, program, plugin = parse_u(x)
            frame2, program2, port = parse_u(y)
            if (frame, program) != (frame2, program2):
                print(f"FAIL  line {i + 1}: out of step ({frame} {program} against {frame2} {program2})")
                return 1
            for name, value in plugin.items():
                seen[program].add(name)
                uniforms += 1
                if name in port:
                    if port[name] != value:
                        print(f"FAIL  frame {frame} {program}: {name} is {value} in the plugin, {port[name]} in the port")
                        print(f"        (line {i + 1})")
                        return 1
                elif not re.fullmatch(r"[fiu]:0{8}(,0{8})*", value):
                    print(f"FAIL  frame {frame} {program}: {name} is {value} in the plugin and never set by the port")
                    return 1
            for name in port:
                if name not in plugin:
                    inactive[program].add(name)
        elif x != y:
            print(f"FAIL  line {i + 1}:")
            print(f"  plugin: {x[:240]}")
            print(f"  port  : {y[:240]}")
            return 1
        elif x.startswith("C "):
            cpu += 1
    print(f"{cpu} frames' CPU state identical; {uniforms} uniform values identical "
          f"({', '.join(f'{p} {len(n)}' for p, n in seen.items())} active uniforms)")
    for program, names in sorted(inactive.items()):
        print(f"   set by the port but not active in the plugin's {program} program: {', '.join(sorted(names))}")
    return 0


def frames_report(cpp_path, js_path):
    """How far the port, on V8's Math, is from a build of the plugin: a report, never a failure."""
    a, b = read(cpp_path), read(js_path)
    if len(a) != len(b):
        print(f"   {len(a)} lines against {len(b)}: not comparable")
        return 1
    cpu_fields = cpu_same = 0
    cpu_worst = 0
    cpu_abs = defaultdict(float)
    u_fields = u_same = 0
    u_worst = defaultdict(int)
    for x, y in zip(a, b):
        if x.startswith("C "):
            for p, q in zip(x.split()[2:], y.split()[2:]):
                cpu_fields += 1
                if p == q:
                    cpu_same += 1
                else:
                    name, hp = p.split("=")
                    hq = q.split("=")[1]
                    cpu_worst = max(cpu_worst, ulps_between(hp, hq))
                    fp, fq = (struct.unpack(">d", bytes.fromhex(h))[0] for h in (hp, hq))
                    cpu_abs[name] = max(cpu_abs[name], abs(fp - fq))
        elif x.startswith("U "):
            _, program, plugin = parse_u(x)
            _, _, port = parse_u(y)
            for name, value in plugin.items():
                if name not in port:
                    continue
                for p, q in zip(value[2:].split(","), port[name][2:].split(",")):
                    u_fields += 1
                    if p == q:
                        u_same += 1
                    elif value[0] == "f":
                        u_worst[f"{program}.{name}"] = max(u_worst[f"{program}.{name}"], ulps_between(p, q))
                    else:
                        u_worst[f"{program}.{name} (an integer!)"] = 1
    biggest = ", ".join(f"{k} {v:.2g}" for k, v in sorted(cpu_abs.items(), key=lambda kv: -kv[1])[:4])
    print(f"   CPU state: {cpu_same} of {cpu_fields} doubles identical; the largest differences: {biggest or 'none'}")
    worst = sorted(u_worst.items(), key=lambda kv: -kv[1])
    print(f"   uniforms: {u_same} of {u_fields} values identical; differing: "
          + (", ".join(f"{k} ({v} ulp{'s' if v != 1 else ''})" for k, v in worst[:12]) or "none"))
    return 0


def main(argv):
    if len(argv) >= 3 and argv[0] == "exact":
        return exact(argv[1], argv[2])
    if len(argv) >= 3 and argv[0] == "ulps":
        return ulps(argv[1], argv[2], "--tables" in argv)
    if len(argv) >= 3 and argv[0] == "frames":
        return frames(argv[1], argv[2])
    if len(argv) >= 3 and argv[0] == "tables":
        return tables(argv[1], argv[2])
    if len(argv) >= 3 and argv[0] == "frames-report":
        return frames_report(argv[1], argv[2])
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
