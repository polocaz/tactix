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

    // Draw graveyard
    DrawRectangle(
        static_cast<int>(sim.graveyard.x),
        static_cast<int>(sim.graveyard.y),
        static_cast<int>(sim.graveyard.width),
        static_cast<int>(sim.graveyard.height),
        Color{40, 35, 45, 255}  // Dark purple-gray
    );
    // Tombstones
    for (int i = 0; i < 8; i++) {
        float tx = sim.graveyard.x + 30 + (i % 3) * 60;
        float ty = sim.graveyard.y + 40 + (i / 3) * 60;
        DrawRectangle(static_cast<int>(tx), static_cast<int>(ty), 20, 30, Color{80, 75, 85, 255});
        DrawRectangle(static_cast<int>(tx + 5), static_cast<int>(ty - 5), 10, 10, Color{90, 85, 95, 255});
    }
    DrawText("GRAVEYARD", static_cast<int>(sim.graveyard.x + 50), static_cast<int>(sim.graveyard.y + 10), 16, Color{120, 110, 130, 255});

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

        // Color based on agent type and state
        Color agentColor;
        if (sim.entities.state[i] == AgentState::Dead) {
            // Corpses are dark red/brown
            agentColor = Color{120, 40, 40, 255};
        } else if (sim.entities.state[i] == AgentState::Bitten) {
            // Bitten civilians - color shifts from white → yellow → sickly green
            float progress = sim.entities.infectionProgress[i];
            uint8_t r = static_cast<uint8_t>(220 - progress * 70);   // 220 → 150
            uint8_t g = static_cast<uint8_t>(220 - progress * 20);   // 220 → 200
            uint8_t b = static_cast<uint8_t>(220 - progress * 120);  // 220 → 100
            agentColor = Color{r, g, b, 255};
        } else if (sim.entities.type[i] == AgentType::Civilian) {
            agentColor = Color{220, 220, 220, 255};  // Light gray/white
        } else if (sim.entities.type[i] == AgentType::Zombie) {
            agentColor = Color{50, 200, 50, 255};     // Green
        } else {  // Hero
            // Color heroes based on health (blue gradient)
            uint8_t health = sim.entities.health[i];
            uint8_t brightness = 100 + (health * 30);  // Brighter with more health
            agentColor = Color{50, 100, brightness, 255};
        }

        // Corpses are rendered as small circles instead of triangles
        if (sim.entities.state[i] == AgentState::Dead) {
            DrawCircle(static_cast<int>(renderX), static_cast<int>(renderY), agentSize * 0.8f, agentColor);
        } else {
            DrawTriangle(
                Vector2{frontX, frontY},
                Vector2{baseLeft_X, baseLeft_Y},
                Vector2{baseRight_X, baseRight_Y},
                agentColor
            );
        }
    }

    // Draw gunshot lines (visualize shooting)
    for (const auto& line : sim.gunshotLines) {
        // Fade based on lifetime (0.15s total)
        float alpha_val = line.lifetime / 0.15f;
        uint8_t alpha_byte = static_cast<uint8_t>(alpha_val * 255.0f);
        DrawLineEx(
            Vector2{line.fromX, line.fromY},
            Vector2{line.toX, line.toY},
            0.8f,  // Thin line
            Color{255, 255, 0, alpha_byte}  // Bright yellow, fading
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
