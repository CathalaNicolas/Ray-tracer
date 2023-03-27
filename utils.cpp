#include "include/Main.hpp"

Vector3 operator*(long double scalar, Vector3 a)
{
  return Vector3(a.x * scalar, a.y * scalar, a.z * scalar);
}

Vector3 operator*(Vector3 a, long double scalar)
{
  return Vector3(a.x * scalar, a.y * scalar, a.z * scalar);
}

Vector3 operator-(Vector3 a, long double scalar)
{
  return Vector3(a.x - scalar, a.y - scalar, a.z - scalar);
}

Vector3 operator-(long double scalar, Vector3 a)
{
  return Vector3(a.x - scalar, a.y - scalar, a.z - scalar);
}

// Define a function to compute the length of a vector
long double length(Vector3 v)
{
  return sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
}

// Define a function to normalize a vector
Vector3 normalize(Vector3 v)
{
  long double len = length(v);
  if (len != 0)
  {
    v.x /= len;
    v.y /= len;
    v.z /= len;
  }
  return v;
}

// Define a function to compute the dot product of two vectors
long double dot(Vector3 a, Vector3 b)
{
  return a.x * b.x + a.y * b.y + a.z * b.z;
}

// Define a function to compute the cross product of two vectors
Vector3 cross(Vector3 a, Vector3 b)
{
  long double x = a.y * b.z - a.z * b.y;
  long double y = a.z * b.x - a.x * b.z;
  long double z = a.x * b.y - a.y * b.x;
  return Vector3(x, y, z);
}