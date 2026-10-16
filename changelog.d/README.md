# Changelog entries

Each change not yet in a release is one file here. Parallel pull requests then add
different files instead of editing the same lines of `CHANGELOG.md`, so merging one
no longer turns the others into conflicts.

## Adding an entry

Create `changelog.d/YYYY-MM-DD-slug.md`, dated the day you write it, with a short
slug of lower-case letters, digits and hyphens. The file holds one Markdown list
item, written exactly as it should read in `CHANGELOG.md`:

```markdown
- **Spectral sodium light** (`web/shaders/sodium`). What changed, what was
  measured, and what missed.
```

The first line starts with `- `. Continuation lines are indented two spaces. One
change per file.

CI (`tools/ci/changelog_entries.mjs`) fails a pull request that:

- edits `CHANGELOG.md` directly, outside a release, or
- changes code under `src/`, `raw/`, `web/`, `tools/`, `app/`, `wasm/`, `cmake/`,
  `scripts/`, `templates/` or `api/` without adding an entry. A change users cannot
  see takes the `no-changelog` label instead.

Docs, evidence and project notes need no entry.

## Reading and releasing

```sh
node tools/changelog/assemble.mjs --check      # every entry is well formed
node tools/changelog/assemble.mjs --preview    # the Unreleased list, newest first
node tools/changelog/assemble.mjs --release 0.9.0 --date 2026-10-20
```

`--release` writes the entries into `CHANGELOG.md` under `## 0.9.0 (2026-10-20)`
and deletes the entry files. The release pull request is the only one that edits
`CHANGELOG.md`; CI recognises it because it deletes entry files.

Entries print newest first, sorted by file name. The `0000-legacy-NN-*` files hold
the list as it stood when this scheme began, on 2026-10-16, with their order kept.
