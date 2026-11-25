#!/usr/bin/env python3
"""Assemble an Emerald PC release (no game content: docs/PC_PORT_STATUS.md, J9).

    python pc_port/tools/pc_build_release.py --version 0.1.0 --tree build/upstream-pc-fr \\
        --rom "Pokemon - Version Emeraude (France).gba" --language fr

From a built language tree (pc.mk's `pc`), its ROM and that language's
reference symbols (build/<language>-reference.s, written by its localization
script; the English game needs the original game's ELF, --gba-elf):

1. pc_release.py: the executable with the game data taken out, the data's
   staging, the engine romfs;
2. tools/gen_recipe.py over the staging (pc_nm.py reads the PE and the
   reference symbols), then tools/voxel_precompile.py: the recipe that
   rebuilds every data file from the player's ROM;
3. the builder (pc_builder.py over Emerald 3DS's builder; PyInstaller when
   --pyinstaller, else left to the caller), its payload/ (recipe, voxel
   generators);
4. dist/EmeraldPC-v<version>-<os>-x86/: emerald-pc.exe, its DLLs, recomplauncher.json,
   engine/, the builder, README.txt, LICENSES/; zipped;
5. tools/release_audit.py over the ZIP with the ROM (no run of its bytes
   anywhere), SHA256SUMS.txt.

The ROM is only read, to write the recipe and to audit; never copied.
"""

from __future__ import annotations

import argparse
import hashlib
import os
import shutil
import subprocess
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PC = ROOT / "pc_port"
TOOLS = PC / "tools"
DIST = ROOT / "dist"
STEMS = {"en": None, "fr": "french", "es": "spanish", "it": "italian", "de": "german", "ja": "japanese"}


def run(cmd, **kw) -> None:
    print("+ " + " ".join(str(c) for c in cmd), flush=True)
    subprocess.run([str(c) for c in cmd], check=True, **kw)


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            h.update(block)
    return h.hexdigest()


