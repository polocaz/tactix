#include "raylib.h"
#include "rlImGui.h"
#include "imgui.h"
#include "spdlog/spdlog.h"
#include <chrono>

#include "Simulation.hpp"

int main() {
    // 1. Setup Window
    const int screenWidth = 1280;
    const int screenHeight = 720;
    
    spdlog::info("Initializing Tactix Engine...");
    InitWindow(screenWidth, screenHeight, "Tactix - High-Performance Agent Simulation");
    SetTargetFPS(144);  // Render at high FPS, simulation runs at fixed 60 TPS

    // 2. Setup ImGui (via rlImGui bridge)
    rlImGuiSetup(true);

    Simulation sim(screenWidth, screenHeight);
    const size_t agentCount = 5000;  // Phase 2 target
    sim.init(agentCount);

    // Fixed timestep accumulator (Design Doc §1.1)
    const float FIXED_DT = 1.0f / 60.0f;  // 60 ticks per second
    float accumulator = 0.0f;
    auto lastTime = std::chrono::steady_clock::now();

    // Metrics
    float tickTimes[60] = {0};  // Rolling window for tick time
    int tickTimeIndex = 0;
    float lastTickTime = 0.0f;
    int tickCount = 0;

    spdlog::info("Starting simulation with {} agents", agentCount);

    // --- Main Loop ---
    while (!WindowShouldClose()) {
        auto currentTime = std::chrono::steady_clock::now();
        float frameTime = std::chrono::duration<float>(currentTime - lastTime).count();
        lastTime = currentTime;

        accumulator += frameTime;

        // Fixed timestep simulation loop
        while (accumulator >= FIXED_DT) {
            auto tickStart = std::chrono::steady_clock::now();
            
            sim.tick(FIXED_DT);
            tickCount++;
            
            auto tickEnd = std::chrono::steady_clock::now();
            lastTickTime = std::chrono::duration<float>(tickEnd - tickStart).count() * 1000.0f;  // ms
            tickTimes[tickTimeIndex] = lastTickTime;
            tickTimeIndex = (tickTimeIndex + 1) % 60;
            
            accumulator -= FIXED_DT;
        }

        // Interpolation alpha for smooth rendering
        float alpha = accumulator / FIXED_DT;

        // ----------- DRAW -----------
        BeginDrawing();
        ClearBackground(Color{15, 15, 20, 255});

        sim.draw(alpha);

        // ----------- IMGUI -----------
        rlImGuiBegin();

        ImGui::Begin("Tactix - Phase 2 Metrics");
        ImGui::Text("Agents: %zu", agentCount);
        ImGui::Separator();
        
        ImGui::Text("Render FPS: %d", GetFPS());
        ImGui::Text("Simulation TPS: 60 (fixed)");
        ImGui::Text("Total Ticks: %d", tickCount);
        ImGui::Separator();
        
        ImGui::Text("Last Tick: %.3f ms", lastTickTime);
        
        // Calculate average tick time
        float avgTickTime = 0.0f;
        for (int i = 0; i < 60; i++) avgTickTime += tickTimes[i];
        avgTickTime /= 60.0f;
        ImGui::Text("Avg Tick (60): %.3f ms", avgTickTime);
        
        // Performance bar
        float tickBudget = FIXED_DT * 1000.0f;  // 16.66 ms
        float tickPercent = (avgTickTime / tickBudget) * 100.0f;
        ImGui::ProgressBar(avgTickTime / tickBudget, ImVec2(-1, 0), 
                          TextFormat("%.1f%% of budget", tickPercent));
        
        if (avgTickTime < 7.5f) {
            ImGui::TextColored(ImVec4(0, 1, 0, 1), "✓ Phase 2 Target: < 7.5ms");
        } else {
            ImGui::TextColored(ImVec4(1, 0, 0, 1), "✗ Exceeds Phase 2 target");
        }
        
        ImGui::Separator();
        ImGui::Text("Spatial Hash: %.3f ms", sim.getLastSpatialHashTime());
        ImGui::Text("Max Cell Occupancy: %u", sim.getMaxCellOccupancy());
        
        if (ImGui::Button(sim.isDebugGridEnabled() ? "Hide Grid" : "Show Grid")) {
            sim.toggleDebugGrid();
        }
        
        ImGui::Separator();
        ImGui::PlotLines("Tick Time (ms)", tickTimes, 60, tickTimeIndex, nullptr, 0.0f, 10.0f, ImVec2(0, 80));
        
        ImGui::End();

        rlImGuiEnd();

        EndDrawing();
    }

    // 3. Cleanup
    rlImGuiShutdown();
    CloseWindow();
    
    spdlog::info("Tactix Engine Shutdown Cleanly. Total ticks: {}", tickCount);
    return 0;
}