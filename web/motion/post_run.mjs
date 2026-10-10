// Runs the post stack (post.mjs) for the Motion renderer: scene passes on the
// linear accumulation texture before bloom, display passes after the finish.
import { PASS_HEADER, getPass, passParams } from "./post.mjs";

const HDR = "rgba16float";
const USE = { COPY_SRC: 0x01, COPY_DST: 0x02, BIND: 0x04, RENDER: 0x10 };

const BLIT = PASS_HEADER + /* wgsl */ `
@fragment fn main(@builtin(position) q: vec4f) -> @location(0) vec4f { return texel(q); }`;

export class PostRunner {
  constructor(host, sampler) {
    this.host = host; this.device = host.device; this.sampler = sampler;
    this.pipes = new Map(); this.tex = new Map(); this.ubufs = [];
  }
  #pipe(name, code, format) {
    const key = name + "|" + format;
    let p = this.pipes.get(key);
    if (!p) {
      const module = this.device.createShaderModule({ label: "post " + name, code });
      p = this.device.createRenderPipeline({ label: "post " + name, layout: this.#layout(),
        vertex: { module, entryPoint: "vs" }, fragment: { module, entryPoint: "main", targets: [{ format }] }, primitive: { topology: "triangle-list" } });
      this.pipes.set(key, p);
    }
    return p;
  }
  // One explicit layout for every pass, so a pass need not use every binding.
  #layout() {
    if (!this.pl) {
      const F = 2; // GPUShaderStage.FRAGMENT
      this.bgl = this.device.createBindGroupLayout({ label: "post pass", entries: [
        { binding: 0, visibility: F, buffer: { type: "uniform" } },
        { binding: 1, visibility: F, texture: { sampleType: "float" } },
        { binding: 2, visibility: F, sampler: { type: "filtering" } },
        { binding: 3, visibility: F, texture: { sampleType: "float" } }] });
      this.pl = this.device.createPipelineLayout({ label: "post pass", bindGroupLayouts: [this.bgl] });
    }
    return this.pl;
  }
  #texture(key, w, h, format = HDR) {
    let t = this.tex.get(key);
    if (!t || t.width !== w || t.height !== h || t.format !== format) {
      if (t) t.destroy();
      t = this.device.createTexture({ label: "post " + key, size: [w, h], format, usage: USE.RENDER | USE.BIND | USE.COPY_SRC | USE.COPY_DST });
      this.tex.set(key, t);
    }
    return t;
  }
  #uniform(i) {
    while (this.ubufs.length <= i) this.ubufs.push(this.host.buffer({ size: 80, usage: ["uniform", "copy-dst"], label: "post pass " + this.ubufs.length }));
    return this.ubufs[i];
  }
  #draw(enc, pipe, ubuf, srcView, prevView, dstView, label) {
    const bind = this.device.createBindGroup({ layout: this.bgl, entries: [
      { binding: 0, resource: { buffer: ubuf } }, { binding: 1, resource: srcView }, { binding: 2, resource: this.sampler }, { binding: 3, resource: prevView }] });
    const rp = enc.beginRenderPass({ label, colorAttachments: [{ view: dstView, loadOp: "clear", storeOp: "store", clearValue: [0, 0, 0, 1] }] });
    rp.setPipeline(pipe); rp.setBindGroup(0, bind); rp.draw(3); rp.end();
  }
  // Run specs of one stage. input: a texture; out: { view, format } for the last pass, or
  // null to leave the result in a texture, which is returned. History passes render into
  // their own history texture (read back next frame as prev), then a blit forwards it.
  run(enc, specs, input, W, H, frame, time, out, stage, ubase = 0) {
    let cur = input, n = 0;
    specs.forEach((spec, i) => {
      const pass = getPass(spec.pass);
      if (pass.stage !== stage) throw new Error(`post pass ${pass.name} is a ${pass.stage} pass, listed among ${stage} passes`);
      const u = this.#uniform(ubase + i);
      const v = new Float32Array(20);
      v.set([W, H, frame, time]); v.set(passParams(pass, spec), 4);
      this.host.write(u, v);
      const last = i === specs.length - 1;
      const code = PASS_HEADER + pass.wgsl;
      if (pass.history) {
        const hist = this.#texture(`${stage}:hist:${i}:${pass.name}`, W, H), next = this.#texture(`${stage}:histnext:${i}`, W, H);
        this.#draw(enc, this.#pipe(pass.name, code, HDR), u, cur.createView(), hist.createView(), next.createView(), "post " + pass.name);
        enc.copyTextureToTexture({ texture: next }, { texture: hist }, [W, H]);
        cur = hist;
        if (last && out) this.#draw(enc, this.#pipe("blit", BLIT, out.format), u, hist.createView(), hist.createView(), out.view, "post blit");
      } else if (last && out) {
        this.#draw(enc, this.#pipe(pass.name, code, out.format), u, cur.createView(), cur.createView(), out.view, "post " + pass.name);
      } else {
        const dst = this.#texture(`${stage}:ping:${n++ % 2}`, W, H);
        this.#draw(enc, this.#pipe(pass.name, code, HDR), u, cur.createView(), cur.createView(), dst.createView(), "post " + pass.name);
        cur = dst;
      }
    });
    return cur;
  }
  destroy() { for (const t of this.tex.values()) t.destroy(); for (const b of this.ubufs) b.destroy(); }
}
