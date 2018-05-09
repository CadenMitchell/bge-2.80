in vec4 texcovar;
out vec4 fragColor;

uniform sampler2D colortex;
uniform sampler2D depthtex;

void main()
{
	vec2 co = texcovar.xy;
	fragColor = texture2D(colortex, co);
	gl_FragDepth = texture2D(depthtex, co).x;
}
