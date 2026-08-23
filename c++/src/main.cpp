#include "platform.h"
#include "raylib.h"
#include "rlImGui.h"
#include "imgui.h"
#include "spdlog/spdlog.h"
#include <algorithm>
#include <chrono>
#include <cmath>

#include "Simulation.hpp"
#include "Renderer.hpp"

// ---------------------------------------------------------------------------
// ImGui theme
//
// Same palette as the battlefield, so the panel reads as part of the same
// instrument rather than as a debug window someone left open on top of it.
// Team colours are deliberately NOT used for chrome: on this screen blue and
// red mean "team A" and "team B" and must never also mean "a button".
// ---------------------------------------------------------------------------
static ImVec4 rgba(int r, int g, int b, float a = 1.0f) {
    return ImVec4(r / 255.0f, g / 255.0f, b / 255.0f, a);
}

static void applyTactixStyle() {
    ImGuiStyle& s = ImGui::GetStyle();

    s.WindowRounding    = 6.0f;
    s.ChildRounding     = 5.0f;
    s.FrameRounding     = 4.0f;
    s.PopupRounding     = 4.0f;
    s.GrabRounding      = 4.0f;
    s.ScrollbarRounding = 8.0f;
    s.TabRounding       = 4.0f;

    s.WindowBorderSize  = 1.0f;
    s.FrameBorderSize   = 0.0f;
    s.WindowPadding     = ImVec2(14, 12);
    s.FramePadding      = ImVec2(9, 5);
    s.ItemSpacing       = ImVec2(9, 7);
    s.ItemInnerSpacing  = ImVec2(7, 5);
    s.IndentSpacing     = 18.0f;
    s.ScrollbarSize     = 12.0f;
    s.GrabMinSize       = 10.0f;
    s.WindowTitleAlign  = ImVec2(0.02f, 0.5f);

    ImVec4* c = s.Colors;
    const ImVec4 accent     = rgba(224, 164,  88);   // amber: the one accent
    const ImVec4 accentDim  = rgba(224, 164,  88, 0.35f);

    c[ImGuiCol_Text]                 = rgba(214, 220, 232);
    c[ImGuiCol_TextDisabled]         = rgba(112, 120, 136);
    c[ImGuiCol_WindowBg]             = rgba( 18,  20,  27, 0.96f);
    c[ImGuiCol_ChildBg]              = rgba( 23,  26,  34, 0.60f);
    c[ImGuiCol_PopupBg]              = rgba( 20,  22,  29, 0.98f);
    c[ImGuiCol_Border]               = rgba( 46,  52,  65);
    c[ImGuiCol_BorderShadow]         = rgba(  0,   0,   0, 0.0f);
    c[ImGuiCol_FrameBg]              = rgba( 32,  36,  46);
    c[ImGuiCol_FrameBgHovered]       = rgba( 42,  48,  61);
    c[ImGuiCol_FrameBgActive]        = rgba( 52,  59,  74);
    c[ImGuiCol_TitleBg]              = rgba( 15,  17,  22);
    c[ImGuiCol_TitleBgActive]        = rgba( 26,  30,  39);
    c[ImGuiCol_TitleBgCollapsed]     = rgba( 15,  17,  22, 0.80f);
    c[ImGuiCol_MenuBarBg]            = rgba( 23,  26,  34);
    c[ImGuiCol_ScrollbarBg]          = rgba( 15,  17,  22, 0.55f);
    c[ImGuiCol_ScrollbarGrab]        = rgba( 48,  54,  68);
    c[ImGuiCol_ScrollbarGrabHovered] = rgba( 62,  70,  87);
    c[ImGuiCol_ScrollbarGrabActive]  = accent;
    c[ImGuiCol_CheckMark]            = accent;
    c[ImGuiCol_SliderGrab]           = rgba(140, 150, 170);
    c[ImGuiCol_SliderGrabActive]     = accent;
    c[ImGuiCol_Button]               = rgba( 38,  43,  55);
    c[ImGuiCol_ButtonHovered]        = rgba( 54,  61,  77);
    c[ImGuiCol_ButtonActive]         = accentDim;
    c[ImGuiCol_Header]               = rgba( 32,  37,  47);
    c[ImGuiCol_HeaderHovered]        = rgba( 45,  52,  66);
    c[ImGuiCol_HeaderActive]         = rgba( 56,  64,  80);
    c[ImGuiCol_Separator]            = rgba( 44,  50,  63);
    c[ImGuiCol_SeparatorHovered]     = accentDim;
    c[ImGuiCol_SeparatorActive]      = accent;
    c[ImGuiCol_ResizeGrip]           = rgba( 46,  52,  65);
    c[ImGuiCol_ResizeGripHovered]    = accentDim;
    c[ImGuiCol_ResizeGripActive]     = accent;
    c[ImGuiCol_PlotLines]            = rgba(126, 176, 224);
    c[ImGuiCol_PlotLinesHovered]     = accent;
    c[ImGuiCol_PlotHistogram]        = accent;
    c[ImGuiCol_PlotHistogramHovered] = rgba(244, 196, 132);
    c[ImGuiCol_TableHeaderBg]        = rgba( 28,  32,  41);
    c[ImGuiCol_TableBorderStrong]    = rgba( 46,  52,  65);
    c[ImGuiCol_TableBorderLight]     = rgba( 34,  38,  48);
    c[ImGuiCol_TextSelectedBg]       = accentDim;

    ImGui::GetIO().FontGlobalScale = 1.05f;
}

