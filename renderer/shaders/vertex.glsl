#version 100
precision highp float;
attribute vec3 in_pos;
attribute vec4 in_color;
attribute vec2 in_uv0;
attribute vec2 in_uv1;
varying vec4 vertex_color;
varying vec2 vertex_uv0;
varying vec2 vertex_uv1;
varying vec4 vertex_modelview_pos;
uniform mat4 u_modelview;
uniform mat4 u_projection;
void main() {
 vertex_modelview_pos=u_modelview*vec4(in_pos,1.0);
 gl_Position=u_projection*vertex_modelview_pos;
 vertex_color=in_color; vertex_uv0=in_uv0; vertex_uv1=in_uv1;
}
