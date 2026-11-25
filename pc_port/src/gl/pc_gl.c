/*
 * OpenGL function loader (see pc_gl.h).
 */
#include "pc_gl.h"

#include <stdio.h>

#define PC_GL_DEFINE(ret, name, params) PFNPGL_##name pgl##name;
PC_GL_FUNCS(PC_GL_DEFINE)
#undef PC_GL_DEFINE

bool PcGl_Load(void)
{
    bool ok = true;
#define PC_GL_LOAD(ret, name, params)                                          \
    pgl##name = (PFNPGL_##name)(void *)SDL_GL_GetProcAddress("gl" #name);       \
    if (pgl##name == NULL)                                                     \
    {                                                                          \
        fprintf(stderr, "[VIDEO] OpenGL function gl%s is missing\n", #name);   \
        ok = false;                                                            \
    }
    PC_GL_FUNCS(PC_GL_LOAD)
#undef PC_GL_LOAD
    return ok;
}

void PcGl_CheckError(const char *where)
{
    GLenum error;
    int count = 0;
    while ((error = pglGetError()) != GL_NO_ERROR && count++ < 8)
        fprintf(stderr, "[VIDEO] GL error 0x%04X at %s\n", (unsigned)error, where);
}
