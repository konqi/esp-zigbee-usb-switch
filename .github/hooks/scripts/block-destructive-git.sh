#!/usr/bin/env bash
set -euo pipefail

payload="$(cat)"
lc_payload="$(printf '%s' "$payload" | tr '[:upper:]' '[:lower:]')"

# Only inspect terminal-oriented tool payloads and block clearly destructive git commands.
if [[ "$lc_payload" =~ run_in_terminal|send_to_terminal|run_task ]]; then
  if [[ "$lc_payload" =~ git[[:space:]]+reset[[:space:]]+--hard ]] || \
     [[ "$lc_payload" =~ git[[:space:]]+checkout[[:space:]]+-- ]] || \
     [[ "$lc_payload" =~ git[[:space:]]+clean[[:space:]]+-fdx? ]] || \
     [[ "$lc_payload" =~ git[[:space:]]+restore[[:space:]]+--source=.*[[:space:]]+--worktree ]] || \
     [[ "$lc_payload" =~ rm[[:space:]]+-rf[[:space:]]+\.[[:space:]]*git ]]; then
    cat <<'JSON'
{
  "hookSpecificOutput": {
    "hookEventName": "PreToolUse",
    "permissionDecision": "deny",
    "permissionDecisionReason": "Blocked by workspace policy: destructive git command. Request explicit user approval and use a safer alternative when possible."
  }
}
JSON
    exit 0
  fi
fi

cat <<'JSON'
{
  "hookSpecificOutput": {
    "hookEventName": "PreToolUse",
    "permissionDecision": "allow",
    "permissionDecisionReason": "No destructive git pattern detected."
  }
}
JSON
