#pragma once

#include "Camera.hpp"
#include "GpuLimits.hpp"
#include "Scene.hpp"

#include <cstdint>
#include <string>
#include <vector>

class GpuRayTracer
{
public:
    bool init();
    void shutdown();
    bool available() const { return ready_; }
    const std::string &failure() const { return failure_; }

    int render(
        const Scene &scene,
        const Camera &camera,
        unsigned texture,
        int width,
        int height,
        int sampleGrid,
        int maxDepth,
        int selectedObject,
        bool linearOutput = false,
        int sampleIndex = -1,
        double timeSeconds = 0,
        bool mirrorBounces = true);

    bool readPixels(unsigned texture, int width, int height, std::vector<std::uint8_t> &pixels) const;
    bool readPixelsLinear(unsigned texture, int width, int height, std::vector<float> &pixels) const;

private:
    void rasterizeMeshes(const Camera &camera, const Scene &scene, const std::vector<float> &vertices, const std::vector<float> &meshMin, const std::vector<float> &meshMax, int meshCount, int width, int height, std::vector<float> &shadowClip, int sampleGrid, int sampleIndex);
    void foldSample(unsigned display, int width, int height, int previous, bool linearOutput);
    void applyBloom(unsigned display, int width, int height);
    bool ready_ = false;
    std::string failure_;
    unsigned program_ = 0;
    unsigned vao_ = 0;
    unsigned fbo_ = 0;
    int width_ = 0;
    int height_ = 0;
    unsigned attached_ = 0;
    bool linear_ = false;
    unsigned albedoArray_ = 0;
    unsigned normalArray_ = 0;
    unsigned materialTex_ = 0;
    unsigned instanceTex_ = 0;
    unsigned envTexture_ = 0;
    unsigned triTexture_ = 0;
    unsigned bvhTexture_ = 0;
    std::string albedoKey_;
    std::string normalKey_;
    std::string envKey_;
    std::vector<float> triCache_;
    std::vector<float> bvhCache_;
    int triHeight_ = 0;
    int bvhHeight_ = 0;
    unsigned rasterProgram_ = 0;
    unsigned shadowProgram_ = 0;
    unsigned meshVao_ = 0;
    unsigned meshVbo_ = 0;
    unsigned gbufferFbo_ = 0;
    unsigned gbufferDepth_ = 0;
    unsigned meshPosTex_ = 0;
    unsigned meshNormTex_ = 0;
    unsigned meshUvTex_ = 0;
    int gbufferW_ = 0;
    int gbufferH_ = 0;
    unsigned shadowFbo_ = 0;
    unsigned shadowDepth_ = 0;
    unsigned shadowTex_ = 0;
    bool shadowReady_ = false;
    unsigned blendProgram_ = 0;
    unsigned resolveProgram_ = 0;
    unsigned bloomProgram_ = 0;
    unsigned copyProgram_ = 0;
    unsigned bloomTex_ = 0;
    unsigned sampleTex_ = 0;
    unsigned accumTex_[2] = {};
    int sampleW_ = 0;
    int sampleH_ = 0;
    int accumCount_ = 0;
    int accumFront_ = 0;
    std::uint64_t storedFingerprint_ = 0;
    bool haveFingerprint_ = false;
    std::vector<float> storedShadowClip_;
    std::uint64_t storedTriHash_ = 0;
    std::size_t storedTriCount_ = 0;
    std::uint64_t storedBvhHash_ = 0;
    std::size_t storedBvhCount_ = 0;
};
