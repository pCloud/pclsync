Generate a user-facing QA focus table for a release range and save it under `.releases/`.

**Audience:** non-technical testers and end users. Treat the reader as someone who uses the pCloud client but does not know the library internals. Translate every change into observable behaviour — what the user would *see*, *do*, or *notice* — not how the code works.

This skill is independent of `/changelog` and is NOT invoked by `/release`. Run it on its own whenever a user-facing QA pass is needed.

Steps:

1. **Detect the range boundary:**
   - If `$ARGUMENTS` is provided (non-empty), use that as the base ref — skip to step 3.
   - Otherwise, auto-detect using the same logic as `/changelog`:
     - Most recent tag: `git describe --tags --abbrev=0 2>/dev/null`.
     - Most recent version bump: `git log --oneline --grep="Bump library version" -1`.
     - Use the more recent of the two if both exist; use whichever exists if only one does; error out if neither exists.

2. **Confirm with user:** Show the detected base ref and the list of commit subject lines from `git log --oneline --no-merges <base>..HEAD`. Ask for confirmation before proceeding.

3. **Gather changes:** Run `git log --oneline --no-merges <base>..HEAD` and `git diff --stat <base>..HEAD`. For files with substantial changes, inspect `git diff <base>..HEAD -- <path>` so you understand what behaviour actually changed.

4. **Read current version** from `psettings.h` — extract the `PSYNC_LIB_VERSION` define value.

5. **Translate each change into user-visible behaviour.** This is the core work of this skill. For every commit or group of related commits, ask:
   - What can the user now do that they could not before?
   - What might *break* or behave differently for the user?
   - In what real-world situation (poor connection, large file, locked screen, expired login, etc.) would the user encounter this change?
   - What error message, dialog, status, sync icon, or app behaviour might appear or disappear?

   Then write the row in everyday language. Specifically:
   - **Do** name the user-facing surface — "the desktop client", "the login screen", "Crypto Folder", "the sync status icon", "the file you just dropped into your sync folder".
   - **Don't** mention internal modules, file names, function names, structs, threads, locks, mutexes, SQL tables, FUSE, sockets, callbacks, or protocol fields.
   - **Don't** say "regression in `pfscrypto.c`" — say "encrypted files may fail to open" or "moving an encrypted file to another folder".
   - **Don't** say "SPKI pinning added to OpenSSL backend" — say "the app now verifies it is talking to the genuine pCloud servers and refuses to connect if a connection is being intercepted".
   - **Don't** say "bumped sqlite locking mode" — say "the app handles being open on multiple windows / sessions more reliably".
   - If a change is purely internal (refactor, build, test plumbing, code style) and has no observable user effect, **omit it**. Do not pad the table with rows that have nothing for the tester to verify.

6. **Compose the QA Focus table** in this format:

```
# QA Focus — v{version}

This page lists the user-visible changes in this release and the scenarios worth checking before shipping. It is written for testers who use the pCloud apps day-to-day; no programming knowledge is needed.

| Area | What to try | What to look for |
|------|-------------|------------------|
| {user-facing feature, e.g. "Sign in", "Sync status", "Encrypted folder"} | {concrete steps a tester can perform on a real device} | {what should happen, or what should *no longer* happen} |
```

   - One row per coherent area of risk. Group related commits into a single row.
   - "What to try" is a recipe a tester can follow without reading the source.
   - "What to look for" describes the expected outcome in user terms (what's on screen, in the file list, in the notification area, etc.) and any specific regression to watch for.
   - Keep each cell short — one or two sentences. Use a bulleted list inside a cell only if there are genuinely separate sub-scenarios.

7. **Show the table** in the conversation.

8. **Save** the table to `.releases/v{version}-qa.md`:
   - Create `.releases/` if it does not exist (`mkdir -p .releases`).
   - If the target file already exists, show a diff against the new content and ask whether to overwrite.

Rules:
- Do NOT modify any source files. Only writes to `.releases/`.
- Do NOT create the git tag.
- Do NOT push anything to remote.
- If you find yourself reaching for a technical term, stop and rewrite the row from the user's point of view. If a change genuinely cannot be expressed in user terms, drop it from the table.