#include "Renderer.hpp"
#include "Simulation.hpp"
#include <raylib.h>
#include <cmath>

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
    const float agentSize = 4.0f;
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

        // Calculate triangle vertices pointing in direction of movement
        float dx = sim.entities.dirX[i];
        float dy = sim.entities.dirY[i];

        // Front vertex (pointing forward)
        float frontX = renderX + dx * agentSize;
        float frontY = renderY + dy * agentSize;

        // Perpendicular for base vertices
        float perpX = -dy;
        float perpY = dx;

        // Base vertices
        float baseLeft_X = renderX - perpX * (agentSize * 0.4f);
        float baseLeft_Y = renderY - perpY * (agentSize * 0.4f);
        float baseRight_X = renderX + perpX * (agentSize * 0.4f);
        float baseRight_Y = renderY + perpY * (agentSize * 0.4f);

        // Every agent draws as one color for now; Task 11 restores real visuals.
        Color agentColor = Color{200, 200, 200, 255};

        DrawTriangle(
            Vector2{frontX, frontY},
            Vector2{baseLeft_X, baseLeft_Y},
            Vector2{baseRight_X, baseRight_Y},
            agentColor
        );
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
