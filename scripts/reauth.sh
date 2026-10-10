#!/bin/sh
# Prewarm every seat's 1Password access, in one go.
#
# This repo keeps only OP_SERVICE_ACCOUNT_TOKEN in the ambient environment
# (see .envrc); every secret value is resolved per command by `op run`.
# Checks the caller's token first, confirms the service account answers, and
# confirms each op:// reference in .env.tpl resolves to a non-empty value
# without printing it. Without --seat it also allows the committed .envrc in
# every git worktree, so a seat started there inherits the token.
#
# Usage: scripts/reauth.sh [--seat] (from the repo root or any worktree)

if [ -z "${OP_SERVICE_ACCOUNT_TOKEN:-}" ]; then
  echo 'FAIL: this shell has no service account token. Stop, and ask for this agent to be restarted from a shell where direnv has loaded. Remember to authorize new worktrees first with scripts/reauth.sh from the main checkout.'
  exit 1
fi
echo 'OK:   service account token present'

case "$*" in
  '') check_worktrees=1 ;;
  --seat) check_worktrees=0 ;;
  *) echo 'FAIL: usage: scripts/reauth.sh [--seat]'; exit 1 ;;
esac

rc=0

check() {
  # $1 label, rest: command. Output is discarded; only the verdict is printed.
  label=$1; shift
  if "$@" >/dev/null 2>&1; then
    echo "OK:   $label"
  else
    echo "FAIL: $label"
    rc=1
  fi
}

repo=$(git -C "$(dirname "$0")" rev-parse --show-toplevel)

check "op service account token valid" op whoami

# Every reference in .env.tpl must resolve. The value goes to /dev/null; the
# test is only that it is non-empty. References may contain spaces, so read
# whole lines; rc is captured through a file because the loop runs in a subshell.
rc_file=$(mktemp)
sed -n 's/^[A-Za-z_][A-Za-z0-9_]*=\(op:\/\/.*\)$/\1/p' "$repo/.env.tpl" | while IFS= read -r ref; do
  # shellcheck disable=SC2016  # the $-expression is for the child sh, deliberately
  check "op read $ref" sh -c '[ -n "$(op read "$1")" ]' _ "$ref" || true
  [ "$rc" -eq 0 ] || echo 1 > "$rc_file"
done
[ -s "$rc_file" ] && rc=1
rm -f "$rc_file"

if [ "$check_worktrees" -eq 1 ]; then
  # A for loop, not a pipeline into `while read`: a pipeline runs the loop in a
  # subshell and the rc=1 below would be lost. Worktree paths here carry no spaces.
  for wt in $(git -C "$repo" worktree list --porcelain | awk '/^worktree /{print $2}'); do
    [ -f "$wt/.envrc" ] || continue
    if cmp -s "$repo/.envrc" "$wt/.envrc"; then
      direnv allow "$wt/.envrc" >/dev/null 2>&1
    else
      echo "FAIL: $wt/.envrc differs from $repo/.envrc; not allowing it"
      rc=1
      continue
    fi
    # shellcheck disable=SC2016  # the $-expressions are for the child sh, deliberately
    check "op token from a non-interactive shell in $wt" \
      env -u OP_SERVICE_ACCOUNT_TOKEN sh -c 'cd "$1" && eval "$(direnv export bash 2>/dev/null)" && [ -n "$OP_SERVICE_ACCOUNT_TOKEN" ]' _ "$wt"
  done
fi

exit "$rc"
