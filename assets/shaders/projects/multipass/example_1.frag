uniform vec2 u_resolution;
uniform float u_time;

uniform sampler2D u_buffer0;
uniform sampler2D u_buffer1;
uniform sampler2D u_buffer2;

in vec2  vTexCoord0;
out vec4 oColor;

void main() {
    vec2 uv = gl_FragCoord.xy / u_resolution.xy;
    // vec2 mouse_uv = u_mouse.xy / u_resolution.xy;

#ifdef BUFFER_0
    oColor = vec4(1. * (sin(u_time / .3) + 1.) / 2., 0., 0., 1.);
#elif defined( BUFFER_1 )
    oColor = vec4(0., 1., 0., 1.);
#elif defined( BUFFER_2 )
    oColor = vec4(0., 0., 1., 1.);
#else
    oColor = texture(u_buffer0, uv) + texture(u_buffer1, uv) + texture(u_buffer2, uv);
//    oColor = texture(u_buffer0, vTexCoord0);
#endif
}