// A label above a value, which is the layout every readout in this panel
// wants and which ImGui::Text formatting on its own does not give.
static void statLine(const char* label, const char* value, ImVec4 valueColor) {
    ImGui::TextColored(rgba(126, 136, 154), "%s", label);
    ImGui::SameLine(0.0f, 8.0f);
    const float w = ImGui::GetContentRegionAvail().x;
    const float tw = ImGui::CalcTextSize(value).x;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, w - tw));
    ImGui::TextColored(valueColor, "%s", value);
}

int main() {
    // Window is what you look through; the world is what you look at.
    const int screenWidth = 1280;
    const int screenHeight = 720;
    const int worldWidth = 2400;
    const int worldHeight = 1600;

    spdlog::info("Initializing Tactix Engine...");

    // macOS Retina fix: Set config flags before window creation
    SetConfigFlags(FLAG_WINDOW_HIGHDPI | FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT);

    InitWindow(screenWidth, screenHeight, "Tactix - Medieval Skirmish");
    SetTargetFPS(144);  // Render at high FPS, simulation runs at fixed 60 TPS

    rlImGuiSetup(true);
    applyTactixStyle();

    Simulation sim(worldWidth, worldHeight);
    size_t agentCount = 100;
    sim.init(agentCount);
    // Presentation-only, and only ever set here: the headless benchmark shares
    // this class and must not pay for the blood on the floor.
    sim.recordDeaths = true;

    renderInit(sim);
    renderResetEffects();
    ViewSettings view;

    // The panel occupies a fixed rail on the right, so the world is centred in
    // what is LEFT of it rather than in the window. Centring on the window put
    // a fifth of the field permanently behind the panel on startup, which for
    // a view whose whole job is "here is the battle" is the wrong first frame.
    const float kPanelRail = 372.0f;

    // offset is recomputed every frame from the live window size, so a resize
    // re-centres instead of leaving the world pinned to a stale corner.
    auto viewWidth  = [&](int sw) { return std::max(320.0f, sw - kPanelRail); };
    auto fitZoomFor = [&](int sw, int sh) {
        return std::min(viewWidth(sw) / worldWidth, (float)sh / worldHeight) * 0.94f;
    };
    Camera2D camera = { 0 };
    camera.target   = Vector2{ worldWidth / 2.0f, worldHeight / 2.0f };
    camera.offset   = Vector2{ viewWidth(screenWidth) / 2.0f, screenHeight / 2.0f };
    camera.rotation = 0.0f;
    camera.zoom     = fitZoomFor(screenWidth, screenHeight);

    float   targetZoom        = camera.zoom;
    bool    zoomAnchored      = false;
    Vector2 zoomAnchorScreen  = { 0, 0 };
    Vector2 zoomAnchorWorld   = { 0, 0 };

    // Fixed timestep accumulator (Design Doc §1.1)
    const float FIXED_DT = 1.0f / 60.0f;  // 60 ticks per second
    float accumulator = 0.0f;
    auto lastTime = std::chrono::steady_clock::now();
    float timeScale = 0.5f;  // start at half speed to observe formation dynamics

    // Metrics
    float tickTimes[60] = {0};
    float renderTimes[60] = {0};
    float frameTimes[60] = {0};
    int timeIndex = 0;
    float lastTickTime = 0.0f;
    float lastRenderTime = 0.0f;
    float lastFrameTime = 0.0f;
    int tickCount = 0;
    float battleSeconds = 0.0f;
    size_t startingA = sim.getTeamCount(Team::A);
    size_t startingB = sim.getTeamCount(Team::B);

    spdlog::info("Starting simulation with {} agents", agentCount);

    while (!WindowShouldClose()) {
        auto frameStart = std::chrono::steady_clock::now();

        const int sw = GetScreenWidth();
        const int sh = GetScreenHeight();
        camera.offset = Vector2{ viewWidth(sw) * 0.5f, sh * 0.5f };

        auto currentTime = std::chrono::steady_clock::now();
        float frameTime = std::chrono::duration<float>(currentTime - lastTime).count();
        lastTime = currentTime;
        // A stall (dragging the window, a breakpoint) otherwise dumps hundreds
        // of catch-up ticks into one frame and the battle jumps.
        frameTime = std::min(frameTime, 0.10f);

        // --- Camera -------------------------------------------------------
        // ImGui reports what it captured last frame, which is the right answer
        // here: without this, scrolling a slider also zooms the battlefield.
        const ImGuiIO& io = ImGui::GetIO();
        const bool uiHasMouse = io.WantCaptureMouse;
        const bool uiHasKeys  = io.WantCaptureKeyboard;

        const float wheel = GetMouseWheelMove();
        if (wheel != 0.0f && !uiHasMouse) {
            // Multiplicative, so one notch is the same proportion of the view
            // at every scale. The old additive step was a crawl when zoomed in
            // and a jump when zoomed out.
            targetZoom = std::clamp(targetZoom * std::pow(1.20f, wheel), 0.12f, 12.0f);
            zoomAnchorScreen = GetMousePosition();
            zoomAnchorWorld  = GetScreenToWorld2D(zoomAnchorScreen, camera);
            zoomAnchored = true;
        }

        // Exponential ease, framerate independent. Snapping straight to the new
        // zoom loses your place on the field; this keeps the eye anchored.
        camera.zoom += (targetZoom - camera.zoom) * (1.0f - std::exp(-16.0f * frameTime));
        if (zoomAnchored) {
            // Hold the point under the cursor still for the whole ease, not
            // just for the frame the wheel turned.
            const Vector2 now = GetScreenToWorld2D(zoomAnchorScreen, camera);
            camera.target.x += zoomAnchorWorld.x - now.x;
            camera.target.y += zoomAnchorWorld.y - now.y;
            if (std::fabs(targetZoom - camera.zoom) < 0.0005f) {
                camera.zoom = targetZoom;
                zoomAnchored = false;
            }
        }

        if (!uiHasMouse && (IsMouseButtonDown(MOUSE_BUTTON_RIGHT) ||
                            IsMouseButtonDown(MOUSE_BUTTON_LEFT))) {
            const Vector2 d = GetMouseDelta();
            camera.target.x -= d.x / camera.zoom;
            camera.target.y -= d.y / camera.zoom;
        }

        if (!uiHasKeys) {
            // Pan speed is in screen pixels per second, so the field moves at a
            // constant apparent rate no matter how far in you are.
            const float pan = 900.0f * frameTime / camera.zoom;
            if (IsKeyDown(KEY_A) || IsKeyDown(KEY_LEFT))  camera.target.x -= pan;
            if (IsKeyDown(KEY_D) || IsKeyDown(KEY_RIGHT)) camera.target.x += pan;
            if (IsKeyDown(KEY_W) || IsKeyDown(KEY_UP))    camera.target.y -= pan;
            if (IsKeyDown(KEY_S) || IsKeyDown(KEY_DOWN))  camera.target.y += pan;

            if (IsKeyPressed(KEY_F) || IsMouseButtonPressed(MOUSE_BUTTON_MIDDLE)) {
                camera.target = Vector2{ worldWidth / 2.0f, worldHeight / 2.0f };
                targetZoom = fitZoomFor(sw, sh);
                zoomAnchored = false;
            }
            if (IsKeyPressed(KEY_SPACE))         sim.togglePause();
            if (IsKeyPressed(KEY_G))             view.grid = !view.grid;
            if (IsKeyPressed(KEY_TAB))           view.squadOverlay = !view.squadOverlay;
            if (IsKeyPressed(KEY_LEFT_BRACKET))  timeScale = std::max(0.125f, timeScale * 0.5f);
            if (IsKeyPressed(KEY_RIGHT_BRACKET)) timeScale = std::min(4.0f, timeScale * 2.0f);
            if (IsKeyPressed(KEY_BACKSPACE))     timeScale = 1.0f;
            if (IsKeyPressed(KEY_F12))           TakeScreenshot("tactix.png");
        }

        // Keep the field reachable. A margin of half a screen means you can
        // still push the edge of the world to the middle of the view, which is
        // what you want when watching a flank, without losing it entirely.
        const float marginX = sw * 0.5f / camera.zoom;
        const float marginY = sh * 0.5f / camera.zoom;
        camera.target.x = std::clamp(camera.target.x, -marginX, worldWidth + marginX);
        camera.target.y = std::clamp(camera.target.y, -marginY, worldHeight + marginY);
        view.zoom = camera.zoom;

        // --- Simulation ---------------------------------------------------
        // tick() is a no-op while paused, so letting the accumulator fill would
        // bank the whole pause and replay it in a burst on resume -- and would
        // count every skipped tick in the tick counter besides.
        float alpha = 1.0f;
        if (sim.isPaused()) {
            accumulator = 0.0f;
        } else {
            accumulator += frameTime * timeScale;
            while (accumulator >= FIXED_DT) {
                auto tickStart = std::chrono::steady_clock::now();

                sim.tick(FIXED_DT);
                tickCount++;
                battleSeconds += FIXED_DT;

                auto tickEnd = std::chrono::steady_clock::now();
                lastTickTime = std::chrono::duration<float>(tickEnd - tickStart).count() * 1000.0f;
                tickTimes[timeIndex] = lastTickTime;

                accumulator -= FIXED_DT;
            }
            alpha = accumulator / FIXED_DT;
        }

        renderUpdateEffects(sim, frameTime);

        // --- Draw -----------------------------------------------------------
        auto renderStart = std::chrono::steady_clock::now();

        BeginDrawing();
        ClearBackground(Color{ 11, 12, 16, 255 });

        BeginMode2D(camera);
        drawSimulation(sim, alpha, view);
        EndMode2D();

        drawHud(sim, view, viewWidth(sw), timeScale, sim.isPaused(), battleSeconds);

        // --- Panel ----------------------------------------------------------
        rlImGuiBegin();

        ImGui::SetNextWindowSize(ImVec2(kPanelRail - 26.0f, 640), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(ImVec2(sw - kPanelRail + 12.0f, 70.0f), ImGuiCond_FirstUseEver);
        ImGui::Begin("TACTIX");

        // Transport controls first: they are what a viewer reaches for, and
        // burying them under twenty readouts made this a dashboard you could
        // not drive.
        const bool paused = sim.isPaused();
        if (ImGui::Button(paused ? "Play" : "Pause", ImVec2(84, 0))) sim.togglePause();
        ImGui::SameLine();
        if (ImGui::Button("/ 2", ImVec2(48, 0))) timeScale = std::max(0.125f, timeScale * 0.5f);
        ImGui::SameLine();
        if (ImGui::Button("x 2", ImVec2(48, 0))) timeScale = std::min(4.0f, timeScale * 2.0f);
        ImGui::SameLine();
        if (ImGui::Button("1x", ImVec2(40, 0))) timeScale = 1.0f;
        ImGui::SameLine();
        if (paused) ImGui::TextColored(rgba(236, 176, 76), "PAUSED");
        else        ImGui::TextColored(rgba(122, 206, 128), "%.2fx", (double)timeScale);

        ImGui::Spacing();

        if (ImGui::CollapsingHeader("Battle", ImGuiTreeNodeFlags_DefaultOpen)) {
            const size_t a = sim.getTeamCount(Team::A);
            const size_t b = sim.getTeamCount(Team::B);

            // Survivors as a fraction of what each side started with. Raw head
            // counts alone hide the thing you want to know, which is who is
            // spending men faster.
            ImGui::PushStyleColor(ImGuiCol_PlotHistogram, rgba(92, 154, 236));
            ImGui::ProgressBar(startingA ? (float)a / (float)startingA : 0.0f,
                               ImVec2(-1, 14), TextFormat("A  %zu / %zu", a, startingA));
            ImGui::PopStyleColor();
            ImGui::PushStyleColor(ImGuiCol_PlotHistogram, rgba(218, 96, 66));
            ImGui::ProgressBar(startingB ? (float)b / (float)startingB : 0.0f,
                               ImVec2(-1, 14), TextFormat("B  %zu / %zu", b, startingB));
            ImGui::PopStyleColor();

            ImGui::Spacing();
            statLine("Squads",  TextFormat("%zu", sim.getSquadCount()), rgba(214, 220, 232));
            statLine("Arrows in flight", TextFormat("%zu", sim.getProjectileCount()),
                     rgba(244, 218, 156));
            statLine("Fallen", TextFormat("%zu",
                     (startingA + startingB) - (a + b)), rgba(198, 110, 96));
            statLine("Elapsed", TextFormat("%02d:%02d", (int)battleSeconds / 60,
                     (int)battleSeconds % 60), rgba(214, 220, 232));

            ImGui::Spacing();
            int agentCountInt = static_cast<int>(agentCount);
            ImGui::TextColored(rgba(126, 136, 154), "Army size (restarts the battle)");
            ImGui::SetNextItemWidth(-1);
            if (ImGui::SliderInt("##agents", &agentCountInt, 100, 10000)) {
                agentCount = static_cast<size_t>(agentCountInt);
                sim.reset(agentCount);
                renderResetEffects();
                startingA = sim.getTeamCount(Team::A);
                startingB = sim.getTeamCount(Team::B);
                tickCount = 0;
                battleSeconds = 0.0f;
                accumulator = 0.0f;
            }
        }

        if (ImGui::CollapsingHeader("View", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Checkbox("Squad overlay", &view.squadOverlay);
            ImGui::SameLine(180); ImGui::Checkbox("Grid", &view.grid);
            ImGui::Checkbox("Shadows", &view.shadows);
            ImGui::SameLine(180); ImGui::Checkbox("Blood", &view.decals);
            ImGui::Checkbox("Health pips", &view.healthPips);
            ImGui::SameLine(180); ImGui::Checkbox("Vignette", &view.vignette);
            ImGui::Checkbox("Objectives", &view.objectiveLines);

            ImGui::Spacing();
            statLine("Zoom", TextFormat("%.2fx", (double)camera.zoom), rgba(214, 220, 232));
            if (ImGui::Button("Fit world", ImVec2(-1, 0))) {
                camera.target = Vector2{ worldWidth / 2.0f, worldHeight / 2.0f };
                targetZoom = fitZoomFor(sw, sh);
                zoomAnchored = false;
            }
        }

        if (ImGui::CollapsingHeader("Performance")) {
            float avgTickTime = 0.0f, avgRenderTime = 0.0f, avgFrameTime = 0.0f;
            for (int i = 0; i < 60; i++) {
                avgTickTime += tickTimes[i];
                avgRenderTime += renderTimes[i];
                avgFrameTime += frameTimes[i];
            }
            avgTickTime /= 60.0f;
            avgRenderTime /= 60.0f;
            avgFrameTime /= 60.0f;

            const float tickBudget = FIXED_DT * 1000.0f;  // 16.66 ms
            const float used = avgTickTime / tickBudget;
            ImGui::PushStyleColor(ImGuiCol_PlotHistogram,
                                  used < 0.9f ? rgba(122, 206, 128) : rgba(214, 82, 62));
            ImGui::ProgressBar(std::min(used, 1.0f), ImVec2(-1, 16),
                               TextFormat("%.1f%% of the 16.7 ms tick budget", used * 100.0f));
            ImGui::PopStyleColor();

            ImGui::Spacing();
            statLine("Render FPS", TextFormat("%d", GetFPS()), rgba(214, 220, 232));
            statLine("Tick",   TextFormat("%.3f ms", avgTickTime),   rgba(214, 220, 232));
            statLine("Render", TextFormat("%.3f ms", avgRenderTime), rgba(214, 220, 232));
            statLine("Frame",  TextFormat("%.3f ms", avgFrameTime),  rgba(214, 220, 232));
            statLine("Ticks",  TextFormat("%d", tickCount),          rgba(214, 220, 232));

            ImGui::Spacing();
            statLine("Worker threads", TextFormat("%u", sim.getWorkerCount()), rgba(214, 220, 232));
            statLine("Jobs / frame",   TextFormat("%u", sim.getJobsExecuted()), rgba(214, 220, 232));
            statLine("Spatial hash",   TextFormat("%.3f ms", sim.getLastSpatialHashTime()),
                     rgba(214, 220, 232));
            statLine("Max cell occupancy", TextFormat("%u", sim.getMaxCellOccupancy()),
                     rgba(214, 220, 232));

            ImGui::Spacing();
            ImGui::PlotLines("##ticks", tickTimes, 60, timeIndex, "tick ms",
                             0.0f, 20.0f, ImVec2(-1, 54));
            ImGui::PlotLines("##render", renderTimes, 60, timeIndex, "render ms",
                             0.0f, 20.0f, ImVec2(-1, 54));
        }

        ImGui::End();
        rlImGuiEnd();

        EndDrawing();

        auto renderEnd = std::chrono::steady_clock::now();
        lastRenderTime = std::chrono::duration<float>(renderEnd - renderStart).count() * 1000.0f;

        auto frameEnd = std::chrono::steady_clock::now();
        lastFrameTime = std::chrono::duration<float>(frameEnd - frameStart).count() * 1000.0f;

        renderTimes[timeIndex] = lastRenderTime;
        frameTimes[timeIndex] = lastFrameTime;
        timeIndex = (timeIndex + 1) % 60;
    }

    renderShutdown();
    rlImGuiShutdown();
    CloseWindow();

    spdlog::info("Tactix Engine Shutdown Cleanly. Total ticks: {}", tickCount);
    return 0;
}
