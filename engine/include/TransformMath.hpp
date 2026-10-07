#pragma once

#include "Components.hpp"
#include "Constants.hpp"
#include "Vec3.hpp"

#include <algorithm>
#include <cmath>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/euler_angles.hpp>
#include <glm/mat4x4.hpp>

inline Vec3 toVec3(const glm::dvec3 &value)
{
    return Vec3(value.x, value.y, value.z);
}

inline glm::dvec3 toGlm(const Vec3 &value)
{
    return glm::dvec3(value.x, value.y, value.z);
}

inline glm::dvec3 rotateByQuat(const glm::dquat &rotation, const glm::dvec3 &value)
{
    return rotation * value;
}

// Same order as meshRotate: yaw Y, then pitch X, then roll Z on the vector.
// q = qRoll * qPitch * qYaw so the rightmost factor is applied first.
inline glm::dquat quatFromEulerDegrees(const Vec3 &degrees)
{
    const double yaw = degrees.y * kPi / 180.0;
    const double pitch = degrees.x * kPi / 180.0;
    const double roll = degrees.z * kPi / 180.0;
    const glm::dquat qYaw = glm::angleAxis(yaw, glm::dvec3(0, 1, 0));
    const glm::dquat qPitch = glm::angleAxis(pitch, glm::dvec3(1, 0, 0));
    const glm::dquat qRoll = glm::angleAxis(roll, glm::dvec3(0, 0, 1));
    return qRoll * qPitch * qYaw;
}

// Inverse of quatFromEulerDegrees for the Y-then-X-then-Z vector convention.
inline Vec3 eulerDegreesFromQuat(const glm::dquat &rotation)
{
    const glm::dmat4 matrix = glm::mat4_cast(glm::normalize(rotation));
    double t1 = 0;
    double t2 = 0;
    double t3 = 0;
    glm::extractEulerAngleZXY(matrix, t1, t2, t3);
    return Vec3(t2 * 180.0 / kPi, t3 * 180.0 / kPi, t1 * 180.0 / kPi);
}

inline glm::dquat slerpRotation(const glm::dquat &from, const glm::dquat &to, double t)
{
    glm::dquat a = glm::normalize(from);
    glm::dquat b = glm::normalize(to);
    if (glm::dot(a, b) < 0.0)
        b = -b;
    return glm::normalize(glm::slerp(a, b, t));
}

inline glm::dmat4 localMatrixOf(const Transform &transform)
{
    const glm::dmat4 translation = glm::translate(glm::dmat4(1), transform.position);
    const glm::dmat4 rotation = glm::mat4_cast(transform.rotation);
    const glm::dmat4 scale = glm::scale(glm::dmat4(1), transform.scale);
    return translation * rotation * scale;
}

inline glm::dmat4 localMatrixOf(const DisplayTransform &transform)
{
    const glm::dmat4 translation = glm::translate(glm::dmat4(1), transform.position);
    const glm::dmat4 rotation = glm::mat4_cast(transform.rotation);
    const glm::dmat4 scale = glm::scale(glm::dmat4(1), transform.scale);
    return translation * rotation * scale;
}

inline void markTransformDirty(Transform &transform)
{
    transform.matricesDirty = true;
}

inline double maxAxisScale(const glm::dvec3 &scale)
{
    return std::max(scale.x, std::max(scale.y, scale.z));
}

inline Vec3 transformPoint(const glm::dmat4 &matrix, const Vec3 &point)
{
    const glm::dvec4 world = matrix * glm::dvec4(point.x, point.y, point.z, 1.0);
    return Vec3(world.x, world.y, world.z);
}

inline Vec3 transformDirection(const glm::dmat4 &matrix, const Vec3 &direction)
{
    const glm::dvec4 world = matrix * glm::dvec4(direction.x, direction.y, direction.z, 0.0);
    return Vec3(world.x, world.y, world.z);
}

inline void worldAxesFromMatrix(const glm::dmat4 &world, Vec3 &axisX, Vec3 &axisY, Vec3 &axisZ)
{
    axisX = normalize(toVec3(glm::dvec3(world[0])));
    axisY = normalize(toVec3(glm::dvec3(world[1])));
    axisZ = normalize(toVec3(glm::dvec3(world[2])));
}
