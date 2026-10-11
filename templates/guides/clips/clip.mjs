// A guide clip: ?guide=tallow|pylon|rerun&clip=idle|talk|action&frame=wide|medium|close
// [&style=thriller|painted|pixel][&move=pan|push&mode=integer|sharp&ease=linear|inout].
// tools/guides/render_clips.py renders each one as a short video.
import { shot } from "../stage.mjs";
import { MOVES, setSpring } from "../motion.mjs";
import { setMouth } from "../tallow.mjs";
import { setLegs } from "../rerun.mjs";
const q = new URL(import.meta.url).searchParams, guide = q.get("guide") || "tallow";
const HOME = { tallow: "thriller", pylon: "painted", rerun: "pixel" };
const mv = q.get("move"), D = 4.8;
// Test switches (PROGRESS.md records which won): spring=soft|firm, mouth=line|open, legs=front|profile.
if (q.get("spring")) setSpring(q.get("spring"));
if (q.get("mouth")) setMouth(q.get("mouth"));
if (q.get("legs")) setLegs(q.get("legs"));
export default shot({ guide, style: q.get("style") || HOME[guide], clip: q.get("clip") || "idle", frame: q.get("frame") || "medium", layer: "all",
  move: mv ? MOVES[mv]({ duration: D, mode: q.get("mode") || undefined, ease: q.get("ease") || undefined }) : null, duration: mv ? D : null });
