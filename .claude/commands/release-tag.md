Create an annotated git tag on HEAD using a previously-generated changelog draft from `.releases/`.

This skill does NOT generate the changelog (`/changelog`) or the QA focus (`/qa-focus`). It only creates the tag.

Steps:

1. **Read current version** from `psettings.h` — extract the `PSYNC_LIB_VERSION` define value.

2. **Locate the draft file:**
   - If `$ARGUMENTS` is provided, treat it as an explicit path to the draft file.
   - Otherwise, default to `.releases/v{version}-changelog.md`.
   - If the draft does not exist, report an error suggesting the user run `/changelog` first, then stop.

3. **Pre-flight checks:**
   - If the tag `v{version}` already exists (`git rev-parse v{version} 2>/dev/null`), report this and stop — do NOT overwrite an existing tag.
   - If the draft file's mtime is older than HEAD's commit time (`git log -1 --format=%ct`), warn the user that the draft may be stale and ask whether to proceed regardless.
   - Verify the working tree is clean (`git status --porcelain`). If dirty, warn the user and ask whether to proceed.

4. **Show the draft** content to the user and ask whether to create the annotated tag on HEAD.

5. **Create the tag** only after the user confirms:

```
git tag -a v{version} -F .releases/v{version}-changelog.md --cleanup=verbatim
```

   - Use `-F <file>` rather than `-m "$(cat ...)"` to preserve formatting and avoid shell-escaping issues.
   - `--cleanup=verbatim` is required: the default `strip` mode treats lines starting with `#` as comments and drops them, which would remove the `## New Features & Enhancements` / `## Bug Fixes` / etc. section headers from the annotation. Verbatim mode keeps the markdown intact.

6. **Confirm success:** Run `git show v{version} --no-patch --format=fuller` and show the user the tag was created correctly.

Rules:
- Do NOT push the tag to remote unless the user explicitly asks.
- Do NOT modify any files.
- Do NOT regenerate the draft. If the user wants a new draft, they should re-run `/changelog`.