#include "GpuRayTracer.hpp"

#include "ImageIO.hpp"
#include "Mesh.hpp"
#include "Plane.hpp"
#include "Sphere.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <unordered_map>

#if !defined(_WIN32)

bool GpuRayTracer::init()
{
    failure_ = "GPU rendering is only built for Windows";
    return false;
}

void GpuRayTracer::shutdown() {}

int GpuRayTracer::render(const Scene &, const Camera &, unsigned, int, int, int, int, int, bool, int, double, bool)
{
    return -1;
}

bool GpuRayTracer::readPixels(unsigned, int, int, std::vector<std::uint8_t> &) const
{
    return false;
}

bool GpuRayTracer::readPixelsLinear(unsigned, int, int, std::vector<float> &) const
{
    return false;
}

#else

#include "GpuGl.hpp"
#include "GpuShaders.hpp"

#include <cstddef>


bool GpuRayTracer::init()
{
    glCreateShaderFn = loadGl<GlCreateShader>("glCreateShader");
    glShaderSourceFn = loadGl<GlShaderSource>("glShaderSource");
    glCompileShaderFn = loadGl<GlCompileShader>("glCompileShader");
    glGetShaderivFn = loadGl<GlGetShaderiv>("glGetShaderiv");
    glGetShaderInfoLogFn = loadGl<GlGetShaderInfoLog>("glGetShaderInfoLog");
    glDeleteShaderFn = loadGl<GlDeleteShader>("glDeleteShader");
    glCreateProgramFn = loadGl<GlCreateProgram>("glCreateProgram");
    glAttachShaderFn = loadGl<GlAttachShader>("glAttachShader");
    glLinkProgramFn = loadGl<GlLinkProgram>("glLinkProgram");
    glGetProgramivFn = loadGl<GlGetProgramiv>("glGetProgramiv");
    glGetProgramInfoLogFn = loadGl<GlGetProgramInfoLog>("glGetProgramInfoLog");
    glUseProgramFn = loadGl<GlUseProgram>("glUseProgram");
    glDeleteProgramFn = loadGl<GlDeleteProgram>("glDeleteProgram");
    glGetUniformLocationFn = loadGl<GlGetUniformLocation>("glGetUniformLocation");
    glUniform1iFn = loadGl<GlUniform1i>("glUniform1i");
    glUniform1fFn = loadGl<GlUniform1f>("glUniform1f");
    glActiveTextureFn = loadGl<GlActiveTexture>("glActiveTexture");
    glTexImage3DFn = loadGl<GlTexImage3D>("glTexImage3D");
    glUniform3fFn = loadGl<GlUniform3f>("glUniform3f");
    glUniform4fvFn = loadGl<GlUniform4fv>("glUniform4fv");
    glUniform1ivFn = loadGl<GlUniform1iv>("glUniform1iv");
    glGenVertexArraysFn = loadGl<GlGenVertexArrays>("glGenVertexArrays");
    glBindVertexArrayFn = loadGl<GlBindVertexArray>("glBindVertexArray");
    glDeleteVertexArraysFn = loadGl<GlDeleteVertexArrays>("glDeleteVertexArrays");
    glGenFramebuffersFn = loadGl<GlGenFramebuffers>("glGenFramebuffers");
    glBindFramebufferFn = loadGl<GlBindFramebuffer>("glBindFramebuffer");
    glDeleteFramebuffersFn = loadGl<GlDeleteFramebuffers>("glDeleteFramebuffers");
    glFramebufferTexture2DFn = loadGl<GlFramebufferTexture2D>("glFramebufferTexture2D");
    glCheckFramebufferStatusFn = loadGl<GlCheckFramebufferStatus>("glCheckFramebufferStatus");
    glGenBuffersFn = loadGl<GlGenBuffers>("glGenBuffers");
    glBindBufferFn = loadGl<GlBindBuffer>("glBindBuffer");
    glBufferDataFn = loadGl<GlBufferData>("glBufferData");
    glDeleteBuffersFn = loadGl<GlDeleteBuffers>("glDeleteBuffers");
    glEnableVertexAttribArrayFn = loadGl<GlEnableVertexAttribArray>("glEnableVertexAttribArray");
    glVertexAttribPointerFn = loadGl<GlVertexAttribPointer>("glVertexAttribPointer");
    glUniformMatrix4fvFn = loadGl<GlUniformMatrix4fv>("glUniformMatrix4fv");
    glDrawBuffersFn = loadGl<GlDrawBuffers>("glDrawBuffers");
    glFramebufferTextureLayerFn = loadGl<GlFramebufferTextureLayer>("glFramebufferTextureLayer");
    glGenRenderbuffersFn = loadGl<GlGenRenderbuffers>("glGenRenderbuffers");
    glBindRenderbufferFn = loadGl<GlBindRenderbuffer>("glBindRenderbuffer");
    glRenderbufferStorageFn = loadGl<GlRenderbufferStorage>("glRenderbufferStorage");
    glFramebufferRenderbufferFn = loadGl<GlFramebufferRenderbuffer>("glFramebufferRenderbuffer");
    glDeleteRenderbuffersFn = loadGl<GlDeleteRenderbuffers>("glDeleteRenderbuffers");
    glClearBufferfvFn = loadGl<GlClearBufferfv>("glClearBufferfv");

    if (glCreateShaderFn == nullptr || glGenFramebuffersFn == nullptr || glGenVertexArraysFn == nullptr || glTexImage3DFn == nullptr || glActiveTextureFn == nullptr || glGenBuffersFn == nullptr || glUniformMatrix4fvFn == nullptr || glDrawBuffersFn == nullptr || glFramebufferTextureLayerFn == nullptr || glClearBufferfvFn == nullptr)
    {
        failure_ = "This OpenGL driver does not expose shader framebuffers";
        return false;
    }

    std::string error;
    GLuint vertex = compileShader(GL_VERTEX_SHADER, kVertexShader, error);
    if (vertex == 0)
    {
        failure_ = "Vertex shader: " + error;
        return false;
    }
    auto linkProgram = [&](const std::string &fragmentSource, unsigned &program) {
        GLuint fragment = compileShader(GL_FRAGMENT_SHADER, fragmentSource.c_str(), error);
        if (fragment == 0)
            return false;
        program = glCreateProgramFn();
        glAttachShaderFn(program, vertex);
        glAttachShaderFn(program, fragment);
        glLinkProgramFn(program);
        glDeleteShaderFn(fragment);
        GLint linked = 0;
        glGetProgramivFn(program, GL_LINK_STATUS, &linked);
        if (linked == GL_FALSE)
        {
            GLint length = 0;
            glGetProgramivFn(program, GL_INFO_LOG_LENGTH, &length);
            std::string log(static_cast<size_t>(length > 1 ? length : 1), '\0');
            glGetProgramInfoLogFn(program, length, nullptr, log.data());
            error = log;
            glDeleteProgramFn(program);
            program = 0;
            return false;
        }
        return true;
    };

    if (!linkProgram(shaderWithLimits(kFragmentShader), program_))
    {
        glDeleteShaderFn(vertex);
        failure_ = "Fragment shader: " + error;
        return false;
    }
    glDeleteShaderFn(vertex);

    auto linkMeshProgram = [&](const char *fragmentSource, unsigned &program) {
        std::string meshError;
        const char *vertexSource = fragmentSource == kShadowFragmentShader ? kShadowVertexShader : kMeshVertexShader;
        GLuint meshVertex = compileShader(GL_VERTEX_SHADER, vertexSource, meshError);
        GLuint meshFragment = meshVertex != 0 ? compileShader(GL_FRAGMENT_SHADER, fragmentSource, meshError) : 0;
        if (meshVertex == 0 || meshFragment == 0)
        {
            if (meshVertex != 0)
                glDeleteShaderFn(meshVertex);
            error = meshError;
            return false;
        }
        program = glCreateProgramFn();
        glAttachShaderFn(program, meshVertex);
        glAttachShaderFn(program, meshFragment);
        glLinkProgramFn(program);
        glDeleteShaderFn(meshVertex);
        glDeleteShaderFn(meshFragment);
        GLint linked = 0;
        glGetProgramivFn(program, GL_LINK_STATUS, &linked);
        if (linked == GL_FALSE)
        {
            GLint length = 0;
            glGetProgramivFn(program, GL_INFO_LOG_LENGTH, &length);
            std::string log(static_cast<size_t>(length > 1 ? length : 1), '\0');
            glGetProgramInfoLogFn(program, length, nullptr, log.data());
            error = log;
            glDeleteProgramFn(program);
            program = 0;
            return false;
        }
        return true;
    };
    if (!linkMeshProgram(kMeshFragmentShader, rasterProgram_) || !linkMeshProgram(kShadowFragmentShader, shadowProgram_))
    {
        glDeleteProgramFn(program_);
        program_ = 0;
        failure_ = "Mesh raster shader: " + error;
        return false;
    }
    auto linkFullscreen = [&](const char *fragmentSource, unsigned &program) {
        std::string passError;
        GLuint passVertex = compileShader(GL_VERTEX_SHADER, kVertexShader, passError);
        GLuint passFragment = passVertex != 0 ? compileShader(GL_FRAGMENT_SHADER, fragmentSource, passError) : 0;
        if (passVertex == 0 || passFragment == 0)
        {
            if (passVertex != 0)
                glDeleteShaderFn(passVertex);
            error = passError;
            return false;
        }
        program = glCreateProgramFn();
        glAttachShaderFn(program, passVertex);
        glAttachShaderFn(program, passFragment);
        glLinkProgramFn(program);
        glDeleteShaderFn(passVertex);
        glDeleteShaderFn(passFragment);
        GLint linked = 0;
        glGetProgramivFn(program, GL_LINK_STATUS, &linked);
        if (linked == GL_FALSE)
        {
            GLint length = 0;
            glGetProgramivFn(program, GL_INFO_LOG_LENGTH, &length);
            std::string log(static_cast<size_t>(length > 1 ? length : 1), '\0');
            glGetProgramInfoLogFn(program, length, nullptr, log.data());
            error = log;
            glDeleteProgramFn(program);
            program = 0;
            return false;
        }
        return true;
    };
    if (!linkFullscreen(kBlendFragmentShader, blendProgram_) || !linkFullscreen(kResolveFragmentShader, resolveProgram_) || !linkFullscreen(kCopyFragmentShader, copyProgram_) || !linkFullscreen(kBloomFragmentShader, bloomProgram_))
    {
        failure_ = "Sample blend shader: " + error;
        return false;
    }

    glGenVertexArraysFn(1, &vao_);
    glGenVertexArraysFn(1, &meshVao_);
    glGenBuffersFn(1, &meshVbo_);
    glBindVertexArrayFn(meshVao_);
    glBindBufferFn(GL_ARRAY_BUFFER, meshVbo_);
    glEnableVertexAttribArrayFn(0);
    glVertexAttribPointerFn(0, 3, GL_FLOAT, GL_FALSE, 9 * static_cast<GLsizei>(sizeof(float)), nullptr);
    glEnableVertexAttribArrayFn(1);
    glVertexAttribPointerFn(1, 3, GL_FLOAT, GL_FALSE, 9 * static_cast<GLsizei>(sizeof(float)), reinterpret_cast<const void *>(3 * sizeof(float)));
    glEnableVertexAttribArrayFn(2);
    glVertexAttribPointerFn(2, 2, GL_FLOAT, GL_FALSE, 9 * static_cast<GLsizei>(sizeof(float)), reinterpret_cast<const void *>(6 * sizeof(float)));
    glEnableVertexAttribArrayFn(3);
    glVertexAttribPointerFn(3, 1, GL_FLOAT, GL_FALSE, 9 * static_cast<GLsizei>(sizeof(float)), reinterpret_cast<const void *>(8 * sizeof(float)));
    glBindVertexArrayFn(0);
    glGenFramebuffersFn(1, &fbo_);
    glGenFramebuffersFn(1, &gbufferFbo_);
    glGenFramebuffersFn(1, &shadowFbo_);
    glGenRenderbuffersFn(1, &gbufferDepth_);
    glGenRenderbuffersFn(1, &shadowDepth_);
    glGenTextures(1, &meshPosTex_);
    glGenTextures(1, &meshNormTex_);
    glGenTextures(1, &meshUvTex_);
    glGenTextures(1, &shadowTex_);
    const float missPixel[4] = {0.0f, 0.0f, 0.0f, -1.0f};
    for (unsigned texture : {meshPosTex_, meshNormTex_, meshUvTex_})
    {
        glBindTexture(GL_TEXTURE_2D, texture);
        configureTexture(GL_TEXTURE_2D, GL_NEAREST);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, 1, 1, 0, GL_RGBA, GL_FLOAT, missPixel);
    }
    glBindTexture(GL_TEXTURE_2D_ARRAY, shadowTex_);
    configureTexture(GL_TEXTURE_2D_ARRAY, GL_NEAREST);
    const float farPixel = 1.0e20f;
    glTexImage3DFn(GL_TEXTURE_2D_ARRAY, 0, GL_R32F, 1, 1, 1, 0, GL_RED, GL_FLOAT, &farPixel);
    ready_ = true;
    return true;
}

