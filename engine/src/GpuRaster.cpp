#include "GpuRayTracer.hpp"
#include "SceneDebug.hpp"

#include <algorithm>
#include <cmath>

#if !defined(_WIN32)

void GpuRayTracer::rasterizeMeshes(const Camera &, const Scene &, const std::vector<float> &, const std::vector<float> &, const std::vector<float> &, int, int, int, std::vector<float> &, int, int)
{
}

#else

#include "GpuContribute.hpp"
#include "GpuGl.hpp"
#include "GpuRenderDetail.hpp"
#include "Mesh.hpp"

namespace
{

void multiplyMat(const float a[16], const float b[16], float out[16])
{
    for (int col = 0; col < 4; ++col)
    {
        for (int row = 0; row < 4; ++row)
        {
            float sum = 0.0f;
            for (int k = 0; k < 4; ++k)
                sum += a[k * 4 + row] * b[col * 4 + k];
            out[col * 4 + row] = sum;
        }
    }
}

void lookAtView(const Vec3 &eye, const Vec3 &target, const Vec3 &up, float out[16])
{
    const Vec3 f = normalize(target - eye);
    const Vec3 s = normalize(cross(f, up));
    const Vec3 u = cross(s, f);
    out[0] = static_cast<float>(s.x);
    out[1] = static_cast<float>(u.x);
    out[2] = static_cast<float>(-f.x);
    out[3] = 0.0f;
    out[4] = static_cast<float>(s.y);
    out[5] = static_cast<float>(u.y);
    out[6] = static_cast<float>(-f.y);
    out[7] = 0.0f;
    out[8] = static_cast<float>(s.z);
    out[9] = static_cast<float>(u.z);
    out[10] = static_cast<float>(-f.z);
    out[11] = 0.0f;
    out[12] = static_cast<float>(-dot(s, eye));
    out[13] = static_cast<float>(-dot(u, eye));
    out[14] = static_cast<float>(dot(f, eye));
    out[15] = 1.0f;
}

void perspectiveClip(const Vec3 &eye, const Vec3 &target, const Vec3 &up, double fovY, double nearP, double farP, float out[16])
{
    float view[16];
    lookAtView(eye, target, up, view);
    const float t = static_cast<float>(std::tan(fovY * 0.5));
    const float n = static_cast<float>(nearP);
    const float f = static_cast<float>(farP);
    float proj[16] = {};
    proj[0] = 1.0f / t;
    proj[5] = 1.0f / t;
    proj[10] = (f + n) / (n - f);
    proj[11] = -1.0f;
    proj[14] = (2.0f * f * n) / (n - f);
    multiplyMat(proj, view, out);
}

void orthoClip(const Vec3 &eye, const Vec3 &target, const Vec3 &up, double extent, double nearP, double farP, float out[16])
{
    float view[16];
    lookAtView(eye, target, up, view);
    const float e = static_cast<float>(extent);
    const float n = static_cast<float>(nearP);
    const float f = static_cast<float>(farP);
    float proj[16] = {};
    proj[0] = 1.0f / e;
    proj[5] = 1.0f / e;
    proj[10] = -2.0f / (f - n);
    proj[14] = -(f + n) / (f - n);
    proj[15] = 1.0f;
    multiplyMat(proj, view, out);
}

void cameraClipMatrix(const Camera &camera, float out[16], Vec3 &right, Vec3 &up, Vec3 &forward, double &halfWidth, double &halfHeight, double lensX, double lensY, double focus)
{
    const Vec3 horizontal = camera.horizontal();
    const Vec3 vertical = camera.vertical();
    halfWidth = std::max(length(horizontal) * 0.5, 1e-6);
    halfHeight = std::max(length(vertical) * 0.5, 1e-6);
    right = normalize(horizontal);
    up = normalize(vertical);
    forward = normalize(camera.origin() - right * halfWidth - up * halfHeight - camera.lowerLeft());
    const Vec3 eye = camera.origin() + right * lensX + up * lensY;
    const double nearP = 0.02;
    const double farP = 2000.0;
    const double c = (farP + nearP) / (farP - nearP);
    const double d = -2.0 * farP * nearP / (farP - nearP);
    const double ox = dot(eye, right);
    const double oy = dot(eye, up);
    const double oz = dot(eye, forward);
    out[0] = static_cast<float>(right.x / halfWidth);
    out[1] = static_cast<float>(-up.x / halfHeight);
    out[2] = static_cast<float>(-c * forward.x);
    out[3] = static_cast<float>(-forward.x);
    out[4] = static_cast<float>(right.y / halfWidth);
    out[5] = static_cast<float>(-up.y / halfHeight);
    out[6] = static_cast<float>(-c * forward.y);
    out[7] = static_cast<float>(-forward.y);
    out[8] = static_cast<float>(right.z / halfWidth);
    out[9] = static_cast<float>(-up.z / halfHeight);
    out[10] = static_cast<float>(-c * forward.z);
    out[11] = static_cast<float>(-forward.z);
    out[12] = static_cast<float>(-ox / halfWidth);
    out[13] = static_cast<float>(oy / halfHeight);
    out[14] = static_cast<float>(c * oz + d);
    out[15] = static_cast<float>(oz);
    if (focus > 1e-4 && (std::abs(lensX) > 1e-8 || std::abs(lensY) > 1e-8))
    {
        const float shiftX = static_cast<float>(lensX / (focus * halfWidth));
        const float shiftY = static_cast<float>(-lensY / (focus * halfHeight));
        out[0] += shiftX * out[3];
        out[4] += shiftX * out[7];
        out[8] += shiftX * out[11];
        out[12] += shiftX * out[15];
        out[1] += shiftY * out[3];
        out[5] += shiftY * out[7];
        out[9] += shiftY * out[11];
        out[13] += shiftY * out[15];
    }
}

void cubeFaceClip(const Vec3 &eye, int face, float clip[16])
{
    const Vec3 forward[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    const Vec3 up[6] = {{0, -1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}, {0, -1, 0}, {0, -1, 0}};
    const int index = face < 0 ? 0 : (face > 5 ? 5 : face);
    perspectiveClip(eye, eye + forward[index], up[index], 1.5707963267948966, 0.02, 40.0, clip);
}

void jitterClip(float *clip, int width, int height, int sampleGrid, int sampleIndex)
{
    if (clip == nullptr || width <= 0 || height <= 0 || sampleGrid <= 1 || sampleIndex < 0)
        return;
    const int sx = sampleIndex % sampleGrid;
    const int sy = sampleIndex / sampleGrid;
    const float jx = 2.0f * ((sx + 0.5f) / static_cast<float>(sampleGrid) - 0.5f) / static_cast<float>(width);
    const float jy = 2.0f * ((sy + 0.5f) / static_cast<float>(sampleGrid) - 0.5f) / static_cast<float>(height);
    clip[0] += jx * clip[3];
    clip[4] += jx * clip[7];
    clip[8] += jx * clip[11];
    clip[12] += jx * clip[15];
    clip[1] += jy * clip[3];
    clip[5] += jy * clip[7];
    clip[9] += jy * clip[11];
    clip[13] += jy * clip[15];
}

} // namespace

void GpuRayTracer::rasterizeMeshes(const Camera &camera, const Scene &scene, const std::vector<float> &vertices, const std::vector<float> &meshMin, const std::vector<float> &meshMax, int meshCount, int width, int height, std::vector<float> &shadowClip, int sampleGrid, int sampleIndex)
{
    if (rasterProgram_ == 0 || width <= 0 || height <= 0 || meshCount <= 0 || vertices.size() < 9)
        return;

    if (gbufferW_ != width || gbufferH_ != height)
    {
        auto allocate = [&](unsigned texture, GLenum internal) {
            glBindTexture(GL_TEXTURE_2D, texture);
            configureTexture(GL_TEXTURE_2D, GL_NEAREST);
            glTexImage2D(GL_TEXTURE_2D, 0, internal, width, height, 0, GL_RGBA, GL_FLOAT, nullptr);
        };
        allocate(meshPosTex_, GL_RGBA32F);
        allocate(meshNormTex_, GL_RGBA16F);
        allocate(meshUvTex_, GL_RGBA16F);
        glBindRenderbufferFn(GL_RENDERBUFFER, gbufferDepth_);
        glRenderbufferStorageFn(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width, height);
        glBindFramebufferFn(GL_FRAMEBUFFER, gbufferFbo_);
        glFramebufferTexture2DFn(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, meshPosTex_, 0);
        glFramebufferTexture2DFn(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, meshNormTex_, 0);
        glFramebufferTexture2DFn(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT2, GL_TEXTURE_2D, meshUvTex_, 0);
        glFramebufferRenderbufferFn(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, gbufferDepth_);
        gbufferW_ = width;
        gbufferH_ = height;
    }

    if (!shadowReady_)
    {
        glBindTexture(GL_TEXTURE_2D_ARRAY, shadowTex_);
        configureTexture(GL_TEXTURE_2D_ARRAY, GL_NEAREST);
        glTexImage3DFn(GL_TEXTURE_2D_ARRAY, 0, GL_R32F, 1024, 1024, kGpuMaxLights, 0, GL_RED, GL_FLOAT, nullptr);
        glBindRenderbufferFn(GL_RENDERBUFFER, shadowDepth_);
        glRenderbufferStorageFn(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, 1024, 1024);
        glBindFramebufferFn(GL_FRAMEBUFFER, shadowFbo_);
        glFramebufferRenderbufferFn(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, shadowDepth_);
        shadowReady_ = true;
    }

    const int vertexCount = static_cast<int>(vertices.size() / 9);
    glBindVertexArrayFn(meshVao_);
    glBindBufferFn(GL_ARRAY_BUFFER, meshVbo_);
    glBufferDataFn(GL_ARRAY_BUFFER, static_cast<ptrdiff_t>(vertices.size() * sizeof(float)), vertices.data(), GL_DYNAMIC_DRAW);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);