def merged_allowlist(out: Path) -> Path:
    """tools/release_audit_allow.toml with pc_port/release_audit_allow.toml's
    entries added (the shared list stays as it is)."""
    import tomllib
    base = tomllib.loads((ROOT / "tools/release_audit_allow.toml").read_text(encoding="utf-8"))
    pc = tomllib.loads((PC / "release_audit_allow.toml").read_text(encoding="utf-8"))

    def strings(values):
        return "[%s]" % ", ".join('"%s"' % v.replace("\\", "\\\\").replace('"', '\\"') for v in values)

    lines = []
    for table, key in (("large", "paths"), ("local_paths", "paths"), ("skip", "globs"), ("runtime", "globs")):
        values = list(base.get(table, {}).get(key, [])) + list(pc.get(table, {}).get(key, []))
        lines += ["[%s]" % table, "%s = %s" % (key, strings(values)), ""]
    lines.append("[media]")
    for name, why in {**base.get("media", {}), **pc.get("media", {})}.items():
        lines.append('"%s" = "%s"' % (name, why.replace('"', "'")))
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text("\n".join(lines) + "\n", encoding="utf-8")
    return out


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--version", required=True)
    ap.add_argument("--tree", type=Path, required=True, help="a built language tree (build/upstream-pc-fr)")
    ap.add_argument("--rom", type=Path, required=True)
    ap.add_argument("--language", default="fr", choices=sorted(STEMS))
    ap.add_argument("--gba-elf", type=Path, default=None, help="English: the original game's ELF")
    ap.add_argument("--nm", default=os.environ.get("PC_NM", "nm"), help="the PE toolchain's nm")
    ap.add_argument("--os", default="Windows" if sys.platform == "win32" else "Linux")
    ap.add_argument("--skip-recipe", action="store_true", help="reuse dist's recipe")
    ap.add_argument("--pyinstaller", action="store_true", help="freeze the builder (else run from Python)")
    args = ap.parse_args()

    tag = "v" + args.version
    port = args.tree / "3ds_port"
    work = ROOT / "build" / ("pc-release-" + args.language)
    DIST.mkdir(exist_ok=True)
    env = dict(os.environ, PC_PYTHON=sys.executable, PC_NM=args.nm)
    nm_wrapper = TOOLS / ("pc_nm.cmd" if sys.platform == "win32" else "pc_nm.py")

    # 1. The executable without the game data, its staging.
    run([sys.executable, TOOLS / "pc_release.py", "--exe", port / "emerald-pc.exe",
         "--romfs", port / "build/pc-romfs", "--out", work, "--nm", args.nm])

    # 2. The recipe.
    recipe = DIST / ("emerald-pc-%s.recipe" % args.language)
    if not args.skip_recipe:
        stem = STEMS[args.language]
        reference = args.gba_elf if stem is None else args.tree / "build" / (stem + "-reference.s")
        if reference is None or not Path(reference).exists():
            raise SystemExit("pc_build_release: no reference symbols for %s (%s)" % (args.language, reference))
        run([sys.executable, ROOT / "tools/gen_recipe.py", "--romfs", work / "staging", "--rom", args.rom,
             "--out", recipe, "--release", tag, "--elf", port / "emerald-pc.exe", "--gba-elf", reference,
             "--image-elf", port / "emerald-pc.exe", "--image-map", port / "build/emerald-pc.rodata.map",
             "--nm", nm_wrapper, "--decomp", args.tree,
             "--report", DIST / ("recipe-literal-report-%s.txt" % args.language)], env=env)
        # The voxel programs. voxel_precompile.py holds the console's
        # releases to the interpreter upstream.lock pins, because the voxel
        # files depend on it; a PC release's own files were generated by this
        # build's interpreter (the same one running this), so that is the one
        # to check against: the pin is not applied here.
        run([sys.executable, "-c",
             "import sys; sys.path.insert(0, sys.argv[1]); import voxel_precompile as v; "
             "v.check_python = lambda: None; sys.argv = ['voxel_precompile.py'] + sys.argv[2:]; v.main()",
             ROOT / "tools", "--recipe", recipe, "--rom", args.rom, "--port", port,
             "--outputs", work / "staging"], env=env)

    # 3-4. The release folder.
    # RecompLauncher picks a release's file by the system named in it
    # (recomplauncher.com's guide): one file per system. The executable is
    # 32-bit x86.
    name = "EmeraldPC-%s-%s-x86" % (tag, args.os)
    release = DIST / name
    if release.exists():
        shutil.rmtree(release)
    release.mkdir(parents=True)
    shutil.copy2(work / "emerald-pc.exe", release / ("emerald-pc.exe" if args.os == "Windows" else "emerald-pc"))
    for dll in sorted(port.glob("*.dll")):
        shutil.copy2(dll, release / dll.name)
    shutil.copytree(work / "engine", release / "engine")
    payload = release / "payload"
    payload.mkdir()
    shutil.copy2(recipe, payload / "emerald3ds.recipe")
    builder_src = release / "builder"
    if args.pyinstaller:
        run([sys.executable, "-m", "PyInstaller", "--noconfirm", "--clean", "--onedir", "--console",
             "--name", "emerald-pc-builder", "--specpath", ROOT / "build/pyi-work", "--distpath", ROOT / "build/pyi-dist", "--workpath",
             ROOT / "build/pyi-work", "--paths", ROOT / "builder", "--hidden-import", "PIL.Image",
             TOOLS / "pc_builder.py"])
        shutil.copytree(ROOT / "build/pyi-dist/emerald-pc-builder", release, dirs_exist_ok=True)
    else:
        # Run from Python: the builder package and the script beside the game.
        shutil.copytree(ROOT / "builder" / "emerald3ds_builder", builder_src / "emerald3ds_builder",
                        ignore=shutil.ignore_patterns("__pycache__"))
        shutil.copy2(TOOLS / "pc_builder.py", builder_src / "pc_builder.py")
    shutil.copy2(PC / "README-release.txt", release / "README.txt")
    # RecompLauncher reads it at the top of the ZIP, next to the program.
    shutil.copy2(ROOT / "recomplauncher.json", release / "recomplauncher.json")
    licenses = release / "LICENSES"
    licenses.mkdir()
    for rel in ("LICENSE-PORT.md", "NOTICE.md", "AI_DISCLOSURE.md"):
        if (ROOT / rel).exists():
            shutil.copy2(ROOT / rel, licenses / rel)
    shutil.copy2(ROOT / "3ds_port/src/voxel/NOTICE.md", licenses / "voxel-NOTICE.md")
    for notice in sorted((PC / "third_party").glob("*/LICENSE*")):
        shutil.copy2(notice, licenses / ("%s-%s" % (notice.parent.name, notice.name)))

    archive = DIST / (name + ".zip")
    if archive.exists():
        archive.unlink()
    with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as zf:
        for path in sorted(release.rglob("*")):
            if path.is_file():
                zf.write(path, path.relative_to(DIST).as_posix())

    # 5. The audit: nothing of the ROM anywhere in the archive.
    allow = merged_allowlist(ROOT / "build" / "pc-release-allow.toml")
    run([sys.executable, ROOT / "tools/release_audit.py", "--zip", archive, "--strict", "--rom", args.rom,
         "--allow", allow])
    sums = DIST / "SHA256SUMS.txt"
    sums.write_text("%s  %s\n" % (sha256(archive), archive.name), encoding="ascii")
    print("release asset: %s (%.1f MiB)" % (archive, archive.stat().st_size / 1048576))


if __name__ == "__main__":
    main()
