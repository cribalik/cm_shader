@depth less write d24 clip
@multisample 4
@cull front
@primitive triangle_list

@vert

    @in() vec3 v0;
    @in(buffer=3, instanced) vec3 v1;
    @in(buffer=0, type=u8) vec4 v2;

    @sampler sampler2D s1;
    @buffer writeonly {int b1;};
    @image(format=rgba8) readonly image2D t1;
    @uniform {int u1;};

    void main() {gl_Position = vec4(0,0,0,1);}

@end

@frag

    @blend src_alpha one_minus_src_alpha add
    @out(format=rgba8) vec4 f0;

    @blend zero one subtract
    @out(format=rg32f) vec2 f1;

    @blend src_color dst_alpha max
    @out(format=r11g11b10f) vec4 f2;

    @sampler sampler2D s2;
    @buffer {int b2;};
    @image(format=r16_snorm) image2D t2;
    @uniform {int u2;};

    void main() {}

@end