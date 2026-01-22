use serde::{Deserialize, Serialize};
use std::ops::{AddAssign, Mul,Sub};

#[derive(Debug, Deserialize, Serialize, Clone, Copy)]
pub struct Vec2 {
    pub x: f32,
    pub y: f32,
}


impl Vec2 {
    pub fn distance(self, b: Vec2) -> f32 {
        ((self.x - b.x).powi(2) + (self.y - b.y).powi(2)).sqrt()
    }

    pub fn length(&self) -> f32 {
        (self.x * self.x + self.y * self.y).sqrt()
    }

    pub fn normalized(&self) -> Vec2 {
        let len = self.length();
        if len == 0.0 {
            Vec2 { x: 0.0, y: 0.0 }
        } else {
            Vec2 {
                x: self.x / len,
                y: self.y / len,
            }
        }
    }

    pub fn sub(self, other: Vec2) -> Vec2 {
        Vec2 {
            x: self.x - other.x,
            y: self.y - other.y,
        }
    }
}

impl AddAssign for Vec2 {
    fn add_assign(&mut self, other: Vec2) {
        self.x += other.x;
        self.y += other.y;
    }
}

impl Mul for Vec2 {
    type Output = Vec2;

    fn mul(self, other: Vec2) -> Self::Output {
        Self {
            x: self.x * other.x,
            y: self.y * other.y,
        }
    }
}

impl Mul<f32> for Vec2 {
    type Output = Vec2;

    fn mul(self, scalar: f32) -> Self::Output {
        Self {
            x: self.x * scalar,
            y: self.y * scalar,
        }
    }
}

impl Sub for Vec2 {
    type Output = Vec2;

    fn sub(self, other: Vec2) -> Self::Output {
        Self {
            x: self.x - other.x,
            y: self.y - other.y,
        }
    }
}
