Generate a changelog draft for a new release and save it under `.releases/`.

This skill only produces the changelog text. It does NOT show QA focus areas (`/qa-focus`) and does NOT create the git tag (`/release-tag`).

Steps:

1. **Detect the range boundary:**
   - If `$ARGUMENTS` is provided (non-empty), use that as the base ref — skip to step 3.
   - Otherwise, auto-detect:
     - Find the most recent git tag on the current branch: `git describe --tags --abbrev=0 2>/dev/null`.
     - Find the most recent version bump commit: `git log --oneline --grep="Bump library version" -1`.
     - If both exist and the tag is on or after the bump commit (i.e., the tag's commit is an ancestor of or equal to the bump commit), use the tag.
     - If they disagree, use whichever is more recent (closer to HEAD).
     - If only one exists, use that one.
     - If neither exists, report an error and stop.

2. **Confirm with user:** Show the detected base ref, its commit message, and the list of commit subject lines from `git log --oneline --no-merges <base>..HEAD`. Ask the user to confirm or provide a different ref before proceeding. Do NOT continue until the user confirms.

3. **Gather changes:** Run `git log --oneline --no-merges <base>..HEAD` and `git diff --stat <base>..HEAD`.

4. **Read current version** from `psettings.h` — extract the `PSYNC_LIB_VERSION` define value.

5. **Compose the changelog** using this exact format (version as title, then grouped sections):

```
v{version}
----------

## New Features & Enhancements
- **{Feature}**: {description}

## Bug Fixes
- **{Category} / {file(s)}**: {description}

## Build & Tooling
- **{Area}**: {description}

## Format / Cleanup
- {description}
```

   - Group each commit into the most appropriate section based on the commit message and the changes.
   - Omit any section that has no entries.
   - Write descriptions in past tense, concise, focusing on what changed and why it matters.
   - Collapse related commits into a single entry when appropriate.

6. **Show the draft** to the user for approval. Do NOT save until the user approves.

7. **Save the approved draft** to `.releases/v{version}-changelog.md`:
   - Create `.releases/` if it does not exist (`mkdir -p .releases`).
   - If the target file already exists, show a diff against the new content and ask whether to overwrite.
   - Write the file content **without** any leading or trailing fences — the file will be passed verbatim to `git tag -F` by `/release-tag`.

Rules:
- Do NOT modify any source files. Only writes to `.releases/`.
- Do NOT create the git tag — that is `/release-tag`.
- Do NOT show QA focus areas — that is `/qa-focus`.
- Do NOT push anything to remote.