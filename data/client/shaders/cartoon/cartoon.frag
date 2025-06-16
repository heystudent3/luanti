
// Cartoon fragment shader
// Implements basic cel shading by quantizing diffuse lighting.

uniform sampler2D texture0;
uniform vec3 LightDirection; // Directional light direction (e.g., from sun)
uniform vec4 materialColor;  // Material color from Irrlicht

varying vec3 vNormal;
varying vec2 vTexCoord;
varying vec4 vColor;
varying vec3 vWorldPos;

const vec3 AMBIENT_COLOR = vec3(0.2, 0.2, 0.2); // Base ambient light
const vec3 DIFFUSE_COLOR = vec3(0.8, 0.8, 0.8); // Diffuse light color

void main()
{
    // Normalize the interpolated normal
    vec3 normal = normalize(vNormal);

    // Simple directional light calculation
    // For cel shading, we'll quantize the diffuse component
    float diff = max(dot(normal, -LightDirection), 0.0);

    // Quantize diffuse lighting into steps
    // Adjust these thresholds and colors to get the desired cel-shading look
    float lightLevel = 0.0;
    if (diff > 0.85) {
        lightLevel = 1.0;
    } else if (diff > 0.5) {
        lightLevel = 0.6;
    } else if (diff > 0.15) {
        lightLevel = 0.3;
    } else {
        lightLevel = 0.0;
    }

    // Combine ambient and quantized diffuse light
    vec3 finalLight = AMBIENT_COLOR + DIFFUSE_COLOR * lightLevel;

    // Sample the texture
    vec4 texColor = texture2D(texture0, vTexCoord);

    // Final color combines texture, vertex color, and cel lighting
    gl_FragColor = texColor * vColor * vec4(finalLight, 1.0) * materialColor;
}
