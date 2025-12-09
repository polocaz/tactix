#pragma once
#include <vector>
#include "Agent.hpp"

class Simulation {
public:
    Simulation(int screenWidth, int screenHeight);

    void init(size_t count);
    void update(float dt);
    void draw();

private:
    int screenWidth;
    int screenHeight;

    std::vector<Agent> agents;

    void updateMovement(Agent& a, float dt);
    void screenWrap(Agent& a);
};
