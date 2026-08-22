#include <doctest/doctest.h>
#include "Terrain.hpp"

namespace {
TerrainField makeField() {
    TerrainField t;
    // One building: x=100,y=100,w=80,h=80  -> spans [100,180]x[100,180]
    t.buildings.push_back({100.0f, 100.0f, 80.0f, 80.0f});
    // One tree at (500,300) radius 20
    t.trees.push_back({500.0f, 300.0f, 20.0f});
    return t;
}
} // namespace

TEST_CASE("insideAnyObstacle detects a point inside a building") {
    TerrainField t = makeField();
    CHECK(t.insideAnyObstacle({140.0f, 140.0f}));
    CHECK_FALSE(t.insideAnyObstacle({10.0f, 10.0f}));
}

TEST_CASE("insideAnyObstacle detects a point inside a tree") {
    TerrainField t = makeField();
    CHECK(t.insideAnyObstacle({500.0f, 300.0f}));
    CHECK(t.insideAnyObstacle({510.0f, 300.0f}));
    CHECK_FALSE(t.insideAnyObstacle({540.0f, 300.0f}));
}

TEST_CASE("clearOfObstacles returns a point outside obstacles") {
    TerrainField t = makeField();
    const Vec2 inside{140.0f, 140.0f};
    CHECK(t.insideAnyObstacle(inside));
    const Vec2 clear = t.clearOfObstacles(inside);
    CHECK_FALSE(t.insideAnyObstacle(clear));
}

TEST_CASE("clearOfObstacles moves a standoff-boundary point outside") {
    TerrainField t = makeField();
    const float standoff = kObstacleStandoff;
    const Vec2 near{100.0f - standoff + 1.0f, 140.0f};
    const Vec2 clear = t.clearOfObstacles(near);
    CHECK_FALSE(t.insideAnyObstacle(clear));
}

TEST_CASE("segmentBlocked flags a lane crossing an expanded building") {
    TerrainField t = makeField();
    CHECK(t.segmentBlocked({0.0f, 140.0f}, {300.0f, 140.0f}));
}

TEST_CASE("segmentBlocked does not flag a lane tangent outside standoff") {
    TerrainField t = makeField();
    const float top = 100.0f + 80.0f + kObstacleStandoff;
    CHECK_FALSE(t.segmentBlocked({0.0f, top + 10.0f}, {300.0f, top + 10.0f}));
}

TEST_CASE("segmentBlocked flags a lane crossing an expanded tree") {
    TerrainField t = makeField();
    CHECK(t.segmentBlocked({500.0f, 250.0f}, {500.0f, 350.0f}));
}

TEST_CASE("clearanceAt is negative inside and positive in open ground") {
    TerrainField t = makeField();
    CHECK(t.clearanceAt({500.0f, 300.0f}) <= 0.0f);
    CHECK(t.clearanceAt({140.0f, 140.0f}) <= 0.0f);
    CHECK(t.clearanceAt({50.0f, 50.0f}) > 0.0f);
}
