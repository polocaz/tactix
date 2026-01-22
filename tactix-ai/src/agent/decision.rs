use crate::agent::agent::{Agent, AgentAction, PerceptionData};
impl Agent {
    pub fn decide(&self, perception: &PerceptionData) -> AgentAction {
        // PRIORITY 1: CRITICAL SURVIVAL
        // If I am about to die, ignore everything and run to cover.
        if self.health < 25.0 {
            if let Some(cover_pos) = perception.nearest_cover {
                // Don't move if we are already there (simple distance check)
                if self.position.distance(cover_pos) > 1.0 {
                    return AgentAction::MoveTo(cover_pos);
                }
            }
            // If no cover is found, we might as well shoot back (fall through to next step)
        }

        // PRIORITY 2: SELF PRESERVATION
        // If I am being shot at, get to cover before doing anything else.
        if perception.is_under_fire {
            if let Some(cover_pos) = perception.nearest_cover {
                if self.position.distance(cover_pos) > 1.0 {
                    return AgentAction::MoveTo(cover_pos);
                }
            }
        }

        // PRIORITY 3: ENGAGEMENT
        // If I see a bad guy, shoot them.
        // (Later you can add logic to pick the closest or most dangerous target)
        if let Some(target_id) = perception.visible_enemies.first() {
            return AgentAction::Attack(*target_id);
        }

        // PRIORITY 4: MISSION / IDLE
        // If safe and no enemies, move forward or hold position.
        // For Phase 0, we'll just hold.
        AgentAction::Idle
    }
}