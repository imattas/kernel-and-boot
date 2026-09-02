#!/bin/sh
set -eu

log=${1:?usage: validate_qemu_input.sh <serial-log>}
test -f "$log"
grep -F 'serial input bridge ready' "$log" >/dev/null
grep -F 'serial input bridge consumed' "$log" >/dev/null
grep -F 'init shell supervisor live' "$log" >/dev/null
if grep -E 'X64 Exception|kernel contract invalid|KERNEL (PANIC|EXCEPTION)' "$log" >/dev/null; then
    echo "QEMU input test reported a failure" >&2
    exit 1
fi
echo "QEMU serial input: PASS"
