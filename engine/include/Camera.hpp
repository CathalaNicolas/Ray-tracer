#pragma once

#include "Constants.hpp"
#include "Ray.hpp"

#include <algorithm>
#include <cmath>

class Camera
{
public:
    Camera(const Vec3 &lookFrom, const Vec3 &lookAt, const Vec3 &up, double fovYDegrees, double aspect)
        : origin_(lookFrom)
    {
        double theta = fovYDegrees * kPi / 180.0;
        double halfHeight = std::tan(theta * 0.5);
        double halfWidth = aspect * halfHeight;

        Vec3 forward = normalize(lookFrom - lookAt);
        Vec3 upAxis = normalize(up);
        if (std::abs(dot(upAxis, forward)) > 0.999)
            upAxis = std::abs(forward.y) < 0.9 ? Vec3(0, 1, 0) : Vec3(1, 0, 0);

        Vec3 right = normalize(cross(upAxis, forward));
        Vec3 trueUp = cross(forward, right);

        horizontal_ = right * (2.0 * halfWidth);
        vertical_ = trueUp * (2.0 * halfHeight);
        lowerLeft_ = origin_ - right * halfWidth - trueUp * halfHeight - forward;
    }

    Ray getRay(double s, double t) const
    {
        Vec3 direction = lowerLeft_ + horizontal_ * s + vertical_ * t - origin_;
        return Ray(origin_, normalize(direction));
    }

    const Vec3 &origin() const { return origin_; }
    const Vec3 &lowerLeft() const { return lowerLeft_; }
    const Vec3 &horizontal() const { return horizontal_; }
    const Vec3 &vertical() const { return vertical_; }
    Vec3 rightAxis() const { return normalize(horizontal_); }
    Vec3 upAxis() const { return normalize(vertical_); }
    Vec3 viewDirection() const { return normalize(lowerLeft_ + horizontal_ * 0.5 + vertical_ * 0.5 - origin_); }

    void setAperture(double aperture) { aperture_ = aperture > 0 ? aperture : 0; }
    void setFocusDistance(double distance) { focusDistance_ = distance > 0 ? distance : 0; }
    double aperture() const { return aperture_; }
    double focusDistance() const { return focusDistance_; }

private:
    Vec3 origin_;
    Vec3 lowerLeft_;
    Vec3 horizontal_;
    Vec3 vertical_;
    double aperture_ = 0;
    double focusDistance_ = 0;
};

inline void lensOffset(int index, int total, double aperture, double &x, double &y)
{
    if (aperture <= 1e-8 || total <= 1 || index < 0)
    {
        x = 0;
        y = 0;
        return;
    }
    const double radius = std::sqrt((static_cast<double>(index) + 0.5) / static_cast<double>(total));
    const double theta = static_cast<double>(index) * 2.399963229728653;
    x = std::cos(theta) * radius * aperture;
    y = std::sin(theta) * radius * aperture;
}

inline void orbitCamera(Vec3 &lookFrom, const Vec3 &lookAt, double yaw, double pitch)
{
    Vec3 offset = lookFrom - lookAt;
    double radius = length(offset);
    if (radius < 1e-8)
    {
        offset = Vec3(0, 0, 1);
        radius = 1;
    }

    double theta = std::atan2(offset.x, offset.z);
    double phi = std::asin(std::clamp(offset.y / radius, -1.0, 1.0));
    theta += yaw;
    phi = std::clamp(phi + pitch, -1.45, 1.45);
    double cosPhi = std::cos(phi);
    lookFrom = lookAt + Vec3(
                            radius * cosPhi * std::sin(theta),
                            radius * std::sin(phi),
                            radius * cosPhi * std::cos(theta));
}

inline void panCamera(Vec3 &lookFrom, Vec3 &lookAt, double pixelsX, double pixelsY)
{
    Vec3 offset = lookFrom - lookAt;
    double distance = std::max(length(offset), 1e-8);
    Vec3 forward = normalize(lookAt - lookFrom);
    Vec3 right = cross(forward, Vec3(0, 1, 0));
    if (length(right) < 1e-8)
        right = Vec3(1, 0, 0);
    right = normalize(right);
    Vec3 up = normalize(cross(right, forward));
    Vec3 shift = (right * -pixelsX + up * -pixelsY) * (distance * 0.0012);
    lookFrom += shift;
    lookAt += shift;
}

inline void dollyCamera(Vec3 &lookFrom, const Vec3 &lookAt, double factor)
{
    Vec3 offset = lookFrom - lookAt;
    double radius = length(offset);
    if (radius < 1e-8)
    {
        offset = Vec3(0, 0, 1);
        radius = 1;
    }
    radius = std::clamp(radius * factor, 0.05, 500.0);
    lookFrom = lookAt + normalize(offset) * radius;
}
