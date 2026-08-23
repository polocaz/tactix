#pragma once
#include <vector>
#include <cstdint>
#include "WorkCounters.hpp"

// Spatial hash grid for efficient neighbor queries (Design Doc §5)
class SpatialHash {
public:
    SpatialHash(float worldWidth, float worldHeight, float cellSize);

    // Clear and rebuild the grid for current frame
    void clear();
    void insert(uint32_t entityId, float x, float y);

    // Query entities in 9-cell neighborhood (3x3 grid around position)
    void queryNeighbors(float x, float y, float radius, std::vector<uint32_t>& outEntities) const;

    // Same neighbourhood, same order, without materialising it. `fn` is called
    // once per candidate and returns false to stop the walk early.
    //
    // This is what the hot phases use instead of queryNeighbors. The copy it
    // avoids is not incidental: the callers examine roughly nineteen million
    // candidates a tick between them, and every one of those used to be stored
    // into a scratch vector and loaded straight back out again.
    template <typename F>
    void forEachNeighbor(float x, float y, F&& fn) const {
        forEachCell(x, y, [&fn](const uint32_t* first, const uint32_t* last) {
            for (const uint32_t* e = first; e != last; ++e) {
                if (!fn(*e)) return false;
            }
            return true;
        });
    }

    // Debug info
    uint32_t getCellCount() const { return gridWidth * gridHeight; }
    uint32_t getMaxOccupancy() const;

    // Get cell coordinates for position
    void getCellCoords(float x, float y, int32_t& cellX, int32_t& cellY) const;

    void setCounters(WorkCounters* c) { counters = c; }

private:
    WorkCounters* counters = nullptr;

    float cellSize;
    uint32_t gridWidth;
    uint32_t gridHeight;
    float worldWidth;
    float worldHeight;
    
    // Cell storage: vector of entity lists per cell
    std::vector<std::vector<uint32_t>> cells;

    // The one place the 3x3 walk and its work counting live. Every query shape
    // above is built on this, so cell visit order -- and therefore the order
    // candidates come back in, which selectMeleeTarget's tie-break depends on
    // -- is defined once rather than per call site.
    //
    // Counting is accumulated in locals and flushed ONCE per query, not once
    // per cell. The totals are identical either way; what changes is that
    // fifteen worker threads no longer contend for the same two cache lines
    // half a million times a tick. `cellsVisited` and `candidatesExamined`
    // therefore keep counting whole cells even when `fn` stops the walk early:
    // the vector-filling form counts every candidate it collects before its
    // caller looks at any of them, and these two shapes must stay comparable.
    template <typename F>
    void forEachCell(float x, float y, F&& fn) const {
        const int32_t centerX = static_cast<int32_t>(x / cellSize);
        const int32_t centerY = static_cast<int32_t>(y / cellSize);

        uint64_t visited = 0;
        uint64_t examined = 0;
        bool stopped = false;

        // Check 9 cells (3x3 grid) around center (Design Doc §5.4)
        for (int32_t dy = -1; dy <= 1; ++dy) {
            for (int32_t dx = -1; dx <= 1; ++dx) {
                const int32_t cellX = centerX + dx;
                const int32_t cellY = centerY + dy;

                if (!isValidCell(cellX, cellY)) continue;

                const auto& cell = cells[cellY * gridWidth + cellX];
                ++visited;
                examined += cell.size();

                if (stopped || cell.empty()) continue;
                if (!fn(cell.data(), cell.data() + cell.size())) stopped = true;
            }
        }

        if (counters) {
            counters->add(counters->cellsVisited, visited);
            counters->add(counters->candidatesExamined, examined);
        }
    }

    // Hash position to cell ID (Design Doc §5.2)
    inline uint32_t hashPosition(float x, float y) const {
        int32_t cellX = static_cast<int32_t>(x / cellSize);
        int32_t cellY = static_cast<int32_t>(y / cellSize);
        
        // Clamp to grid bounds
        cellX = (cellX < 0) ? 0 : (cellX >= static_cast<int32_t>(gridWidth) ? gridWidth - 1 : cellX);
        cellY = (cellY < 0) ? 0 : (cellY >= static_cast<int32_t>(gridHeight) ? gridHeight - 1 : cellY);
        
        return cellY * gridWidth + cellX;
    }
    
    inline bool isValidCell(int32_t cellX, int32_t cellY) const {
        return cellX >= 0 && cellX < static_cast<int32_t>(gridWidth) &&
               cellY >= 0 && cellY < static_cast<int32_t>(gridHeight);
    }
};
