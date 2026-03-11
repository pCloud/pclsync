# Git Commit Message Pattern

This document describes the commit message pattern used in this project.

## Format

```
{Category} | {Brief description of the change in imperative tone}
```

**Optional body with bullet points:**
```
- {Detail 1}
- {Detail 2}
- {Detail 3}
```

## Pattern Rules

### Subject Line

The subject line follows this structure:
- `{Category} | {Description}`
- **Category**: A scope label indicating the area of the project affected (see [Categories](#categories) below)
- **Pipe separator**: ` | ` (space-pipe-space)
- **Description**: Brief, imperative mood description of what the commit does (Fix, not Fixed / Fixes etc.).
- The subject line should be capitalized and must not end in a period
- The subject line must not exceed 80 characters

### Categories

**Feature areas:**

| Category  | When to use                                                         |
|-----------|---------------------------------------------------------------------|
| Sync      | Sync engine, local scan, remote diff, upload, download              |
| Crypto    | Client-side encryption, AES-256/RSA, `pcloudcrypto`, `pcrypto`     |
| FS        | FUSE virtual filesystem, `pfs`, `pfsfolder`, `pfsstatic`, `pfsxattr`|
| Net       | Network communication, binary API protocol, `pnetlibs`, `papi`     |
| Cache     | Caching layers, `pcache`, `ppagecache`                              |
| SSL       | SSL/TLS backends, `pssl-openssl`, `pssl-mbedtls`, `pssl-wolfssl`   |
| Platform  | Platform abstraction, overlays, `pcompat`, `poverlay`               |
| Core      | Data structures and utilities: `plist`, `ptree`, `plocks`, `pcrc32c`|

**Infrastructure:**

| Category  | When to use                                               |
|-----------|-----------------------------------------------------------|
| Build     | Makefile, compiler flags, SSL backend selection, linking  |
| Deps      | Third-party dependencies (sqlite3, zlib, miniz, libfuse)  |
| CI        | Continuous integration pipelines and automation            |
| Docs      | Documentation, README, CLAUDE.md, COMMIT.md               |
| Test      | Test harness, unit tests, mocking infrastructure          |
| Refactor  | Code restructuring with no behavior change                |

### Use the Imperative

In keeping with the standard output of git itself, all commit subject lines must be written using the imperative:

**Good**

- Refactor subsystem X for readability
- Update getting started documentation
- Remove deprecated methods
- Release version 1.0.0

**Bad**

- Fixed bug with Y
- Changing behavior of X

***Very* Bad**

- More fixes for broken stuff
- Sweet new API methods
- 42

Your commit subject line must be able to complete the sentence

> If applied, this commit will ...

## Subject Line Standard Terminology

| First Word | Meaning                                              |
|------------|------------------------------------------------------|
| Add        | Create a capability e.g. feature, test, dependency.  |
| Cut/Drop   | Remove a capability e.g. feature, test, dependency.  |
| Fix        | Fix an issue e.g. bug, typo, accident, misstatement. |
| Bump       | Increase the version of something e.g. dependency.   |
| Make       | Change the build process, or tooling, or infra.      |
| Start      | Begin doing something; e.g. create a feature flag.   |
| Stop       | End doing something; e.g. remove a feature flag.     |
| Refactor   | A code change that MUST be just a refactoring.       |
| Reformat   | Refactor of formatting, e.g. omit whitespace.        |
| Optimize   | Refactor of performance, e.g. speed up code.         |
| Document   | Refactor of documentation, e.g. help files.          |

### Examples

```
Sync | Fix race condition in upload retry logic
Build | Add unit tests for ptree, pintervaltree, and pcrc32c
Crypto | Add `--password-file` option to `crypto setup`
FS | Fix null pointer dereference in `pfsfolder_stat()`
Refactor | Split `plibs.h` into 4 cohesive headers
SSL | Fix RSA key initialization for wolfSSL
Net | Add deep copy function for `binresult` trees
Platform | Allow defining custom default FUSE mount location
Core | Optimize `pintervaltree` lookup for overlapping ranges
Docs | Add CLAUDE.md and commit style guide
Test | Add Check-based test harness with FFF for mocking
Deps | Bump embedded miniz to v3.0
```

### Body Format (Optional)

When additional details are needed:
- **Leave a blank line after the subject line**
- **The body must only contain explanations as to what and why, never how.**
- **Use the Body to Explain the Background and Reasoning, not the Implementation.** Especially if the diff is rather large or extremely clustered, you can save all fellow developers some time by explaining why you did what.
- Use bulleted lists with `-` for multiple changes.
- Use backticks for code elements (`` `ClassName` ``, `` `methodName()` ``)
- The body copy must be wrapped at 90 columns

Example:
```
Crypto | Add `--password-file` option to `crypto setup`

- Allow reading the crypto password from a file descriptor
- Useful for scripted/automated setups where interactive input is not possible

Closes #42
```

## Closing Issues

Use GitHub keywords in the commit body to automatically close issues when merged:
- `Fixes #N` — closes the issue (implies a bug fix)
- `Closes #N` — closes the issue (general purpose)
- `Resolves #N` — closes the issue (general purpose)

Place these on their own line at the end of the body, after a blank line.

## GitHub Markdown Support

The pattern uses these GitHub-supported markdown elements:
- **Inline code**: `` `ClassName` `` for referencing code elements
- **Bullet lists**: `-` for listing multiple changes
- **Issue references**: `#N` automatically links to the corresponding GitHub issue

## Template

```
{Category} | {Imperative verb} {what changed}

- {Additional detail if needed}
- {Additional detail if needed}

Closes #N
```
