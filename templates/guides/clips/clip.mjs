// A guide clip: ?guide=tallow|pylon|rerun&clip=idle|talk|action&frame=wide|medium|close,
// in the guide's home world. tools/guides/render_clips.py renders each one as a short video.
import { shot } from "../stage.mjs";
const q = new URL(import.meta.url).searchParams, guide = q.get("guide") || "tallow";
const HOME = { tallow: "thriller", pylon: "painted", rerun: "pixel" };
export default shot({ guide, style: HOME[guide], clip: q.get("clip") || "idle", frame: q.get("frame") || "medium", layer: "all" });
