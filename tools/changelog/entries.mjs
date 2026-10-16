// Per-change changelog entries (changelog.d/).
//
// Every PR adds its own file under changelog.d/ instead of editing the shared
// "Unreleased" list in CHANGELOG.md. Parallel PRs then touch different files and
// stop conflicting with each other. At release time `assemble` writes the entries
// into CHANGELOG.md under the new version heading and the entry files are removed.
//
// Entry file name: YYYY-MM-DD-slug.md (the day the change was written), or
// 0000-legacy-NN-slug.md for entries migrated from the old shared list. The body
// is one Markdown list item: it starts with "- ", and continuation lines are
// indented two spaces, exactly as it will appear in CHANGELOG.md.
// Entries print newest first: file names sorted in descending order.

import { readdirSync, readFileSync } from 'node:fs';
import { join } from 'node:path';

export const ENTRY_DIR = 'changelog.d';
export const NAME_RE = /^(?:\d{4}-\d{2}-\d{2}|0000-legacy-\d{2})-[a-z0-9][a-z0-9-]*\.md$/;
const NOT_ENTRIES = new Set(['README.md']);

// Paths whose changes need an entry. Docs, evidence and project notes do not.
export const CODE_PREFIXES = ['src/', 'raw/', 'web/', 'tools/', 'app/', 'wasm/', 'cmake/', 'scripts/', 'templates/', 'api/'];

export function isEntryPath(p) {
  if (!p.startsWith(`${ENTRY_DIR}/`)) return false;
  const name = p.slice(ENTRY_DIR.length + 1);
  return !name.includes('/') && !NOT_ENTRIES.has(name);
}

// Problems with one entry file, as strings; an empty list means it is valid.
export function validateEntry(name, text) {
  const problems = [];
  if (!NAME_RE.test(name)) {
    problems.push(`${name}: name must be YYYY-MM-DD-slug.md (lower case, digits and hyphens)`);
  }
  const body = text.replace(/\r\n/g, '\n').replace(/\s+$/, '');
  if (!body) {
    problems.push(`${name}: empty entry`);
    return problems;
  }
  const lines = body.split('\n');
  if (!lines[0].startsWith('- ')) problems.push(`${name}: the entry must start with "- " (one list item)`);
  lines.slice(1).forEach((line, i) => {
    if (line.startsWith('- ')) problems.push(`${name}: line ${i + 2} starts a second list item; use one file per change`);
    else if (line.trim() && !line.startsWith('  ')) problems.push(`${name}: line ${i + 2} must be indented two spaces`);
  });
  return problems;
}

// [{name, text}] for every entry file in root/changelog.d, newest first.
export function readEntries(root) {
  let names;
  try {
    names = readdirSync(join(root, ENTRY_DIR));
  } catch (err) {
    if (err.code === 'ENOENT') return [];
    throw err;
  }
  return names
    .filter((n) => n.endsWith('.md') && !NOT_ENTRIES.has(n))
    .sort()
    .reverse()
    .map((name) => ({ name, text: readFileSync(join(root, ENTRY_DIR, name), 'utf8') }));
}

export function entryBlock(entries) {
  return entries.map((e) => e.text.replace(/\r\n/g, '\n').replace(/\s+$/, '')).join('\n');
}

// CHANGELOG.md text with the entries written under a new "## version (date)" heading,
// placed after the Unreleased section. The Unreleased pointer paragraph is kept.
export function assemble(changelog, entries, version, date) {
  if (!entries.length) throw new Error('no entries in changelog.d to release');
  if (!/^\d+\.\d+\.\d+$/.test(version)) throw new Error(`version must be X.Y.Z, got ${version}`);
  if (!/^\d{4}-\d{2}-\d{2}$/.test(date)) throw new Error(`date must be YYYY-MM-DD, got ${date}`);
  const text = changelog.replace(/\r\n/g, '\n');
  if (text.includes(`\n## ${version} `)) throw new Error(`CHANGELOG.md already has ${version}`);
  const unreleased = text.indexOf('\n## Unreleased\n');
  if (unreleased < 0) throw new Error('CHANGELOG.md has no "## Unreleased" heading');
  const next = text.indexOf('\n## ', unreleased + 1);
  const section = `## ${version} (${date})\n\n${entryBlock(entries)}\n\n`;
  if (next < 0) return `${text.replace(/\s+$/, '')}\n\n${section}`;
  return `${text.slice(0, next + 1)}${section}${text.slice(next + 1)}`;
}

// Check one PR's changed files. `changes` is [{status, path}] from
// `git diff --name-status base...head` (status A, M, D, R...). Returns problems.
export function checkPr(changes, { skip = false } = {}) {
  const problems = [];
  const touchedLog = changes.some((c) => c.path === 'CHANGELOG.md');
  const deletedEntries = changes.some((c) => c.status === 'D' && isEntryPath(c.path));
  const addedEntries = changes.filter((c) => c.status !== 'D' && isEntryPath(c.path));
  if (touchedLog && !deletedEntries) {
    problems.push('CHANGELOG.md was edited directly. Add your entry as changelog.d/YYYY-MM-DD-slug.md instead; '
      + 'CHANGELOG.md changes only at release, when `node tools/changelog/assemble.mjs --release` moves the entries in.');
  }
  const code = changes.filter((c) => CODE_PREFIXES.some((p) => c.path.startsWith(p)));
  if (code.length && !addedEntries.length && !deletedEntries && !skip) {
    problems.push(`this PR changes ${code.length} file(s) under ${CODE_PREFIXES.join(', ')} but adds no changelog.d entry `
      + '(add one, or label the PR no-changelog if the change is invisible to users)');
  }
  return problems;
}

export function parseNameStatus(out) {
  return out.split('\n').filter(Boolean).map((line) => {
    const parts = line.split('\t');
    const status = parts[0][0];
    return { status, path: parts[parts.length - 1] };
  });
}
