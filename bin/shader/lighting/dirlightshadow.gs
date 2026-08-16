#version 430 core
layout (triangles) in;
layout (triangle_strip, max_vertices=48) out;

uniform mat4 shadowMatrices[16];
uniform int count = 0;

void processIndex(int index)
{
    for(int i = 0; i < 3; ++i)
    {
        gl_Position = shadowMatrices[index] * gl_in[i].gl_Position;
        EmitVertex();
    }
    EndPrimitive();
}

void main()
{
    for(int instance_index = 0 ; instance_index < count; ++instance_index)
    {
        gl_ViewportIndex = instance_index;
        processIndex(instance_index);
    }
}