    const float depthOne = 1.0f;
    if (!gpu_detail::reuseShadowMaps())
    {
    Vec3 boundsMin(1e9, 1e9, 1e9);
    Vec3 boundsMax(-1e9, -1e9, -1e9);
    for (int mesh = 0; mesh < meshCount; ++mesh)
    {
        const size_t slot = static_cast<size_t>(mesh) * 4;
        boundsMin.x = std::min(boundsMin.x, static_cast<double>(meshMin[slot]));
        boundsMin.y = std::min(boundsMin.y, static_cast<double>(meshMin[slot + 1]));
        boundsMin.z = std::min(boundsMin.z, static_cast<double>(meshMin[slot + 2]));
        boundsMax.x = std::max(boundsMax.x, static_cast<double>(meshMax[slot]));
        boundsMax.y = std::max(boundsMax.y, static_cast<double>(meshMax[slot + 1]));
        boundsMax.z = std::max(boundsMax.z, static_cast<double>(meshMax[slot + 2]));
    }
    const Vec3 center = (boundsMin + boundsMax) * 0.5;
    const double radius = std::max(length(boundsMax - boundsMin) * 0.5, 0.5) * 1.8;

    const float farClear[4] = {1.0e20f, 1.0e20f, 1.0e20f, 1.0e20f};
    glBindFramebufferFn(GL_FRAMEBUFFER, shadowFbo_);
    glViewport(0, 0, 1024, 1024);
    glUseProgramFn(shadowProgram_);
    const GLint lightPosLoc = glGetUniformLocationFn(shadowProgram_, "uLightPos");
    const GLint lightDirLoc = glGetUniformLocationFn(shadowProgram_, "uLightDir");
    const GLint directionalLoc = glGetUniformLocationFn(shadowProgram_, "uDirectional");
    const GLint shadowClipLoc = glGetUniformLocationFn(shadowProgram_, "uClip");
    const GLint skipMeshLoc = glGetUniformLocationFn(shadowProgram_, "uSkipMesh");
    const GLint paraboloidLoc = glGetUniformLocationFn(shadowProgram_, "uParaboloid");
    int lightIndex = 0;
    glEnable(GL_CULL_FACE);
    glCullFace(GL_FRONT);
    auto drawPointShadow = [&](const Vec3 &eye, int skipMesh) {
        float *clip = shadowClip.data() + static_cast<size_t>(lightIndex) * 16;
        glUniform3fFn(lightPosLoc, static_cast<float>(eye.x), static_cast<float>(eye.y), static_cast<float>(eye.z));
        glUniform1iFn(directionalLoc, 0);
        glUniform1iFn(paraboloidLoc, 0);
        glUniform1fFn(skipMeshLoc, static_cast<float>(skipMesh));
        glFramebufferTextureLayerFn(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, shadowTex_, 0, lightIndex);
        const GLenum shadowDraw = GL_COLOR_ATTACHMENT0;
        glDrawBuffersFn(1, &shadowDraw);
        glClearBufferfvFn(GL_COLOR, 0, farClear);
        glClearBufferfvFn(GL_DEPTH, 0, &depthOne);
        for (int face = 0; face < 6; ++face)
        {
            cubeFaceClip(eye, face, clip);
            glUniformMatrix4fvFn(shadowClipLoc, 1, GL_FALSE, clip);
            const int col = face % 3;
            const int row = face / 3;
            glViewport(col * 341, row * 512, 341, 512);
            glDrawArrays(GL_TRIANGLES, 0, vertexCount);
        }
        glViewport(0, 0, 1024, 1024);
    };
    std::vector<int> meshSlotForId;
    auto slotFor = [&](int id) {
        for (size_t index = 0; index + 1 < meshSlotForId.size(); index += 2)
        {
            if (meshSlotForId[index] == id)
                return meshSlotForId[index + 1];
        }
        return -1;
    };
    int nextSlot = 0;
    struct MeshSlots : gpu_detail::GpuContribute
    {
        std::vector<int> *ids = nullptr;
        int *slot = nullptr;
        void sphere(const Hittable &, const Vec3 &, double) override {}
        void plane(const Hittable &, const Vec3 &, const Vec3 &, bool, const Vec3 &, double) override {}
        void mesh(const Mesh &object) override
        {
            if (object.triangles().empty() || ids == nullptr || slot == nullptr)
                return;
            ids->push_back(object.id);
            ids->push_back(*slot);
            ++(*slot);
        }
    } slots;
    slots.ids = &meshSlotForId;
    slots.slot = &nextSlot;
    for (const auto &object : scene.objects())
        object->contributeGpu(slots);
    for (const PointLight &light : scene.lights())
    {
        if (lightIndex >= kGpuMaxLights)
            break;
        float *clip = shadowClip.data() + static_cast<size_t>(lightIndex) * 16;
        Vec3 up(0, 1, 0);
        if (light.directional)
        {
            Vec3 lightDir = normalize(light.position);
            if (std::abs(lightDir.y) > 0.9)
                up = Vec3(1, 0, 0);
            const Vec3 eye = center + lightDir * (radius + 2.0);
            orthoClip(eye, center, up, radius, 0.05, radius * 2.0 + 4.0, clip);
            glUniform3fFn(lightDirLoc, static_cast<float>(lightDir.x), static_cast<float>(lightDir.y), static_cast<float>(lightDir.z));
            glUniform1iFn(directionalLoc, 1);
            glUniform1iFn(paraboloidLoc, 0);
            glUniformMatrix4fvFn(shadowClipLoc, 1, GL_FALSE, clip);
            glUniform1fFn(skipMeshLoc, -1.0f);
            glFramebufferTextureLayerFn(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, shadowTex_, 0, lightIndex);
            const GLenum shadowDraw = GL_COLOR_ATTACHMENT0;
            glDrawBuffersFn(1, &shadowDraw);
            glClearBufferfvFn(GL_COLOR, 0, farClear);
            glClearBufferfvFn(GL_DEPTH, 0, &depthOne);
            glViewport(0, 0, 1024, 1024);
            glDrawArrays(GL_TRIANGLES, 0, vertexCount);
        }
        else
            drawPointShadow(light.position, -1);
        ++lightIndex;
    }
    for (const auto &object : scene.objects())
    {
        if (lightIndex >= kGpuMaxLights)
            break;
        const Material material = object->material();
        if (material.emission <= 0.01)
            continue;
        const Vec3 eye = object->worldPosition();
        const int skipMesh = slotFor(object->id);
        drawPointShadow(eye, skipMesh);
        ++lightIndex;
    }
    glDisable(GL_CULL_FACE);
    }

