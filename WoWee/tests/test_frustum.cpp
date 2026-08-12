// Frustum plane extraction and intersection tests
#include <catch_amalgamated.hpp>
#include "rendering/frustum.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

using wowee::rendering::Frustum;
using wowee::rendering::Plane;

TEST_CASE("Plane distanceToPoint", "[frustum]") {
    // Plane facing +Y at y=5
    Plane p(glm::vec3(0.0f, 1.0f, 0.0f), -5.0f);

    // Point at y=10 → distance = 10 + (-5) = 5 (in front)
    REQUIRE(p.distanceToPoint(glm::vec3(0, 10, 0)) == Catch::Approx(5.0f));

    // Point at y=5 → distance = 0 (on plane)
    REQUIRE(p.distanceToPoint(glm::vec3(0, 5, 0)) == Catch::Approx(0.0f));

    // Point at y=0 → distance = -5 (behind)
    REQUIRE(p.distanceToPoint(glm::vec3(0, 0, 0)) == Catch::Approx(-5.0f));
}

TEST_CASE("Frustum extractFromMatrix with identity", "[frustum]") {
    Frustum f;
    f.extractFromMatrix(glm::mat4(1.0f));

    // Identity matrix gives clip-space frustum: [-1,1]^3 (or [0,1] for z)
    // The origin should be inside
    REQUIRE(f.containsPoint(glm::vec3(0.0f, 0.0f, 0.5f)));
}

TEST_CASE("Frustum containsPoint perspective", "[frustum]") {
    // Create a typical perspective projection and look-at
    glm::mat4 proj = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 100.0f);
    glm::mat4 view = glm::lookAt(
        glm::vec3(0, 0, 0),   // eye
        glm::vec3(0, 0, -1),  // center (looking -Z)
        glm::vec3(0, 1, 0)    // up
    );
    glm::mat4 vp = proj * view;

    Frustum f;
    f.extractFromMatrix(vp);

    // Point in front (at -Z)
    REQUIRE(f.containsPoint(glm::vec3(0, 0, -10)));

    // Point behind camera (at +Z) should be outside
    REQUIRE_FALSE(f.containsPoint(glm::vec3(0, 0, 10)));

    // Point very far away inside the frustum
    REQUIRE(f.containsPoint(glm::vec3(0, 0, -50)));

    // Point beyond far plane
    REQUIRE_FALSE(f.containsPoint(glm::vec3(0, 0, -200)));
}

TEST_CASE("Frustum intersectsSphere", "[frustum]") {
    glm::mat4 proj = glm::perspective(glm::radians(60.0f), 1.0f, 1.0f, 100.0f);
    glm::mat4 view = glm::lookAt(
        glm::vec3(0, 0, 0),
        glm::vec3(0, 0, -1),
        glm::vec3(0, 1, 0)
    );
    Frustum f;
    f.extractFromMatrix(proj * view);

    // Sphere clearly inside
    REQUIRE(f.intersectsSphere(glm::vec3(0, 0, -10), 1.0f));

    // Sphere behind camera
    REQUIRE_FALSE(f.intersectsSphere(glm::vec3(0, 0, 50), 1.0f));

    // Large sphere that straddles the near plane
    REQUIRE(f.intersectsSphere(glm::vec3(0, 0, 0), 5.0f));

    // Sphere at edge of frustum — large radius should still intersect
    REQUIRE(f.intersectsSphere(glm::vec3(0, 0, -105), 10.0f));
}

TEST_CASE("Frustum intersectsAABB", "[frustum]") {
    glm::mat4 proj = glm::perspective(glm::radians(60.0f), 1.0f, 1.0f, 100.0f);
    glm::mat4 view = glm::lookAt(
        glm::vec3(0, 0, 0),
        glm::vec3(0, 0, -1),
        glm::vec3(0, 1, 0)
    );
    Frustum f;
    f.extractFromMatrix(proj * view);

    // Box inside frustum
    REQUIRE(f.intersectsAABB(glm::vec3(-1, -1, -11), glm::vec3(1, 1, -9)));

    // Box behind camera
    REQUIRE_FALSE(f.intersectsAABB(glm::vec3(-1, -1, 5), glm::vec3(1, 1, 10)));

    // Box beyond far plane
    REQUIRE_FALSE(f.intersectsAABB(glm::vec3(-1, -1, -200), glm::vec3(1, 1, -150)));

    // Large box straddling near/far
    REQUIRE(f.intersectsAABB(glm::vec3(-5, -5, -50), glm::vec3(5, 5, 0)));
}

TEST_CASE("Frustum getPlane returns 6 planes", "[frustum]") {
    glm::mat4 proj = glm::perspective(glm::radians(90.0f), 1.0f, 1.0f, 100.0f);
    Frustum f;
    f.extractFromMatrix(proj);

    // Access all 6 planes — should not crash
    for (int i = 0; i < 6; ++i) {
        const auto& p = f.getPlane(static_cast<Frustum::Side>(i));
        // Normal should be a unit vector (after normalization)
        float len = glm::length(p.normal);
        REQUIRE(len == Catch::Approx(1.0f).margin(0.01f));
    }
}

TEST_CASE("Frustum box far right is outside", "[frustum]") {
    glm::mat4 proj = glm::perspective(glm::radians(45.0f), 1.0f, 1.0f, 100.0f);
    glm::mat4 view = glm::lookAt(
        glm::vec3(0, 0, 0),
        glm::vec3(0, 0, -1),
        glm::vec3(0, 1, 0)
    );
    Frustum f;
    f.extractFromMatrix(proj * view);

    // Box far off to the right — outside the frustum
    REQUIRE_FALSE(f.intersectsAABB(glm::vec3(200, 0, -10), glm::vec3(201, 1, -9)));
}
