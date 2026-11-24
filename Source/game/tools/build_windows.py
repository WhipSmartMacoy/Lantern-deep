"""Lantern Deep - build the game on this PC (Windows).

Does what build_pocket.sh and build_web.sh do, without Linux:
  1. tools/gen_art.py      art/*.png  ->  src/art_data.h
  2. clang (RISC-V)        ->  boot.bin for the Analogue Pocket
  3. clang (WebAssembly)   ->  the PC test page
  4. puts boot.bin in "Pocket SD" (for the Pocket Updater) and the page in "Lantern Deep - PC test.html".
     The previous ones are kept in Source/game/build/previous/.

Needs: Python 3 with Pillow and NumPy, and LLVM 20 or newer (clang, ld.lld, llvm-objcopy).
Run it with BUILD_LANTERN_DEEP.bat. Options: --check (only check the tools), --no-install (build, don't copy).
"""
import os
import sys
import shutil
import base64
import subprocess
import time

GAME = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))       # Lantern Deep/Source/game
ROOT = os.path.dirname(os.path.dirname(GAME))                             # Lantern Deep
BUILD = os.path.join(GAME, 'build')
LINKER_DIRS = [os.path.join(ROOT, 'Core build (M4)', 'openfpga-litex', 'lang', 'linker'),
               os.path.join(ROOT, 'Source', 'core', 'openfpga-litex', 'lang', 'linker')]
BOOT_DEST = os.path.join(ROOT, 'Pocket SD', 'Assets', 'lanterndeep', 'common', 'boot.bin')
PAGE_DEST = os.path.join(ROOT, 'Lantern Deep - PC test.html')
LLVM_DIRS = [r'C:\Program Files\LLVM\bin', r'C:\Program Files (x86)\LLVM\bin', os.path.expandvars(r'%LOCALAPPDATA%\Programs\LLVM\bin')]


LOG = os.path.join(ROOT, 'build_game_log.txt')


class Fail(Exception):
    pass


def say(*a):
    line = ' '.join(str(x) for x in a)
    print(line, flush=True)
    try:
        with open(LOG, 'a', encoding='utf-8') as fh:
            fh.write(line + '\n')
    except OSError:
        pass


def tool(name):
    exe = name + ('.exe' if os.name == 'nt' else '')
    found = shutil.which(exe) or shutil.which(name)
    if found:
        return found
    for d in LLVM_DIRS:
        p = os.path.join(d, exe)
        if os.path.exists(p):
            return p
    return None


def run(cmd, what):
    say('  ' + what)
    r = subprocess.run(cmd, cwd=GAME, capture_output=True, text=True)
    out = (r.stdout or '') + (r.stderr or '')
    if r.returncode != 0:
        raise Fail('%s failed:\n%s' % (what, out[-4000:]))
    return out


def check():
    say('Checking tools...')
    problems = []
    try:
        import PIL      # noqa: F401
        import numpy    # noqa: F401
        say('  Python %s with Pillow and NumPy: OK' % sys.version.split()[0])
    except ImportError as e:
        problems.append('Python is missing %s. Fix: open a Command Prompt and run:  py -m pip install pillow numpy' % e.name)
    tools = {n: tool(n) for n in ('clang', 'ld.lld', 'llvm-objcopy')}
    for n, p in tools.items():
        if not p:
            problems.append('%s not found. Install LLVM for Windows (LLVM-<version>-win64.exe from github.com/llvm/llvm-project/releases).' % n)
    if tools['clang']:
        r = subprocess.run([tools['clang'], '--version'], capture_output=True, text=True)
        say('  ' + (r.stdout.splitlines() or ['clang'])[0] + ': ' + tools['clang'])
        t = subprocess.run([tools['clang'], '-print-targets'], capture_output=True, text=True).stdout
        for want in ('riscv32', 'wasm32'):
            if want not in t:
                problems.append('This clang cannot build for %s. Install LLVM 20 or newer from github.com/llvm/llvm-project/releases.' % want)
    linker = next((d for d in LINKER_DIRS if os.path.exists(os.path.join(d, 'c-linker.ld'))), None)
    if not linker:
        problems.append('The Pocket linker files were not found (looked in: %s).' % '; '.join(LINKER_DIRS))
    if problems:
        raise Fail('\n'.join('  - ' + p for p in problems))
    say('  Everything needed is installed.')
    return tools, linker


