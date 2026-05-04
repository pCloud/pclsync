Run the release tagging workflow: changelog draft, then annotated git tag.

This is a thin orchestrator over two atomic skills. Each step is gated by user confirmation inside the underlying skill — this orchestrator does not add or skip any prompts.

Steps:

1. **Invoke `/changelog`** with `$ARGUMENTS` passed through verbatim. Wait for it to finish (the user has approved the draft and it has been saved to `.releases/v{version}-changelog.md`). If the user declines or the skill errors out, stop here.

2. **Invoke `/release-tag`** with no arguments — it will auto-locate `.releases/v{version}-changelog.md` from the current `PSYNC_LIB_VERSION`. If the user declines or the skill errors out, stop here.

Rules:
- Stop the chain as soon as any step fails or the user declines.
- Do not bypass any of the per-skill confirmation prompts.
- `/qa-focus` is intentionally NOT part of this orchestrator — QA notes target a different audience (end users / non-technical testers) and are produced on a separate cadence. Run `/qa-focus` independently when needed.
- The atomic skills remain available individually — use `/release` only when you want changelog + tag end-to-end.