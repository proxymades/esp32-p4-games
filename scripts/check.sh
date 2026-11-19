#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p .private/checks
cc -std=c11 -O2 -g -Wall -Wextra -Werror -fsanitize=address,undefined \
   -I main tests/util_check.c main/audio_resample.c main/button_filter.c main/checksum.c \
   -o .private/checks/util_check
.private/checks/util_check
python3 -c 'from pathlib import Path; import ast; [ast.parse(p.read_text(), filename=str(p)) for p in Path("scripts").glob("*.py")]; print("Python syntax PASS")'
bash -n scripts/*.sh
