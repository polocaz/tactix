use serde::{Deserialize, Serialize};

#[derive(Debug, Clone, Copy, PartialEq, Serialize, Deserialize)]
pub struct WeaponStats {
    pub range: f32,
    pub accuracy: f32,      // 0.0–1.0
    pub damage: f32,
    pub fire_rate: f32,
}