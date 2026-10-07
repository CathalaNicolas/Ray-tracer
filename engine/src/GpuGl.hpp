#pragma once

#if defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <GL/gl.h>

#ifndef GLchar
typedef char GLchar;
#endif
#ifndef GL_VERTEX_SHADER
#define GL_VERTEX_SHADER 0x8B31
#endif
#ifndef GL_FRAGMENT_SHADER
#define GL_FRAGMENT_SHADER 0x8B30
#endif
#ifndef GL_COMPILE_STATUS
#define GL_COMPILE_STATUS 0x8B81
#endif
#ifndef GL_LINK_STATUS
#define GL_LINK_STATUS 0x8B82
#endif
#ifndef GL_INFO_LOG_LENGTH
#define GL_INFO_LOG_LENGTH 0x8B84
#endif
#ifndef GL_FRAMEBUFFER_COMPLETE
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#endif

#ifndef GL_RGBA32F
#define GL_RGBA32F 0x8814
#endif
#ifndef GL_RGBA16F
#define GL_RGBA16F 0x881A
#endif
#ifndef GL_HALF_FLOAT
#define GL_HALF_FLOAT 0x140B
#endif
#ifndef GL_TEXTURE_2D_ARRAY
#define GL_TEXTURE_2D_ARRAY 0x8C1A
#endif
#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif
#ifndef GL_TEXTURE_MAX_LEVEL
#define GL_TEXTURE_MAX_LEVEL 0x813D
#endif
#ifndef GL_RGBA8
#define GL_RGBA8 0x8058
#endif
#ifndef GL_TEXTURE0
#define GL_TEXTURE0 0x84C0
#endif
#ifndef GL_R32F
#define GL_R32F 0x822E
#endif
#ifndef GL_ARRAY_BUFFER
#define GL_ARRAY_BUFFER 0x8892
#endif
#ifndef GL_ELEMENT_ARRAY_BUFFER
#define GL_ELEMENT_ARRAY_BUFFER 0x8893
#endif
#ifndef GL_DYNAMIC_DRAW
#define GL_DYNAMIC_DRAW 0x88E8
#endif
#ifndef GL_UNSIGNED_INT
#define GL_UNSIGNED_INT 0x1405
#endif
#ifndef GL_DEPTH_COMPONENT24
#define GL_DEPTH_COMPONENT24 0x81A6
#endif
#ifndef GL_FRAMEBUFFER
#define GL_FRAMEBUFFER 0x8D40
#endif
#ifndef GL_RENDERBUFFER
#define GL_RENDERBUFFER 0x8D41
#endif
#ifndef GL_DEPTH_ATTACHMENT
#define GL_DEPTH_ATTACHMENT 0x8D00
#endif
#ifndef GL_COLOR_ATTACHMENT0
#define GL_COLOR_ATTACHMENT0 0x8CE0
#endif
#ifndef GL_COLOR_ATTACHMENT1
#define GL_COLOR_ATTACHMENT1 0x8CE1
#endif
#ifndef GL_COLOR_ATTACHMENT2
#define GL_COLOR_ATTACHMENT2 0x8CE2
#endif
#ifndef GL_COMPRESSED_RGBA_S3TC_DXT1_EXT
#define GL_COMPRESSED_RGBA_S3TC_DXT1_EXT 0x83F1
#endif
#ifndef GL_COMPRESSED_RGBA_S3TC_DXT3_EXT
#define GL_COMPRESSED_RGBA_S3TC_DXT3_EXT 0x83F2
#endif
#ifndef GL_COMPRESSED_RGBA_S3TC_DXT5_EXT
#define GL_COMPRESSED_RGBA_S3TC_DXT5_EXT 0x83F3
#endif
#ifndef GL_COMPRESSED_RG_RGTC2
#define GL_COMPRESSED_RG_RGTC2 0x8DBD
#endif
#ifndef GL_COMPRESSED_RGBA_BPTC_UNORM_ARB
#define GL_COMPRESSED_RGBA_BPTC_UNORM_ARB 0x8E8C
#endif
#ifndef GL_LINEAR_MIPMAP_LINEAR
#define GL_LINEAR_MIPMAP_LINEAR 0x2703
#endif

