use serde::Deserialize;


#[derive(Debug, Deserialize, Clone)]
pub struct SimulationConfig {
    pub rng_seed: u64,      // CRITICAL for determinism
    pub tick_rate: f32,
}