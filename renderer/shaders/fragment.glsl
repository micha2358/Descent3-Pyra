#version 100
precision mediump float;
varying vec4 vertex_color;
varying vec2 vertex_uv0;
varying vec2 vertex_uv1;
varying vec4 vertex_modelview_pos;
uniform sampler2D u_texture0; uniform sampler2D u_texture1;
uniform int u_texture_enable; uniform bool u_fog_enable; uniform vec4 u_fog_color;
uniform float u_fog_start; uniform float u_fog_end; uniform float u_gamma;
float inv_or_zero(float v){ float s=sign(v)*sign(v); return s/(v+s-1.0); }
void main(){
 // Both switches are uniform for a draw. Do not fetch a texture that is
 // disabled: the old mix() path sampled both textures for every fragment.
 vec4 c = vertex_color;
 if (u_texture_enable == 1 || u_texture_enable == 3)
   c *= texture2D(u_texture0, vertex_uv0);
 if (u_texture_enable >= 2)
   c *= texture2D(u_texture1, vertex_uv1);

 // Fog distance (including length/sqrt and reciprocal) is only necessary
 // when fog is enabled, not for every pixel in menus or UI.
 if (u_fog_enable) {
   float fog = clamp((u_fog_end - length(vertex_modelview_pos)) *
                     inv_or_zero(u_fog_end - u_fog_start), 0.0, 1.0);
   c = c * fog + (1.0 - fog) * u_fog_color;
 }

 // Keep the original gamma correction, but avoid three pow() calls per
 // fragment at the neutral gamma value (1.0).
 if (u_gamma != 1.0)
   c.rgb = pow(c.rgb, vec3(1.0 / u_gamma));
 gl_FragColor = c;
}
