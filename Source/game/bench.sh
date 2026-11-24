#!/bin/sh
# emulator benchmark of town and dungeon scenes (instructions per frame at 1 IPC).
# The Pocket runs locked at 30 fps, so the real budget is 2x a 60 Hz frame: 200% here = full budget.
cd "$(dirname "$0")"
EXTRA=-DBENCH=1 OUT=build/bench_town.bin ./build_pocket.sh >/dev/null && ./build/rv32emu build/bench_town.bin ${1:-400} 2>&1 | tail -1 | sed 's/^/town:    /'
cp build/emu_play.ppm build/bench_town.ppm
EXTRA=-DBENCH=2 OUT=build/bench_dungeon.bin ./build_pocket.sh >/dev/null && ./build/rv32emu build/bench_dungeon.bin ${1:-400} 2>&1 | tail -1 | sed 's/^/dungeon: /'
cp build/emu_play.ppm build/bench_dungeon.ppm
