#version 330

// Vertex attributes
in vec3 vertexPosition;
in vec2 vertexTexCoord;

// Instance attributes (per-agent data)
in vec2 instancePosition;  // Agent position in world space

// Uniforms
uniform mat4 mvp;

// Outputs to fragment shader
out vec2 fragTexCoord;

void main() {
    // Scale circle to 5x5 pixels (2.5 radius)
    vec2 scaledPos = vertexPosition.xy * 2.5;
    
    // Translate to instance position
    vec2 worldPos = scaledPos + instancePosition;
    
    gl_Position = mvp * vec4(worldPos, 0.0, 1.0);
    fragTexCoord = vertexTexCoord;
}
