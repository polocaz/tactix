#include "raylib.h"
#include "rlImGui.h"
#include "imgui.h"
#include "spdlog/spdlog.h"

#include "Simulation.hpp"

int main() {
    // 1. Setup Window
    const int screenWidth = 1280;
    const int screenHeight = 720;
    
    spdlog::info("Initializing Tactix Engine...");
    InitWindow(screenWidth, screenHeight, "Tactix - Zombie Infection Sim");
    SetTargetFPS(60);

    // 2. Setup ImGui (via rlImGui bridge)
    rlImGuiSetup(true);

    Simulation sim(screenWidth, screenHeight);
    size_t agentCount = 300;
    sim.init(agentCount);

    float speedMultiplier = 1.0f;

    // --- Main Loop ---
    while (!WindowShouldClose()) {
        float dt = GetFrameTime() * speedMultiplier;

        sim.update(dt);

        // ----------- DRAW -----------
        BeginDrawing();
        ClearBackground(BLACK);

        sim.draw();

        // ----------- IMGUI -----------
        rlImGuiBegin();

        ImGui::Begin("Phase 1 Controls");
        ImGui::SliderFloat("Speed Multiplier", &speedMultiplier, 0.1f, 50.0f);
        ImGui::Text("Agents: %zu", agentCount);
        ImGui::Separator();
        ImGui::Text("FPS: %d", GetFPS());
        ImGui::Text("Frame Time: %.3f ms", GetFrameTime() * 1000.0f);
        ImGui::End();

        rlImGuiEnd();

        EndDrawing();
    }

    // 3. Cleanup
    rlImGuiShutdown();
    CloseWindow();
    
    spdlog::info("Tactix Engine Shutdown Cleanly.");
    return 0;
}