// SPDX-License-Identifier: MIT
#pragma once
#include <array>
#include <cstdint>
#include <windows.h>
namespace xpfsr::nr {
enum Field { Enabled, Intensity, Tone, Structure, Skin, Colour, Detail, Highlight,
             History, Style, Scale, Passes, Transfer, Scaler, Model, Mask, Grade, AllowFG, Scope, Order, Count };
struct Setting { const wchar_t *key, *label, *help; int minimum, maximum, initial; };
inline constexpr Setting settings[Count] = {
 {L"NROn",L"Enable neural rendering",L"Live On/Off. First use prepares the model in the background. External-only scope protects the cockpit; Full scene includes it. Ground-contact safety views remain protected.",0,1,0},
 {L"NRIntensity",L"Overall strength (%)",L"Blends the NR edit into the original scene. Start at 25%; stronger settings change the picture more. This does not reduce network cost.",0,100,25},
 {L"NRTone",L"Tone / lighting (%)",L"Broad model lighting changes. Start at 0 to retain the simulator's lighting. Higher values may alter exposure and contrast.",0,200,0},
 {L"NRStructure",L"Structure (%)",L"Fine model detail and local contrast. 100% follows the model default; excessive structure can emphasize shimmer.",0,200,100},
 {L"NRSkin",L"Skin structure (%)",L"-100 means Auto, following Structure. Other values tune model skin detail; X-Plane character coverage is not guaranteed.",-100,200,-100},
 {L"NRColour",L"Color transfer (%)",L"Transfers model color into the scene. Start at 25%. Values above 100% can exaggerate saturation.",0,400,25},
 {L"NRDetail",L"Detail transfer (%)",L"How much model detail is composed onto the full input image. Start at 50%; lower it if fine scenery looks unstable.",0,200,50},
 {L"NRHighlight",L"Highlight guard (%)",L"Limits the model's luminance ratio. 150% is conservative; high values allow larger changes around bright lights.",100,800,150},
 {L"NRHistory",L"Temporal history (%)",L"Balances stability and responsiveness in GPU-estimated motion. Start at 50%; lower if moving clouds leave trails. Changes reset history.",0,100,50},
 {L"NRStyle",L"Style",L"Standard, Natural or Cinematic model conditioning. These are appearance choices, not AA modes.",0,2,0},
 {L"NRModelScale",L"Model resolution (%)",L"25/33/50/67/100% of the selected NR stage: scene input for Before, output resolution for After. Lower values reduce network cost and can lose detail. Changes rebuild in the background, without restarting the sim.",25,100,50},
 {L"NRPasses",L"Model passes",L"One pass is recommended for flight. Only the selected pass budget is allocated. Changes prepare in the background without a sim restart. Two or three repeat the network and add substantial GPU/memory cost; they can compound tone changes.",1,3,1},
 {L"NRTransfer",L"Composition",L"Matched residual, edge-aware lighting/color, or classic enlargement. Matched is the default at reduced model resolution.",0,2,0},
 {L"NRScaler",L"Classic scaler",L"Bilinear, Catmull-Rom, Lanczos3 or FSR 1 EASU. Used only by NR Classic composition. FSR 3/4 is a separate reconstruction stage, before or after NR as selected.",0,3,1},
 {L"NRModel",L"Network",L"Auto prefers the small model. Small uses fewer model layers; Original uses the larger local model. Missing models bypass with a reason. Switching prepares in the background.",0,2,0},
 {L"NRMask",L"Automatic model mask",L"Uses the model's automatic edit mask. It does not identify every cockpit display or replace the cockpit protection.",0,1,1},
 {L"NRGrade",L"Natural / Cinematic style grade",L"Apply the reference style grade after the network. Standard style is unaffected.",0,1,1},
 {L"NRAllowFG",L"Allow NR + frame generation (experimental)",L"Opt-in combination testing only. NR adds GPU cost and FG artifacts remain possible. Choose the FG provider before launch; NR controls remain live afterward.",0,1,0},
 {L"NRScope",L"Scene scope",L"External only pauses NR in the cockpit. Full scene includes cockpit and exterior. This is live and resets history. NR can change display text: retain cockpit-screen restore where supported; start at Subtle. That restore currently supports the default Cirrus SR22, not all aircraft.",0,1,0},
 {L"NROrder",L"Processing order",L"Before upscaling (recommended) runs NR at scene-input resolution, then feeds the edited image into FSR 3/4. After upscaling processes the reconstructed image at output resolution and may cost substantially more GPU time. Compare the same scene; neither order guarantees better quality. Changes apply live once the matching model is ready; histories reset and FSR continues during preparation. Cockpit-screen restore and frame generation run afterward in both modes. Native AA keeps the same resolution, but the order still changes.",0,1,0}
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
 HANDLE handle{};SharedControls*view{};
 public:
 ControlChannel()=default;ControlChannel(const ControlChannel&)=delete;ControlChannel&operator=(const ControlChannel&)=delete;
 ~ControlChannel(){close();}
 void close(){if(view)UnmapViewOfFile(view);if(handle)CloseHandle(handle);view=nullptr;handle=nullptr;}
 bool create(const wchar_t*name,const Controls&c){close();handle=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,sizeof(SharedControls),name);if(!handle)return false;view=static_cast<SharedControls*>(MapViewOfFile(handle,FILE_MAP_ALL_ACCESS,0,0,sizeof(SharedControls)));if(!view){close();return false;}writeControls(view,c);return true;}
 bool open(const wchar_t*name){close();handle=OpenFileMappingW(FILE_MAP_ALL_ACCESS,FALSE,name);if(!handle)return false;view=static_cast<SharedControls*>(MapViewOfFile(handle,FILE_MAP_ALL_ACCESS,0,0,sizeof(SharedControls)));if(!view){close();return false;}return true;}
 bool read(Controls&c,LONG*rev=nullptr){if(view&&rev&&*rev>=0&&InterlockedCompareExchange(&view->sequence,0,0)==*rev)return false;return readControls(view,c,rev);}
 void write(const Controls&c){writeControls(view,c);}
};
}