void GpuRayTracer::shutdown()
{
    if (fbo_ != 0 && glDeleteFramebuffersFn != nullptr)
        glDeleteFramebuffersFn(1, &fbo_);
    if (vao_ != 0 && glDeleteVertexArraysFn != nullptr)
        glDeleteVertexArraysFn(1, &vao_);
    if (program_ != 0 && glDeleteProgramFn != nullptr)
        glDeleteProgramFn(program_);
    if (rasterProgram_ != 0 && glDeleteProgramFn != nullptr)
        glDeleteProgramFn(rasterProgram_);
    if (shadowProgram_ != 0 && glDeleteProgramFn != nullptr)
        glDeleteProgramFn(shadowProgram_);
    if (blendProgram_ != 0 && glDeleteProgramFn != nullptr)
        glDeleteProgramFn(blendProgram_);
    if (resolveProgram_ != 0 && glDeleteProgramFn != nullptr)
        glDeleteProgramFn(resolveProgram_);
    if (copyProgram_ != 0 && glDeleteProgramFn != nullptr)
        glDeleteProgramFn(copyProgram_);
    if (bloomProgram_ != 0 && glDeleteProgramFn != nullptr)
        glDeleteProgramFn(bloomProgram_);
    rasterProgram_ = 0;
    shadowProgram_ = 0;
    blendProgram_ = 0;
    resolveProgram_ = 0;
    copyProgram_ = 0;
    bloomProgram_ = 0;
    if (bloomTex_ != 0)
        glDeleteTextures(1, &bloomTex_);
    bloomTex_ = 0;
    if (meshVao_ != 0 && glDeleteVertexArraysFn != nullptr)
        glDeleteVertexArraysFn(1, &meshVao_);
    if (meshVbo_ != 0 && glDeleteBuffersFn != nullptr)
        glDeleteBuffersFn(1, &meshVbo_);
    if (gbufferFbo_ != 0 && glDeleteFramebuffersFn != nullptr)
        glDeleteFramebuffersFn(1, &gbufferFbo_);
    if (shadowFbo_ != 0 && glDeleteFramebuffersFn != nullptr)
        glDeleteFramebuffersFn(1, &shadowFbo_);
    if (gbufferDepth_ != 0 && glDeleteRenderbuffersFn != nullptr)
        glDeleteRenderbuffersFn(1, &gbufferDepth_);
    if (shadowDepth_ != 0 && glDeleteRenderbuffersFn != nullptr)
        glDeleteRenderbuffersFn(1, &shadowDepth_);
    meshVao_ = 0;
    meshVbo_ = 0;
    gbufferFbo_ = 0;
    shadowFbo_ = 0;
    gbufferDepth_ = 0;
    shadowDepth_ = 0;
    gbufferW_ = 0;
    gbufferH_ = 0;
    const unsigned extra[] = {albedoArray_, normalArray_, materialTex_, instanceTex_, envTexture_, triTexture_, bvhTexture_, meshPosTex_, meshNormTex_, meshUvTex_, shadowTex_, sampleTex_, accumTex_[0], accumTex_[1]};
    glDeleteTextures(14, extra);
    albedoArray_ = 0;
    normalArray_ = 0;
    materialTex_ = 0;
    instanceTex_ = 0;
    envTexture_ = 0;
    triTexture_ = 0;
    bvhTexture_ = 0;
    meshPosTex_ = 0;
    meshNormTex_ = 0;
    meshUvTex_ = 0;
    shadowTex_ = 0;
    sampleTex_ = 0;
    accumTex_[0] = 0;
    accumTex_[1] = 0;
    sampleW_ = 0;
    sampleH_ = 0;
    accumCount_ = 0;
    accumFront_ = 0;
    albedoKey_.clear();
    normalKey_.clear();
    envKey_.clear();
    triCache_.clear();
    bvhCache_.clear();
    triHeight_ = 0;
    bvhHeight_ = 0;
    storedFingerprint_ = 0;
    haveFingerprint_ = false;
    storedShadowClip_.clear();
    storedTriHash_ = 0;
    storedTriCount_ = 0;
    storedBvhHash_ = 0;
    storedBvhCount_ = 0;
    fbo_ = 0;
    vao_ = 0;
    program_ = 0;
    ready_ = false;
    width_ = 0;
    height_ = 0;
    attached_ = 0;
    linear_ = false;
    shadowReady_ = false;
}

