- **Changelog entries, one file per change** (`changelog.d/`, `tools/changelog/`). Every pull request
  used to edit the shared Unreleased list in `CHANGELOG.md`, so each merge put every other open pull
  request into conflict and cost about an hour of CI to recover. Each change now adds its own
  `changelog.d/YYYY-MM-DD-slug.md`; `tools/changelog/assemble.mjs` previews the list and writes it in at
  release. A CI job fails a pull request that edits `CHANGELOG.md` directly outside a release, or that
  changes code without an entry (the `no-changelog` label waives the second rule only). The 23 entries
  then in the list moved into `0000-legacy-NN-*` files, and reassembling them gives the list back byte
  for byte.
