#version 450
layout(set=0,binding=0)uniform sampler sceneSampler;
layout(set=0,binding=1)uniform texture2DArray scene;
layout(location=0)out vec4 color;
void main(){vec2 size=vec2(textureSize(sampler2DArray(scene,sceneSampler),0).xy);color=texture(sampler2DArray(scene,sceneSampler),vec3(gl_FragCoord.xy/size,0));}
