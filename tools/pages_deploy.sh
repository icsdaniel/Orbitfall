#!/usr/bin/env bash
# Publishes build folders to the gh-pages branch and refreshes its index.
#
#   tools/pages_deploy.sh --remote URL [--site DIR] [--remove PATH]... [--message MSG]
#
# --site DIR   every folder under DIR that holds an index.html replaces the folder
#              with the same relative path on gh-pages (latest/, pr-12/, toys/x/).
# --remove P   deletes folder P from gh-pages (closed PR previews).
# Other folders are kept. The branch is rewritten as ONE commit each time, so the
# repository does not grow by a full web build per deploy.
set -euo pipefail
TOOLS="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REMOTE=""; SITE=""; MESSAGE="Publish playtest builds"; REMOVE=()
BRANCH="${PAGES_BRANCH:-gh-pages}"
while [ $# -gt 0 ]; do
  case "$1" in
    --remote) REMOTE="$2"; shift 2 ;;
    --site) SITE="$(cd "$2" && pwd)"; shift 2 ;;
    --remove) REMOVE+=("$2"); shift 2 ;;
    --message) MESSAGE="$2"; shift 2 ;;
    *) echo "unknown argument: $1" >&2; exit 2 ;;
  esac
done
[ -n "$REMOTE" ] || { echo "--remote is required" >&2; exit 2; }

safe_rel() {  # only plain relative folder names, no traversal
  [[ "$1" =~ ^[A-Za-z0-9._-]+(/[A-Za-z0-9._-]+)?$ ]] && [[ "$1" != *..* ]] && [[ "$1" != .* ]]
}

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

attempt() {
  local dir="$WORK/pages" old=""
  rm -rf "$dir"; mkdir -p "$dir"
  git -C "$dir" init -q
  git -C "$dir" remote add origin "$REMOTE"
  if git -C "$dir" fetch -q --depth 1 origin "$BRANCH" 2>/dev/null; then
    old="$(git -C "$dir" rev-parse FETCH_HEAD)"
    git -C "$dir" checkout -q -B "$BRANCH" FETCH_HEAD
  else
    echo "branch $BRANCH does not exist yet; creating it"
    git -C "$dir" checkout -q --orphan "$BRANCH"
  fi

  local rel
  for rel in "${REMOVE[@]}"; do
    safe_rel "$rel" || { echo "refusing to remove '$rel'" >&2; exit 2; }
    rm -rf "${dir:?}/$rel"
    echo "removed $rel/"
  done
  if [ -n "$SITE" ]; then
    while IFS= read -r index; do
      rel="$(dirname "${index#"$SITE"/}")"
      safe_rel "$rel" || { echo "refusing to publish '$rel'" >&2; exit 2; }
      rm -rf "${dir:?}/$rel"; mkdir -p "$dir/$rel"
      cp -a "$SITE/$rel/." "$dir/$rel/"
      echo "published $rel/"
    done < <(find "$SITE" -mindepth 2 -maxdepth 3 -name index.html | sort)
  fi
  rmdir "$dir/toys" 2>/dev/null || true
  python3 "$TOOLS/pages_index.py" "$dir"

  git -C "$dir" add -A
  local tree commit
  tree="$(git -C "$dir" write-tree)"
  commit="$(git -C "$dir" -c user.name="${GIT_AUTHOR_NAME:-github-actions[bot]}" \
    -c user.email="${GIT_AUTHOR_EMAIL:-41898282+github-actions[bot]@users.noreply.github.com}" \
    commit-tree "$tree" -m "$MESSAGE")"
  # The lease makes a concurrent deploy fail here instead of being overwritten.
  git -C "$dir" push -q --force-with-lease="refs/heads/$BRANCH:$old" origin "$commit:refs/heads/$BRANCH"
}

for try in 1 2 3 4; do
  # Subshell outside any `if`/`&&` so that `set -e` stays active inside attempt().
  set +e; ( set -e; attempt ); rc=$?; set -e
  if [ "$rc" -eq 0 ]; then echo "deployed to $BRANCH"; exit 0; fi
  if [ "$rc" -eq 2 ]; then exit 2; fi
  echo "deploy attempt $try failed; retrying" >&2
  sleep $((try * 3))
done
echo "deploy failed" >&2
exit 1
