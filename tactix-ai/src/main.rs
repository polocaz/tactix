mod agent;
mod world;

use crate::world::World;

fn main() {
    let path = "scenarios/scenario01.json";

    // We unwrap() here to say "Crash if this fails," which mimics 
    // the behavior of a simple assignment in other languages.
    let test_world = World::from_scenario(path).expect("Failed to load scenario");

    println!("World loaded at tick {}", test_world.tick);
    println!("Agents loaded: {}", test_world.agents.len());
    
    // Check the complex enum state
    for agent in test_world.agents {
        match agent.state {
            agent::AgentState::Moving(target_pos) => {
                println!("Agent {} is moving to x:{} y:{}", agent.id, target_pos.x, target_pos.y);
            },
            agent::AgentState::Attacking(target_id) => {
                println!("Agent {} is attacking Agent {}", agent.id, target_id);
            },
            _ => println!("Agent {} is {:?}", agent.id, agent.state),
        }
    }
}