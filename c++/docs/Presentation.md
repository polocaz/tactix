# **Linux – GPU Collection Expansion (Script)**

**“This slide covers our expanded GPU metric collection on Linux.**  

**Previously, we only collected GPU data for NVIDIA devices.**  

**With this update, Linux now supports GPU utilization and memory usage collection for AMD, NVIDIA, and Intel GPUs.**

**On macOS, there are no changes.**  
**The existing collection of GPU utilization and frequency remains the same.**

**For anyone troubleshooting or validating this functionality, most of the related diagnostic logging is at Trace level 5.**

**On the right side of the slide, you can see an example of the new data collected from an AMD GPU on Linux.**  

---

# **Android Changes (Script)**

**On the left, we have the first major change related to permissions.**  

**Google flagged that our app did not have a valid use case for the Photo and Video permissions under the current scoped storage rules.**  

**So to stay compliant, we removed those permission requests from the app.**


**The only user-facing impact is the removal of our in-app storage usage breakdown, which depended on media access.**  

**Aside from that, no agent functionality changed, and the app operates the same as before.**

---

**On the right side, you’ll see the second major change, which is our migration to API level 35.**  

**We updated the app to API 35 to meet Play Store requirements for upcoming releases.**  

**This migration mainly affects UI behavior because API 35 now enforces edge-to-edge layouts by default.**

**As a result, we made a few minor adjustments to padding, insets, and spacing around the status bar.**  
**However, no features or workflows were changed, and the app’s functionality remains the same.**

That is all for android