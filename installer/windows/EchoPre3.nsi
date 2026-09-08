; Echo Pre 3 VST3 installer (NSIS). Windows has no AU format, so there's no plugin-type choice
; here — VST3 only, installed to the shared system VST3 folder.
;
; Build with (from repo root, after building EchoPre3_VST3):
;   makensis /DVERSION=0.4.0 /DARTEFACTS_DIR=build\EchoPre3_artefacts\Release\VST3 installer\windows\EchoPre3.nsi
;
; ARTEFACTS_DIR should point at the directory CONTAINING "Echo Pre 3.vst3" (i.e. the VST3 release
; output folder), not the bundle itself.

!ifndef VERSION
  !define VERSION "0.0.0"
!endif
!ifndef ARTEFACTS_DIR
  !define ARTEFACTS_DIR "..\..\build\EchoPre3_artefacts\Release\VST3"
!endif

Name "Echo Pre 3"
OutFile "EchoPre3-Windows-v${VERSION}-Installer.exe"
InstallDir "$COMMONFILES64\VST3"
RequestExecutionLevel admin
SetCompressor /SOLID lzma

Page directory
Page instfiles
UninstPage uninstConfirm
UninstPage instfiles

Section "Echo Pre 3 VST3 Plugin" SecVST3
    SetOutPath "$INSTDIR\Echo Pre 3.vst3"
    File /r "${ARTEFACTS_DIR}\Echo Pre 3.vst3\*.*"

    WriteUninstaller "$INSTDIR\Echo Pre 3.vst3\Uninstall-EchoPre3.exe"

    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Echo Pre 3" \
        "DisplayName" "Echo Pre 3 VST3"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Echo Pre 3" \
        "UninstallString" "$INSTDIR\Echo Pre 3.vst3\Uninstall-EchoPre3.exe"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Echo Pre 3" \
        "DisplayVersion" "${VERSION}"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Echo Pre 3" \
        "Publisher" "Leigh Pierce"
SectionEnd

Section "Uninstall"
    RMDir /r "$INSTDIR\Echo Pre 3.vst3"
    DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Echo Pre 3"
SectionEnd
