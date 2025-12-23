#include "platform.h"
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
    
    // macOS Retina fix: Set config flags before window creation
    SetConfigFlags(FLAG_WINDOW_HIGHDPI);
    
    InitWindow(screenWidth, screenHeight, "Tactix - High-Performance Agent Simulation");
    SetTargetFPS(144);  // Render at high FPS, simulation runs at fixed 60 TPS

    // 2. Setup ImGui (via rlImGui bridge)
    rlImGuiSetup(true);

    // 3. Setup Camera for zoom/pan
    Camera2D camera = { 0 };
    camera.target = Vector2{ screenWidth / 2.0f, screenHeight / 2.0f };
    camera.offset = Vector2{ screenWidth / 2.0f, screenHeight / 2.0f };
    camera.rotation = 0.0f;
    camera.zoom = 1.0f;

    Simulation sim(screenWidth, screenHeight);
    size_t agentCount = 100;  // Start with fewer agents to focus on behavior
    sim.init(agentCount);

    // Fixed timestep accumulator (Design Doc §1.1)
    const float FIXED_DT = 1.0f / 60.0f;  // 60 ticks per second
    float accumulator = 0.0f;
    auto lastTime = std::chrono::steady_clock::now();
    float timeScale = 0.5f;  // Time scaling: start at half speed to observe infection dynamics

    // Metrics
    float tickTimes[60] = {0};  // Rolling window for tick time
    float renderTimes[60] = {0};  // Rolling window for render time
    float frameTimes[60] = {0};  // Rolling window for total frame time
    int timeIndex = 0;
    float lastTickTime = 0.0f;
    float lastRenderTime = 0.0f;
    float lastFrameTime = 0.0f;
    int tickCount = 0;

    spdlog::info("Starting simulation with {} agents", agentCount);

    // --- Main Loop ---
    while (!WindowShouldClose()) {
        auto frameStart = std::chrono::steady_clock::now();
        
        // Camera controls (before ImGui captures input)
        float wheel = GetMouseWheelMove();
        if (wheel != 0) {
            // Zoom towards mouse position
            Vector2 mouseWorldPos = GetScreenToWorld2D(GetMousePosition(), camera);
            camera.offset = GetMousePosition();
            camera.target = mouseWorldPos;
            
            // Smooth zoom
            float zoomIncrement = 0.125f;
            camera.zoom += wheel * zoomIncrement;
            camera.zoom = std::max(0.125f, std::min(camera.zoom, 8.0f));
        }
        
        // Pan with right mouse button
        if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) {
            Vector2 delta = GetMouseDelta();
            delta.x *= -1.0f / camera.zoom;
            delta.y *= -1.0f / camera.zoom;
            camera.target.x += delta.x;
            camera.target.y += delta.y;
        }
        
        // Reset camera with middle mouse button
        if (IsMouseButtonPressed(MOUSE_BUTTON_MIDDLE)) {
            camera.target = Vector2{ screenWidth / 2.0f, screenHeight / 2.0f };
            camera.zoom = 1.0f;
        }
        
        // Time scale keyboard controls
        if (IsKeyPressed(KEY_LEFT_BRACKET)) {
            timeScale = std::max(0.125f, timeScale * 0.5f);
        }
        if (IsKeyPressed(KEY_RIGHT_BRACKET)) {
            timeScale = std::min(4.0f, timeScale * 2.0f);
        }
        if (IsKeyPressed(KEY_BACKSPACE)) {
            timeScale = 1.0f;  // Reset to normal speed
        }
        
        auto currentTime = std::chrono::steady_clock::now();
        float frameTime = std::chrono::duration<float>(currentTime - lastTime).count();
        lastTime = currentTime;

        accumulator += frameTime * timeScale;

        // Fixed timestep simulation loop
        while (accumulator >= FIXED_DT) {
            auto tickStart = std::chrono::steady_clock::now();
            
            sim.tick(FIXED_DT);
            tickCount++;
            
            auto tickEnd = std::chrono::steady_clock::now();
            lastTickTime = std::chrono::duration<float>(tickEnd - tickStart).count() * 1000.0f;  // ms
            tickTimes[timeIndex] = lastTickTime;
            
            accumulator -= FIXED_DT;
        }

        // Interpolation alpha for smooth rendering
        float alpha = accumulator / FIXED_DT;

        // ----------- DRAW -----------
        auto renderStart = std::chrono::steady_clock::now();
        
        BeginDrawing();
        ClearBackground(Color{15, 15, 20, 255});

        // Apply camera transform
        BeginMode2D(camera);
        sim.draw(alpha);
        EndMode2D();
        
        // Draw camera instructions (screen space)
        DrawText("Mouse Wheel: Zoom | Right Click: Pan | Middle Click: Reset | [/]: Time Scale", 10, screenHeight - 25, 16, Color{200, 200, 200, 180});

        // ----------- IMGUI -----------
        rlImGuiBegin();

        ImGui::Begin("Tactix - Zombie Simulation");
        
        // Agent count control
        int agentCountInt = static_cast<int>(agentCount);
        if (ImGui::SliderInt("Total Agents", &agentCountInt, 100, 10000)) {
            agentCount = static_cast<size_t>(agentCountInt);
            sim.setAgentCount(agentCount);
        }
        ImGui::Text("Active Agents: %zu", sim.getAgentCount());
        
        // Population breakdown
        ImGui::Separator();
        ImGui::Text("Population Breakdown:");
        size_t civilianCount = sim.getCivilianCount();
        size_t zombieCount = sim.getZombieCount();
        size_t heroCount = sim.getHeroCount();
        ImGui::TextColored(ImVec4(0.85f, 0.85f, 0.85f, 1.0f), "  Civilians: %zu (%.1f%%)", 
                          civilianCount, (civilianCount / (float)sim.getAgentCount()) * 100.0f);
        ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.2f, 1.0f), "  Zombies: %zu (%.1f%%)", 
                          zombieCount, (zombieCount / (float)sim.getAgentCount()) * 100.0f);
        ImGui::TextColored(ImVec4(0.2f, 0.4f, 0.9f, 1.0f), "  Heroes: %zu (%.1f%%)", 
                          heroCount, (heroCount / (float)sim.getAgentCount()) * 100.0f);
        ImGui::Separator();
        
        ImGui::Text("Render FPS: %d", GetFPS());
        ImGui::Text("Simulation TPS: 60 (fixed)");
        ImGui::Text("Total Ticks: %d", tickCount);
        
        ImGui::Separator();
        ImGui::Text("Time Scale: %.2fx", timeScale);
        if (ImGui::SliderFloat("##TimeScale", &timeScale, 0.125f, 4.0f, "%.3fx")) {
            // Clamp to powers of 2 when using slider for cleaner values
        }
        ImGui::SameLine();
        if (ImGui::Button("Reset##TimeScale")) {
            timeScale = 1.0f;
        }
        ImGui::Text("[ / ]: Slow/Speed | Backspace: Reset");
        ImGui::Separator();
        
        // Calculate averages
        float avgTickTime = 0.0f;
        float avgRenderTime = 0.0f;
        float avgFrameTime = 0.0f;
        for (int i = 0; i < 60; i++) {
            avgTickTime += tickTimes[i];
            avgRenderTime += renderTimes[i];
            avgFrameTime += frameTimes[i];
        }
        avgTickTime /= 60.0f;
        avgRenderTime /= 60.0f;
        avgFrameTime /= 60.0f;
        
        ImGui::Text("Frame Breakdown (avg over 60):");
        ImGui::Text("  Tick:   %.3f ms (%.1f%%)", avgTickTime, (avgTickTime/avgFrameTime)*100.0f);
        ImGui::Text("  Render: %.3f ms (%.1f%%)", avgRenderTime, (avgRenderTime/avgFrameTime)*100.0f);
        ImGui::Text("  Total:  %.3f ms", avgFrameTime);
        ImGui::Separator();
        
        ImGui::Text("Last Tick: %.3f ms", lastTickTime);
        
        // Performance bar
        float tickBudget = FIXED_DT * 1000.0f;  // 16.66 ms
        float tickPercent = (avgTickTime / tickBudget) * 100.0f;
        ImGui::ProgressBar(avgTickTime / tickBudget, ImVec2(-1, 0), 
                          TextFormat("%.1f%% of budget", tickPercent));
        
        if (avgTickTime < 15.0f) {
            ImGui::TextColored(ImVec4(0, 1, 0, 1), "✓ Phase 3 Target: < 15ms");
        } else {
            ImGui::TextColored(ImVec4(1, 0, 0, 1), "✗ Exceeds Phase 3 target");
        }
        
        ImGui::Separator();
        ImGui::Text("Worker Threads: %u", sim.getWorkerCount());
        ImGui::Text("Jobs/Frame: %u", sim.getJobsExecuted());
        ImGui::Text("Spatial Hash: %.3f ms", sim.getLastSpatialHashTime());
        ImGui::Text("Max Cell Occupancy: %u", sim.getMaxCellOccupancy());
        
        ImGui::Separator();
        ImGui::Text("Camera Zoom: %.2fx", camera.zoom);
        ImGui::Text("Camera Target: (%.0f, %.0f)", camera.target.x, camera.target.y);
        
        if (ImGui::Button(sim.isDebugGridEnabled() ? "Hide Grid" : "Show Grid")) {
            sim.toggleDebugGrid();
        }
        
        ImGui::Separator();
        ImGui::PlotLines("Tick Time (ms)", tickTimes, 60, timeIndex, nullptr, 0.0f, 20.0f, ImVec2(0, 60));
        ImGui::PlotLines("Render Time (ms)", renderTimes, 60, timeIndex, nullptr, 0.0f, 20.0f, ImVec2(0, 60));
        
        ImGui::End();

        rlImGuiEnd();

        EndDrawing();
        
        auto renderEnd = std::chrono::steady_clock::now();
        lastRenderTime = std::chrono::duration<float>(renderEnd - renderStart).count() * 1000.0f;  // ms
        
        auto frameEnd = std::chrono::steady_clock::now();
        lastFrameTime = std::chrono::duration<float>(frameEnd - frameStart).count() * 1000.0f;  // ms
        
        renderTimes[timeIndex] = lastRenderTime;
        frameTimes[timeIndex] = lastFrameTime;
        timeIndex = (timeIndex + 1) % 60;
    }

    // 4. Cleanup
    rlImGuiShutdown();
    CloseWindow();
    
    spdlog::info("Tactix Engine Shutdown Cleanly. Total ticks: {}", tickCount);
    return 0;
}