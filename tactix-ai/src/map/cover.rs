use serde::Deserialize;
use crate::utls::math::Vec2;

#[derive(Debug, Deserialize)]
pub struct CoverNode {
    pub id: u32,
    pub position: Vec2,
    pub protection: f32,    // 0.0 (none) to 1.0 (full)
}