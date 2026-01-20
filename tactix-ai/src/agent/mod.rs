use serde::{Deserialize, Serialize};
use crate::utils::Vec2;


#[derive(Debug, Deserialize, Serialize)]
pub enum AgentAction {
    MoveTo(Vec2),
    Idle,
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
    pub target_position: Option<Vec2>,
}

impl Agent {
    pub fn is_alive(&self) -> bool {
        matches!(self.state, AgentState::Dead | AgentState::Downed) == false
    }

    pub fn apply_action(&mut self, action: AgentAction) {
        match action {
            AgentAction::MoveTo(target) => self.move_towards(target),
            AgentAction::Idle => {}
        }
    }

    pub fn move_towards(&mut self, target: Vec2) {
        let dir = target - self.position;
        let dist = dir.length();

        if dist < 0.01 {
            self.position = target;
            self.target_position = None;
        } else {
            let step = dir.normalized() * 1.0;
            self.position += step;
        }
    }
}