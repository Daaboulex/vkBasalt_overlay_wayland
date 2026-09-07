#!/usr/bin/env bash
set -euo pipefail

src=${1:?source directory}
build=$src/meson.build

users=$(grep -rlE 'dlopen|dlsym|dlclose|dlerror|dlfcn\.h' "$src" \
  --include='*.c' --include='*.cpp' | sed "s|^$src/||" | sort | tr '\n' ' ')

awk -v users="$users" '
  BEGIN { split(users, u, " "); for (i in u) if (u[i] != "") need[u[i]] = 1 }
  /^[A-Za-z_][A-Za-z0-9_]*[ \t]*=[ \t]*\[[ \t]*$/ {
    var = $1; inlist = 1; next
  }
  inlist {
    if ($0 ~ /^[ \t]*\][ \t]*$/) { inlist = 0; next }
    if (match($0, /'"'"'[^'"'"']+'"'"'/)) {
      s = substr($0, RSTART + 1, RLENGTH - 2); members[var] = members[var] " " s
    }
    next
  }
  /(static_library|shared_library|executable)[ \t]*\(/ && !depth {
    collecting = 1; text = ""; depth = 0
  }
  collecting {
    text = text $0 "\n"
    n = split($0, ch, "")
    for (i = 1; i <= n; i++) { if (ch[i] == "(") depth++; else if (ch[i] == ")") depth-- }
    if (depth <= 0) { blocks[++nb] = text; collecting = 0; depth = 0 }
  }
  END {
    bad = 0
    for (b = 1; b <= nb; b++) {
      t = blocks[b]; own = ""
      while (match(t, /'"'"'[^'"'"']+'"'"'/)) {
        own = own " " substr(t, RSTART + 1, RLENGTH - 2)
        t = substr(t, RSTART + RLENGTH)
      }
      for (var in members) if (index(blocks[b], var)) own = own members[var]
      for (s in need) {
        if (index(own, " " s) && !index(blocks[b], "dl_dep")) {
          split(blocks[b], firstline, "\n")
          printf "%s uses dl symbols but its target does not declare dl_dep:\n  %s\n", s, firstline[1]
          bad = 1
        }
      }
    }
    exit bad
  }
' "$build"
