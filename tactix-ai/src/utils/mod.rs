

#[derive(Debug, Deserialize, Serialize, Clone)]
pub struct Vec2 {
    pub x: f32,
    pub y: f32,
}


impl Vec2 {
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
}