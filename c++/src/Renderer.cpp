#include "Renderer.hpp"
#include "Simulation.hpp"
#include <raylib.h>
#include <cmath>
#include <algorithm>

void drawSimulation(const Simulation& sim, float alpha) {
    // Draw simulation world boundary
    const float borderThickness = 3.0f;
    DrawRectangleLinesEx(
        Rectangle{0, 0, static_cast<float>(sim.screenWidth), static_cast<float>(sim.screenHeight)},
        borderThickness,
        Color{100, 150, 255, 255}
    );

    // Debug: Draw grid
    if (sim.debugGrid) {
        const float cellSize = 50.0f;
        for (int x = 0; x < sim.screenWidth; x += static_cast<int>(cellSize)) {
            DrawLine(x, 0, x, sim.screenHeight, Color{80, 255, 100, 180});
        }
        for (int y = 0; y < sim.screenHeight; y += static_cast<int>(cellSize)) {
            DrawLine(0, y, sim.screenWidth, y, Color{80, 255, 100, 180});
        }
    }

    // Interpolated rendering with directional triangles
    // Triangles show movement direction - useful for AI visualization
    const float wrapThreshold = static_cast<float>(sim.screenWidth) * 0.5f;  // Detect wrapping

    for (size_t i = 0; i < sim.entities.count; i++) {
        // Check if agent wrapped this frame (large position delta)
        float deltaX = std::abs(sim.entities.posX[i] - sim.prevPosX[i]);
        float deltaY = std::abs(sim.entities.posY[i] - sim.prevPosY[i]);

        // If wrapped, don't interpolate (use current position to avoid stretching)
        float renderX, renderY;
        if (deltaX > wrapThreshold || deltaY > wrapThreshold) {
            renderX = sim.entities.posX[i];
            renderY = sim.entities.posY[i];
        } else {
            renderX = sim.prevPosX[i] + (sim.entities.posX[i] - sim.prevPosX[i]) * alpha;
            renderY = sim.prevPosY[i] + (sim.entities.posY[i] - sim.prevPosY[i]) * alpha;
        }

        // Team identity carries in hue, unit type in shape. Reading a battle
        // at zoomed-out scale depends on those being separable at a few pixels.
        const bool teamA = sim.entities.team[i] == Team::A;
        Color agentColor = teamA ? Color{ 90, 140, 235, 255 }   // steel blue
                                 : Color{ 210,  95,  70, 255 }; // rust red

        switch (sim.entities.unitType[i]) {
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

        const float size = (sim.entities.unitType[i] == UnitType::Cavalry) ? 6.0f : 4.0f;

        if (sim.entities.unitType[i] == UnitType::Archer) {
            DrawRectangleV(Vector2{ renderX - 2.0f, renderY - 2.0f },
                           Vector2{ 4.0f, 4.0f }, agentColor);
        } else {
            // Calculate triangle vertices pointing in direction of movement
            float dx = sim.entities.dirX[i];
            float dy = sim.entities.dirY[i];

            // Front vertex (pointing forward)
            float frontX = renderX + dx * size;
            float frontY = renderY + dy * size;

            // Perpendicular for base vertices
            float perpX = -dy;
            float perpY = dx;

            // Base vertices
            float baseLeft_X = renderX - perpX * (size * 0.4f);
            float baseLeft_Y = renderY - perpY * (size * 0.4f);
            float baseRight_X = renderX + perpX * (size * 0.4f);
            float baseRight_Y = renderY + perpY * (size * 0.4f);

            DrawTriangle(
                Vector2{frontX, frontY},
                Vector2{baseLeft_X, baseLeft_Y},
                Vector2{baseRight_X, baseRight_Y},
                agentColor
            );
        }
    }

    // Draw buildings
    for (const auto& building : sim.buildings) {
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
    for (const auto& tree : sim.trees) {
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
