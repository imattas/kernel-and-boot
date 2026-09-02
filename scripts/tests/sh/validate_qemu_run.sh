#!/bin/sh
set -eu
log=${1:?usage: validate_qemu_run.sh <serial-log>}
test -f "$log"
grep -F 'serial input bridge consumed' "$log" >/dev/null
grep -F 'USB HID keyboard preferred over PS2' "$log" >/dev/null
grep -F 'init shell supervisor live' "$log" >/dev/null
grep -F 'userland nested spawn live' "$log" >/dev/null
grep -F 'userland nested wait live' "$log" >/dev/null
grep -F 'userland nested reap live' "$log" >/dev/null
if grep -E 'X64 Exception|kernel contract invalid|KERNEL (PANIC|EXCEPTION)' "$log" >/dev/null; then
    echo "QEMU run test reported a failure" >&2
    exit 1
fi
echo "QEMU shell run: PASS"
