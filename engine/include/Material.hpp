#pragma once

#include "Vec3.hpp"

#include <string>

struct Material
{
    Vec3 albedo{0.8, 0.8, 0.8};
    double ambient = 0.1;
    double diffuse = 0.8;
    double specular = 0.2;
    double shininess = 32;
    double reflectivity = 0;
    double transmission = 0;
    double ior = 1.5;
    double uvScale = 1;
    double uvScrollU = 0;
    double uvScrollV = 0;
    double roughness = -1;
    double emission = 0;
    std::string albedoMap;
    std::string normalMap;

    static Material makeDiffuse(const Vec3 &albedo)
    {
        Material material;
        material.albedo = albedo;
        material.ambient = 0.12;
        material.diffuse = 0.85;
        material.specular = 0.08;
        material.shininess = 24;
        material.reflectivity = 0.02;
        return material;
    }

    static Material makeMetal(const Vec3 &albedo, double reflectivity)
    {
        Material material;
        material.albedo = albedo;
        material.ambient = 0.04;
        material.diffuse = 0.15;
        material.specular = 0.9;
        material.shininess = 128;
        material.reflectivity = reflectivity;
        return material;
    }

    static Material makeGlass(const Vec3 &albedo, double ior = 1.5)
    {
        Material material;
        material.albedo = albedo;
        material.ambient = 0.02;
        material.diffuse = 0.02;
        material.specular = 0.5;
        material.shininess = 128;
        material.reflectivity = 0;
        material.transmission = 1;
        material.ior = ior;
        return material;
    }
};
