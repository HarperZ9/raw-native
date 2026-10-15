"""HW H2 v5 driver (evidence/rr-bounds-amendment-7.json); copy of v4_driver.py. Original header: HW H2 v4 driver (evidence/rr-bounds-amendment-6.json), adapted from the HW session's h2a5_driver.py (long-job
survival pattern, mission-control/LONG-JOB-SURVIVAL-NOTE-2026-10-13.md).

Run with pythonw.exe (no console) from v4_watchdog.py. Steps, each skipped when done:
  1. train v4main and v4shuffled in parallel until progress.json reaches 6,000 steps (rr_train.py --resume; each child
     has the shutdown guard, checkpoints every 5 minutes and exits 75 on a session end);
  2. the amendment-6 selection (amendment 5's learning check, v1 as the control that must fail);
  3. the conditioning check on v4: validation and test sequences, continuous validity, and its control (threshold
     validity) on the validation sequences;
  4. the test evaluation rr_eval.py (per-sequence skip-done cache), when v4 qualifies.
Children: the base interpreter with __PYVENV_LAUNCHER__ set to the venv, CREATE_NO_WINDOW and CREATE_NEW_PROCESS_GROUP,
FOR_DISABLE_CONSOLE_CTRL_HANDLER=1, 6 threads each, BelowNormal (inherited from the logon task's pythonw).
Heartbeat v5_driver.heartbeat.json every 30 s. Log v5_chain.log: "H2V5 CHAIN DONE" at the end, "CHAIN: " on a failure.
v5: 2,000 fine-tune steps from v4, the amendment-7 gate, then conditioning and evaluation only when the gate passes. The scripts run from D:/raw-native-hwev on branch diag/rr-raw-hall (keep it there until the chain is done).
"""
import datetime
import json
import os
import subprocess
import sys
import time

D = "C:/dev/mission-control/2026-10-14-engine-open-failures/h2v5"
sys.path.insert(0, D)
import shutdown_guard as guard  # noqa: E402

BASE = "C:/Users/Zain/AppData/Local/Programs/Python/Python312/python.exe"
VENV = "D:/venvs/mmbu/Scripts/python.exe"
S = "D:/raw-native-hwev/scripts"
R = "H:/rr-dataset-v4"
T = "H:/rr-train"
CLI = "D:/raw-native-hwev/build-cpu/Release/raw_native_cli.exe"
STEPS = 2000
LOG = f"{D}/v5_chain.log"
HB = f"{D}/v5_driver.heartbeat.json"
STOP = f"{D}/V5.stop"
FLAGS = 0x08000000 | 0x00000200  # CREATE_NO_WINDOW | CREATE_NEW_PROCESS_GROUP
stage = "start"


def now():
    return datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


def log(m):
    with open(LOG, "a", encoding="utf-8") as f:
        f.write(f"{now()} {m}\n")


def beat():
    tmp = HB + ".tmp"
    with open(tmp, "w") as f:
        json.dump({"pid": os.getpid(), "time": time.time(), "utc": now(), "stage": stage}, f)
    os.replace(tmp, HB)


def env():
    e = dict(os.environ)
    e.update({"FOR_DISABLE_CONSOLE_CTRL_HANDLER": "1", "__PYVENV_LAUNCHER__": VENV, "OMP_NUM_THREADS": "6",
              "MKL_NUM_THREADS": "6", "RR_GUARD_DIR": D, "PYTHONUNBUFFERED": "1"})
    return e


def spawn(args, out):
    f = open(out, "a", encoding="utf-8")
    return subprocess.Popen([BASE] + args, env=env(), creationflags=FLAGS, stdin=subprocess.DEVNULL, stdout=f,
                            stderr=subprocess.STDOUT, cwd=D)


def wait_all(procs):
    while any(p.poll() is None for p in procs):
        beat()
        time.sleep(30)
    return [p.returncode for p in procs]


def step_of(name):
    try:
        return json.load(open(f"{T}/{name}/progress.json"))["step"]
    except (OSError, ValueError, KeyError):
        return 0


def stopping():
    return guard.requested() or os.path.exists(STOP)


def train():
    global stage
    jobs = {"v5main": ["--init", f"{T}/v4main/best.pt"], "v5shuffled": ["--shuffle-targets", "--init", f"{T}/v4shuffled/best.pt"]}
    while True:
        todo = [n for n in jobs if step_of(n) < STEPS]
        if not todo:
            return 0
        if stopping():
            return 75
        stage = "train " + ",".join(f"{n}@{step_of(n)}" for n in todo)
        log(stage)
        procs = [spawn([f"{S}/rr_train.py", R, f"{T}/{n}", "--cpu", "--minutes", "60", "--resume", "--eps", "0.01",
                        "--version", "5", "--steps", str(STEPS), "--lr", "2e-4"] + jobs[n], f"{T}/{n}.log") for n in todo]
        codes = wait_all(procs)
        log(f"train exits {dict(zip(todo, codes))}")
        if 75 in codes:
            return 75
        if any(c != 0 for c in codes):
            return 1


