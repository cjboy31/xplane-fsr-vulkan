#version 450
layout(location=0)in vec2 scenePosition;
layout(location=0)out vec4 color;
void main(){color=scenePosition.y<.37*scenePosition.x+31.2?vec4(.2,.4,.6,1):vec4(.8,.7,.1,1);}
