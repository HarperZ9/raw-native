// The web frame graph against the C++ one: every case of tests/test_frame_graph.cpp,
// with the same golden describe() text and the same recorded event log.
// Run: node --test web/
import { test } from "node:test";
import assert from "node:assert/strict";
import { FrameGraph, Access } from "./frame-graph.mjs";

test("a read before any write is refused at compile", () => {
  const g = new FrameGraph();
  const a = g.createHost("a");
  g.addPass("reader", [[a, Access.StorageRead]], () => {});
  assert.throws(() => g.compile(), /reads a before any pass writes it/);
});

test("the same resource twice in one pass is refused; so is a buffer in a host graph", () => {
  const g = new FrameGraph();
  const a = g.createHost("a");
  g.addPass("twice", [[a, Access.StorageWrite], [a, Access.StorageRead]], () => {});
  assert.throws(() => g.compile(), /twice/);
  const h = new FrameGraph();
  h.createBuffer("b", { size: 64 });
  assert.throws(() => h.compile(), /no device/);
});

test("a host graph runs kept passes in order and culls an unread pass", () => {
  const g = new FrameGraph();
  const a = g.createHost("a"), b = g.createHost("b"), unused = g.createHost("unused");
  let order = "";
  g.addPass("first", [[a, Access.StorageWrite]], () => { order += "1"; });
  g.addPass("dead", [[a, Access.StorageRead], [unused, Access.StorageWrite]], () => { order += "x"; });
  g.addPass("second", [[a, Access.StorageRead], [b, Access.StorageWrite]], () => { order += "2"; });
  g.markOutput(b);
  g.execute();
  assert.equal(order, "12");
  assert.ok(g.culled(1) && !g.culled(0) && !g.culled(2));
  assert.equal(g.describe(), "pass first kept: a=storage-write\n"
    + "pass dead culled: a=storage-read unused=storage-write\n"
    + "pass second kept: a=storage-read b=storage-write\n");
});

test("on a device: lazy creation, barriers on every change and write, the C++ event log", () => {
  const log = [];
  let next = 1, live = 0;
  const dev = {
    createBuffer(d) { log.push(`create ${d.label}`); live++; return { index: next++ }; },
    destroyBuffer() { live--; },
  };
  const ctx = {
    barrier(list) { for (const x of list) log.push(`barrier ${x.buffer.index} ${x.before}->${x.after}`); },
    upload(b) { log.push(`upload ${b.index}`); },
    dispatch() { log.push("dispatch"); },
    copy(s, d) { log.push(`copy ${s.index}->${d.index}`); },
  };
  const g = new FrameGraph(dev);
  const desc = (label) => ({ size: 64, label });
  const inp = g.createBuffer("in", desc("in")), mid = g.createBuffer("mid", desc("mid"));
  g.createBuffer("skipped", desc("skipped"));
  const out = g.createBuffer("out", desc("out")), stage = g.createBuffer("stage", desc("stage"));
  const skipped = 2;
  g.addPass("upload", [[inp, Access.CopyDst]], (c, gr) => c.upload(gr.buffer(inp)));
  g.addPass("a", [[inp, Access.StorageRead], [mid, Access.StorageWrite]], (c) => c.dispatch());
  g.addPass("unread", [[mid, Access.StorageRead], [skipped, Access.StorageWrite]], (c) => c.dispatch());
  g.addPass("b", [[inp, Access.StorageRead], [mid, Access.StorageRead], [out, Access.StorageWrite]], (c) => c.dispatch());
  g.addPass("readback", [[out, Access.CopySrc], [stage, Access.CopyDst]], (c, gr) => c.copy(gr.buffer(out), gr.buffer(stage)));
  g.markOutput(stage);
  log.push("begin");
  g.execute(ctx);
  log.push("submit");
  assert.ok(g.culled(2));
  // The C++ test creates buffers before begin; the web host creates them inside
  // execute. The order of creations and of everything after begin is the same.
  const creates = log.filter((l) => l.startsWith("create"));
  assert.deepEqual(creates, ["create in", "create mid", "create out", "create stage"]);
  assert.deepEqual(log.filter((l) => !l.startsWith("create")), [
    "begin",
    "barrier 1 undefined->copy-dst", "upload 1",
    "barrier 1 copy-dst->storage-read", "barrier 2 undefined->storage-write", "dispatch",
    "barrier 2 storage-write->storage-read", "barrier 3 undefined->storage-write", "dispatch",
    "barrier 3 storage-write->copy-src", "barrier 4 undefined->copy-dst", "copy 3->4",
    "submit"]);
  assert.equal(live, 4);
  g.destroy();
  assert.equal(live, 0);
});

test("an import given as a function resolves on every execute (ping-pong)", () => {
  const bufs = [{ id: "A" }, { id: "B" }];
  let cur = 0;
  const g = new FrameGraph();
  const src = g.importBuffer("src", () => bufs[cur]);
  const dst = g.importBuffer("dst", () => bufs[1 - cur]);
  const seen = [];
  g.addPass("step", [[src, Access.StorageRead], [dst, Access.StorageWrite]], (c, gr) => seen.push(gr.buffer(src).id + gr.buffer(dst).id));
  g.markOutput(dst);
  g.execute(); cur = 1; g.execute();
  assert.deepEqual(seen, ["AB", "BA"]);
});
