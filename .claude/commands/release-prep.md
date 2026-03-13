Prepare the codebase for a new release commit by updating `gitcommit.h` and `psettings.h`.

Steps:

1. **Read current state** — read `gitcommit.h` and `psettings.h` to see the current values.

2. **Update `gitcommit.h`:**
   - Set `GIT_PREV_COMMIT_ID` to the full SHA of the current HEAD (`git rev-parse HEAD`).
   - Set `GIT_PREV_COMMIT_DATE` to HEAD's author date (`git log -1 --format="%ai"`).
   - Set `GIT_COMMIT_DATE` to the current local timestamp (`date "+%Y-%m-%d %H:%M:%S %z"`).

3. **Update `psettings.h`:**
   - Find `PSYNC_LIB_VERSION`. The format is `x.yy.mm.z` where:
     - `x` = major version
     - `yy` = 2-digit year
     - `mm` = 2-digit month (zero-padded)
     - `z` = minor version (incremented for each release within the same month)
   - Compute the new version:
     - Get the current year (2-digit) and month (2-digit, zero-padded) from today's date.
     - If `yy` and `mm` already match today's year and month, increment `z` by 1.
     - If the month or year differs, set `yy.mm` to today's values and reset `z` to `1`.
     - Keep `x` (major) unchanged.
   - Update the `PSYNC_LIB_VERSION` define with the new version string.

4. **Show the user a summary** of the changes made (old → new values for each field).

5. **Do NOT commit.** The user will run `/commit` separately when ready.

Rules:
- Only touch `gitcommit.h` and `psettings.h`. Do not modify any other files.
- If either file is missing, report an error and stop.