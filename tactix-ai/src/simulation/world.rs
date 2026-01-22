use std::collections::HashMap;
use serde::Deserialize;
use std::fs::File;
use std::io::BufReader;
use crate::agent::agent::{Agent, AgentAction, AgentState, PerceptionData};
use crate::simulation::config::SimulationConfig;
use crate::utls::math::Vec2;
use crate::map::cover::CoverNode;

#[derive(Debug, Deserialize)]
pub struct World {
    pub agents: Vec<Agent>, 
    pub cover_nodes: Vec<CoverNode>,
    pub config: SimulationConfig,

    #[serde(default)]
    pub tick: u64,
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
        // Phase 1: Gather Data
        let perceptions = self.gather_perceptions();
        let actions = self.calculate_decisions(&perceptions);

        // Phase 2 : Apply changes
        self.resolve_actions(actions);
        self.update_passive_stats();

        // Phase 3: End of Tick Updates
        self.tick += 1;
        self.log_tick();
    }

    /// 1. Look at the world state without changing it
    fn gather_perceptions(&self) -> HashMap<u32, PerceptionData> {
        let mut map = HashMap::new();
        for agent in &self.agents {
            // Logic to check line-of-sight goes here
            // For Phase 0, just check distance
            map.insert(agent.id, PerceptionData { 
                visible_enemies: vec![], // Fill this logic later
                nearest_cover: None,
                is_under_fire: false
            });
        }
        map
    }

    /// 2. Ask agents what they WANT to do based on perception
    fn calculate_decisions(&self, perceptions: &HashMap<u32, PerceptionData>) -> Vec<(u32, AgentAction)> {
        let mut action_queue = Vec::new();

        for agent in &self.agents {
            // If dead, do nothing
            if agent.health <= 0.0 { continue; }

            let perception = perceptions.get(&agent.id).unwrap();
            
            // Pass the perception to the agent's brain
            // We return a Tuple (AgentID, Action) so we can map it back later
            let action = agent.decide(perception); 
            action_queue.push((agent.id, action));
        }

        action_queue
    }

    /// 3. Execute the actions (Mutate the world)
    fn resolve_actions(&mut self, actions: Vec<(u32, AgentAction)>) {
        // We separate Movement and Combat.
        // Usually, you want to resolve Shots before Movement (or vice versa),
        // but pick ONE order to stay deterministic.
        
        for (agent_id, action) in actions {
            match action {
                AgentAction::MoveTo(pos) => {
                    self.move_agent(agent_id, pos);
                }
                AgentAction::Attack(target_id) => {
                    self.resolve_combat(agent_id, target_id);
                }
                AgentAction::Idle => { /* Do nothing */ }
            }
        }
    }

    // Helper to find agent mutable by ID
    // Note: iterating a Vec to find an ID is O(N), but fine for <100 agents.
    fn move_agent(&mut self, agent_id: u32, target: Vec2) {
        if let Some(agent) = self.agents.iter_mut().find(|a| a.id == agent_id) {
            // Simple interpolation for Phase 0
            // logic: move agent.position closer to target
            agent.position = target; // Teleport for now, add delta_time later
        }
    }

    fn resolve_combat(&mut self, attacker_id: u32, target_id: u32) {
        // This is tricky in Rust because we need to look up Attacker AND Target
        // in the same mutable Vec. 

        println!("Agent {} is shooting at Agent {}!", attacker_id, target_id);
        
        // 1. Get attacker stats (Copy what we need to avoid borrow conflicts)
        let (accuracy, damage) = {
            let attacker = self.agents.iter().find(|a| a.id == attacker_id);
            if let Some(a) = attacker {
                (a.weapon.accuracy, a.weapon.damage)
            } else {
                return; // Attacker died before shooting?
            }
        };

        // 2. Roll RNG (Deterministic!)
        // let hit = self.rng.roll() < accuracy; 

        // 3. Apply to Target
        if let Some(target) = self.agents.iter_mut().find(|a| a.id == target_id) {
             target.health -= damage;
             println!("Agent {} hit Agent {}!", attacker_id, target_id);
        }
        else {
            println!("Agent {} missed Agent {}!", attacker_id, target_id);
        }
    }
    
    fn update_passive_stats(&mut self) {
         // Loop through agents, lower morale if under fire, etc.
    }

    fn log_tick(&self) {
        println!("\n========================================");
        println!("           TICK {} REPORT", self.tick);
        println!("========================================");

        for agent in &self.agents {
            // 1. visual status indicator (Skull for dead, Circle for alive)
            let status_icon = if agent.health <= 0.0 { "💀" } else { "🟢" };
            
            // 2. Format the state into a readable string
            let state_desc = match agent.state {
                AgentState::Idle => "Holding Position".to_string(),
                AgentState::Moving(pos) => format!("Moving to ({:.1}, {:.1})", pos.x, pos.y),
                AgentState::Attacking(target_id) => format!("FIRING at Agent #{}", target_id),
                AgentState::Suppressing(target_id) => format!("Suppressing Agent #{}", target_id),
                AgentState::Downed => "DOWNED - Bleeding Out".to_string(),
                AgentState::Dead => "KIA".to_string(),
                _ => "Unknown".to_string(), // Fallback
            };

            // 3. Print the aligned row
            // {:02} = pad numbers with zero (01, 02)
            // {:>5.1} = align right, 1 decimal place
            println!(
                "{} [Agent {:02} | T{}] HP: {:>5.1} | Mor: {:>3.0}% | {}", 
                status_icon, 
                agent.id, 
                agent.team, 
                agent.health, 
                agent.morale * 100.0, 
                state_desc
            );
        }
        println!("----------------------------------------\n");
    }

}