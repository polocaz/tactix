mod agent;
mod combat;
mod map;
mod simulation;
mod utls;

use std::thread;
use std::time::Duration;
use crate::simulation::world::World;
use crate::agent::agent::AgentState; // If you need this for logging

fn main() {
    let path = "scenarios/scenario01.json";

    // 1. Initialize the World
    let mut world = World::from_scenario(path).expect("Failed to load scenario");

    println!("--- SIMULATION STARTING ---");
    println!("Agents loaded: {}", world.agents.len());
    println!("Cover nodes: {}", world.cover_nodes.len());

    // 2. The Simulation Loop
    // We run until the battle is "over" or forever.
    loop {
        // A. Run one step of simulation
        world.tick();

        // B. (Optional) Exit condition
        // e.g., if one team is wiped out, break the loop
        if is_battle_over(&world) {
            println!("Battle ended at tick {}", world.tick);
            break;
        }

        // C. Rate Limiting
        // If you don't sleep, this will run 1,000,000 ticks/sec and flood your console.
        // 500ms sleep = 2 ticks per second (easy to read logs)
        thread::sleep(Duration::from_millis(500));
    }
}

// Simple helper to check if we should stop
fn is_battle_over(world: &World) -> bool {
    // 1. Collect all unique team IDs from living agents
    let mut active_teams = Vec::new();
    
    for agent in &world.agents {
        if agent.health > 0.0 && !active_teams.contains(&agent.team) {
            active_teams.push(agent.team);
        }
    }

    // 2. If fewer than 2 teams are left, the battle is over
    active_teams.len() < 2
}