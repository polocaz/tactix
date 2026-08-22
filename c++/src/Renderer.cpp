#include "Renderer.hpp"
#include "Simulation.hpp"
#include <raylib.h>
#include <algorithm>
#include <cmath>

void drawSimulation(const Simulation& sim, float alpha) {
    // Draw simulation world boundary
    const float borderThickness = 3.0f;
    DrawRectangleLinesEx(
        Rectangle{0, 0, static_cast<float>(sim.worldWidth), static_cast<float>(sim.worldHeight)},
        borderThickness,
        Color{100, 150, 255, 255}
    );

    // Debug: Draw grid
    if (sim.debugGrid) {
        const float cellSize = 50.0f;
        for (int x = 0; x < sim.worldWidth; x += static_cast<int>(cellSize)) {
            DrawLine(x, 0, x, sim.worldHeight, Color{80, 255, 100, 180});
        }
        for (int y = 0; y < sim.worldHeight; y += static_cast<int>(cellSize)) {
            DrawLine(0, y, sim.worldWidth, y, Color{80, 255, 100, 180});
        }
    }

    // Interpolated rendering with directional triangles
    // Triangles show movement direction - useful for AI visualization
    for (size_t i = 0; i < sim.soldiers.count; i++) {
        // clampToWorld only clamps and bounces (it has never wrapped a
        // position), so interpolating from the previous tick's position is
        // always safe here -- no large-delta special case needed.
        const float renderX = sim.prevPosX[i] + (sim.soldiers.posX[i] - sim.prevPosX[i]) * alpha;
        const float renderY = sim.prevPosY[i] + (sim.soldiers.posY[i] - sim.prevPosY[i]) * alpha;

        // Team identity carries in hue, unit type in shape. Reading a battle
        // at zoomed-out scale depends on those being separable at a few pixels.
        const bool teamA = sim.soldiers.team[i] == Team::A;
        Color agentColor = teamA ? Color{ 90, 140, 235, 255 }   // steel blue
                                 : Color{ 210,  95,  70, 255 }; // rust red

        switch (sim.soldiers.unitType[i]) {
            case UnitType::Archer:
                // Slightly lighter, drawn as a small square.
                agentColor.r = (uint8_t)std::min(255, agentColor.r + 45);
                agentColor.g = (uint8_t)std::min(255, agentColor.g + 45);
                agentColor.b = (uint8_t)std::min(255, agentColor.b + 45);
                break;
            case UnitType::Cavalry:
                // Darker and drawn larger.
                agentColor.r = (uint8_t)(agentColor.r * 0.7f);
                agentColor.g = (uint8_t)(agentColor.g * 0.7f);
                agentColor.b = (uint8_t)(agentColor.b * 0.7f);
                break;
            default:
                break;
        }

        // Health reads as brightness, so a worn-down line is visible before it
        // breaks rather than only when it vanishes.
        const uint8_t maxHp = kUnitStats[(int)sim.soldiers.unitType[i]].maxHealth;
        if (maxHp > 1) {
            const float frac = 0.45f + 0.55f * ((float)sim.soldiers.health[i] / (float)maxHp);
            agentColor.r = (uint8_t)(agentColor.r * frac);
            agentColor.g = (uint8_t)(agentColor.g * frac);
            agentColor.b = (uint8_t)(agentColor.b * frac);
        }

        // Cavalry and infantry share the triangle, so the two have to differ
        // in PROPORTION, not just size: at 6px vs 4px they were the same
        // blob two pixels apart and a charge was unreadable. Cavalry is a
        // long narrow dart, infantry a short broad arrowhead. Both stay
        // under the 12px slot spacing so a packed rank does not overlap.
        const bool cavalry = sim.soldiers.unitType[i] == UnitType::Cavalry;
        const float size      = cavalry ? 9.0f : 4.0f;
        const float halfWidth = size * (cavalry ? 0.28f : 0.6f);

        if (sim.soldiers.unitType[i] == UnitType::Archer) {
            DrawRectangleV(Vector2{ renderX - 2.0f, renderY - 2.0f },
                           Vector2{ 4.0f, 4.0f }, agentColor);
        } else {
            // Calculate triangle vertices pointing in direction of movement
            float dx = sim.soldiers.dirX[i];
            float dy = sim.soldiers.dirY[i];

            // Front vertex (pointing forward)
            float frontX = renderX + dx * size;
            float frontY = renderY + dy * size;

            // Perpendicular for base vertices
            float perpX = -dy;
            float perpY = dx;

            // Base vertices, pulled back behind the centre so the whole
            // shape reads as a body with a point rather than a fan.
            float backX = renderX - dx * size * 0.35f;
            float backY = renderY - dy * size * 0.35f;
            float baseLeft_X = backX - perpX * halfWidth;
            float baseLeft_Y = backY - perpY * halfWidth;
            float baseRight_X = backX + perpX * halfWidth;
            float baseRight_Y = backY + perpY * halfWidth;

            DrawTriangle(
                Vector2{frontX, frontY},
                Vector2{baseLeft_X, baseLeft_Y},
                Vector2{baseRight_X, baseRight_Y},
                agentColor
            );
        }
    }

    // Arrows are drawn along their velocity rather than as dots, so a volley
    // reads as direction and not as speckle.
    for (size_t i = 0; i < sim.projectiles.count; ++i) {
        const float vx = sim.projectiles.velX[i];
        const float vy = sim.projectiles.velY[i];
        const float len = std::sqrt(vx * vx + vy * vy);
        if (len < 1e-4f) continue;
        const float sx = sim.projectiles.posX[i];
        const float sy = sim.projectiles.posY[i];
        const float ex = sx - (vx / len) * 6.0f;
        const float ey = sy - (vy / len) * 6.0f;
        DrawLineV(Vector2{sx, sy}, Vector2{ex, ey}, Color{235, 225, 190, 255});
    }

    // Draw buildings
    for (const auto& building : sim.terrain.buildings) {
        DrawRectangle(
            static_cast<int>(building.x),
            static_cast<int>(building.y),
            static_cast<int>(building.width),
            static_cast<int>(building.height),
            Color{80, 80, 90, 255}  // Dark gray
        );
        // Outline
        DrawRectangleLines(
            static_cast<int>(building.x),
            static_cast<int>(building.y),
            static_cast<int>(building.width),
            static_cast<int>(building.height),
            Color{60, 60, 70, 255}
        );
    }

    // Draw trees
    for (const auto& tree : sim.terrain.trees) {
        DrawCircle(
            static_cast<int>(tree.x),
            static_cast<int>(tree.y),
            tree.radius,
            Color{40, 120, 40, 255}  // Forest green
        );
        // Darker center for depth
        DrawCircle(
            static_cast<int>(tree.x),
            static_cast<int>(tree.y),
            tree.radius * 0.6f,
            Color{30, 90, 30, 255}
        );
    }
}
