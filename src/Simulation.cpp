#include "platform.h"
#include "Simulation.hpp"
#include <raylib.h>
#include <cmath>
#include <chrono>
#include "spdlog/spdlog.h"

Simulation::Simulation(int w, int h)
    : screenWidth(w), screenHeight(h)
    , spatialHash(static_cast<float>(w), static_cast<float>(h), 50.0f)  // 50 pixel cells (Design Doc §5.1)
{
    neighborBuffer.reserve(200);  // Pre-allocate for typical neighbor count
}

void Simulation::init(size_t count) {
    spdlog::info("Initializing {} agents with SoA layout", count);
    entities.reserve(count);
    prevPosX.reserve(count);
    prevPosY.reserve(count);

    // Spawn agents with random positions and velocities
    for (size_t i = 0; i < count; i++) {
        float px = (float)GetRandomValue(0, screenWidth);
        float py = (float)GetRandomValue(0, screenHeight);
        float vx = (float)GetRandomValue(-100, 100);
        float vy = (float)GetRandomValue(-100, 100);
        
        entities.spawn(px, py, vx, vy);
        prevPosX.push_back(px);
        prevPosY.push_back(py);
    }
    
    // Calculate memory usage
    size_t memoryPerEntity = sizeof(float) * 6;  // 4 floats hot + 2 floats prev
    float totalMB = (memoryPerEntity * count) / (1024.0f * 1024.0f);
    spdlog::info("Memory usage: {:.2f} MB ({} bytes/entity)", totalMB, memoryPerEntity);
    spdlog::info("Spatial grid: {} cells", spatialHash.getCellCount());
}

void Simulation::setAgentCount(size_t count) {
    if (count == entities.count) return;
    
    if (count > entities.count) {
        // Add more agents
        size_t toAdd = count - entities.count;
        for (size_t i = 0; i < toAdd; i++) {
            float px = (float)GetRandomValue(0, screenWidth);
            float py = (float)GetRandomValue(0, screenHeight);
            float vx = (float)GetRandomValue(-100, 100);
            float vy = (float)GetRandomValue(-100, 100);
            
            entities.spawn(px, py, vx, vy);
            prevPosX.push_back(px);
            prevPosY.push_back(py);
        }
        spdlog::info("Added {} agents (total: {})", toAdd, entities.count);
    } else {
        // Remove agents
        size_t toRemove = entities.count - count;
        entities.count = count;
        entities.posX.resize(count);
        entities.posY.resize(count);
        entities.velX.resize(count);
        entities.velY.resize(count);
        entities.dirX.resize(count);
        entities.dirY.resize(count);
        entities.state.resize(count);
        prevPosX.resize(count);
        prevPosY.resize(count);
        spdlog::info("Removed {} agents (total: {})", toRemove, entities.count);
    }
}

void Simulation::tick(float dt) {
    // Store previous positions for interpolation
    for (size_t i = 0; i < entities.count; i++) {
        prevPosX[i] = entities.posX[i];
        prevPosY[i] = entities.posY[i];
    }
    
    // Rebuild spatial hash (Design Doc §5.3)
    rebuildSpatialHash();
    
    // Reset job counter for metrics
    jobSystem.resetJobCounter();
    
    // Update behaviors in parallel (Design Doc §6.2)
    updateSeparation(dt);  // Collision avoidance using spatial queries
    updateMovement(dt);    // Apply velocities
    
    // Screen wrapping
    screenWrap();
}

void Simulation::rebuildSpatialHash() {
    auto start = std::chrono::steady_clock::now();
    
    spatialHash.clear();
    for (size_t i = 0; i < entities.count; i++) {
        spatialHash.insert(static_cast<uint32_t>(i), entities.posX[i], entities.posY[i]);
    }
    
    auto end = std::chrono::steady_clock::now();
    lastSpatialHashTime = std::chrono::duration<float>(end - start).count() * 1000.0f;  // ms
}

void Simulation::updateSeparation(float dt) {
    // Parallelize collision avoidance (Design Doc §6.2)
    const size_t chunkSize = 256;  // Job granularity
    
    for (size_t start = 0; start < entities.count; start += chunkSize) {
        size_t end = std::min(start + chunkSize, entities.count);
        jobSystem.submit([this, start, end, dt]() {
            updateSeparationChunk(start, end, dt);
        });
    }
    
    jobSystem.waitAll();  // Barrier (Design Doc §6.3)
}

