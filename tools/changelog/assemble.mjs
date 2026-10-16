// Changelog entries: preview, check, or assemble a release.
//
//   node tools/changelog/assemble.mjs --check            every entry file is well formed
//   node tools/changelog/assemble.mjs --preview          print the Unreleased list as it will read
//   node tools/changelog/assemble.mjs --release 0.9.0 --date 2026-10-20
//       write the entries into CHANGELOG.md under "## 0.9.0 (2026-10-20)" and delete the entry files
//
// Run from the repository root. See changelog.d/README.md.

import { readFileSync, writeFileSync, unlinkSync } from 'node:fs';
import { join } from 'node:path';
import { ENTRY_DIR, assemble, entryBlock, readEntries, validateEntry } from './entries.mjs';

function arg(name) {
  const i = process.argv.indexOf(name);
  return i < 0 ? undefined : process.argv[i + 1];
}

const root = process.cwd();
const entries = readEntries(root);
const problems = entries.flatMap((e) => validateEntry(e.name, e.text));

if (process.argv.includes('--check')) {
  if (problems.length) {
    console.error(problems.join('\n'));
    process.exit(1);
  }
  console.log(`changelog.d: ${entries.length} entr${entries.length === 1 ? 'y' : 'ies'}, all well formed`);
} else if (process.argv.includes('--preview')) {
  console.log(entryBlock(entries));
} else if (process.argv.includes('--release')) {
  if (problems.length) {
    console.error(problems.join('\n'));
    process.exit(1);
  }
  const path = join(root, 'CHANGELOG.md');
  const out = assemble(readFileSync(path, 'utf8'), entries, arg('--release'), arg('--date'));
  writeFileSync(path, out);
  for (const e of entries) unlinkSync(join(root, ENTRY_DIR, e.name));
  console.log(`CHANGELOG.md: ${entries.length} entries released as ${arg('--release')}; entry files removed`);
} else {
  console.error('usage: assemble.mjs --check | --preview | --release X.Y.Z --date YYYY-MM-DD');
  process.exit(2);
}
