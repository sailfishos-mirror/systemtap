---
name: post-release
description: >-
  Start the next SystemTap development cycle after a release by bumping
  version numbers per the SystemTapReleaseGuide "Formally begin the next
  release's development cycle" section. Use when the user mentions
  post-release, version bump, next release cycle, or bumping configure.ac /
  NEWS / systemtap.spec after tagging a release.
---

# Post-release (begin next development cycle)

Follow the **Formally begin the next release's development cycle** section of
[SystemTapReleaseGuide](https://sourceware.org/systemtap/wiki/SystemTapReleaseGuide).

This runs **after** the release tarball (and related publish steps) are
already done, when the user asks to start the next development cycle. It
is only the version-bump steps.

**Keep this private and local** until the user asks to commit and/or push.
See `AGENTS.md` for autoconf commit quirks (regenerate and commit both
sources and generated files).

Ask for the **just-released version** (e.g. `5.6`) and the **next version**
(e.g. `5.7`) if not already known.

## Progress checklist

```
Post-release:
- [ ] Bump AC_INIT in configure.ac
- [ ] Bump AC_INIT in testsuite/configure.ac
- [ ] Book_Info.xml edition + productnumber
- [ ] systemtap.spec Version: (optional %changelog placeholder)
- [ ] NEWS: new "What's new in VERSION, PRERELEASE" heading
- [ ] autoconf / autoreconf -i; commit generated configure files too
```

---

## Bump version numbers

Update these files from `${RELEASED}` to `${NEXT}` (example: `5.6` → `5.7`).
`configure.ac` itself lists the companion files in a comment next to
`AC_INIT`.

### 1. `configure.ac`

In the `AC_INIT` macro:

```autoconf
AC_INIT([systemtap],[${NEXT}],[systemtap@sourceware.org],[systemtap])
```

### 2. `testsuite/configure.ac`

Same version in that file's `AC_INIT`.

### 3. Beginners Guide book info

In `doc/SystemTap_Beginners_Guide/en-US/Book_Info.xml`, set both:

```xml
<edition>${NEXT}</edition>        <!-- PRERELEASE -->
<productnumber>${NEXT}</productnumber>        <!-- PRERELEASE -->
```

Leave the `<!-- PRERELEASE -->` markers in place (they stay across releases).

### 4. `systemtap.spec`

Update the `Version:` line (near the `# PRERELEASE` comment):

```spec
# PRERELEASE
Version: ${NEXT}
```

Optionally add a `%changelog` placeholder matching prior entries, e.g.:

```spec
* <Day> <Mon> <DD> <YYYY> <Name> <email> - ${NEXT}-1
- Upstream release, see wiki page below for detailed notes.
  https://sourceware.org/systemtap/wiki/SystemTapReleases
```

(The real changelog date/author usually wait until the eventual release.)

### 5. `NEWS`

Add a new top section (or ensure the next-version heading exists) of the form:

```text
* What's new in version ${NEXT}, PRERELEASE

```

If a `${NEXT}` section already exists without `, PRERELEASE`, add that
marker. Do not invent feature blurbs here — leave the body empty or keep
only items already present.

---

## Regenerate configury

After editing the `.ac` files:

```bash
autoreconf -i
# if that fails with version mismatch noise, retry; plain autoconf alone
# is what the wiki mentions, but this tree normally needs autoreconf -i
```

Per `AGENTS.md`, commit **both** the hand-edited sources and the regenerated
`configure` / `Makefile.in` / `aclocal.m4` (and testsuite equivalents) when
the user asks for a commit.

Typical commit subject from prior bumps:

```text
prerelease: bump devel version to ${NEXT}, regenerate configury
```

---

## Out of scope

This skill is only the next-cycle version bump. For the earlier
build/notes checklist, see **pre-release** (`.skills/pre-release/`).
