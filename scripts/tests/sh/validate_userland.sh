#!/bin/sh
set -eu

elf=${1:?usage: validate_userland.sh <elf>}
case "$elf" in
    build/userland/*) ;;
    *) echo "userland artifact outside build/userland: $elf" >&2; exit 1 ;;
esac
test -f "$elf"
file "$elf" | grep -E 'ELF 64-bit.*x86-64' >/dev/null
readelf -h "$elf" | grep -E 'Type:[[:space:]]+EXEC' >/dev/null
readelf -h "$elf" | grep -E 'Machine:[[:space:]]+Advanced Micro Devices X86-64' >/dev/null
readelf -s "$elf" | grep -E '[[:space:]]_start$' >/dev/null
echo "userland init ELF contract: PASS"
