import { test } from 'node:test';
import assert from 'node:assert/strict';
import { mkdtempSync, mkdirSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { assemble, checkPr, parseNameStatus, readEntries, validateEntry } from './entries.mjs';

const LOG = '# Changelog\n\nIntro.\n\n## Unreleased\n\nEntries live in changelog.d/.\n\n## 0.8.0 (2026-10-14)\n\n- old item\n';

test('a well-formed entry passes', () => {
  assert.deepEqual(validateEntry('2026-10-16-sodium.md', '- **Sodium** light.\n  More detail.\n'), []);
  assert.deepEqual(validateEntry('0000-legacy-07-lens-suite.md', '- Lens suite.\n'), []);
});

test('malformed entries are refused', () => {
  assert.equal(validateEntry('Sodium.md', '- x\n').length, 1);
  assert.equal(validateEntry('2026-10-16-x.md', '').length, 1);
  assert.equal(validateEntry('2026-10-16-x.md', 'plain text\n').length, 1);
  assert.equal(validateEntry('2026-10-16-x.md', '- one\n- two\n').length, 1);
  assert.equal(validateEntry('2026-10-16-x.md', '- one\nunindented\n').length, 1);
});

test('entries read newest first and README is not an entry', () => {
  const root = mkdtempSync(join(tmpdir(), 'cl-'));
  mkdirSync(join(root, 'changelog.d'));
  writeFileSync(join(root, 'changelog.d', 'README.md'), 'how to');
  writeFileSync(join(root, 'changelog.d', '0000-legacy-01-a.md'), '- legacy\n');
  writeFileSync(join(root, 'changelog.d', '2026-10-17-b.md'), '- newer\n');
  writeFileSync(join(root, 'changelog.d', '2026-10-16-c.md'), '- older\n');
  assert.deepEqual(readEntries(root).map((e) => e.name), ['2026-10-17-b.md', '2026-10-16-c.md', '0000-legacy-01-a.md']);
});

test('assemble writes a version section after Unreleased and keeps older releases', () => {
  const out = assemble(LOG, [{ name: '2026-10-17-b.md', text: '- newer\n' }, { name: '2026-10-16-c.md', text: '- older\n' }], '0.9.0', '2026-10-20');
  assert.match(out, /## Unreleased\n\nEntries live in changelog.d\/.\n\n## 0.9.0 \(2026-10-20\)\n\n- newer\n- older\n\n## 0.8.0/);
  assert.throws(() => assemble(out, [{ name: 'x', text: '- y' }], '0.9.0', '2026-10-21'), /already has/);
  assert.throws(() => assemble(LOG, [], '0.9.0', '2026-10-20'), /no entries/);
  assert.throws(() => assemble(LOG, [{ name: 'x', text: '- y' }], 'v1', '2026-10-20'), /X.Y.Z/);
});

test('PR rule: an entry file passes; a direct CHANGELOG edit fails, even with the skip label', () => {
  const ok = parseNameStatus('M\tweb/shaders/sodium/sodium.mjs\nA\tchangelog.d/2026-10-16-sodium.md\n');
  assert.deepEqual(checkPr(ok), []);
  const direct = parseNameStatus('M\tweb/shaders/sodium/sodium.mjs\nM\tCHANGELOG.md\n');
  assert.ok(checkPr(direct).some((p) => p.startsWith('CHANGELOG.md was edited directly')));
  assert.deepEqual(checkPr(direct, { skip: true }).map((p) => p.slice(0, 32)), ['CHANGELOG.md was edited directly']);
});

test('PR rule: code without an entry fails unless labelled; docs-only needs none', () => {
  const code = parseNameStatus('M\tsrc/renderer/frame.cpp\n');
  assert.equal(checkPr(code).length, 1);
  assert.deepEqual(checkPr(code, { skip: true }), []);
  assert.deepEqual(checkPr(parseNameStatus('M\tproject-docs/RELEASE-PLAN.md\nA\tevidence/x.json\n')), []);
});

test('PR rule: a release assembly (entries deleted, CHANGELOG edited) passes', () => {
  const release = parseNameStatus('M\tCHANGELOG.md\nD\tchangelog.d/2026-10-16-sodium.md\nM\tCMakeLists.txt\n');
  assert.deepEqual(checkPr(release), []);
});

test('renames are read by their new path', () => {
  assert.deepEqual(parseNameStatus('R100\tchangelog.d/a.md\tchangelog.d/2026-10-16-a.md\n'), [{ status: 'R', path: 'changelog.d/2026-10-16-a.md' }]);
});
