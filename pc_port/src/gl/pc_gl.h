/*
 * OpenGL 3.3 core functions used by the citro shim, loaded at run time
 * through SDL (pc_gl.c). Each is called as pglName(...), so nothing here
 * depends on which prototypes a platform's GL headers declare.
 */
#ifndef PC_GL_H
#define PC_GL_H

#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>

#ifndef APIENTRY
#define APIENTRY
#endif

#define PC_GL_FUNCS(X)                                                                                        \
    X(void, Viewport, (GLint x, GLint y, GLsizei w, GLsizei h))                                              \
    X(void, Scissor, (GLint x, GLint y, GLsizei w, GLsizei h))                                               \
    X(void, Enable, (GLenum cap))                                                                            \
    X(void, Disable, (GLenum cap))                                                                           \
    X(void, Clear, (GLbitfield mask))                                                                        \
    X(void, ClearColor, (GLfloat r, GLfloat g, GLfloat b, GLfloat a))                                        \
    X(void, ClearDepth, (GLdouble d))                                                                        \
    X(void, ClearStencil, (GLint s))                                                                         \
    X(void, ColorMask, (GLboolean r, GLboolean g, GLboolean b, GLboolean a))                                 \
    X(void, DepthMask, (GLboolean flag))                                                                     \
    X(void, DepthFunc, (GLenum func))                                                                        \
    X(void, CullFace, (GLenum mode))                                                                         \
    X(void, FrontFace, (GLenum mode))                                                                        \
    X(void, LogicOp, (GLenum op))                                                                            \
    X(void, StencilFunc, (GLenum func, GLint ref, GLuint mask))                                              \
    X(void, StencilOp, (GLenum sfail, GLenum dpfail, GLenum dppass))                                         \
    X(void, StencilMask, (GLuint mask))                                                                      \
    X(void, BlendEquationSeparate, (GLenum rgb, GLenum alpha))                                               \
    X(void, BlendFuncSeparate, (GLenum sr, GLenum dr, GLenum sa, GLenum da))                                 \
    X(void, BlendColor, (GLfloat r, GLfloat g, GLfloat b, GLfloat a))                                        \
    X(void, PixelStorei, (GLenum pname, GLint param))                                                        \
    X(void, ReadPixels, (GLint x, GLint y, GLsizei w, GLsizei h, GLenum f, GLenum t, void *p))               \
    X(GLenum, GetError, (void))                                                                              \
    X(void, Finish, (void))                                                                                  \
    X(const GLubyte *, GetString, (GLenum name))                                                             \
    X(void, GenSamplers, (GLsizei n, GLuint *s))                                                             \
    X(void, SamplerParameteri, (GLuint s, GLenum p, GLint v))                                                \
    X(void, BindSampler, (GLuint unit, GLuint s))                                                            \
    X(void, GenTextures, (GLsizei n, GLuint *t))                                                           \
    X(void, DeleteTextures, (GLsizei n, const GLuint *t))                                                    \
    X(void, BindTexture, (GLenum target, GLuint t))                                                          \
    X(void, ActiveTexture, (GLenum unit))                                                                    \
    X(void, TexImage2D, (GLenum t, GLint l, GLint i, GLsizei w, GLsizei h, GLint b, GLenum f, GLenum ty,     \
                         const void *p))                                                                     \
    X(void, TexSubImage2D, (GLenum t, GLint l, GLint x, GLint y, GLsizei w, GLsizei h, GLenum f, GLenum ty,  \
                            const void *p))                                                                  \
    X(void, TexParameteri, (GLenum t, GLenum p, GLint v))                                                    \
    X(void, TexParameterfv, (GLenum t, GLenum p, const GLfloat *v))                                          \
    X(void, GenFramebuffers, (GLsizei n, GLuint *f))                                                         \
    X(void, DeleteFramebuffers, (GLsizei n, const GLuint *f))                                                \
    X(void, BindFramebuffer, (GLenum t, GLuint f))                                                           \
    X(void, FramebufferTexture2D, (GLenum t, GLenum a, GLenum tt, GLuint tex, GLint l))                      \
    X(void, FramebufferRenderbuffer, (GLenum t, GLenum a, GLenum rt, GLuint rb))                             \
    X(GLenum, CheckFramebufferStatus, (GLenum t))                                                            \
    X(void, BlitFramebuffer, (GLint sx0, GLint sy0, GLint sx1, GLint sy1, GLint dx0, GLint dy0, GLint dx1,   \
                              GLint dy1, GLbitfield m, GLenum f))                                            \
    X(void, GenRenderbuffers, (GLsizei n, GLuint *r))                                                        \
    X(void, DeleteRenderbuffers, (GLsizei n, const GLuint *r))                                               \
    X(void, BindRenderbuffer, (GLenum t, GLuint r))                                                          \
    X(void, RenderbufferStorage, (GLenum t, GLenum f, GLsizei w, GLsizei h))                                 \
    X(void, RenderbufferStorageMultisample, (GLenum t, GLsizei n, GLenum f, GLsizei w, GLsizei h))                 \
    X(void, GetIntegerv, (GLenum p, GLint *v))                                                                     \
    X(GLuint, CreateShader, (GLenum type))                                                                   \
    X(void, ShaderSource, (GLuint s, GLsizei n, const GLchar *const *src, const GLint *len))                 \
    X(void, CompileShader, (GLuint s))                                                                       \
    X(void, GetShaderiv, (GLuint s, GLenum p, GLint *v))                                                     \
    X(void, GetShaderInfoLog, (GLuint s, GLsizei max, GLsizei *len, GLchar *log))                            \
    X(void, DeleteShader, (GLuint s))                                                                        \
    X(GLuint, CreateProgram, (void))                                                                         \
    X(void, AttachShader, (GLuint p, GLuint s))                                                              \
    X(void, LinkProgram, (GLuint p))                                                                         \
    X(void, GetProgramiv, (GLuint p, GLenum n, GLint *v))                                                    \
    X(void, GetProgramInfoLog, (GLuint p, GLsizei max, GLsizei *len, GLchar *log))                           \
    X(void, UseProgram, (GLuint p))                                                                          \
    X(void, BindAttribLocation, (GLuint p, GLuint i, const GLchar *n))                                       \
    X(void, BindFragDataLocation, (GLuint p, GLuint c, const GLchar *n))                                     \
    X(GLint, GetUniformLocation, (GLuint p, const GLchar *n))                                                \
    X(void, Uniform1i, (GLint l, GLint v))                                                                   \
    X(void, Uniform1iv, (GLint l, GLsizei n, const GLint *v))                                                \
    X(void, Uniform1f, (GLint l, GLfloat v))                                                                 \
    X(void, Uniform2f, (GLint l, GLfloat a, GLfloat b))                                                      \
    X(void, Uniform3f, (GLint l, GLfloat a, GLfloat b, GLfloat c))                                                 \
    X(void, Uniform4fv, (GLint l, GLsizei n, const GLfloat *v))                                              \
    X(void, Uniform4iv, (GLint l, GLsizei n, const GLint *v))                                                \
    X(void, GenVertexArrays, (GLsizei n, GLuint *a))                                                         \
    X(void, BindVertexArray, (GLuint a))                                                                     \
    X(void, GenBuffers, (GLsizei n, GLuint *b))                                                              \
    X(void, BindBuffer, (GLenum t, GLuint b))                                                                \
    X(void, BufferData, (GLenum t, GLsizeiptr s, const void *d, GLenum u))                                   \
    X(void, VertexAttribPointer, (GLuint i, GLint s, GLenum t, GLboolean n, GLsizei st, const void *p))      \
    X(void, EnableVertexAttribArray, (GLuint i))                                                             \
    X(void, DisableVertexAttribArray, (GLuint i))                                                            \
    X(void, VertexAttrib4f, (GLuint i, GLfloat x, GLfloat y, GLfloat z, GLfloat w))                          \
    X(void, DrawArrays, (GLenum m, GLint first, GLsizei count))                                              \
    X(void, DrawElements, (GLenum m, GLsizei count, GLenum type, const void *indices))

#define PC_GL_DECLARE(ret, name, params)          \
    typedef ret(APIENTRY *PFNPGL_##name) params; \
    extern PFNPGL_##name pgl##name;
PC_GL_FUNCS(PC_GL_DECLARE)
#undef PC_GL_DECLARE

/* Resolves every function above in the current context; false if one is
 * missing (the error names it). */
bool PcGl_Load(void);
/* Logs and clears pending GL errors, tagged with where they were seen. */
void PcGl_CheckError(const char *where);

#endif
