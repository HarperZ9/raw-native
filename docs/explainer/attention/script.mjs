// The words of "How attention moves information", timed. The scene burns them in as type and
// tools/explainer/captions.mjs writes the same rows as WebVTT, so the captions and the picture
// cannot drift apart. Numbers are filled from data.json at load time ({...} fields), never typed.
import { show } from "../../../web/explainer/data.mjs";

// Beat times (seconds).
export const CUE = Object.freeze({
  open: 0, title: 2.5, cloud: 8, cloudPush: 13, task: 20, taskCard: 25, step1: 31, step1Card: 37,
  step2: 45, step2Focus: 50, step2Card: 54, iris: 60, close: 61.6, source: 66, end: 72,
});

// One row per caption: [start, end, text]. `d` is data.json.
export function script(d) {
  const a = d.accuracy, ab = d.ablations, h = d.heads;
  const n = d.residual.shape.reduce((x, y) => x * y, 1);
  return [
    [CUE.title, CUE.cloud, "How attention moves information between tokens."],
    [CUE.cloud, CUE.cloudPush, `Every dot is one number this small model computed: ${show(n, "int")} of them.`],
    [CUE.cloudPush, CUE.task, "Here is one sequence it read, 64 positions deep."],
    [CUE.task, CUE.taskCard, "The sequence is 64 random characters. One stretch of them repeats."],
    [CUE.taskCard, CUE.step1, `Outside the repeat the model gets ${show(a.elsewhere, "pct")} of next characters right, about chance. Inside it, ${show(a.repeat, "pct")}.`],
    [CUE.step1, CUE.step1Card, "First, a head in layer 0 moves each character one position forward."],
    [CUE.step1Card, CUE.step2, `It puts ${show(h.previous_token.mean_weight, "f2")} of its attention on the previous token. Switch it off and repeat accuracy falls to ${show(ab.previous_token_head_zeroed.repeat, "pct")}.`],
    [CUE.step2, CUE.step2Focus, "Then layer 1 looks for the earlier copy of the current character."],
    [CUE.step2Focus, CUE.step2Card, "It attends to the character that came after that copy, and that is the prediction."],
    [CUE.step2Card, CUE.iris, `Switch off both layer 1 heads and repeat accuracy falls to ${show(ab.both_layer_1_heads_zeroed.repeat, "pct")}.`],
    [CUE.close, CUE.source, "Two steps: copy the previous character forward, then match it to find what came next."],
    [CUE.source, CUE.end, "Every number here was read from the model's output file while the film was drawn."],
  ];
}