void GpuRayTracer::foldSample(unsigned display, int width, int height, int previous, bool linearOutput)
{
    const int source = accumFront_;
    const int dest = 1 - accumFront_;
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glBindFramebufferFn(GL_FRAMEBUFFER, fbo_);
    glFramebufferTexture2DFn(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, accumTex_[dest], 0);
    const GLenum draw = GL_COLOR_ATTACHMENT0;
    glDrawBuffersFn(1, &draw);
    glViewport(0, 0, width, height);
    glUseProgramFn(blendProgram_);
    glBindVertexArrayFn(vao_);
    glActiveTextureFn(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, sampleTex_);
    glActiveTextureFn(GL_TEXTURE0 + 1);
    glBindTexture(GL_TEXTURE_2D, accumTex_[source]);
    glUniform1iFn(glGetUniformLocationFn(blendProgram_, "uSample"), 0);
    glUniform1iFn(glGetUniformLocationFn(blendProgram_, "uAccum"), 1);
    glUniform1fFn(glGetUniformLocationFn(blendProgram_, "uPrevious"), static_cast<float>(previous));
    glDrawArrays(GL_TRIANGLES, 0, 3);
    accumFront_ = dest;

    glFramebufferTexture2DFn(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, display, 0);
    glUseProgramFn(resolveProgram_);
    glActiveTextureFn(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, accumTex_[accumFront_]);
    glUniform1iFn(glGetUniformLocationFn(resolveProgram_, "uAccum"), 0);
    glUniform1iFn(glGetUniformLocationFn(resolveProgram_, "uLinear"), linearOutput ? 1 : 0);
    glDrawArrays(GL_TRIANGLES, 0, 3);
}

