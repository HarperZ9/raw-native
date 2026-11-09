#pragma once
// raw_native_cli action-dump: the frame-data inspector for raw/game/action (docs/action-timeline.md).
//   action-dump table <moves.json> [--foe-windup <x>]   every move's frame line, sorted
//   action-dump strip <moves.json> <set> <id>           one move as a frame strip
//   action-dump diff <a.txt> <b.txt>                    two frame tables, move by move
//   action-dump tells <clip-markers.json> [--windup x --strike y --ease e]
//                                                       the telegraph retime lint, every tell move
//   action-dump trials <clip-markers.json> <trials.json> <key.json> [--seed n --per-kind n]
//                                                       the blind readability test's files
// <moves.json> is an lw-moves v1 file; <set> is base or tool/<tool id>/<lamb|slow|foe>.
// Exit 0 done (diff: equal), 1 diff found a difference or tells found an issue, 2 bad input.
namespace raw {
int actionDumpCommand(int argc, char** argv);
int actionTellsCommand(int argc, char** argv);   // tells, trials
}
