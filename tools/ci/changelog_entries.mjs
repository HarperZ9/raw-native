// CI: a pull request adds a changelog.d entry instead of editing CHANGELOG.md.
//
//   node tools/ci/changelog_entries.mjs <base-sha> <head-sha>
//
// SKIP_CHANGELOG=true (set from the PR label no-changelog) waives the "needs an
// entry" rule for changes users cannot see. Editing CHANGELOG.md directly is never
// waived outside a release, which is recognised by entry files being deleted.

import { execFileSync } from 'node:child_process';
import { checkPr, parseNameStatus } from '../changelog/entries.mjs';

const [base, head] = process.argv.slice(2);
if (!base || !head) {
  console.error('usage: changelog_entries.mjs <base-sha> <head-sha>');
  process.exit(2);
}
// The rule applies once the scheme exists on the base branch. The pull request that
// introduces changelog.d/ moves the old list out of CHANGELOG.md, so it is exempt.
try {
  execFileSync('git', ['cat-file', '-e', `${base}:changelog.d/README.md`], { stdio: 'ignore' });
} catch {
  console.log('changelog: base has no changelog.d/ yet (this PR introduces it); rule not in force');
  process.exit(0);
}
const out = execFileSync('git', ['diff', '--name-status', '-M', `${base}...${head}`], { encoding: 'utf8' });
const problems = checkPr(parseNameStatus(out), { skip: process.env.SKIP_CHANGELOG === 'true' });
if (problems.length) {
  console.error(problems.map((p) => `changelog: ${p}`).join('\n'));
  process.exit(1);
}
console.log('changelog: entry rule satisfied');
