-- SPDX-License-Identifier: MIT
-- Optional FlyWithLua NG script for X-Plane 12. Starts OFF; no settings.txt edits.
-- Name: make XP shadows great again - enhanced edition
-- Private art controls can change between simulator updates. Probe, don't guess.
local prefix = "[Make XP shadows] "
local function report(message)
    local text = prefix .. message
    if type(logMsg) == "function" then logMsg(text) end
    if type(XPLMSpeakString) == "function" then XPLMSpeakString(message) end
end
local ok, ffi = pcall(require, "ffi")
if not ok then report("Shadow option unavailable: LuaJIT FFI is missing."); return end
pcall(ffi.cdef, [[
    typedef void* mxps_dataref;
    mxps_dataref XPLMFindDataRef(const char* name);
    int XPLMCanWriteDataRef(mxps_dataref ref);
    int XPLMGetDataRefTypes(mxps_dataref ref);
    float XPLMGetDataf(mxps_dataref ref);
    int XPLMGetDatai(mxps_dataref ref);
    void XPLMSetDataf(mxps_dataref ref, float value);
    void XPLMSetDatai(mxps_dataref ref, int value);
]])
local sdk
if pcall(function() local f=ffi.C.XPLMFindDataRef; assert(f); sdk=ffi.C end) then
    -- XPLM exports are visible globally on some systems.
else
    local paths = SYSTEM == "IBM" and {"XPLM_64"} or SYSTEM == "APL" and
        {"Resources/plugins/XPLM.framework/XPLM"} or {"Resources/plugins/XPLM_64.so", "XPLM_64.so"}
    for _,path in ipairs(paths) do
        local loaded,lib=pcall(ffi.load,path)
        if loaded and pcall(function() assert(lib.XPLMFindDataRef) end) then sdk=lib;break end
    end
end
if not sdk then report("Shadow option unavailable: could not find X-Plane's data controls.");return end
local ref=sdk.XPLMFindDataRef("sim/private/controls/fbo/shadow_cam_size")
if ref==nil or sdk.XPLMCanWriteDataRef(ref)==0 then
    report("This X-Plane version does not offer a writable live shadow-size control. No settings changed.");return
end
local types=sdk.XPLMGetDataRefTypes(ref)
local bit=require("bit")
local is_float=bit.band(types,2)~=0
if not is_float and bit.band(types,1)==0 then report("Shadow control has an unsupported type. No settings changed.");return end
local function read() return is_float and tonumber(sdk.XPLMGetDataf(ref)) or tonumber(sdk.XPLMGetDatai(ref)) end
local function write(v) if is_float then sdk.XPLMSetDataf(ref,v) else sdk.XPLMSetDatai(ref,v) end end
local original,last_written,enabled
local function valid(v) return v and v==v and v>=256 and v<=16384 end
local function restore()
    if enabled and last_written and valid(original) then
        if math.abs(read()-last_written)<0.5 then write(original)
        else report("Another add-on changed shadow size. Its choice was kept.") end
    end
    enabled=false;last_written=nil;original=nil
end
function MXPS_Enhanced_Select(size)
    if size==0 then restore();report("Shadow enhancement off. Previous size restored when still owned by this script.");return end
    if size~=2048 and size~=4096 and size~=8192 then report("Choose a listed shadow size.");return end
    if enabled and math.abs(read()-last_written)>=0.5 then
        enabled=false;last_written=nil;original=nil
        report("Shadow size was changed elsewhere. Select this option again to take control.");return
    end
    if not enabled then original=read();if not valid(original) then report("Unexpected shadow size. No settings changed.");return end end
    write(size)
    if math.abs(read()-size)>=0.5 then
        if not enabled and valid(original) then write(original) end
        report("X-Plane did not accept that live shadow size. Previous settings kept.");return
    end
    enabled=true;last_written=size
    report("Shadow size set to "..size..". Higher sizes can reduce frame rate and use more graphics memory.")
end
function MXPS_Enhanced_Restore() restore() end
add_macro("Make XP shadows: Off / restore", "MXPS_Enhanced_Select(0)")
add_macro("Make XP shadows: 2K - lower cost", "MXPS_Enhanced_Select(2048)")
add_macro("Make XP shadows: 4K - recommended", "MXPS_Enhanced_Select(4096)")
add_macro("Make XP shadows: 8K - high memory use", "MXPS_Enhanced_Select(8192)")
if type(do_on_exit)=="function" then do_on_exit("MXPS_Enhanced_Restore()") end
report("Optional shadow controls ready. Currently Off. Open Plugins, FlyWithLua, Macros to choose a size.")
