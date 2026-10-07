#include "GpuGl.hpp"

#if defined(_WIN32)

GlCreateShader glCreateShaderFn = nullptr;
GlShaderSource glShaderSourceFn = nullptr;
GlCompileShader glCompileShaderFn = nullptr;
GlGetShaderiv glGetShaderivFn = nullptr;
GlGetShaderInfoLog glGetShaderInfoLogFn = nullptr;
GlDeleteShader glDeleteShaderFn = nullptr;
GlCreateProgram glCreateProgramFn = nullptr;
GlAttachShader glAttachShaderFn = nullptr;
GlLinkProgram glLinkProgramFn = nullptr;
GlGetProgramiv glGetProgramivFn = nullptr;
GlGetProgramInfoLog glGetProgramInfoLogFn = nullptr;
GlUseProgram glUseProgramFn = nullptr;
GlDeleteProgram glDeleteProgramFn = nullptr;
GlGetUniformLocation glGetUniformLocationFn = nullptr;
GlUniform1i glUniform1iFn = nullptr;
GlUniform1f glUniform1fFn = nullptr;
GlActiveTexture glActiveTextureFn = nullptr;
GlTexImage3D glTexImage3DFn = nullptr;
GlCompressedTexImage2D glCompressedTexImage2DFn = nullptr;
GlCompressedTexImage3D glCompressedTexImage3DFn = nullptr;
GlUniform3f glUniform3fFn = nullptr;
GlUniform4fv glUniform4fvFn = nullptr;
GlUniform1iv glUniform1ivFn = nullptr;
GlGenVertexArrays glGenVertexArraysFn = nullptr;
GlBindVertexArray glBindVertexArrayFn = nullptr;
GlDeleteVertexArrays glDeleteVertexArraysFn = nullptr;
GlGenFramebuffers glGenFramebuffersFn = nullptr;
GlBindFramebuffer glBindFramebufferFn = nullptr;
GlDeleteFramebuffers glDeleteFramebuffersFn = nullptr;
GlFramebufferTexture2D glFramebufferTexture2DFn = nullptr;
GlCheckFramebufferStatus glCheckFramebufferStatusFn = nullptr;
GlGenBuffers glGenBuffersFn = nullptr;
GlBindBuffer glBindBufferFn = nullptr;
GlBufferData glBufferDataFn = nullptr;
GlDeleteBuffers glDeleteBuffersFn = nullptr;
GlEnableVertexAttribArray glEnableVertexAttribArrayFn = nullptr;
GlVertexAttribPointer glVertexAttribPointerFn = nullptr;
GlVertexAttribDivisor glVertexAttribDivisorFn = nullptr;
GlDrawArraysInstanced glDrawArraysInstancedFn = nullptr;
GlDrawElementsInstanced glDrawElementsInstancedFn = nullptr;
GlUniformMatrix4fv glUniformMatrix4fvFn = nullptr;
GlDrawBuffers glDrawBuffersFn = nullptr;
GlFramebufferTextureLayer glFramebufferTextureLayerFn = nullptr;
GlGenRenderbuffers glGenRenderbuffersFn = nullptr;
GlBindRenderbuffer glBindRenderbufferFn = nullptr;
GlRenderbufferStorage glRenderbufferStorageFn = nullptr;
GlFramebufferRenderbuffer glFramebufferRenderbufferFn = nullptr;
GlDeleteRenderbuffers glDeleteRenderbuffersFn = nullptr;
GlClearBufferfv glClearBufferfvFn = nullptr;

GLuint compileShader(GLenum type, const char *source, std::string &error)
{
    GLuint shader = glCreateShaderFn(type);
    glShaderSourceFn(shader, 1, &source, nullptr);
    glCompileShaderFn(shader);
    GLint status = 0;
    glGetShaderivFn(shader, GL_COMPILE_STATUS, &status);
    if (status == GL_FALSE)
    {
        GLint length = 0;
        glGetShaderivFn(shader, GL_INFO_LOG_LENGTH, &length);
        std::string log(static_cast<size_t>(length > 1 ? length : 1), '\0');
        glGetShaderInfoLogFn(shader, length, nullptr, log.data());
        error = log;
        glDeleteShaderFn(shader);
        return 0;
    }
    return shader;
}

void configureTexture(GLenum target, GLint filter)
{
    configureTextureMips(target, filter, 0);
}

void configureTextureMips(GLenum target, GLint filter, int maxLevel)
{
    const GLint minFilter = maxLevel > 0 ? GL_LINEAR_MIPMAP_LINEAR : filter;
    glTexParameteri(target, GL_TEXTURE_MIN_FILTER, minFilter);
    glTexParameteri(target, GL_TEXTURE_MAG_FILTER, filter);
    glTexParameteri(target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(target, GL_TEXTURE_MAX_LEVEL, maxLevel);
}

#endif

