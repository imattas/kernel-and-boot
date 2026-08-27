#!/usr/bin/env python3
import pathlib
import random
import subprocess
import sys

output = pathlib.Path(sys.argv[1])
rng = random.Random(1)
raw = bytes(0 if rng.random() < 0.8 else rng.randrange(1, 256) for _ in range(256))
compressed = subprocess.run(
    ["zstd", "-q", "-c", "--format=zstd", "--compress-literals", "-1"],
    input=raw, stdout=subprocess.PIPE, check=True).stdout
output.write_bytes(compressed)
output.with_suffix(".raw").write_bytes(raw)