#include <string>

using GlCreateShader = GLuint(APIENTRY *)(GLenum);
using GlShaderSource = void(APIENTRY *)(GLuint, GLsizei, const GLchar *const *, const GLint *);
using GlCompileShader = void(APIENTRY *)(GLuint);
using GlGetShaderiv = void(APIENTRY *)(GLuint, GLenum, GLint *);
using GlGetShaderInfoLog = void(APIENTRY *)(GLuint, GLsizei, GLsizei *, GLchar *);
using GlDeleteShader = void(APIENTRY *)(GLuint);
using GlCreateProgram = GLuint(APIENTRY *)();
using GlAttachShader = void(APIENTRY *)(GLuint, GLuint);
using GlLinkProgram = void(APIENTRY *)(GLuint);
using GlGetProgramiv = void(APIENTRY *)(GLuint, GLenum, GLint *);
using GlGetProgramInfoLog = void(APIENTRY *)(GLuint, GLsizei, GLsizei *, GLchar *);
using GlUseProgram = void(APIENTRY *)(GLuint);
using GlDeleteProgram = void(APIENTRY *)(GLuint);
using GlGetUniformLocation = GLint(APIENTRY *)(GLuint, const GLchar *);
using GlUniform1i = void(APIENTRY *)(GLint, GLint);
using GlUniform1f = void(APIENTRY *)(GLint, GLfloat);
using GlActiveTexture = void(APIENTRY *)(GLenum);
using GlTexImage3D = void(APIENTRY *)(GLenum, GLint, GLint, GLsizei, GLsizei, GLsizei, GLint, GLenum, GLenum, const void *);
using GlCompressedTexImage2D = void(APIENTRY *)(GLenum, GLint, GLenum, GLsizei, GLsizei, GLint, GLsizei, const void *);
using GlCompressedTexImage3D = void(APIENTRY *)(GLenum, GLint, GLenum, GLsizei, GLsizei, GLsizei, GLint, GLsizei, const void *);
using GlUniform3f = void(APIENTRY *)(GLint, GLfloat, GLfloat, GLfloat);
using GlUniform4fv = void(APIENTRY *)(GLint, GLsizei, const GLfloat *);
using GlUniform1iv = void(APIENTRY *)(GLint, GLsizei, const GLint *);
using GlGenVertexArrays = void(APIENTRY *)(GLsizei, GLuint *);
using GlBindVertexArray = void(APIENTRY *)(GLuint);
using GlDeleteVertexArrays = void(APIENTRY *)(GLsizei, const GLuint *);
using GlGenFramebuffers = void(APIENTRY *)(GLsizei, GLuint *);
using GlBindFramebuffer = void(APIENTRY *)(GLenum, GLuint);
using GlDeleteFramebuffers = void(APIENTRY *)(GLsizei, const GLuint *);
using GlFramebufferTexture2D = void(APIENTRY *)(GLenum, GLenum, GLenum, GLuint, GLint);
using GlCheckFramebufferStatus = GLenum(APIENTRY *)(GLenum);

