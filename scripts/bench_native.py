#!/usr/bin/env python3
"""Native timing for the same matrix as bench/index.html.
Usage: bench_native.py <raw_native_cli> <out.json> [threads...]"""
import ctypes, json, os, platform, subprocess, sys

SIZES = [(256, 256), (512, 512), (1440, 900)]


def run_unthrottled(args):
    """On Windows, start the child at high priority and opt it out of power
    throttling (EcoQoS), so the scheduler does not move a long single-threaded
    run onto efficiency cores while other work is running. Elsewhere, run plainly."""
    if os.name != "nt":
        return subprocess.run(args, capture_output=True, text=True, check=True).stdout
    proc = subprocess.Popen(args, stdout=subprocess.PIPE, text=True,
                            creationflags=subprocess.HIGH_PRIORITY_CLASS)
    k32 = ctypes.windll.kernel32
    handle = k32.OpenProcess(0x0200 | 0x0400, False, proc.pid)   # SET_INFORMATION | QUERY_INFORMATION
    state = (ctypes.c_ulong * 3)(1, 1, 0)   # version 1, control EXECUTION_SPEED, state off
    k32.SetProcessInformation(handle, 4, ctypes.byref(state), ctypes.sizeof(state))  # ProcessPowerThrottling
    k32.CloseHandle(handle)
    out, _ = proc.communicate()
    if proc.returncode:
        raise RuntimeError(f"{args} exited {proc.returncode}")
    return out


def main():
    cli, out = sys.argv[1], sys.argv[2]
    thread_counts = [int(t) for t in sys.argv[3:]] or [1, os.cpu_count()]
    results = []
    for threads in thread_counts:
        for w, h in SIZES:
            for rt in (True, False):
                args = [cli, "--bench", "5", "--width", str(w), "--height", str(h), "--threads", str(threads)]
                if not rt:
                    args.append("--no-rt")
                r = json.loads(run_unthrottled(args).strip())
                r["variant"] = "native" if threads == 1 else f"native-{threads}t"
                results.append(r)
                print(r["variant"], w, h, "rt" if rt else "no-rt", r["median_ms"], "ms", flush=True)
    meta = {"note": "Windows runs use high priority with power throttling disabled",
            "platform": platform.platform(), "processor": platform.processor(),
            "cpu_count": os.cpu_count(), "results": results}
    with open(out, "w") as f:
        json.dump(meta, f, indent=2)
        f.write("\n")


if __name__ == "__main__":
    main()
