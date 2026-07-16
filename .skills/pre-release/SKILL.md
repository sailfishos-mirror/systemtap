---
name: pre-release
description: >-
  Carry out SystemTap pre-release build checks and compile release notes per
  the SystemTapReleaseGuide. Use when preparing a release, checking the build
  before tagging, drafting announcement notes, or when the user mentions
  pre-release, release notes, NEWS, AUTHORS, make rpm, or installcheck for a
  release.
---

# Pre-release (build check + release notes)

Follow the **Check the build** and **Compile the release notes** sections of
[SystemTapReleaseGuide](https://sourceware.org/systemtap/wiki/SystemTapReleaseGuide).

**Keep all of this private and local.** Prepare the tree and notes so they are
ready to push, but do **not** push, tag, upload tarballs, publish container
images, edit the public wiki, or send announcement mail. Leave those steps
for the user (or a later explicit request covering later guide sections).

Ask the user for the **previous release tag** (e.g. `release-5.5`) and the
**new version** (e.g. `5.6`) if not already known. Run long builds from the
top-level tree; see `AGENTS.md` for install/test quirks.

## Progress checklist

```
Pre-release:
- [ ] make update-po; commit po/* if changed
- [ ] Update AUTHORS via AUTHORS.sh
- [ ] Review PRERELEASE markers (leave them in place)
- [ ] Copyright years in banner sources (if needed)
- [ ] Book_Info.xml productnumber matches release
- [ ] ./scripts/update-docs; bump doc copyrights if changed
- [ ] Regenerate examples index
- [ ] NEWS current with major changes
- [ ] Fuller build: configure + make rpm + all install + installcheck
- [ ] stap-jupyter-container --build (advise user to --publish)
- [ ] Draft release notes from template sections below
```

---

## Check the build

Work in a configured top-level build tree (source tree for in-tree edits).

### 1. Translations and AUTHORS

```bash
make update-po   # may update po/* in the source tree; commit those changes
./AUTHORS.sh > AUTHORS
```

Review the `AUTHORS` diff; commit when appropriate.

### 2. PRERELEASE markers

```bash
git grep -Oemacs PRERELEASE
# or: git grep PRERELEASE
```

Review hits so you know where version/prerelease wording lives (`NEWS`,
docs, etc.). **Do not remove `PRERELEASE` markers** — they stay in the tree
to help subsequent releases.

### 3. Copyright years in messages

If the calendar year rolled over since the last bump, increment copyright
years in:

- `session.cxx`
- `staprun/common.c`
- `stapdyn/stapdyn.cxx`
- `stapbpf/stapbpf.cxx`

### 4. Beginners Guide product number

Set `productnumber` in
`doc/SystemTap_Beginners_Guide/en-US/Book_Info.xml` to match the release
number (keep `<edition>` consistent with project practice).

### 5. Regenerated docs

```bash
./scripts/update-docs
```

Also increment copyright years for any docs that changed (commonly
`doc/SystemTap_Tapset_Reference/tapsets.tmpl` and `dummy-tapsets.xml`).

### 6. Examples index

```bash
cd testsuite/systemtap.examples && perl examples-index-gen.pl
```

Commit regenerated `index.html` / `index.txt` / keyword indexes if they
change. (`make example_index` only warns when stale.)

### 7. NEWS

Ensure `NEWS` lists the major changes for this release. It is the primary
input for the release notes.

### 8. Fuller-than-usual build

```bash
configure --enable-dejazilla --enable-testapps=all
make rpm    # on several OS generations, for dependency/build-tool differences
make all install
sudo -E make installcheck
```

`--enable-testapps=all` matters especially if **`sys/sdt.h`** changed: other
apps compile against it; avoid regressions in compilability or performance.

Prefer `sudo -E` so debuginfod / env vars survive (see `AGENTS.md`).

### 9. Interactive container image

Build locally only:

```bash
stap-jupyter-container --build
```

Do **not** run `--publish` (no privilege to `quay.io/systemtap/isystemtap`).
Tell the user they should publish when ready:

```bash
stap-jupyter-container --publish
```

Report other blockers (rpm/OS matrix gaps, build failures) rather than
inventing substitutes.

---

## Compile the release notes

Before drafting, fetch recent notes from
[SystemTapReleases](https://sourceware.org/systemtap/wiki/SystemTapReleases)
and match their structure, section headings, wording habits, and overall
tone. Use the previous release as the primary template; skim one or two
older notes if style drifted. Draft locally in a file the user can edit
(e.g. `/tmp/systemtap-VERSION-notes.txt`); do not mail or update the wiki.

### Sections to fill

1. **Release URL** — must be  
   `https://sourceware.org/ftp/systemtap/releases/`  
   (older announcements sometimes used a wrong URL).

2. **News blurb** — 3–5 lines of highlights. Reporters often use only this.
   Write it **last**, after the other sections exist.

3. **Front-end changes** — major changes, new CLI options, miscellaneous
   internals. Bullets can come straight from `NEWS`.

4. **Script language changes** — `NEWS` items that change script syntax.

5. **Tapset changes** — new probe points and tapset functions from `NEWS`,
   preferably cross-checked with:

   ```bash
   git log --oneline "${OLD_RELEASE}.." -- tapset/
   ```

6. **Script examples** — name and title of new examples:

   ```bash
   git log --summary "${OLD_RELEASE}.." -- testsuite/systemtap.examples/
   ```

   Use the matching `.meta` description when present. **Include** the
   examples URL (`https://sourceware.org/systemtap/examples/`) and the
   **current count** of examples.

7. **Contributors**

   ```bash
   ./AUTHORS.sh "${OLD_RELEASE}.."
   git diff "${OLD_RELEASE}" -- AUTHORS   # specially welcome newcomers
   ```

8. **Tested kernels** — platforms used for this release (local
   installcheck, buildbot/Bunsen, Fedora/RHEL matrices, etc.).

9. **Known issues** — keep prior issues that still apply; add new ones.

10. **Bugs fixed** — sourceware bug numbers and titles fixed since the last
    release. Query Bugzilla: Resolution `FIXED`, changes from the **day
    after** the last release through now; trim columns to bug id + summary.
    Edit titles lightly for clarity. Also check CVEs and
    bugzilla.redhat.com.

    With Bunsen MCP available, `sourceware_bz_search` (instance
    `sourceware`, product/component as appropriate, status `RESOLVED`) can
    help enumerate candidates; still verify resolution dates against the
    previous release day.

### Drafting tips

- Prefer consistency with archived notes on SystemTapReleases over inventing
  a new layout.
- Keep the news blurb short; put detail in the sectional lists.
- Count commits since `${OLD_RELEASE}` if the template includes that stat:
  `git rev-list --count "${OLD_RELEASE}..HEAD"`.
- Do not invent bug IDs or contributor names; derive them from git/Bugzilla.

---

## Out of scope (later guide sections / publish)

Leave for the user: `git push`, signed tags, `make dist-gzip` upload,
`stap-jupyter-container --publish`, Fedora packaging, `update-htmldocs`,
mailing the announcement, wiki HomePage / SystemTapReleases updates, or
starting the next version bump cycle.