void GpuRayTracer::applyBloom(unsigned display, int width, int height)
{
    if (display == 0 || width <= 0 || height <= 0 || copyProgram_ == 0 || bloomProgram_ == 0)
        return;
    if (bloomTex_ == 0)
        glGenTextures(1, &bloomTex_);
    glBindTexture(GL_TEXTURE_2D, bloomTex_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glBindFramebufferFn(GL_FRAMEBUFFER, fbo_);
    glFramebufferTexture2DFn(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, bloomTex_, 0);
    const GLenum draw = GL_COLOR_ATTACHMENT0;
    glDrawBuffersFn(1, &draw);
    glViewport(0, 0, width, height);
    glUseProgramFn(copyProgram_);
    glBindVertexArrayFn(vao_);
    glActiveTextureFn(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, display);
    glUniform1iFn(glGetUniformLocationFn(copyProgram_, "uColor"), 0);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    glFramebufferTexture2DFn(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, display, 0);
    glUseProgramFn(bloomProgram_);
    glBindTexture(GL_TEXTURE_2D, bloomTex_);
    glUniform1iFn(glGetUniformLocationFn(bloomProgram_, "uColor"), 0);
    glUniform1fFn(glGetUniformLocationFn(bloomProgram_, "uBloomThreshold"), static_cast<float>(engineSettings().bloomThreshold));
    glUniform1fFn(glGetUniformLocationFn(bloomProgram_, "uBloomStrength"), static_cast<float>(engineSettings().bloomStrength));
    glDrawArrays(GL_TRIANGLES, 0, 3);
}

bool GpuRayTracer::readPixels(unsigned texture, int width, int height, std::vector<std::uint8_t> &pixels) const
{
    if (!ready_ || width <= 0 || height <= 0)
        return false;
    pixels.resize(static_cast<size_t>(width) * static_cast<size_t>(height) * 4);
    glBindTexture(GL_TEXTURE_2D, texture);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    return true;
}

bool GpuRayTracer::readPixelsLinear(unsigned texture, int width, int height, std::vector<float> &pixels) const
{
    if (!ready_ || width <= 0 || height <= 0)
        return false;
    pixels.resize(static_cast<size_t>(width) * static_cast<size_t>(height) * 4);
    glBindTexture(GL_TEXTURE_2D, texture);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_FLOAT, pixels.data());
    return true;
}

#endif
