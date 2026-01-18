use std::fs::File;
use std::io::BufReader;
use std::path::Path;
use std::error::Error;

// Import the Agent struct from the root of the crate
use crate::agent::Agent; 

// Helper struct to match your JSON structure: { "agents": [...] }
#[derive(serde::Deserialize)]
struct ProjectJsonRoot {
    agents: Vec<Agent>,
}

// Result<T, E> is the standard way to handle IO errors in Rust
pub fn load_agents_from_file<P: AsRef<Path>>(path: P) -> Result<Vec<Agent>, Box<dyn Error>> {
    // 1. Open the file
    let file = File::open(path)?;
    let reader = BufReader::new(file);

    // 2. Parse the JSON
    let parsed_data: ProjectJsonRoot = serde_json::from_reader(reader)?;

    // 3. Return just the list of agents
    Ok(parsed_data.agents)
}