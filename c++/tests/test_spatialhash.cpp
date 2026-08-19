#include <doctest/doctest.h>
#include "SpatialHash.hpp"
#include <algorithm>

TEST_CASE("inserted entity is returned by a query at its own position") {
    SpatialHash grid(1280.0f, 720.0f, 50.0f);
    grid.clear();
    grid.insert(7u, 100.0f, 100.0f);

    std::vector<uint32_t> out;
    grid.queryNeighbors(100.0f, 100.0f, 25.0f, out);

    CHECK(std::find(out.begin(), out.end(), 7u) != out.end());
}

TEST_CASE("query returns only the 3x3 neighbourhood, not the whole world") {
    SpatialHash grid(1280.0f, 720.0f, 50.0f);
    grid.clear();
    grid.insert(1u, 100.0f, 100.0f);
    grid.insert(2u, 1000.0f, 600.0f);   // far away, outside the 3x3 block

    std::vector<uint32_t> out;
    grid.queryNeighbors(100.0f, 100.0f, 25.0f, out);

    CHECK(std::find(out.begin(), out.end(), 1u) != out.end());
    CHECK(std::find(out.begin(), out.end(), 2u) == out.end());
}

TEST_CASE("clear empties every cell") {
    SpatialHash grid(1280.0f, 720.0f, 50.0f);
    grid.insert(1u, 100.0f, 100.0f);
    grid.clear();

    std::vector<uint32_t> out;
    grid.queryNeighbors(100.0f, 100.0f, 25.0f, out);

    CHECK(out.empty());
    CHECK(grid.getMaxOccupancy() == 0u);
}

TEST_CASE("out-of-bounds positions clamp instead of reading out of range") {
    SpatialHash grid(1280.0f, 720.0f, 50.0f);
    grid.clear();
    grid.insert(3u, -500.0f, -500.0f);
    grid.insert(4u, 99999.0f, 99999.0f);

    std::vector<uint32_t> lowCorner;
    grid.queryNeighbors(0.0f, 0.0f, 25.0f, lowCorner);
    CHECK(std::find(lowCorner.begin(), lowCorner.end(), 3u) != lowCorner.end());

    std::vector<uint32_t> highCorner;
    grid.queryNeighbors(1279.0f, 719.0f, 25.0f, highCorner);
    CHECK(std::find(highCorner.begin(), highCorner.end(), 4u) != highCorner.end());
}
