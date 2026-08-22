#pragma once
#include "Units.hpp"
#include "Formation.hpp"
#include <vector>
#include <cmath>
#include <cstdint>

// Obstacle geometry shared by Simulation (spawn clearing, per-soldier
// collision) and squad decision code (tactical anchors, lane scoring).
//
// Design §4.1: Simulation keeps ownership of generation and world lifecycle;
// this helper owns only geometry operations, so squad code can score terrain
// without depending on all of Simulation or duplicating obstacle definitions.
struct Building {
    float x, y, width, height;
};

struct Tree {
    float x, y, radius;
};

struct TerrainField {
    std::vector<Building> buildings;
    std::vector<Tree>     trees;

    // True if p lies inside any building or tree (raw occupancy, no standoff).
    // Preserves Simulation::insideAnyObstacle behavior exactly (a move of
    // responsibility, not a retune -- design §4.1).
    bool insideAnyObstacle(Vec2 p) const {
        for (const auto& b : buildings) {
            if (p.x > b.x && p.x < b.x + b.width &&
                p.y > b.y && p.y < b.y + b.height) {
                return true;
            }
        }
        for (const auto& t : trees) {
            const float dx = p.x - t.x;
            const float dy = p.y - t.y;
            if (dx * dx + dy * dy < t.radius * t.radius) return true;
        }
        return false;
    }

    // Nearest point to p a soldier can actually stand on: clear of every
    // building and tree by kObstacleStandoff. Preserves
    // Simulation::clearOfObstacles behavior exactly (design §4.1).
    Vec2 clearOfObstacles(Vec2 p) const {
        const float margin = kObstacleStandoff;
        // Repeated to a fixed point, capped at 4 passes. generateObstacles
        // keeps obstacles kObstacleStandoff apart, so one pass is enough and
        // the loop exits on the first unchanged pass; the repetition lets a
        // hand-built or future overlapping layout degrade to "a few passes"
        // instead of leaving a soldier in a wall. The cap bounds the work.
        for (int pass = 0; pass < 4; ++pass) {
            const Vec2 before = p;

            for (const auto& b : buildings) {
                const float minX = b.x - margin;
                const float maxX = b.x + b.width + margin;
                const float minY = b.y - margin;
                const float maxY = b.y + b.height + margin;
                if (p.x <= minX || p.x >= maxX || p.y <= minY || p.y >= maxY) continue;

                const float dLeft   = p.x - minX;
                const float dRight  = maxX - p.x;
                const float dTop    = p.y - minY;
                const float dBottom = maxY - p.y;
                const float nearest = std::min(std::min(dLeft, dRight), std::min(dTop, dBottom));
                if      (nearest == dLeft)  p.x = minX;
                else if (nearest == dRight) p.x = maxX;
                else if (nearest == dTop)   p.y = minY;
                else                        p.y = maxY;
            }

            for (const auto& t : trees) {
                const float r = t.radius + margin;
                const float dx = p.x - t.x;
                const float dy = p.y - t.y;
                const float distSq = dx * dx + dy * dy;
                if (distSq >= r * r) continue;
                if (distSq < 1e-6f) {
                    p.x = t.x + r;
                    continue;
                }
                const float dist = std::sqrt(distSq);
                p.x = t.x + (dx / dist) * r;
                p.y = t.y + (dy / dist) * r;
            }

            if (p.x == before.x && p.y == before.y) break;
        }
        return p;
    }

    // True if the straight lane a->b crosses terrain expanded by
    // kObstacleStandoff. Squad-level scoring input only (design §4.2); not
    // used for per-soldier movement. Deterministic and order-independent.
    bool segmentBlocked(Vec2 a, Vec2 b) const {
        for (const auto& bl : buildings) {
            if (segmentHitsExpandedRect(a, b, bl, kObstacleStandoff)) return true;
        }
        for (const auto& t : trees) {
            if (segmentHitsExpandedCircle(a, b, t.x, t.y, t.radius + kObstacleStandoff)) {
                return true;
            }
        }
        return false;
    }

