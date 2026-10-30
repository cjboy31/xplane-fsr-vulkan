; SPDX-License-Identifier: MIT
Unicode true
RequestExecutionLevel user
SetCompressor zlib
Name "FSR 3/4 + Frame Gen for X-Plane 12 (Experimental)"
OutFile "${PACKAGE}\XPlane-FSR24-Setup.exe"
InstallDir "$LOCALAPPDATA\Programs\XPlaneFSR24"
BrandingText "Community graphics preview - SDK 2.4.0"
VIProductVersion "${WINDOWS_VERSION}"
VIAddVersionKey /LANG=1033 "ProductName" "FSR 3/4 + Frame Gen for X-Plane 12 (Experimental)"
VIAddVersionKey /LANG=1033 "FileDescription" "Per-user setup for the experimental FSR mod"
VIAddVersionKey /LANG=1033 "FileVersion" "${PACKAGE_VERSION}"
VIAddVersionKey /LANG=1033 "LegalCopyright" "2026 XPlane FSR24 prototype contributors; NSIS Contributors"

!include MUI2.nsh
!include nsDialogs.nsh
!include LogicLib.nsh
!include FileFunc.nsh
!include x64.nsh
!ifdef APP_ICON
!define MUI_ICON "${APP_ICON}"
!define MUI_UNICON "${APP_ICON}"
!endif
Var XPlaneDir
Var FolderEdit
Var DesktopCheck
Var DesktopWanted
Var TestMode
Var Dialog
Var Parameters
Var AutoUpdate
Var InstallLock
Var ParentProcess

!define MUI_ABORTWARNING
!define MUI_WELCOMEPAGE_TITLE "FSR 3/4 + Frame Gen for X-Plane 12"
!define MUI_WELCOMEPAGE_TEXT "Install the experimental Native AA and upscaling launcher. Built-in frame generation is temporarily suspended in this regular release.$\r$\n$\r$\nClose X-Plane and the FSR launcher before continuing. This preview can produce visual artifacts and is not a production-ready graphics upgrade.$\r$\n$\r$\nSetup installs for your Windows account. The camera plugin is installed by the launcher on first use."
!insertmacro MUI_PAGE_WELCOME
Page custom SimulatorPage SimulatorLeave
!define MUI_DIRECTORYPAGE_TEXT_TOP "Choose an empty writable folder for the application, or a folder installed by this setup. Keep the default for a normal per-user installation. Do not select X-Plane's plugins folder."
!define MUI_PAGE_CUSTOMFUNCTION_LEAVE DirectoryLeave
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!define MUI_FINISHPAGE_TITLE "Installation complete"
!define MUI_FINISHPAGE_TEXT "Open the launcher from the Start menu. Run the GPU checks, then choose an AA/upscaling mode. Frame generation starts Off.$\r$\n$\r$\nThe installed guide explains settings, limitations and removal."
!define MUI_FINISHPAGE_RUN "$INSTDIR\XPlaneFSR24.exe"
!define MUI_FINISHPAGE_RUN_TEXT "Open FSR launcher"
!define MUI_FINISHPAGE_RUN_NOTCHECKED
!define MUI_FINISHPAGE_SHOWREADME "$INSTDIR\INSTALLATION-AND-USE.txt"
!define MUI_FINISHPAGE_SHOWREADME_TEXT "Read installation and use instructions"
!insertmacro MUI_PAGE_FINISH
!define MUI_UNCONFIRMPAGE_TEXT_TOP "Close X-Plane. Before uninstalling, use Remove camera plugin in the launcher for your simulator folder. The uninstaller does not remove files from the simulator. User logs, check output and backups are preserved."
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_UNPAGE_FINISH
!insertmacro MUI_LANGUAGE English

