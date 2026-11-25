# Third-party code

Unmodified copies, kept here so the PC build can use the same types,
enums, inline helpers and maths as the console build. All three are under
the zlib license (see each folder's LICENSE); their notices are kept.

| Folder | Upstream | Commit | What is copied |
|---|---|---|---|
| `libctru/` | https://github.com/devkitPro/libctru | 0347d0d (2026-11-14) | `include/3ds/types.h`, `include/3ds/gpu/{enums,gpu,registers,shbin,shaderProgram,gx}.h`, `include/3ds/ndsp/{ndsp,channel}.h` |
| `citro3d/` | https://github.com/devkitPro/citro3d | 9f21cf7 (2023-10-28) | public headers (`include/`), `source/maths/*.c` |
| `citro2d/` | https://github.com/devkitPro/citro2d | 147b02a (2025-01-26) | public headers (`include/`), `source/base.c`, `source/internal.h` |

`ndsp.h` includes `<3ds/os.h>`, which is not copied: the port's `compat/3ds/os.h` stands in
for it (only `SYSCLOCK_SOC`), so the copy itself stays unmodified. NDSP's implementation is
the port's own, `pc_port/src/pc_ndsp.c`.

What runs on the PC instead of the console's GPU is not here: the citro3d
implementation over OpenGL is the port's own code, `pc_port/src/gl/`.
citro2d's `base.c` is compiled unmodified on top of it; its vertex shader
(`render2d.v.pica`) is rewritten in GLSL in `pc_port/src/gl/`.

Any change to a file in this folder must be marked in the file itself
(zlib license, clause 2).
