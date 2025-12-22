#include <iostream>
#include <vector>
#include <cmath>
#include <chrono>

struct Vector3 {
    double x, y, z;

    Vector3(double x, double y, double z) : x(x), y(y), z(z) {}

    static Vector3 add(const Vector3& a, const Vector3& b) {
        return Vector3(a.x + b.x, a.y + b.y, a.z + b.z);
    }

    static Vector3 sub(const Vector3& a, const Vector3& b) {
        return Vector3(a.x - b.x, a.y - b.y, a.z - b.z);
    }

    static Vector3 mul(const Vector3& a, double b) {
        return Vector3(a.x * b, a.y * b, a.z * b);
    }

    static double dot(const Vector3& a, const Vector3& b) {
        return a.x * b.x + a.y * b.y + a.z * b.z;
    }

    static Vector3 cross(const Vector3& a, const Vector3& b) {
        return Vector3(
            a.y * b.z - a.z * b.y,
            a.z * b.x - a.x * b.z,
            a.x * b.y - a.y * b.x
        );
    }

    static Vector3 normalize(const Vector3& a) {
        double len = std::sqrt(dot(a, a));
        return mul(a, 1.0 / len);
    }
};

struct Ray {
    Vector3 origin;
    Vector3 direction;

    Ray(const Vector3& origin, const Vector3& direction) : origin(origin), direction(direction) {}
};

class Sphere {
public:
    Vector3 center;
    double radius;
    Vector3 color;

    Sphere(Vector3 center, double radius, Vector3 color) 
        : center(center), radius(radius), color(color) {}

    double intersect(const Ray& ray) const {
        Vector3 oc = Vector3::sub(ray.origin, center);
        double a = Vector3::dot(ray.direction, ray.direction);
        double b = 2.0 * Vector3::dot(oc, ray.direction);
        double c = Vector3::dot(oc, oc) - radius * radius;
        double discriminant = b * b - 4.0 * a * c;

        if (discriminant < 0) {
            return -1.0;
        } else {
            return (-b - std::sqrt(discriminant)) / (2.0 * a);
        }
    }
};

double render() {
    const int width = 200;
    const int height = 100;
    std::vector<Sphere> spheres = {
        Sphere(Vector3(0, 0, -5), 1, Vector3(1, 0, 0)),
        Sphere(Vector3(2, 0, -5), 1, Vector3(0, 1, 0)),
        Sphere(Vector3(-2, 0, -5), 1, Vector3(0, 0, 1)),
        Sphere(Vector3(0, -1001, -5), 1000, Vector3(0.5, 0.5, 0.5))
    };

    Vector3 origin(0, 0, 0);
    double checksum = 0;

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            double u = (double(x) - width / 2.0) / width;
            double v = (double(y) - height / 2.0) / height;
            Vector3 dir = Vector3::normalize(Vector3(u, v, -1));
            Ray ray(origin, dir);

            double closestT = 1000000;
            const Sphere* hitSphere = nullptr;

            for (const auto& sphere : spheres) {
                double t = sphere.intersect(ray);
                if (t > 0 && t < closestT) {
                    closestT = t;
                    hitSphere = &sphere;
                }
            }

            if (hitSphere) {
                checksum += hitSphere->color.x + hitSphere->color.y + hitSphere->color.z;
            }
        }
    }
    return checksum;
}

int main() {
    const int iterations = 100;
    std::cout << "Starting C++ Ray Tracer Benchmark..." << std::endl;

    // Warmup
    for (int i = 0; i < 2; i++) {
        render();
    }

    auto start = std::chrono::high_resolution_clock::now();
    double totalChecksum = 0;
    for (int i = 0; i < iterations; i++) {
        totalChecksum += render();
    }
    auto end = std::chrono::high_resolution_clock::now();

    std::chrono::duration<double, std::milli> elapsed = end - start;

    std::cout << "Benchmark Complete." << std::endl;
    std::cout << "Total Checksum: " << totalChecksum << std::endl;
    std::cout << "Average Time: " << elapsed.count() / iterations << "ms" << std::endl;

    return 0;
}
