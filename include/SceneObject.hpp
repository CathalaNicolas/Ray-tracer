#pragma once

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
    Vector3 operator*(long double scalar)
    {
        return Vector3(x - scalar, y - scalar, z - scalar);
    }
};

class SceneObject
{
public:
    Vector3 position;
    sf::Color color;
    long double ambient;
    long double diffuse;
    long double specular;
    long double shininess;

    SceneObject(Vector3 objPosition, sf::Color objColor,
                long double ObjAmbient = 0.2l, long double ObjDiffuse = 0.5l,
                long double ObjSpecular = 0.5l, long double objShininess = 15.0l) : position(objPosition), color(objColor),
                                                                                    ambient(ObjAmbient), diffuse(ObjDiffuse),
                                                                                    specular(ObjSpecular), shininess(objShininess){};
};

// Define a function to compute the length of a vector
long double length(const Vector3 &v);

// Define a function to normalize a vector
Vector3 normalize(Vector3 v);

// Define a function to compute the dot product of two vectors
long double dot(const Vector3 &a, const Vector3 &b);

// Define a function to compute the cross product of two vectors
Vector3 cross(const Vector3 &a, const Vector3 &b);

Vector3 operator*(Vector3 a, long double scalar);
Vector3 operator*(long double scalar, Vector3 a);
Vector3 operator-(Vector3 a, long double scalar);

Vector3 operator-(long double scalar, Vector3 a);