    // Signed distance from p to the nearest obstacle boundary: negative (or
    // zero) when p is inside an obstacle, positive in open ground. A scoring
    // input, not a physics value (design §4.3). Min over obstacles so the
    // closest one dominates.
    float clearanceAt(Vec2 p) const {
        float best = 1e30f;
        for (const auto& b : buildings) {
            best = std::min(best, signedBoxDistance(p, b));
        }
        for (const auto& t : trees) {
            const float d = std::sqrt((p.x - t.x) * (p.x - t.x) +
                                      (p.y - t.y) * (p.y - t.y)) - t.radius;
            best = std::min(best, d);
        }
        return best;
    }

    // Does the lane a->b pass within `margin` of a building/tree (their raw
    // shape expanded by `margin`)? Used by squad candidate generation to pick
    // which obstacles the direct lane actually grazes, so only those contribute
    // anchors (design §6.2/§6.3), bounding the candidate count. Deterministic.
    bool laneNearBuilding(Vec2 a, Vec2 b, const Building& bl, float margin) const {
        return segmentHitsExpandedRect(a, b, bl, margin);
    }
    bool laneNearTree(Vec2 a, Vec2 b, const Tree& t, float margin) const {
        return segmentHitsExpandedCircle(a, b, t.x, t.y, t.radius + margin);
    }

private:
    // Signed distance to an axis-aligned box [x, x+width] x [y, y+height]:
    // negative inside, positive outside (distance to the nearest edge).
    static float signedBoxDistance(Vec2 p, const Building& b) {
        const float dx = std::max(b.x - p.x, p.x - (b.x + b.width));
        const float dy = std::max(b.y - p.y, p.y - (b.y + b.height));
        if (dx <= 0.0f && dy <= 0.0f) {
            return std::max(dx, dy);  // inside: negative
        }
        const float ox = (dx > 0.0f) ? dx : 0.0f;
        const float oy = (dy > 0.0f) ? dy : 0.0f;
        return std::sqrt(ox * ox + oy * oy);
    }

    // Segment vs AABB expanded by `m` on every side (slab method).
    static bool segmentHitsExpandedRect(Vec2 a, Vec2 b, const Building& bl, float m) {
        const float minX = bl.x - m, maxX = bl.x + bl.width + m;
        const float minY = bl.y - m, maxY = bl.y + bl.height + m;
        const float dx = b.x - a.x;
        const float dy = b.y - a.y;

        float tmin = 0.0f, tmax = 1.0f;

        // X slab.
        if (std::abs(dx) < 1e-9f) {
            if (a.x < minX || a.x > maxX) return false;
        } else {
            float t1 = (minX - a.x) / dx;
            float t2 = (maxX - a.x) / dx;
            if (t1 > t2) { float tmp = t1; t1 = t2; t2 = tmp; }
            tmin = std::max(tmin, t1);
            tmax = std::min(tmax, t2);
            if (tmin > tmax) return false;
        }

        // Y slab.
        if (std::abs(dy) < 1e-9f) {
            if (a.y < minY || a.y > maxY) return false;
        } else {
            float t1 = (minY - a.y) / dy;
            float t2 = (maxY - a.y) / dy;
            if (t1 > t2) { float tmp = t1; t1 = t2; t2 = tmp; }
            tmin = std::max(tmin, t1);
            tmax = std::min(tmax, t2);
            if (tmin > tmax) return false;
        }

        return tmax >= tmin && tmax >= 0.0f && tmin <= 1.0f;
    }

    // Segment vs circle of radius `r` (closest point on segment to center).
    static bool segmentHitsExpandedCircle(Vec2 a, Vec2 b, float cx, float cy, float r) {
        const float dx = b.x - a.x;
        const float dy = b.y - a.y;
        const float lenSq = dx * dx + dy * dy;
        float t = 0.0f;
        if (lenSq > 1e-12f) {
            t = ((cx - a.x) * dx + (cy - a.y) * dy) / lenSq;
            if (t < 0.0f) t = 0.0f;
            else if (t > 1.0f) t = 1.0f;
        }
        const float px = a.x + dx * t;
        const float py = a.y + dy * t;
        const float ex = px - cx;
        const float ey = py - cy;
        return (ex * ex + ey * ey) <= r * r;
    }
};
