#!/bin/sh
set -eu

image=${1:?usage: validate_image.sh <image>}
test -f "$image"
python3 - "$image" <<'PY'
import sys
data = open(sys.argv[1], 'rb').read()
assert len(data) == 67500 * 512
assert data[510:512] == b'\x55\xaa'
assert int.from_bytes(data[11:13], 'little') == 512
assert data[13] == 1
assert int.from_bytes(data[14:16], 'little') == 32
assert data[16] == 2
assert int.from_bytes(data[36:40], 'little') == 520
assert int.from_bytes(data[44:48], 'little') == 2
assert data[82:90] == b'FAT32   '
assert data[6 * 512:7 * 512] == data[:512]
assert data[7 * 512:8 * 512] == data[512:2 * 512]
fat = data[32 * 512:(32 + 520) * 512]
assert int.from_bytes(fat[8:12], 'little') == 5
assert int.from_bytes(fat[20:24], 'little') >= 0x0fffffff
root = data[(32 + 2 * 520) * 512:(33 + 2 * 520) * 512]
assert root[:11] == b'EFI        '
assert root[26:28] == b'\x03\x00'
assert root[32:43] == b'KERNEL  ELF'
assert int.from_bytes(root[58:60], 'little') >= 5
assert root[64:75] == b'INIT    ELF'
assert int.from_bytes(root[90:92], 'little') >= 5
assert root[96:107] == b'SHELL   ELF'
assert int.from_bytes(root[122:124], 'little') >= 5
assert root[128:139] == b'ARGS    ELF'
assert int.from_bytes(root[154:156], 'little') >= 5
assert root[160:171] == b'ENV     ELF'
assert int.from_bytes(root[186:188], 'little') >= 5
assert root[192:203] == b'CAT     ELF'
assert int.from_bytes(root[218:220], 'little') >= 5
assert root[224:235] == b'PWD     ELF'
assert int.from_bytes(root[250:252], 'little') >= 5
assert root[256:267] == b'MKDIR   ELF'
assert int.from_bytes(root[282:284], 'little') >= 5
assert root[288:299] == b'RM      ELF'
assert int.from_bytes(root[314:316], 'little') >= 5
assert root[320:331] == b'RMDIR   ELF'
assert int.from_bytes(root[346:348], 'little') >= 5
assert root[352:363] == b'TOUCH   ELF'
assert int.from_bytes(root[378:380], 'little') >= 5
assert root[384:395] == b'WRITE   ELF'
assert int.from_bytes(root[410:412], 'little') >= 5
assert root[416:427] == b'LS      ELF'
assert int.from_bytes(root[442:444], 'little') >= 5
assert root[448:459] == b'CHMOD   ELF'
assert int.from_bytes(root[474:476], 'little') >= 5
root_extension = data[(32 + 2 * 520 + 3) * 512:(32 + 2 * 520 + 4) * 512]
assert root_extension[:11] == b'ECHO    ELF'
assert int.from_bytes(root_extension[26:28], 'little') >= 5
assert root_extension[32:43] == b'STAT    ELF'
assert int.from_bytes(root_extension[58:60], 'little') >= 5
assert root_extension[64:75] == b'MV      ELF'
assert int.from_bytes(root_extension[90:92], 'little') >= 5
assert root_extension[96:107] == b'KILL    ELF'
assert int.from_bytes(root_extension[122:124], 'little') >= 5
assert data[(33 + 2 * 520) * 512 + 64:(33 + 2 * 520) * 512 + 75] == b'BOOT       '
assert data[(34 + 2 * 520) * 512 + 64:(34 + 2 * 520) * 512 + 75] == b'BOOTX64 EFI'
print('FAT image contract: PASS')
PY
if command -v fsck.fat >/dev/null 2>&1; then
    fsck.fat -n "$image" >/dev/null
fi
