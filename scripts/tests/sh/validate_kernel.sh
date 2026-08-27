#!/bin/sh
set -eu

elf=${1:?usage: validate_kernel.sh <elf>}
test -f "$elf"
file "$elf" | grep -E 'ELF 64-bit.*x86-64' >/dev/null
readelf -h "$elf" | grep -E 'Type:[[:space:]]+DYN' >/dev/null
readelf -h "$elf" | grep -E 'Machine:[[:space:]]+Advanced Micro Devices X86-64' >/dev/null
readelf -s "$elf" | grep -E '[[:space:]]kernel_entry$' >/dev/null
echo "kernel ELF contract: PASS"