Function .onInit
  SetShellVarContext current
  ${IfNot} ${RunningX64}
    MessageBox MB_OK|MB_ICONSTOP "This application requires 64-bit Windows."
    SetErrorLevel 2
    Abort
  ${EndIf}
  StrCpy $TestMode 0
  StrCpy $AutoUpdate 0
  StrCpy $InstallLock -1
  StrCpy $DesktopWanted 0
  StrCpy $XPlaneDir ""
  IfFileExists "D:\X-Plane 12\X-Plane.exe" 0 +2
    StrCpy $XPlaneDir "D:\X-Plane 12"
  ${GetParameters} $Parameters
  ClearErrors
  ${GetOptions} $Parameters "/AUTOUPDATE=" $0
  ${If} $0 == "1"
    StrCpy $AutoUpdate 1
    ; /D= is parsed by NSIS before .onInit. Never guess an update target.
    ReadINIStr $0 "$INSTDIR\XPFSR24-Installation.ini" "Installation" "Product"
    ${If} $0 != "XPFSR24-community-alpha"
      SetErrorLevel 5
      Quit
    ${EndIf}
    StrCpy $XPlaneDir ""
    ClearErrors
    ${GetOptions} $Parameters "/PARENT=" $ParentProcess
    ${IfNot} ${Errors}
      System::Call 'kernel32::OpenProcess(i 0x100000, i 0, i $ParentProcess) p.r0'
      ${If} $0 != 0
        System::Call 'kernel32::WaitForSingleObject(p r0, i 30000) i.r1'
        System::Call 'kernel32::CloseHandle(p r0)'
        ${If} $1 != 0
          MessageBox MB_OK|MB_ICONEXCLAMATION "Update stopped: close the launcher and try again. No files were installed."
          SetErrorLevel 6
          Quit
        ${EndIf}
      ${EndIf}
    ${EndIf}
  ${EndIf}
  ReadINIStr $0 "$INSTDIR\XPFSR24-Installation.ini" "Installation" "Product"
  ${If} $0 == "XPFSR24-community-alpha"
    ReadINIStr $DesktopWanted "$INSTDIR\XPFSR24-Installation.ini" "Installation" "DesktopShortcut"
  ${EndIf}
  ClearErrors
  ${GetOptions} $Parameters "/TEST=" $0
  ${If} $0 == "1"
    StrCpy $TestMode 1
  ${EndIf}
  ClearErrors
  ${GetOptions} $Parameters "/XPLANE=" $0
  ${IfNot} ${Errors}
    StrCpy $XPlaneDir $0
  ${EndIf}
  IfSilent 0 doneInit
    Call ValidateSimulator
    Call ValidateDirectory
  doneInit:
FunctionEnd

Function SimulatorPage
  !insertmacro MUI_HEADER_TEXT "Choose X-Plane" "Select your simulator folder, or leave it blank to choose later."
  nsDialogs::Create 1018
  Pop $Dialog
  ${NSD_CreateLabel} 0 0 100% 30u "Choose the folder containing X-Plane.exe. The launcher copies its camera plugin on first selected-mode launch; setup does not change the simulator."
  Pop $0
  ${NSD_CreateDirRequest} 0 40u 78% 13u "$XPlaneDir"
  Pop $FolderEdit
  ${NSD_CreateBrowseButton} 80% 39u 20% 15u "Browse..."
  Pop $0
  ${NSD_OnClick} $0 BrowseSimulator
  ${NSD_CreateCheckbox} 0 72u 100% 14u "Create a desktop shortcut"
  Pop $DesktopCheck
  ${If} $DesktopWanted == 1
    ${NSD_Check} $DesktopCheck
  ${EndIf}
  ${NSD_CreateLabel} 0 103u 100% 44u "Tested: X-Plane 12.4.4-b5, Radeon AI PRO R9700.$\r$\nLater betas and other hardware may be unsupported.$\r$\nCompatibility AA is the suggested starting mode; FG is optional."
  Pop $0
  nsDialogs::Show
FunctionEnd

Function BrowseSimulator
  nsDialogs::SelectFolderDialog "Select the folder containing X-Plane.exe" "$XPlaneDir"
  Pop $0
  ${If} $0 != error
    ${NSD_SetText} $FolderEdit $0
  ${EndIf}
FunctionEnd

Function SimulatorLeave
  ${NSD_GetText} $FolderEdit $XPlaneDir
  ${NSD_GetState} $DesktopCheck $DesktopWanted
  Call ValidateSimulator
FunctionEnd

Function ValidateSimulator
  ${If} $XPlaneDir != ""
    IfFileExists "$XPlaneDir\X-Plane.exe" simulatorValid
    IfSilent simSilentError
    MessageBox MB_OK|MB_ICONEXCLAMATION "That folder does not contain X-Plane.exe. Select the simulator root, or clear the field to choose it later."
    Abort
    simSilentError:
    SetErrorLevel 3
    Quit
  ${EndIf}
  simulatorValid:
