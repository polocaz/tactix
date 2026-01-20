use serde::Deserialize;
use std::fs::File;
use std::io::BufReader;
use crate::agent::{Agent, AgentAction};
use crate::utils::Vec2;

#[derive(Debug, Deserialize)]
pub struct CoverNode {
    pub id: u32,
    pub position: Vec2,
    pub protection: f32,
}

#[derive(Debug, Deserialize)]
pub struct World {
    pub agents: Vec<Agent>,
    pub cover_nodes: Vec<CoverNode>,
    // Default to 0 if not in JSON, or require it. 
    // Here we require it to be in the JSON.
    pub tick: u64, 
    pub tick_count: u64,
}

impl World {
    /// Loads a world from a JSON file path.
    /// Returns a Result because IO or Parsing might fail.
    pub fn from_scenario(path: &str) -> Result<Self, Box<dyn std::error::Error>> {
        let file = File::open(path)?;
        let reader = BufReader::new(file);
        
        // Deserialize directly into the World struct
        let world_obj: World = serde_json::from_reader(reader)?;
        
        Ok(world_obj)
    }

    pub fn tick(&mut self) {
        self.tick_count += 1;

        // 1. Decision Phase
        let actions = self.collect_actions();

        // 2. Action Phase
        self.apply_actions(actions);

        // 3. Logging
        self.log_state();
    }

    fn collect_actions(&self) -> Vec<AgentAction> {
        self.agents.iter().map(|agent| {
            if let Some(target) = agent.target_position {
                AgentAction::MoveTo(target)
            } else {
                AgentAction::Idle
            }
        }).collect()
    }
    
    fn apply_actions(&mut self, actions: Vec<AgentAction>) {
        for (agent, action) in self.agents.iter_mut().zip(actions) {
            agent.apply_action(action);
        } 
    }

    fn log_state(&self) {
    println!("--- TICK {} ---", self.tick_count);
    for agent in &self.agents {
        println!(
            "Agent {} pos=({:.2},{:.2})",
            agent.id,
            agent.position.x,
            agent.position.y
        );
    }
}

}