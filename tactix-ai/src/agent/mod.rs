use serde::{Deserialize, Serialize};

// Re-export Vec2 or define it here if it's not in a shared 'utils' module.
// Assuming Vec2 is defined in this file or imported:
#[derive(Debug, Deserialize, Serialize, Clone)]
pub struct Vec2 {
    pub x: f32,
    pub y: f32,
}

#[derive(Debug, Deserialize, Serialize)]
pub enum AgentState {
    Idle,
    // This expects a JSON object: { "Moving": { "x": 1.0, "y": 2.0 } }
    Moving(Vec2), 
    // This expects a JSON object: { "Attacking": 10 }
    Attacking(u32), 
    Suppressing(u32),
    Downed,
    Dead,
}

#[derive(Debug, Deserialize, Serialize)]
pub struct Agent {
    pub id: u32,
    pub team: u8,
    pub position: Vec2,
    pub health: f32,
    pub morale: f32,
    pub state: AgentState,
}