void Simulation::updateSeparationChunk(size_t start, size_t end, float dt) {
    // Collision avoidance using spatial queries (Phase 2)
    const float separationRadius = 20.0f;
    const float separationStrength = 200.0f;
    const float separationRadiusSq = separationRadius * separationRadius;
    
    // Thread-local neighbor buffer
    std::vector<uint32_t> localNeighbors;
    localNeighbors.reserve(200);
    
    for (size_t i = start; i < end; i++) {
        float px = entities.posX[i];
        float py = entities.posY[i];
        
        // Query nearby neighbors (Design Doc §5.4)
        spatialHash.queryNeighbors(px, py, separationRadius, localNeighbors);
        
        float steerX = 0.0f;
        float steerY = 0.0f;
        
        // Calculate separation force from neighbors
        for (uint32_t neighborIdx : localNeighbors) {
            if (neighborIdx == i) continue;  // Skip self
            
            float dx = px - entities.posX[neighborIdx];
            float dy = py - entities.posY[neighborIdx];
            float distSq = dx * dx + dy * dy;
            
            if (distSq < separationRadiusSq && distSq > 0.01f) {
                float dist = std::sqrt(distSq);
                // Stronger force when closer
                float force = (separationRadius - dist) / separationRadius;
                steerX += (dx / dist) * force;
                steerY += (dy / dist) * force;
            }
        }
        
        // Apply separation steering
        entities.velX[i] += steerX * separationStrength * dt;
        entities.velY[i] += steerY * separationStrength * dt;
        
        // Limit velocity
        const float maxSpeed = 150.0f;
        float speedSq = entities.velX[i] * entities.velX[i] + entities.velY[i] * entities.velY[i];
        if (speedSq > maxSpeed * maxSpeed) {
            float speed = std::sqrt(speedSq);
            entities.velX[i] = (entities.velX[i] / speed) * maxSpeed;
            entities.velY[i] = (entities.velY[i] / speed) * maxSpeed;
        }
    }
}

void Simulation::updateMovement(float dt) {
    // Parallelize movement integration (Design Doc §6.2)
    const size_t chunkSize = 256;
    
    for (size_t start = 0; start < entities.count; start += chunkSize) {
        size_t end = std::min(start + chunkSize, entities.count);
        jobSystem.submit([this, start, end, dt]() {
            updateMovementChunk(start, end, dt);
        });
    }
    
    jobSystem.waitAll();  // Barrier
}

void Simulation::updateMovementChunk(size_t start, size_t end, float dt) {
    // SIMD-friendly: compiler auto-vectorizes this loop
    for (size_t i = start; i < end; i++) {
        entities.posX[i] += entities.velX[i] * dt;
        entities.posY[i] += entities.velY[i] * dt;
        
        // Update direction from velocity (for rendering)
        float speed = std::sqrt(entities.velX[i] * entities.velX[i] + 
                               entities.velY[i] * entities.velY[i]);
        if (speed > 0.1f) {  // Only update if moving
            entities.dirX[i] = entities.velX[i] / speed;
            entities.dirY[i] = entities.velY[i] / speed;
        }
    }
}

void Simulation::screenWrap() {
    const float w = static_cast<float>(screenWidth);
    const float h = static_cast<float>(screenHeight);
    
    for (size_t i = 0; i < entities.count; i++) {
        if (entities.posX[i] < 0) entities.posX[i] += w;
        if (entities.posX[i] > w) entities.posX[i] -= w;
        if (entities.posY[i] < 0) entities.posY[i] += h;
        if (entities.posY[i] > h) entities.posY[i] -= h;
    }
}

uint32_t Simulation::getMaxCellOccupancy() const {
    return spatialHash.getMaxOccupancy();
}

void Simulation::draw(float alpha) {
    // Draw simulation world boundary
    const float borderThickness = 3.0f;
    DrawRectangleLinesEx(
        Rectangle{0, 0, static_cast<float>(screenWidth), static_cast<float>(screenHeight)},
        borderThickness,
        Color{100, 150, 255, 255}
    );
    
    // Debug: Draw grid
    if (debugGrid) {
        const float cellSize = 50.0f;
        for (int x = 0; x < screenWidth; x += static_cast<int>(cellSize)) {
            DrawLine(x, 0, x, screenHeight, Color{80, 255, 100, 180});
        }
        for (int y = 0; y < screenHeight; y += static_cast<int>(cellSize)) {
            DrawLine(0, y, screenWidth, y, Color{80, 255, 100, 180});
        }
    }
    
    // Interpolated rendering with directional triangles
    // Triangles show movement direction - useful for AI visualization
    const float agentSize = 4.0f;
    const float wrapThreshold = static_cast<float>(screenWidth) * 0.5f;  // Detect wrapping
    
    for (size_t i = 0; i < entities.count; i++) {
        // Check if agent wrapped this frame (large position delta)
        float deltaX = std::abs(entities.posX[i] - prevPosX[i]);
        float deltaY = std::abs(entities.posY[i] - prevPosY[i]);
        
        // If wrapped, don't interpolate (use current position to avoid stretching)
        float renderX, renderY;
        if (deltaX > wrapThreshold || deltaY > wrapThreshold) {
            renderX = entities.posX[i];
            renderY = entities.posY[i];
        } else {
            renderX = prevPosX[i] + (entities.posX[i] - prevPosX[i]) * alpha;
            renderY = prevPosY[i] + (entities.posY[i] - prevPosY[i]) * alpha;
        }
        
        // Calculate triangle vertices pointing in direction of movement
        float dx = entities.dirX[i];
        float dy = entities.dirY[i];
        
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
        
        DrawTriangle(
            Vector2{frontX, frontY},
            Vector2{baseLeft_X, baseLeft_Y},
            Vector2{baseRight_X, baseRight_Y},
            RAYWHITE
        );
    }
}
