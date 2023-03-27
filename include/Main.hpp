#pragma once

#include <SFML/Graphics.hpp>
#define _USE_MATH_DEFINES
#include <cmath>
#include <iostream>

const long double EPSILON = 0.000001f;

// Define a vector class to represent 3D points and directions
class Vector3
{
public:
  long double x, y, z;

  Vector3(long double x = 0, long double y = 0, long double z = 0) : x(x), y(y), z(z) {}

  Vector3 operator+(const Vector3 &other) const
  {
    return Vector3(x + other.x, y + other.y, z + other.z);
  }

  Vector3 operator-(const Vector3 &other) const
  {
    return Vector3(x - other.x, y - other.y, z - other.z);
  }
};

// Define a class to represent a sphere
class Sphere
{
public:
  Vector3 center;
  long double radius;

  Sphere(Vector3 center = Vector3(), long double radius = 1.0l) : center(center), radius(radius) {}
};

struct Plane
{
  Vector3 normal;
  long double distance;

  Plane(const Vector3 &n, long double d) : normal(n), distance(d) {}
};

struct Ray
{
  Vector3 origin;
  Vector3 direction;

  Ray(const Vector3 &origin, const Vector3 &direction)
      : origin(origin), direction(direction)
  {
  }
};

// Define a function to compute the length of a vector
long double length(Vector3 v);

// Define a function to normalize a vector
Vector3 normalize(Vector3 v);

// Define a function to compute the dot product of two vectors
long double dot(Vector3 a, Vector3 b);

// Define a function to compute the cross product of two vectors
Vector3 cross(Vector3 a, Vector3 b);

Vector3 operator*(long double scalar, Vector3 a);

Vector3 operator*(Vector3 a, long double scalar);

Vector3 operator-(Vector3 a, long double scalar);

Vector3 operator-(long double scalar, Vector3 a);