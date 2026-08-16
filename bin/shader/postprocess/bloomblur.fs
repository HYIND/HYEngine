#version 330
out vec4 FragColor;

in vec2 TexCoords;

uniform sampler2D image;

uniform bool horizontal;

uniform float weight[5] = float[] (0.2270270270, 0.1945945946, 0.1216216216, 0.0540540541, 0.0162162162);

void main()
{
	vec2 tex_offset = 1.0 / textureSize(image, 0);
	vec3 result = texture(image, TexCoords).rgb * weight[0];
	if(horizontal)
	{
		for(int i = 1; i < 5; i++)
		{
			float offset = tex_offset.x * i;
			// result += texture(image, vec2(TexCoords.x - offset, TexCoords.y)).rgb * weight[i];
			// result += texture(image, vec2(TexCoords.x + offset, TexCoords.y)).rgb * weight[i];

			if (TexCoords.x - offset >= 0.0)
                result += texture(image, vec2(TexCoords.x - offset, TexCoords.y)).rgb * weight[i];
            if (TexCoords.x + offset <= 1.0)
                result += texture(image, vec2(TexCoords.x + offset, TexCoords.y)).rgb * weight[i];
		}
	}
	else 
	{
		for(int i = 1; i < 5; i++)
		{
			float offset = tex_offset.y * i;
			// result += texture(image, vec2(TexCoords.x, TexCoords.y - offset)).rgb * weight[i];
			// result += texture(image, vec2(TexCoords.x, TexCoords.y + offset)).rgb * weight[i];

			if (TexCoords.y - offset >= 0.0)
				result += texture(image, vec2(TexCoords.x, TexCoords.y - offset)).rgb * weight[i];
            if (TexCoords.y + offset <= 1.0)
				result += texture(image, vec2(TexCoords.x, TexCoords.y + offset)).rgb * weight[i];
		}
	}

	FragColor = vec4(result, 1.0f);
}