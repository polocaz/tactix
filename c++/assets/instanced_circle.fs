#version 330

// Input from vertex shader
in vec2 fragTexCoord;

// Output
out vec4 finalColor;

void main() {
    // Calculate distance from center
    vec2 centered = fragTexCoord * 2.0 - 1.0;
    float dist = length(centered);
    
    // Discard pixels outside circle
    if (dist > 1.0) {
        discard;
    }
    
    // Simple white circle
    finalColor = vec4(1.0, 1.0, 1.0, 1.0);
}
