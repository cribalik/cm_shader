@vert
@out vec3 color;
void main() {
    vec2 pos = vec2[3](vec2(-0.5,-0.5), vec2(0,0.5), vec2(0.5, -0.5))[gl_VertexIndex];
    color = vec3[3](vec3(1,0,0), vec3(0,1,0), vec3(0,0,1))[gl_VertexIndex];
    gl_Position = vec4(pos, 0.0, 1.0);
}
@end

@frag
@out(format=rgba8) vec4 color_out;
void main() {
    color_out = vec4(color, 1);
}
@end
