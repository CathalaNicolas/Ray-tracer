#pragma once

#include <string>

extern const char *kVertexShader;
extern const char *kMeshVertexShader;
extern const char *kShadowVertexShader;
extern const char *kMeshFragmentShader;
extern const char *kShadowFragmentShader;
extern const char *kBlendFragmentShader;
extern const char *kResolveFragmentShader;
extern const char *kBloomFragmentShader;
extern const char *kCopyFragmentShader;
extern const char *kFragmentShader;

std::string shaderWithLimits(std::string source);
