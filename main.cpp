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

bool intersectRayPlane(const Ray &ray, const Vector3 &position, const Vector3 &normal, long double &t)
{
    // Calculate the denominator of the intersection equation
    long double denom = dot(ray.direction, normal);

    // If the denominator is zero, the ray and plane are parallel
    if (abs(denom) < EPSILON)
    {
        return false;
    }

    // Calculate the distance to the intersection point
    Vector3 rayToPlane = position - ray.origin;
    t = dot(rayToPlane, normal) / denom;

    // If the intersection point is behind the ray, there is no intersection
    if (t < EPSILON)
    {
        return false;
    }

    return true;
}

// Define a function to compute the intersection between a ray and a sphere
bool intersectRaySphere(Vector3 rayOrigin, Vector3 rayDirection, const Sphere &sphere, long double &t)
{
    Vector3 oc = rayOrigin - sphere.center;
    long double a = dot(rayDirection, rayDirection);
    long double b = 2 * dot(oc, rayDirection);
    long double c = dot(oc, oc) - sphere.radius * sphere.radius;
    long double discriminant = b * b - 4 * a * c;
    if (discriminant < 0)
    {
        return false;
    }
    else
    {
        long double t0 = (-b - sqrt(discriminant)) / (2 * a);
        long double t1 = (-b + sqrt(discriminant)) / (2 * a);
        if (t0 > t1)
        {
            std::swap(t0, t1);
        }
        if (t1 < EPSILON)
        {
            return false;
        }
        else
        {
            t = t0 < EPSILON ? t1 : t0;
            return true;
        }
    }
}

bool isPointInShadow(const Vector3 &point, const Vector3 &toLight, Sphere spheres, const Vector3 &planePosition, const Vector3 &planeNormal)
{
    long double t;
    // for (const auto &sphere : spheres)
    // {
    if (intersectRaySphere(point, toLight, spheres, t))
    {
        return true;
    }
    // }
    return intersectRayPlane(Ray(point, toLight), planePosition, planeNormal, t);
}

