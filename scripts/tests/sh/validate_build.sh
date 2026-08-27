#!/bin/sh
set -eu

elf=${1:?usage: validate_build.sh <elf>}

case "$elf" in
    build/tests/*) ;;
    *) echo "artifact outside build/tests: $elf" >&2; exit 1 ;;
esac

test -f "$elf"
file "$elf" | grep -E 'ELF 64-bit.*x86-64' >/dev/null

if command -v readelf >/dev/null 2>&1; then
    readelf -h "$elf" | grep -E 'Class:[[:space:]]+ELF64' >/dev/null
    readelf -s "$elf" | grep -E '[[:space:]]contract_entry$' >/dev/null
fi

test ! -e scripts/tests/c/toolchain_contract.o
test ! -e scripts/tests/asm/toolchain_contract.o
echo "toolchain contract: PASS"
