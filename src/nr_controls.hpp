// SPDX-License-Identifier: MIT
#pragma once
#include <array>
#include <cstdint>
#include <windows.h>
#include "named_lock.hpp"
namespace xpfsr::nr {
enum Field { Enabled, Intensity, Tone, Structure, Skin, Colour, Detail, Highlight,
             History, Style, Scale, Passes, Transfer, Scaler, Model, Mask, Grade, AllowFG, Scope, Order, Count };
struct Setting { const wchar_t *key, *label, *help; int minimum, maximum, initial; };
inline constexpr Setting settings[Count] = {
 {L"NROn",L"Enable neural rendering",L"Turn neural rendering on or off. It changes the look of lighting and detail. It needs a supported graphics card and an imported model. If it cannot run, FSR keeps working.",0,1,0},
 {L"NRIntensity",L"Effect strength (%)",L"Mix the new look with the original picture. Start at 25%. Higher values make a bigger change, but do not make the model run faster.",0,100,25},
 {L"NRTone",L"Lighting change (%)",L"Change how much the model affects lighting and contrast. Start at 0% to keep the original lighting. Raise it slowly if you want a stronger look.",0,200,0},
 {L"NRStructure",L"Fine detail (%)",L"Control the model's small details and contrast. Start at 100%. Too much can make thin lines sparkle or flicker.",0,200,100},
 {L"NRSkin",L"Character detail (%)",L"Adjust detail on people where the model recognizes them. Auto follows Fine detail. This may have little effect in most flight views.",-100,200,-100},
 {L"NRColour",L"Color strength (%)",L"Mix the model's colors into the scene. Start at 25%. High values can make colors look too strong.",0,400,25},
 {L"NRDetail",L"Texture detail mix (%)",L"Mix the model's detail with the original textures. Start at 50%. Lower it if ground textures look soft or unstable.",0,200,50},
 {L"NRHighlight",L"Bright-light protection (%)",L"Limit large changes around bright lights. Start at 150%. Higher values let the model change bright areas more.",100,800,150},
 {L"NRHistory",L"Motion smoothing (%)",L"Balance steady detail against quick movement. Start at 50%. Lower it if moving clouds or scenery leave trails.",0,100,50},
 {L"NRStyle",L"Visual style",L"Choose Standard, Natural or Cinematic. These change the look of the picture. They do not change the AA method.",0,2,0},
 {L"NRModelScale",L"Model image size (%)",L"Choose how large a picture the model processes. Lower values can improve speed but lose fine detail. Start at 50%. Changes prepare in the background while you fly.",25,100,50},
 {L"NRPasses",L"Processing passes",L"Choose how many times the model processes the picture. Use one pass for flight. Extra passes take more graphics time and can blur ground textures. More passes do not mean better AA.",1,3,1},
 {L"NRTransfer",L"Detail blending method",L"Choose how the model's result is mixed into the scene. Matched detail is the usual starting point. Edge-aware puts more focus on lighting and color. Classic resizes the whole result.",0,2,0},
 {L"NRScaler",L"Classic resize filter",L"Choose how Classic mode enlarges the model picture. This only affects the Classic blending method. Try Catmull-Rom first; switch if edges look too soft or bright.",0,3,1},
 {L"NRModel",L"Model size",L"Auto prefers the smaller model. Small usually costs less graphics time. Original uses the larger model. Import the matching model first. A missing model leaves NR off and keeps FSR running.",0,2,0},
 {L"NRMask",L"Automatic edit mask",L"Let the model choose where its changes are applied. This does not protect every cockpit display. Compare On and Off in the same view.",0,1,1},
 {L"NRGrade",L"Style color finish",L"Add the color finish for Natural or Cinematic style. Standard is unchanged. Turn this off if you prefer the original colors.",0,1,1},
 {L"NRAllowFG",L"Allow NR with frame generation",L"Allow both effects together. This uses more graphics time and may show visual errors. Start with frame generation off, then compare. The frame-generation choice needs a restart.",0,1,0},
 {L"NRScope",L"Where to use NR",L"Outside only pauses NR in the cockpit. Whole scene includes the cockpit. This changes live. Low strength is a good starting point because NR can change small display text.",0,1,0},
 {L"NROrder",L"When to apply NR",L"Before upscaling is recommended: NR works on the smaller picture first, which usually costs less graphics time. After upscaling processes the larger picture. Changes apply live once preparation finishes.",0,1,0}
};
struct Controls {
 std::array<int32_t,Count> value{};
 Controls(){for(int i=0;i<Count;++i)value[i]=settings[i].initial;}
 int32_t& operator[](Field i){return value[i];}
 int32_t operator[](Field i)const{return value[i];}
 bool valid()const{
  for(int i=0;i<Count;++i)if(value[i]<settings[i].minimum||value[i]>settings[i].maximum)return false;
  return (*this)[Scale]==25||(*this)[Scale]==33||(*this)[Scale]==50||(*this)[Scale]==67||(*this)[Scale]==100;
 }
 bool operator==(const Controls&o)const{return value==o.value;}
 bool operator!=(const Controls&o)const{return !(*this==o);}
};
inline void performanceProfile(Controls&c,int profile){
 c[Scale]=profile==0?33:profile==2?67:50;c[Passes]=1;c[Model]=0;
 // Retain artistic strengths; performance presets only change the work budget.
 c[Transfer]=0;
}
// Separate IPC ABI: camera/plugin memory layout stays unchanged. All data words
// use interlocked access; bounded reads reject a concurrent/torn update.
struct SharedControls { volatile LONG magic{},version{},bytes{},sequence{}; volatile LONG value[Count]{}; };
inline constexpr LONG controlMagic=0x4e525833,controlVersion=4;
inline void writeControls(SharedControls*p,const Controls&c){
 if(!p||!c.valid())return;
 InterlockedIncrement(&p->sequence);
 for(int i=0;i<Count;++i)InterlockedExchange(&p->value[i],c.value[i]);
 InterlockedExchange(&p->bytes,sizeof(*p));InterlockedExchange(&p->version,controlVersion);
 InterlockedExchange(&p->magic,controlMagic);InterlockedIncrement(&p->sequence);
}
inline bool readControls(SharedControls*p,Controls&result,LONG*revision=nullptr){
 if(!p)return false;
 for(int retry=0;retry<3;++retry){
  LONG before=InterlockedCompareExchange(&p->sequence,0,0);if(before&1)continue;
  if(InterlockedCompareExchange(&p->magic,0,0)!=controlMagic||InterlockedCompareExchange(&p->version,0,0)!=controlVersion||InterlockedCompareExchange(&p->bytes,0,0)!=sizeof(*p))return false;
  Controls c;for(int i=0;i<Count;++i)c.value[i]=InterlockedCompareExchange(&p->value[i],0,0);
  LONG after=InterlockedCompareExchange(&p->sequence,0,0);
  if(before==after&&!(after&1)&&c.valid()){result=c;if(revision)*revision=after;return true;}
 }
 return false;
}
class ControlChannel {
 HANDLE handle{};SharedControls*view{};std::wstring writerName;
 public:
 ControlChannel()=default;ControlChannel(const ControlChannel&)=delete;ControlChannel&operator=(const ControlChannel&)=delete;
 ~ControlChannel(){close();}
 void close(){if(view)UnmapViewOfFile(view);if(handle)CloseHandle(handle);view=nullptr;handle=nullptr;writerName.clear();}
 bool create(const wchar_t*name,const Controls&c){close();handle=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,sizeof(SharedControls),name);if(!handle)return false;view=static_cast<SharedControls*>(MapViewOfFile(handle,FILE_MAP_ALL_ACCESS,0,0,sizeof(SharedControls)));if(!view){close();return false;}writerName=std::wstring(name)+L"-writers";write(c);return true;}
 bool open(const wchar_t*name){close();handle=OpenFileMappingW(FILE_MAP_ALL_ACCESS,FALSE,name);if(!handle)return false;view=static_cast<SharedControls*>(MapViewOfFile(handle,FILE_MAP_ALL_ACCESS,0,0,sizeof(SharedControls)));if(!view){close();return false;}writerName=std::wstring(name)+L"-writers";return true;}
 bool read(Controls&c,LONG*rev=nullptr){if(view&&rev&&*rev>=0&&InterlockedCompareExchange(&view->sequence,0,0)==*rev)return false;return readControls(view,c,rev);}
 void write(const Controls&c){if(view){app::NamedLock guard(writerName);writeControls(view,c);}}
};
}