int main()
{
    // Define the size of the window and the image resolution
    const int windowWidth = 800;
    const int windowHeight = 600;
    const int imageWidth = 400;
    const int imageHeight = 300;

    // Define the camera
    Vector3 cameraPos(5, 0, 1);
    Vector3 cameraDir(-1, 0, 1);
    Vector3 cameraUp(0, 1, 0);
    long double fov = 60.0l;

    // Define the light source
    Vector3 lightPos(-10, -15, 0);
    Vector3 lightColor(1, 1, 1);
    const Vector3 highlightColor(1.0l, 1.0l, 1.0l); // white
    const long double shininess = 50.0l;

    // Define the sphere
    Sphere sphere(Vector3(-4, 0, 10), 4.0l);
    // Define the material properties of the objects
    Vector3 sphereColor(1, 0, 0);
    long double sphereAmbient = 0.2l;
    long double sphereDiffuse = 0.2l;
    long double sphereSpecular = 0.5l;
    long double sphereShininess = 4.0l;

    // Define the SFML window and image
    sf::RenderWindow window(sf::VideoMode(windowWidth, windowHeight), "Raycasting with SFML");
    sf::Texture texture;
    texture.create(imageWidth, imageHeight);
    sf::Sprite sprite(texture);

    // Create an image buffer to store the pixel colors
    sf::Color *image = new sf::Color[imageWidth * imageHeight];

    // Define the plane
    Vector3 planePosition(0, 0, 10); // position the plane 5 units in front of the camera
    Vector3 planeNormal(10, 0, 10);  // make the plane face towards the camera
    Vector3 planeColor(0, 1, 0);
    long double planeAmbient = 0.2l;
    long double planeDiffuse = 0.5l;
    long double planeSpecular = 0.5l;
    long double planeShininess = 15.0l;

    // Loop through each pixel in the image and cast a ray
    for (int y = 0; y < imageHeight; y++)
    {
        for (int x = 0; x < imageWidth; x++)
        {
            // Compute the ray direction for this pixel
            long double fx = (2.0l * (x + 0.5l) / imageWidth - 1.0l) * tan(0.5f * fov * M_PI / 180);
            long double fy = (1.0l - 2.0l * (y + 0.5l) / imageHeight) * tan(0.5f * fov * M_PI / 180) * imageHeight / imageWidth;
            Vector3 rayDir = normalize(cameraDir + fx * cameraUp + fy * cross(cameraDir, cameraUp));

            // Cast the ray and compute the color of the pixel
            long double tSphere, tPlane = 1000000000000000.0l;
            bool hitSphere = intersectRaySphere(cameraPos, rayDir, sphere, tSphere);
            bool hitPlane = intersectRayPlane(Ray(cameraPos, rayDir), planePosition, planeNormal, tPlane);

            // Compute the surface normal and distance to the nearest object
            Vector3 surfaceNormal;
            long double distanceToNearestObject;
            if (hitSphere && (!hitPlane || tSphere < tPlane))
            {
                // The ray hits the sphere
                surfaceNormal = normalize(cameraPos + tSphere * rayDir - sphere.center);
                distanceToNearestObject = tSphere;
            }
            else if (hitPlane)
            {
                // The ray hits the plane
                surfaceNormal = planeNormal;
                distanceToNearestObject = tPlane;
            }
            else
            {
                // The ray misses both objects
                image[y * imageWidth + x] = sf::Color::Black;
                continue;
            }

            // Compute the direction from the intersection point to the light
            Vector3 toLight = normalize(lightPos - (cameraPos + distanceToNearestObject * rayDir));
            sf::Color blendedColor;
            // Check if the intersection point is in shadow
            int shadow = 0;
            Vector3 shadowColor({0, 0, 0});
            Vector3 shadowRayOrigin = cameraPos + (distanceToNearestObject + 0.00000000001) * (rayDir);
            long double tShadow;
            bool hitShadow = intersectRaySphere(shadowRayOrigin, toLight, sphere, tShadow) || intersectRayPlane(Ray(shadowRayOrigin, toLight), planePosition, planeNormal, tShadow);
            if (hitShadow && tShadow < length(toLight - shadowRayOrigin))
            {
                // If the point is in shadow, add the shadow color to the pixel color and skip
                blendedColor.r += shadowColor.x * 255;
                blendedColor.g += shadowColor.y * 255;
                blendedColor.b += shadowColor.z * 255;
                shadow = 1;
                image[y * imageWidth + x] = blendedColor;
                continue;
            }

            // Compute the diffuse component of the color using the Lambertian reflectance model
            long double diffuse = std::max(dot(surfaceNormal, toLight), 0.0l);

            // Compute the specular component of the color using the Blinn-Phong reflectance model
            Vector3 halfway = normalize(toLight - rayDir);
            long double specular = powf(std::max(dot(surfaceNormal, halfway), 0.0l), shininess);

            // Compute the color of the nearest object
            sf::Color nearestObjectColor;
            if (hitSphere && (!hitPlane || tSphere < tPlane))
            {
                // The nearest object is the sphere
                nearestObjectColor = sf::Color(sphereColor.x * 255, sphereColor.y * 255, sphereColor.z * 255);
            }
            else if (hitPlane)
            {
                // The nearest object is the plane
                nearestObjectColor = sf::Color(planeColor.x * 255, planeColor.y * 255, planeColor.z * 255);
            }

            // Compute the blended color of the pixel based on the alpha value of the nearest object color
            long double alpha = 1.0l - expf(-distanceToNearestObject * 0.1l); // Use an exponential function to weight the blending
            blendedColor = sf::Color(
                alpha * nearestObjectColor.r + (1.0l - alpha) * diffuse * 255,
                alpha * nearestObjectColor.g + (1.0l - alpha) * diffuse * 255,
                alpha * nearestObjectColor.b + (1.0l - alpha) * diffuse * 255);

            // Add the specular highlight to the blended color
            blendedColor.r = std::min(blendedColor.r + specular * highlightColor.x * 255, 255.0l);
            blendedColor.g = std::min(blendedColor.g + specular * highlightColor.y * 255, 255.0l);
            blendedColor.b = std::min(blendedColor.b + specular * highlightColor.z * 255, 255.0l);
            // Add the specular highlight to the blended color
            // blendedColor.r = std::min(blendedColor.r + (1 - shadow) * specular * highlightColor.x * 255 + shadow * shadowColor.x * 255, 255.0f);
            // blendedColor.g = std::min(blendedColor.g + (1 - shadow) * specular * highlightColor.y * 255 + shadow * shadowColor.y * 255, 255.0f);
            // blendedColor.b = std::min(blendedColor.b + (1 - shadow) * specular * highlightColor.z * 255 + shadow * shadowColor.z * 255, 255.0f);
            // // Set the color of the pixel
            image[y * imageWidth + x] = blendedColor;
        }
    }

    // Update the texture with the modified image data
    texture.update((sf::Uint8 *)image, imageWidth, imageHeight, 0, 0);

    // Main loop
    while (window.isOpen())
    {
        // Handle events
        sf::Event event;
        while (window.pollEvent(event))
        {
            if (event.type == sf::Event::Closed)
            {
                window.close();
            }
        }

        // Draw the image
        window.clear();
        window.draw(sprite);
        window.display();
    }

    // Free memory
    delete[] image;

    return 0;
}