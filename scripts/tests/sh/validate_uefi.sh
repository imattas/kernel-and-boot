#!/bin/sh
set -eu

efi=${1:?usage: validate_uefi.sh <efi>}
test -f "$efi"
case "$efi" in
    build/image/esp/EFI/BOOT/BOOTX64.EFI) ;;
    *) echo "UEFI artifact outside build/image/esp/EFI/BOOT: $efi" >&2; exit 1 ;;
esac

file "$efi" | grep -E 'PE32\+ executable.*x86-64' >/dev/null
echo "UEFI artifact contract: PASS"