    float cameraMatrix[16];
    Vec3 right;
    Vec3 up;
    Vec3 forward;
    double halfWidth = 1;
    double halfHeight = 1;
    double lensX = 0;
    double lensY = 0;
    const int sampleTotal = sampleGrid > 1 ? sampleGrid * sampleGrid : 1;
    lensOffset(sampleIndex, sampleTotal, camera.aperture(), lensX, lensY);
    double focus = camera.focusDistance();
    if (focus <= 1e-4)
        focus = 10;
    cameraClipMatrix(camera, cameraMatrix, right, up, forward, halfWidth, halfHeight, lensX, lensY, focus);
    jitterClip(cameraMatrix, width, height, sampleGrid, sampleIndex);
    glBindFramebufferFn(GL_FRAMEBUFFER, gbufferFbo_);
    const GLenum draws[] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2};
    glDrawBuffersFn(3, draws);
    glViewport(0, 0, width, height);
    glUseProgramFn(rasterProgram_);
    glUniformMatrix4fvFn(glGetUniformLocationFn(rasterProgram_, "uClip"), 1, GL_FALSE, cameraMatrix);
    const float miss[4] = {0.0f, 0.0f, 0.0f, -1.0f};
    const float zero[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    glClearBufferfvFn(GL_COLOR, 0, miss);
    glClearBufferfvFn(GL_COLOR, 1, zero);
    glClearBufferfvFn(GL_COLOR, 2, zero);
    glClearBufferfvFn(GL_DEPTH, 0, &depthOne);
    glDrawArrays(GL_TRIANGLES, 0, vertexCount);
    glDisable(GL_DEPTH_TEST);
    glBindVertexArrayFn(0);
}

#endif