FunctionEnd

Function DirectoryLeave
  Call ValidateDirectory
FunctionEnd

Function ValidateDirectory
  ; Updates are allowed only in a folder bearing this installer's marker.
  IfFileExists "$INSTDIR\XPFSR24-Installation.ini" 0 checkEmpty
  ReadINIStr $0 "$INSTDIR\XPFSR24-Installation.ini" "Installation" "Product"
  ${If} $0 == "XPFSR24-community-alpha"
    Return
  ${EndIf}
  checkEmpty:
  FindFirst $0 $1 "$INSTDIR\*"
  scanDirectory:
    ${If} $1 == ""
      FindClose $0
      Return
    ${EndIf}
    ${If} $1 != "."
    ${AndIf} $1 != ".."
      FindClose $0
      IfSilent dirSilentError
      MessageBox MB_OK|MB_ICONEXCLAMATION "Choose an empty application folder. Setup will not overwrite an unrelated installation."
      Abort
      dirSilentError:
      SetErrorLevel 4
      Quit
    ${EndIf}
    FindNext $0 $1
    Goto scanDirectory
FunctionEnd

Section "Application"
  SetShellVarContext current
  Call ValidateSimulator
  Call ValidateDirectory
  CreateDirectory "$INSTDIR"
  ; The same exclusive lock is held by the launcher. Keep it for the entire write.
  System::Call 'kernel32::CreateFileW(w "$INSTDIR\launcher.lock", i 0x40000000, i 0, p 0, i 4, i 0x04000100, p 0) p.s'
  Pop $InstallLock
  ${If} $InstallLock == -1
    IfSilent +2
      MessageBox MB_OK|MB_ICONEXCLAMATION "Close the FSR launcher before updating. No application files were installed."
    SetErrorLevel 6
    Quit
  ${EndIf}
  ; Loaded executable/DLL files must be writable before any payload is replaced.
  !macro CheckUnlocked Relative
    ${If} ${FileExists} "$INSTDIR\${Relative}"
      System::Call 'kernel32::CreateFileW(w "$INSTDIR\${Relative}", i 0x40000000, i 0, p 0, i 3, i 0x80, p 0) p.r0'
      ${If} $0 == -1
        ${If} $TestMode != 1
          MessageBox MB_OK|MB_ICONEXCLAMATION "Close X-Plane and all FSR GPU checks before updating. No application files were installed."
        ${EndIf}
        System::Call 'kernel32::CloseHandle(p $InstallLock)'
        SetErrorLevel 7
        Quit
      ${EndIf}
      System::Call 'kernel32::CloseHandle(p r0)'
    ${EndIf}
  !macroend
  !insertmacro CheckUnlocked "XPlaneFSR24.exe"
  !insertmacro CheckUnlocked "layer\XPFSR24Layer.dll"
  !insertmacro CheckUnlocked "NativeAACheck.exe"
  !insertmacro CheckUnlocked "FrameGenCheck.exe"
  !insertmacro CheckUnlocked "CockpitRestoreCheck.exe"
  SetOverwrite on
  !include "${GENERATED}\InstallFiles.nsh"
  ${If} $XPlaneDir != ""
    WriteINIStr "$INSTDIR\settings.ini" "Application" "Simulator" "$XPlaneDir"
  ${EndIf}
  WriteINIStr "$INSTDIR\XPFSR24-Installation.ini" "Installation" "Product" "XPFSR24-community-alpha"
  WriteINIStr "$INSTDIR\XPFSR24-Installation.ini" "Installation" "Version" "${PACKAGE_VERSION}"
  WriteINIStr "$INSTDIR\XPFSR24-Installation.ini" "Installation" "TestMode" "$TestMode"
  WriteINIStr "$INSTDIR\XPFSR24-Installation.ini" "Installation" "DesktopShortcut" "$DesktopWanted"
  WriteUninstaller "$INSTDIR\Uninstall.exe"
  ${If} $TestMode != 1
    ; Remove only shortcut names created by previous owned installs.
    Delete "$SMPROGRAMS\FSR for X-Plane 12 (Experimental)\FSR Launcher.lnk"
    Delete "$SMPROGRAMS\FSR for X-Plane 12 (Experimental)\Instructions.lnk"
    Delete "$SMPROGRAMS\FSR for X-Plane 12 (Experimental)\Uninstall.lnk"
    RMDir "$SMPROGRAMS\FSR for X-Plane 12 (Experimental)"
    ${If} $DesktopWanted == 1
      Delete "$DESKTOP\FSR for X-Plane 12 (Experimental).lnk"
    ${EndIf}
    CreateDirectory "$SMPROGRAMS\FSR 3-4 + Frame Gen for X-Plane"
    CreateShortcut "$SMPROGRAMS\FSR 3-4 + Frame Gen for X-Plane\FSR 3-4 + Frame Gen.lnk" "$INSTDIR\XPlaneFSR24.exe"
    CreateShortcut "$SMPROGRAMS\FSR 3-4 + Frame Gen for X-Plane\Instructions.lnk" "$INSTDIR\INSTALLATION-AND-USE.txt"
    CreateShortcut "$SMPROGRAMS\FSR 3-4 + Frame Gen for X-Plane\Uninstall.lnk" "$INSTDIR\Uninstall.exe"
    ${If} $DesktopWanted == 1
      CreateShortcut "$DESKTOP\FSR 3-4 + Frame Gen for X-Plane.lnk" "$INSTDIR\XPlaneFSR24.exe"
    ${EndIf}
    WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\XPFSR24CommunityAlpha" "DisplayName" "FSR 3/4 + Frame Gen for X-Plane 12 (Experimental)"
    WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\XPFSR24CommunityAlpha" "DisplayVersion" "${PACKAGE_VERSION}"
    WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\XPFSR24CommunityAlpha" "UninstallString" '$\"$INSTDIR\Uninstall.exe$\"'
    WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\XPFSR24CommunityAlpha" "QuietUninstallString" '$\"$INSTDIR\Uninstall.exe$\" /S'
    WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\XPFSR24CommunityAlpha" "InstallLocation" "$INSTDIR"
    WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\XPFSR24CommunityAlpha" "DisplayIcon" "$INSTDIR\XPlaneFSR24.exe"
    WriteRegDWORD HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\XPFSR24CommunityAlpha" "NoModify" 1
    WriteRegDWORD HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\XPFSR24CommunityAlpha" "NoRepair" 1
  ${EndIf}
  System::Call 'kernel32::CloseHandle(p $InstallLock)'
  StrCpy $InstallLock -1