extern GlCreateShader glCreateShaderFn;
extern GlShaderSource glShaderSourceFn;
extern GlCompileShader glCompileShaderFn;
extern GlGetShaderiv glGetShaderivFn;
extern GlGetShaderInfoLog glGetShaderInfoLogFn;
extern GlDeleteShader glDeleteShaderFn;
extern GlCreateProgram glCreateProgramFn;
extern GlAttachShader glAttachShaderFn;
extern GlLinkProgram glLinkProgramFn;
extern GlGetProgramiv glGetProgramivFn;
extern GlGetProgramInfoLog glGetProgramInfoLogFn;
extern GlUseProgram glUseProgramFn;
extern GlDeleteProgram glDeleteProgramFn;
extern GlGetUniformLocation glGetUniformLocationFn;
extern GlUniform1i glUniform1iFn;
extern GlUniform1f glUniform1fFn;
extern GlActiveTexture glActiveTextureFn;
extern GlTexImage3D glTexImage3DFn;
extern GlCompressedTexImage2D glCompressedTexImage2DFn;
extern GlCompressedTexImage3D glCompressedTexImage3DFn;
extern GlUniform3f glUniform3fFn;
extern GlUniform4fv glUniform4fvFn;
extern GlUniform1iv glUniform1ivFn;
extern GlGenVertexArrays glGenVertexArraysFn;
extern GlBindVertexArray glBindVertexArrayFn;
extern GlDeleteVertexArrays glDeleteVertexArraysFn;
extern GlGenFramebuffers glGenFramebuffersFn;
extern GlBindFramebuffer glBindFramebufferFn;
extern GlDeleteFramebuffers glDeleteFramebuffersFn;
extern GlFramebufferTexture2D glFramebufferTexture2DFn;
extern GlCheckFramebufferStatus glCheckFramebufferStatusFn;
using GlGenBuffers = void(APIENTRY *)(GLsizei, GLuint *);
using GlBindBuffer = void(APIENTRY *)(GLenum, GLuint);
using GlBufferData = void(APIENTRY *)(GLenum, ptrdiff_t, const void *, GLenum);
using GlDeleteBuffers = void(APIENTRY *)(GLsizei, const GLuint *);
using GlEnableVertexAttribArray = void(APIENTRY *)(GLuint);
using GlVertexAttribPointer = void(APIENTRY *)(GLuint, GLint, GLenum, GLboolean, GLsizei, const void *);
using GlVertexAttribDivisor = void(APIENTRY *)(GLuint, GLuint);
using GlDrawArraysInstanced = void(APIENTRY *)(GLenum, GLint, GLsizei, GLsizei);
using GlDrawElementsInstanced = void(APIENTRY *)(GLenum, GLsizei, GLenum, const void *, GLsizei);
using GlUniformMatrix4fv = void(APIENTRY *)(GLint, GLsizei, GLboolean, const GLfloat *);
using GlDrawBuffers = void(APIENTRY *)(GLsizei, const GLenum *);
using GlFramebufferTextureLayer = void(APIENTRY *)(GLenum, GLenum, GLuint, GLint, GLint);
using GlGenRenderbuffers = void(APIENTRY *)(GLsizei, GLuint *);
using GlBindRenderbuffer = void(APIENTRY *)(GLenum, GLuint);
using GlRenderbufferStorage = void(APIENTRY *)(GLenum, GLenum, GLsizei, GLsizei);
using GlFramebufferRenderbuffer = void(APIENTRY *)(GLenum, GLenum, GLenum, GLuint);
using GlDeleteRenderbuffers = void(APIENTRY *)(GLsizei, const GLuint *);
using GlClearBufferfv = void(APIENTRY *)(GLenum, GLint, const GLfloat *);

extern GlGenBuffers glGenBuffersFn;
extern GlBindBuffer glBindBufferFn;
extern GlBufferData glBufferDataFn;
extern GlDeleteBuffers glDeleteBuffersFn;
extern GlEnableVertexAttribArray glEnableVertexAttribArrayFn;
extern GlVertexAttribPointer glVertexAttribPointerFn;
extern GlVertexAttribDivisor glVertexAttribDivisorFn;
extern GlDrawArraysInstanced glDrawArraysInstancedFn;
extern GlDrawElementsInstanced glDrawElementsInstancedFn;
extern GlUniformMatrix4fv glUniformMatrix4fvFn;
extern GlDrawBuffers glDrawBuffersFn;
extern GlFramebufferTextureLayer glFramebufferTextureLayerFn;
extern GlGenRenderbuffers glGenRenderbuffersFn;
extern GlBindRenderbuffer glBindRenderbufferFn;
extern GlRenderbufferStorage glRenderbufferStorageFn;
extern GlFramebufferRenderbuffer glFramebufferRenderbufferFn;
extern GlDeleteRenderbuffers glDeleteRenderbuffersFn;
extern GlClearBufferfv glClearBufferfvFn;

template <typename T>
T loadGl(const char *name)
{
    return reinterpret_cast<T>(wglGetProcAddress(name));
}

void configureTexture(GLenum target, GLint filter);
void configureTextureMips(GLenum target, GLint filter, int maxLevel);
GLuint compileShader(GLenum type, const char *source, std::string &error);

#endif
