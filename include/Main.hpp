#pragma once

#include <SFML/Graphics.hpp>
#define _USE_MATH_DEFINES
#include <cmath>
#include <iostream>
#include "SceneObject.hpp"

const long double EPSILON = 0.000001f;
class SceneObject;

// Define a class to represent a sphere
class Sphere : public SceneObject
{
public:
  Vector3 center;
  long double radius;

  Sphere(Vector3 center = Vector3(), double radius = 1.0l) : SceneObject(center, sf::Color(255, 0, 0)), center(center), radius(radius) {}
  Sphere(Vector3 center = Vector3(), sf::Color color = sf::Color(255, 0, 0), double radius = 1.0l) : SceneObject(center, color), radius(radius) {}
};

struct Plane : public SceneObject
{
  Vector3 normal;
  long double distance;

  Plane(Vector3 position, Vector3 n, sf::Color color = sf::Color(0, 255, 0), long double d = 10) : SceneObject(position, color), normal(n), distance(d) {}
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
