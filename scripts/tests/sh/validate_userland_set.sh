#!/bin/sh
set -eu

root=${1:?usage: validate_userland_set.sh <directory>}
case "$root" in
    build/userland) ;;
    *) echo "userland directory outside build/userland: $root" >&2; exit 1 ;;
esac

count=0
for elf in "$root"/*.elf; do
    test -f "$elf"
    file "$elf" | grep -E 'ELF 64-bit.*x86-64' >/dev/null
    readelf -h "$elf" | grep -E 'Type:[[:space:]]+EXEC' >/dev/null
    readelf -h "$elf" | grep -E 'Machine:[[:space:]]+Advanced Micro Devices X86-64' >/dev/null
    readelf -s "$elf" | grep -E '[[:space:]]_start$' >/dev/null
    count=$((count + 1))
done
test "$count" -ge 50
echo "userland ELF set contract: PASS ($count artifacts)"
