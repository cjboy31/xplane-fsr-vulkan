// SPDX-License-Identifier: MIT
#pragma once
#include "launcher_preferences.hpp"
#include "renderer_selection.hpp"
namespace app {
inline graphics::Options compatibleInput(const launcher::Preferences& p,xp_renderer::Choice selected,graphics::Options options){
    if(selected==xp_renderer::Choice::native_aa){options.scale=0;options.fxaa=0;}
    else if(selected==xp_renderer::Choice::fsr&&p.mode){options.fxaa=0;if(p.mode==1||p.mode==3)options.scale=0;}
    return options;
}
// X-Plane exposes no supported writable live FSR slider. Manage only its
// verified preference while closed, with the usual transactional backup.
// Never change display size, UI scale, clouds, MSAA or pending graphics edits.
inline std::filesystem::path synchronizeInput(const std::filesystem::path& root){
    const auto p=launcher::load(root/L"settings.ini");
    if(p.simulator.empty()||p.manualPending)return {};
    const auto selected=xp_renderer::visibleChoice(p.simulator);
    if(selected==xp_renderer::Choice::off||(selected==xp_renderer::Choice::fsr&&!p.mode))return {};
    auto options=graphics::current(p.simulator);
    const auto requested=p.presetId.empty()?p.manual:graphics::presetById(p.presetId).settings;
    options.scale=requested.scale>0||p.manualApplied||!p.presetId.empty()?requested.scale:2;
    options=compatibleInput(p,selected,options);
    return graphics::apply(p.simulator,root/L"graphics-backups",options);
}
}
