// SPDX-License-Identifier: MIT
// Shared motion contract v1 (GLSL side of motion_contract.hpp). Byte-identical
// in the FSR / frame generation mod and Native AA; pinned by each repo's source
// check. Include with GL_GOOGLE_include_directive. Original code.
#ifndef XPM_MOTION_CONTRACT
#define XPM_MOTION_CONTRACT 1
const float XPM_MAX_MOTION_FRACTION=2.0;
const float XPM_STORE_LIMIT=32700.0;
const float XPM_REACTIVE_INVALID=1.0;
const float XPM_REACTIVE_RIGID_CABIN=0.05;
const float XPM_REACTIVE_UNKNOWN_CABIN=1.0;
const float XPM_REACTIVE_CABIN_GHOSTING=0.2;
const float XPM_REACTIVE_SKY_GHOSTING=0.15;
// Previous-frame UV of a point already transformed into previous clip space.
vec2 xpm_previous_uv(vec4 previousClip,float ySign){
    vec2 oldUv=previousClip.xy/previousClip.w;
    oldUv.y*=ySign;
    return oldUv*0.5+0.5;
}
// Canonical motion: previous minus current, render pixels, +Y down, unjittered.
vec2 xpm_motion(vec4 previousClip,vec2 unjitteredUv,vec2 size,float ySign){
    return (xpm_previous_uv(previousClip,ySign)-unjitteredUv)*size;
}
bool xpm_motion_invalid(vec2 motion,vec2 size){
    return any(isnan(motion))||any(isinf(motion))||
        any(greaterThan(abs(motion),size*XPM_MAX_MOTION_FRACTION));
}
vec2 xpm_store(vec2 motion){
    return clamp(motion,vec2(-XPM_STORE_LIMIT),vec2(XPM_STORE_LIMIT));
}
#endif
