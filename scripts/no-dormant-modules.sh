#!/usr/bin/env bash
# Fails when a module is compiled into the layer that nothing includes.
#
#   scripts/no-dormant-modules.sh [src-root] [extra-root ...]
#
# A file that builds but has no caller reads as a working capability from the
# outside and is not one. lut_cube.cpp was compiled for months with nothing
# calling it, so .cube LUTs looked supported and were not.
#
# Vendored trees are skipped: they are upstream's to shape, not ours.
set -uo pipefail

ROOTS=("$@")
[ "${#ROOTS[@]}" -gt 0 ] || ROOTS=(src tools)
SRC="${ROOTS[0]}"

fail=0
while read -r header; do
    case "$header" in
        */imgui/*|*/reshade/*) continue ;;
    esac

    base=$(basename "$header")
    referenced=0
    while read -r consumer; do
        [ "$consumer" = "$header" ] && continue
        referenced=1
        break
    done < <(grep -rlE "#include \"[^\"]*${base}\"" "${ROOTS[@]}" \
        --include='*.cpp' --include='*.hpp' --include='*.c' 2>/dev/null)

    if [ "$referenced" -eq 0 ]; then
        echo "  dormant  $header is included by nothing"
        fail=1
    fi
done < <(find "$SRC" -name '*.hpp' -type f | sort)

if [ "$fail" -ne 0 ]; then
    echo "a module compiled into the layer must have a caller, or be deleted"
    exit 1
fi

echo "no dormant modules"