def select():
    """Amendment 7's gate: the learning check (rr_select.py), then validation FLIP against v4 (rr_diag.py)."""
    global stage
    if os.path.exists(f"{T}/gate-a7.json"):
        return 0
    if stopping():
        return 75
    stage = "learning check (amendment 7)"
    if not os.path.exists(f"{T}/selection-a7.json"):
        p = spawn([f"{S}/rr_select.py", R, T, "main=shuffled", "v5main=v5shuffled", "--control-must-fail", "main",
                   "--out", f"{T}/selection-a7.json"], f"{T}/selection-a7.log")
        rc = wait_all([p])[0]
        if rc not in (0, 1) or not os.path.exists(f"{T}/selection-a7.json"):
            return rc or 1
    sel = json.load(open(f"{T}/selection-a7.json"))
    learn = bool(sel.get("valid")) and bool(sel["candidates"]["v5main"]["qualifies"])
    flips = {}
    for seq in ("hall_b_p7", "terrain_100_p7"):
        out = f"{T}/diag-v5main-{seq}.json"
        if not os.path.exists(out):
            stage = f"FLIP gate {seq}"
            log(stage)
            p = spawn([f"{S}/rr_diag.py", f"{T}/v5main/best.pt", f"{R}/val/{seq}", "--out", out + ".tmp"], f"{T}/diag-v5.log")
            rc = wait_all([p])[0]
            if rc != 0:
                return rc
            os.replace(out + ".tmp", out)
        flips[seq] = json.load(open(out))["mean_flip_ours"]
    gate = learn and flips["hall_b_p7"] < 0.103 and flips["terrain_100_p7"] <= 0.0461 + 0.005
    json.dump({"rule": "evidence/rr-bounds-amendment-7.json", "learning_check": learn, "flip": flips,
               "v4_flip": {"hall_b_p7": 0.103, "terrain_100_p7": 0.0461}, "pass": gate}, open(f"{T}/gate-a7.json", "w"), indent=1)
    log(f"gate: learning {learn}, flip {flips}, pass {gate}")
    return 0


def condition():
    global stage
    if not json.load(open(f"{T}/gate-a7.json"))["pass"]:
        return 0
    runs = {"condition-v5main-val.json": ([f"{R}/val/{s}" for s in ("hall_b_p7", "terrain_100_p7")], []),
            "condition-v5main-test.json": ([f"{R}/test/{s}" for s in ("iso_street_p7", "raw_hall_p7", "raw_hero_p7")], []),
            "condition-v5main-val-hard-control.json": ([f"{R}/val/{s}" for s in ("hall_b_p7", "terrain_100_p7")], ["--hard-validity"])}
    for out, (seqs, extra) in runs.items():
        if os.path.exists(f"{T}/{out}"):
            continue
        if stopping():
            return 75
        stage = f"conditioning {out}"
        log(stage)
        p = spawn([f"{S}/rr_condition.py", f"{T}/v5main/best.pt"] + seqs + extra, f"{T}/{out}.tmp")
        rc = wait_all([p])[0]
        if rc != 0:
            return rc
        os.replace(f"{T}/{out}.tmp", f"{T}/{out}")
    return 0


def evaluate():
    global stage
    out = f"{T}/eval-test-a7.json"
    if os.path.exists(out) or not json.load(open(f"{T}/gate-a7.json"))["pass"]:
        return 0
    if stopping():
        return 75
    stage = "evaluation (amendment 7)"
    log("evaluating v5main with control v5shuffled (amendment 7 verdict run)")
    p = spawn([f"{S}/rr_eval.py", R, "--model", f"{T}/v5main/best.pt", "--shuffled", f"{T}/v5shuffled/best.pt", "--cli", CLI,
               "--work", f"{T}/eval-work-a7-v5main", "--out", out + ".tmp"], f"{T}/eval-a7.log")
    rc = wait_all([p])[0]
    if rc == 0:
        os.replace(out + ".tmp", out)
    return rc


def main():
    log(f"H2 v5 driver start pid {os.getpid()} guard {json.dumps(guard.install())}")
    beat()
    for name, fn in (("train", train), ("select", select), ("condition", condition), ("evaluate", evaluate)):
        rc = fn()
        if rc == 75:
            log(f"{name}: stopped for a shutdown or stop file; resumes later ({guard.reason() or 'stop file'})")
            guard.done()
            return 75
        if rc != 0:
            log(f"CHAIN: {name} failed with {rc}")
            return 1
    log("H2V5 CHAIN DONE")
    return 0


if __name__ == "__main__":
    sys.exit(main())
