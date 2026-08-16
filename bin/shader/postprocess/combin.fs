#version 330 core

in vec2 TexCoords;

layout (location = 0) out vec4 FragColor;
layout (location = 1) out vec4 BrightColor;

#ifndef MAX_COLOR_NUMBER
#define MAX_COLOR_NUMBER 10
#endif

#ifndef COMBIN_MODE
#define COMBIN_MODE 0 // 0:Alpha Blend 1:Additive
#endif

uniform sampler2D ColorMap[MAX_COLOR_NUMBER];
uniform int ColorMapCount = 0;

#if COMBIN_MODE == 0
vec4 combinColor(vec4 bottom, vec4 top)
{
	vec3 color = top.rgb * top.a + bottom.rgb * (1.0 - top.a);
	float alpha = top.a + bottom.a * (1.0 - top.a);
	return vec4(color, alpha);
}
#endif

#if COMBIN_MODE == 1
vec4 combinColor(vec4 bottom, vec4 top)
{
	vec3 color = bottom.rgb + top.rgb;
	float alpha = clamp(top.a + bottom.a, 0, 1);
	return vec4(color, alpha);
}
#endif

void main()
{
	vec4 result = vec4(0.f);
	int mapcount = min(ColorMapCount, MAX_COLOR_NUMBER);

	for(int i = 0; i < mapcount; i++)
	{
		vec4 color = texture(ColorMap[i], TexCoords).rgba;
		result = combinColor(result, color);
	}

	FragColor = result;
	float brightness = dot(FragColor.rgb, vec3(0.2126, 0.7152, 0.0722));
    if(brightness > 1.0)
        BrightColor = vec4(FragColor.rgb, 1.0);
	else
		BrightColor = vec4(0.0, 0.0, 0.0, 1.0);
}