; KickCraft NSIS Installer
; Gravitas Audio

!define PRODUCT_NAME "KickCraft"
!define PRODUCT_VERSION "1.0.0"
!define PRODUCT_PUBLISHER "Gravitas Audio"
!define VST3_DIR "$COMMONFILES\VST3"

Name "${PRODUCT_NAME} ${PRODUCT_VERSION}"
OutFile "KickCraft-Setup.exe"
InstallDir "${VST3_DIR}"
RequestExecutionLevel admin
ShowInstDetails show

!include "MUI2.nsh"

!define MUI_ABORTWARNING

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "English"

Section "KickCraft VST3" SecMain
  SetOutPath "${VST3_DIR}"
  File /r "KickCraft.vst3"

  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${PRODUCT_NAME}" "DisplayName" "${PRODUCT_NAME}"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${PRODUCT_NAME}" "DisplayVersion" "${PRODUCT_VERSION}"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${PRODUCT_NAME}" "Publisher" "${PRODUCT_PUBLISHER}"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${PRODUCT_NAME}" "UninstallString" '"$COMMONFILES\VST3\KickCraft.vst3\uninstall.exe"'
  WriteUninstaller "$COMMONFILES\VST3\KickCraft.vst3\uninstall.exe"
SectionEnd

Section "Uninstall"
  RMDir /r "${VST3_DIR}\KickCraft.vst3"
  DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${PRODUCT_NAME}"
SectionEnd
