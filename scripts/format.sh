#!/usr/bin/env bash
# clang-format over the hand-written C/C++ sources.
#
#   scripts/format.sh           rewrite files in place
#   scripts/format.sh --check   exit non-zero if anything would change (CI)
#
# src/display/ui/** (EEZ Studio output) and src/display/drivers/** (vendored
# panel drivers) are deliberately skipped. Note that `find src` prints paths
# without a leading "./", so the -path patterns must not start with one --
# an earlier version used './src/display/ui/**/*' and excluded nothing.
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/.."

mode=(-i)
if [[ "${1:-}" == "--check" ]]; then
    mode=(--dry-run --Werror)
fi

find src lib sim \( -iname '*.h' -o -iname '*.c' -o -iname '*.cpp' \) \
    ! -path 'src/display/ui/*' ! -path 'src/display/drivers/*' -print0 \
    | xargs -0 clang-format "${mode[@]}"
