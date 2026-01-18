use serde::Deserialize;
use std::fs::File;
use std::io::BufReader;
use crate::agent::{Agent, Vec2}; // Import from sibling module

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
}