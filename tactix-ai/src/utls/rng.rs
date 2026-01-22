use rand::{Rng, SeedableRng};
use rand::rngs::StdRng;

pub struct SeededRng {
    rng: StdRng,
}

impl SeededRng {
    pub fn new(seed: u64) -> Self {
        Self {
            rng: StdRng::seed_from_u64(seed),
        }
    }
    
    pub fn roll(&mut self) -> f32 {
        self.rng.random()
    }
    
    pub fn range(&mut self, min: f32, max: f32) -> f32 {
        min + self.roll() * (max - min)
    }
}