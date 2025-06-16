
// Standard vertex shader for cartoon rendering
// Performs model-view-projection transformations
// Passes vertex color, texture coordinates, normal, and world position to the fragment shader.

attribute vec4 irr_Vertex;
attribute vec3 irr_Normal;
attribute vec2 irr_MultiTexCoord1;
attribute vec4 irr_Color;

uniform mat4 mWorldViewProj; // Model-View-Projection matrix
uniform mat4 matWorld;       // World matrix (for normal transformation)

varying vec3 vNormal;
varying vec2 vTexCoord;
varying vec4 vColor;
varying vec3 vWorldPos;

void main()
{
    gl_Position = mWorldViewProj * irr_Vertex;
    vNormal = normalize(mat3(matWorld) * irr_Normal); // Transform normal to world space
    vTexCoord = irr_MultiTexCoord1;
    vColor = irr_Color;
    vWorldPos = (matWorld * irr_Vertex).xyz; // Transform vertex to world space
}