def build(tools, linker, install=True):
    os.makedirs(BUILD, exist_ok=True)
    t0 = time.time()
    say('Building Lantern Deep...')
    out = run([sys.executable, os.path.join('tools', 'gen_art.py'), os.path.join('src', 'art_data.h')], 'Art: art\\*.png -> src\\art_data.h')
    n = sum(1 for l in out.splitlines() if l.startswith('imported'))
    say('    %d sprites imported' % n)
    cf = ['--target=riscv32-unknown-elf', '-march=rv32im', '-mabi=ilp32', '-O2', '-g', '-ffreestanding', '-fno-builtin', '-nostdlib', '-fno-pic',
          '-mcmodel=medany', '-ffunction-sections', '-fdata-sections', '-Wall', '-Wno-unused-function', '-Wno-unused-variable']
    run([tools['clang']] + cf + ['-c', os.path.join('plat', 'pocket_main.c'), '-o', os.path.join(BUILD, 'pocket_main.o')], 'Pocket: compiling the game')
    run([tools['clang'], '--target=riscv32-unknown-elf', '-march=rv32im', '-mabi=ilp32', '-c', os.path.join(linker, 'init_asm.S'),
         '-o', os.path.join(BUILD, 'init_asm.o')], 'Pocket: start-up code')
    elf = os.path.join(BUILD, 'lanterndeep.elf')
    run([tools['ld.lld'], '-m', 'elf32lriscv', '-L', linker, '-T', os.path.join(linker, 'c-linker.ld'), '--gc-sections', '-N', '-o', elf,
         os.path.join(BUILD, 'init_asm.o'), os.path.join(BUILD, 'pocket_main.o')], 'Pocket: linking')
    boot = os.path.join(BUILD, 'boot.bin')
    run([tools['llvm-objcopy'], '-O', 'binary', elf, boot], 'Pocket: boot.bin')
    wasm = os.path.join(BUILD, 'lanterndeep.wasm')
    run([tools['clang'], '--target=wasm32', '-O2', '-ffreestanding', '-fno-builtin', '-nostdlib', '-Wno-unused-function', '-Wno-unused-variable',
         '-Wl,--no-entry', '-Wl,--export-dynamic', '-Wl,-z,stack-size=262144', '-o', wasm, os.path.join('plat', 'wasm_main.c')], 'PC test: compiling')
    with open(os.path.join(GAME, 'web', 'template.html'), encoding='utf-8') as fh:
        page = fh.read().replace('__WASM__', base64.b64encode(open(wasm, 'rb').read()).decode())
    index = os.path.join(BUILD, 'index.html')
    with open(index, 'w', encoding='utf-8') as fh:
        fh.write(page)
    say('  PC test: page')
    say('Built in %.0f s: boot.bin %d KB, PC test page %d KB.' % (time.time() - t0, os.path.getsize(boot) // 1024, os.path.getsize(index) // 1024))
    if install:
        prev = os.path.join(BUILD, 'previous')
        os.makedirs(prev, exist_ok=True)
        for src, dst in ((boot, BOOT_DEST), (index, PAGE_DEST)):
            if os.path.exists(dst):
                shutil.copy2(dst, os.path.join(prev, os.path.basename(dst)))
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            shutil.copy2(src, dst)
        say('Installed:')
        say('  ' + BOOT_DEST + '   (the Pocket Updater copies it to the card)')
        say('  ' + PAGE_DEST + '   (double-click to play on the PC)')
        say('The previous versions are in ' + prev)


def main():
    args = set(sys.argv[1:])
    try:
        open(LOG, 'w').close()
    except OSError:
        pass
    say('Lantern Deep build  ' + time.strftime('%Y-%m-%d %H:%M:%S'))
    try:
        tools, linker = check()
        if '--check' not in args:
            build(tools, linker, install='--no-install' not in args)
        say('DONE')
        return 0
    except Fail as e:
        say('\nPROBLEM:\n' + str(e))
        return 1


if __name__ == '__main__':
    sys.exit(main())
