#!/bin/sh
set -e
cd "$(dirname "$0")"
python3 tools/gen_art.py src/art_data.h
CF="--target=riscv32-unknown-elf -march=${MARCH:-rv32im} -mabi=ilp32 ${OPT:--O2} -g -ffreestanding -fno-builtin -nostdlib -fno-pic -mcmodel=medany -ffunction-sections -fdata-sections -Wall -Wno-unused-function -Wno-unused-variable"
clang $CF $EXTRA -c plat/pocket_main.c -o build/pocket_main.o
clang --target=riscv32-unknown-elf -march=rv32im -mabi=ilp32 -c /home/claude/ext/openfpga-litex/lang/linker/init_asm.S -o build/init_asm.o
ld.lld -m elf32lriscv -L /home/claude/ext/openfpga-litex/lang/linker -T /home/claude/ext/openfpga-litex/lang/linker/c-linker.ld --gc-sections -N -o build/lanterndeep.elf build/init_asm.o build/pocket_main.o
llvm-objcopy -O binary build/lanterndeep.elf ${OUT:-build/boot.bin} 2>/dev/null || objcopy -O binary build/lanterndeep.elf ${OUT:-build/boot.bin}
ls -la ${OUT:-build/boot.bin}
