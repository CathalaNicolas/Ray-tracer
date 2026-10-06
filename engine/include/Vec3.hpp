#pragma once

#include <cmath>

struct Vec3
{
    double x = 0;
    double y = 0;
    double z = 0;

    Vec3() = default;
    Vec3(double x, double y, double z) : x(x), y(y), z(z) {}

    Vec3 operator-() const
    {
        return Vec3(-x, -y, -z);
    }

    Vec3 operator+(const Vec3 &other) const
    {
        return Vec3(x + other.x, y + other.y, z + other.z);
    }

    Vec3 operator-(const Vec3 &other) const
    {
        return Vec3(x - other.x, y - other.y, z - other.z);
    }

    Vec3 operator*(const Vec3 &other) const
    {
        return Vec3(x * other.x, y * other.y, z * other.z);
    }

    Vec3 operator*(double scalar) const
    {
        return Vec3(x * scalar, y * scalar, z * scalar);
    }

    Vec3 operator/(double scalar) const
    {
        return Vec3(x / scalar, y / scalar, z / scalar);
    }

    Vec3 &operator+=(const Vec3 &other)
    {
        x += other.x;
        y += other.y;
        z += other.z;
        return *this;
    }

    Vec3 &operator*=(double scalar)
    {
        x *= scalar;
        y *= scalar;
        z *= scalar;
        return *this;
    }
};

inline Vec3 operator*(double scalar, const Vec3 &v)
{
    return v * scalar;
}

inline double dot(const Vec3 &a, const Vec3 &b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

inline Vec3 cross(const Vec3 &a, const Vec3 &b)
{
    return Vec3(
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x);
}

inline double length(const Vec3 &v)
{
    return std::sqrt(dot(v, v));
}

inline Vec3 normalize(const Vec3 &v)
{
    double len = length(v);
    if (len <= 0)
        return Vec3();
    return v / len;
}

inline Vec3 reflect(const Vec3 &direction, const Vec3 &normal)
{
    return direction - normal * (2.0 * dot(direction, normal));
}