SectionEnd

Function .onInstSuccess
  ${If} $AutoUpdate == 1
  ${AndIf} $TestMode != 1
    Exec '$\"$INSTDIR\XPlaneFSR24.exe$\"'
  ${EndIf}
FunctionEnd

Function .onInstFailed
  ${If} $InstallLock != -1
    System::Call 'kernel32::CloseHandle(p $InstallLock)'
  ${EndIf}
  ${If} $AutoUpdate == 1
    MessageBox MB_OK|MB_ICONSTOP "The application update did not complete. Download the full installer from GitHub Releases and install into the same application folder. Saved choices are retained."
  ${EndIf}
FunctionEnd

Section "Uninstall"
  SetShellVarContext current
  ReadINIStr $TestMode "$INSTDIR\XPFSR24-Installation.ini" "Installation" "TestMode"
  ReadINIStr $DesktopWanted "$INSTDIR\XPFSR24-Installation.ini" "Installation" "DesktopShortcut"
  ${If} $TestMode != 1
    Delete "$SMPROGRAMS\FSR 3-4 + Frame Gen for X-Plane\FSR 3-4 + Frame Gen.lnk"
    Delete "$SMPROGRAMS\FSR 3-4 + Frame Gen for X-Plane\Instructions.lnk"
    Delete "$SMPROGRAMS\FSR 3-4 + Frame Gen for X-Plane\Uninstall.lnk"
    RMDir "$SMPROGRAMS\FSR 3-4 + Frame Gen for X-Plane"
    ${If} $DesktopWanted == 1
      Delete "$DESKTOP\FSR 3-4 + Frame Gen for X-Plane.lnk"
    ${EndIf}
    DeleteRegKey HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\XPFSR24CommunityAlpha"
  ${EndIf}
  !include "${GENERATED}\UninstallFiles.nsh"
  Delete "$INSTDIR\settings.ini"
  Delete "$INSTDIR\XPFSR24-Installation.ini"
  Delete "$INSTDIR\Uninstall.exe"
  RMDir "$INSTDIR"
SectionEnd
