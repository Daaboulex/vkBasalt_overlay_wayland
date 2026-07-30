#!/usr/bin/env bash
# Compiles every shader in ReShade's official package index through this layer's
# compile environment and diffs the result against the recorded baseline.
#
#   scripts/shader-corpus.sh            # run and diff against test/shader-corpus-baseline.txt
#   scripts/shader-corpus.sh --record   # overwrite the baseline with this run
#
# Needs network: it clones the packages listed in EffectPackages.ini. That is why
# this is a script and not a flake check -- a sandboxed build cannot fetch them.
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BASELINE="$ROOT/test/shader-corpus-baseline.txt"
RECORD=0
[ "${1:-}" = "--record" ] && RECORD=1

WORK=$(mktemp -d "${TMPDIR:-/tmp}/vkb-corpus.XXXXXX") || exit 1
trap 'rm -rf "$WORK"' EXIT

TESTER=$(nix build "$ROOT#vkbasalt-overlay" --no-link --print-out-paths 2>/dev/null | head -1)/bin/vkbasalt-test-shaders
[ -x "$TESTER" ] || { echo "could not build vkbasalt-test-shaders"; exit 1; }

IDX=$(nix eval --impure --raw --expr \
  'toString (builtins.fetchGit { url = "https://github.com/crosire/reshade-shaders"; ref = "list"; })' 2>/dev/null)
[ -n "$IDX" ] || { echo "could not fetch the package index"; exit 1; }

mapfile -t REPOS < <(grep -oE "https://github.com/[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+" \
  "$IDX/EffectPackages.ini" | sed 's/\.git$//' | sort -u)
echo "packages listed: ${#REPOS[@]}"

: > "$WORK/packs.txt"
for r in "${REPOS[@]}"; do
  p=$(nix eval --impure --raw --expr "toString (builtins.fetchGit { url = \"$r\"; allRefs = true; })" 2>/dev/null)
  if [ -n "$p" ] && [ -d "$p" ]; then
    echo "$p" >> "$WORK/packs.txt"
  else
    echo "unfetched: $r" >&2
  fi
done
echo "packages fetched: $(wc -l < "$WORK/packs.txt")"

# Every directory holding a .fx or .fxh is an include path, so cross-pack
# includes resolve as they would in a real install.
while read -r p; do
  find "$p" -type f \( -name '*.fx' -o -name '*.fxh' \) -printf '%h\n' 2>/dev/null
done < "$WORK/packs.txt" | sort -u > "$WORK/dirs.txt"

mapfile -t DIRS < "$WORK/dirs.txt"
"$TESTER" "${DIRS[@]}" > "$WORK/report.txt" 2>&1

grep -E "^(PASS|WARN|FAIL) " "$WORK/report.txt" | awk '{print $1, $2}' | sort > "$WORK/verdicts.txt"
grep -E "  RESULTS:" "$WORK/report.txt"

if [ "$RECORD" -eq 1 ]; then
  mkdir -p "$(dirname "$BASELINE")"
  cp "$WORK/verdicts.txt" "$BASELINE"
  echo "baseline recorded: $(wc -l < "$BASELINE") shaders"
  exit 0
fi

[ -f "$BASELINE" ] || { echo "no baseline at $BASELINE -- run with --record"; exit 1; }

if diff -u "$BASELINE" "$WORK/verdicts.txt" > "$WORK/diff.txt"; then
  echo "no change against the baseline"
  exit 0
fi

echo
echo "CHANGED against the baseline:"
grep -E "^[+-](PASS|WARN|FAIL)" "$WORK/diff.txt"
exit 1
