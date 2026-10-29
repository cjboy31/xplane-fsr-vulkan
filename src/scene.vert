#version 450
layout(location=0)out vec2 scenePosition;
void main(){
    const vec2 positions[3]=vec2[3](vec2(-1,-1),vec2(3,-1),vec2(-1,3));
    gl_Position=vec4(positions[gl_VertexIndex],.5,1);
    scenePosition=(positions[gl_VertexIndex]+1)*vec2(128,72);
}
