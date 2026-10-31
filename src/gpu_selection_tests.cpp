// SPDX-License-Identifier: MIT
#include "gpu_selection.hpp"
#include <iostream>
static void need(bool value,const char*why){if(!value)throw std::runtime_error(why);}
int main(){try{using namespace xpfsr::gpu;const std::string a(32,'a'),b(32,'b');GpuChoices choices{b,a};auto saved=encodeChoices(choices);auto reopened=decodeChoices("[Launcher]\nMode=4\n"+saved);need(reopened.neural==b&&reopened.frameGeneration==a,"GPU choices did not survive serialization/reopen");
    bool invalid=false;try{normalizedChoice("../device");}catch(...){invalid=true;}need(invalid,"Malformed GPU choice accepted");
    std::vector<Adapter>adapters{{a,"Render","driver-a",true,true,true},{b,"Processing","driver-b",true,true,true}};
    auto result=selectGpu(b,GpuTask::Neural,a,adapters,{},7);need(!result.offloaded&&result.backendAllowed&&result.effective==a&&result.requested==b&&result.reason==SelectionReason::TransportUnverified,"Untested offload falsely advertised active");
    LinkProof proof{a,b,"driver-a","driver-b",7,true,true,false,false};
    result=selectGpu(b,GpuTask::Neural,a,adapters,{proof},7);need(result.reason==SelectionReason::BackendUnverified&&!result.offloaded,"Buffer transport substituted for neural-backend validation");
    proof.neural=true;result=selectGpu(b,GpuTask::Neural,a,adapters,{proof},7);need(result.offloaded&&result.effective==b&&result.backendAllowed,"Verified processing target rejected");
    std::reverse(adapters.begin(),adapters.end());need(selectGpu(b,GpuTask::Neural,a,adapters,{proof},7).effective==b,"Adapter enumeration order changed selection");
    need(!selectGpu(b,GpuTask::Neural,a,adapters,{proof},8).offloaded,"Prior-session validation reused");
    adapters[0].driverTag="new-driver";need(!selectGpu(b,GpuTask::Neural,a,adapters,{proof},7).offloaded,"Stale driver proof reused");adapters[0].driverTag="driver-b";
    adapters[0].neural=false;need(selectGpu(b,GpuTask::Neural,a,adapters,{proof},7).reason==SelectionReason::FeatureUnavailable,"Unsupported neural GPU allowed");adapters[0].neural=true;
    proof.distinctPhysical=false;need(!selectGpu(b,GpuTask::Neural,a,adapters,{proof},7).offloaded,"Same-physical fixture certified cross-card support");
    adapters.erase(adapters.begin());result=selectGpu(b,GpuTask::Neural,a,adapters,{},7);need(result.effective==a&&result.requested==b&&choices.neural==b&&result.reason==SelectionReason::ProcessingMissing,"GPU disappearance erased the user's requested device");
    need(selectGpu("render",GpuTask::FrameGeneration,a,adapters,{},7).backendAllowed,"Native default requires an offload proof");
    std::cout<<"PASS: saved UUID choices, enumeration reorder, unavailable/unsupported devices, exact driver/session proof and explicit native fallback.\n";return 0;
}catch(const std::exception&e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}
