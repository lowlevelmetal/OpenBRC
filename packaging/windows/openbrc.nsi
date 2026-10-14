; OpenBRC - Windows installer (NSIS 3, Unicode).
;
; Built by the package_installer CMake target (cmake/Packaging.cmake):
;   makensis -DVERSION=<x.y.z> -DSTAGE_DIR=<cmake --install prefix> -DSOURCE_DIR=<repo> -DOUTFILE=<setup.exe> openbrc.nsi
;
; The installer contains only OpenBRC itself. It never contains game data: the
; game needs the user's own Barbie Riding Club CD-ROM, a disc image of it, or an
; installation of the original game. This installer can record where that is
; (checked with "openbrc --check-source"); otherwise OpenBRC asks on first start.
;
; Silent installs: setup.exe /S [/SOURCE=<disc image, drive or folder>] [/D=<install dir>]

Unicode true
SetCompressor /SOLID lzma
ManifestDPIAware true
RequestExecutionLevel admin

!ifndef VERSION
  !error "pass -DVERSION=x.y.z"
!endif
!ifndef STAGE_DIR
  !error "pass -DSTAGE_DIR=<staging directory>"
!endif
!ifndef SOURCE_DIR
  !error "pass -DSOURCE_DIR=<repository root>"
!endif
!ifndef OUTFILE
  !define OUTFILE "OpenBRC-${VERSION}-windows-x86_64-setup.exe"
!endif

!define PRODUCT "OpenBRC"
!define EXE "openbrc.exe"
!define UNINST_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\OpenBRC"

Name "${PRODUCT}"
OutFile "${OUTFILE}"
InstallDir "$PROGRAMFILES64\${PRODUCT}"
InstallDirRegKey HKLM "Software\${PRODUCT}" "InstallDir"
BrandingText "${PRODUCT} ${VERSION}"

VIProductVersion "${VERSION}.0"
VIAddVersionKey "ProductName" "${PRODUCT}"
VIAddVersionKey "FileDescription" "${PRODUCT} Setup"
VIAddVersionKey "FileVersion" "${VERSION}"
VIAddVersionKey "ProductVersion" "${VERSION}"
VIAddVersionKey "LegalCopyright" "OpenBRC contributors, GPL-3.0-or-later. Barbie is a trademark of Mattel, Inc."

!include "MUI2.nsh"
!include "nsDialogs.nsh"
!include "x64.nsh"
!include "LogicLib.nsh"
!include "FileFunc.nsh"

!define MUI_ICON "${SOURCE_DIR}\packaging\icons\openbrc.ico"
!define MUI_UNICON "${SOURCE_DIR}\packaging\icons\openbrc.ico"
!define MUI_ABORTWARNING

Var SourcePath
Var SourceText
Var SourceOk

!define MUI_WELCOMEPAGE_TITLE "Welcome to ${PRODUCT}"
!define MUI_WELCOMEPAGE_TEXT "This installs ${PRODUCT} ${VERSION}, an open source reimplementation of the 1998 game Barbie Riding Club for modern Windows.$\r$\n$\r$\nYou need your original Barbie Riding Club CD-ROM. ${PRODUCT} contains no game data: it reads everything from your disc, an image of it (ISO, BIN/CUE) or an installation of the original game.$\r$\n$\r$\nClick Next to continue."
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "${SOURCE_DIR}\LICENSE"
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_DIRECTORY
Page custom GameDataPage GameDataPageLeave
!insertmacro MUI_PAGE_INSTFILES
!define MUI_FINISHPAGE_RUN
!define MUI_FINISHPAGE_RUN_TEXT "Play ${PRODUCT} now"
!define MUI_FINISHPAGE_RUN_FUNCTION LaunchGame
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "English"

Function .onInit
  ${IfNot} ${RunningX64}
    MessageBox MB_ICONSTOP "${PRODUCT} requires 64-bit Windows."
    Abort
  ${EndIf}
  SetRegView 64
  SetShellVarContext all   ; per-machine install: shortcuts for all users
  ${GetParameters} $0
  ClearErrors
  ${GetOptions} $0 "/SOURCE=" $SourcePath
  ${If} ${Errors}
    StrCpy $SourcePath ""
  ${EndIf}
FunctionEnd

Function un.onInit
  SetRegView 64
  SetShellVarContext all
FunctionEnd

; Launch unelevated through Explorer so settings land in the user's profile.
Function LaunchGame
  Exec '"$WINDIR\explorer.exe" "$INSTDIR\${EXE}"'
FunctionEnd

; --- Game data page -----------------------------------------------------------
Function GameDataPage
  !insertmacro MUI_HEADER_TEXT "Your Barbie Riding Club game" "Where is your original game disc?"
  nsDialogs::Create 1018
  Pop $0
  ${NSD_CreateLabel} 0 0 100% 36u "Choose your Barbie Riding Club CD-ROM drive, a disc image of it (.iso, .cue, .bin, .img), or a folder with an installation of the original game. You can also leave this empty: ${PRODUCT} will ask the first time it starts."
  Pop $0
  ${NSD_CreateText} 0 42u 100% 12u "$SourcePath"
  Pop $SourceText
  ${NSD_CreateButton} 0 60u 48% 14u "Disc image file..."
  Pop $0
  ${NSD_OnClick} $0 BrowseImage
  ${NSD_CreateButton} 52% 60u 48% 14u "Drive or folder..."
  Pop $0
  ${NSD_OnClick} $0 BrowseFolder
  ${NSD_CreateLabel} 0 84u 100% 24u "The game files are always read from your own copy. Nothing is copied into ${PRODUCT}'s folder."
  Pop $0
  nsDialogs::Show
FunctionEnd

Function BrowseImage
  nsDialogs::SelectFileDialog open "" "Disc images (*.iso;*.cue;*.bin;*.img)|*.iso;*.cue;*.bin;*.img|All files (*.*)|*.*"
  Pop $0
  ${If} $0 != ""
    ${NSD_SetText} $SourceText $0
  ${EndIf}
FunctionEnd

Function BrowseFolder
  nsDialogs::SelectFolderDialog "Choose the Barbie Riding Club CD-ROM drive or an installation folder" ""
  Pop $0
  ${If} $0 != "error"
  ${AndIf} $0 != ""
    ${NSD_SetText} $SourceText $0
  ${EndIf}
FunctionEnd

Function GameDataPageLeave
  ${NSD_GetText} $SourceText $SourcePath
FunctionEnd

; --- Sections ---------------------------------------------------------------
Section "!${PRODUCT}" SecMain
  SectionIn RO
  SetOutPath "$INSTDIR"
  File /r "${STAGE_DIR}\*.*"
  WriteUninstaller "$INSTDIR\Uninstall.exe"

  CreateDirectory "$SMPROGRAMS\${PRODUCT}"
  CreateShortcut "$SMPROGRAMS\${PRODUCT}\${PRODUCT}.lnk" "$INSTDIR\${EXE}"
  CreateShortcut "$SMPROGRAMS\${PRODUCT}\Uninstall ${PRODUCT}.lnk" "$INSTDIR\Uninstall.exe"

  WriteRegStr HKLM "Software\${PRODUCT}" "InstallDir" "$INSTDIR"
  WriteRegStr HKLM "${UNINST_KEY}" "DisplayName" "${PRODUCT}"
  WriteRegStr HKLM "${UNINST_KEY}" "DisplayVersion" "${VERSION}"
  WriteRegStr HKLM "${UNINST_KEY}" "Publisher" "OpenBRC contributors"
  WriteRegStr HKLM "${UNINST_KEY}" "DisplayIcon" "$INSTDIR\${EXE}"
  WriteRegStr HKLM "${UNINST_KEY}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKLM "${UNINST_KEY}" "URLInfoAbout" "https://github.com/lowlevelmetal/OpenBRC"
  WriteRegStr HKLM "${UNINST_KEY}" "UninstallString" '"$INSTDIR\Uninstall.exe"'
  WriteRegStr HKLM "${UNINST_KEY}" "QuietUninstallString" '"$INSTDIR\Uninstall.exe" /S'
  WriteRegDWORD HKLM "${UNINST_KEY}" "NoModify" 1
  WriteRegDWORD HKLM "${UNINST_KEY}" "NoRepair" 1
  ${GetSize} "$INSTDIR" "/S=0K" $0 $1 $2
  IntFmt $0 "0x%08X" $0
  WriteRegDWORD HKLM "${UNINST_KEY}" "EstimatedSize" "$0"

  ; Record the game data location if one was given and openbrc accepts it.
  StrCpy $SourceOk 0
  ${If} $SourcePath != ""
    DetailPrint "Checking the game data at $SourcePath..."
    nsExec::ExecToLog '"$INSTDIR\${EXE}" --check-source "$SourcePath"'
    Pop $0
    ${If} $0 == 0
      WriteINIStr "$INSTDIR\openbrc-install.ini" "Game" "Source" "$SourcePath"
      StrCpy $SourceOk 1
    ${Else}
      MessageBox MB_ICONEXCLAMATION|MB_OK "$SourcePath does not hold a complete copy of Barbie Riding Club. ${PRODUCT} will ask for your game disc the first time it starts." /SD IDOK
    ${EndIf}
  ${EndIf}
SectionEnd

Section "Desktop shortcut" SecDesktop
  CreateShortcut "$DESKTOP\${PRODUCT}.lnk" "$INSTDIR\${EXE}"
SectionEnd

!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
  !insertmacro MUI_DESCRIPTION_TEXT ${SecMain} "OpenBRC (requires your original Barbie Riding Club CD-ROM to play)."
  !insertmacro MUI_DESCRIPTION_TEXT ${SecDesktop} "Put a shortcut on the desktop."
!insertmacro MUI_FUNCTION_DESCRIPTION_END

Section "Uninstall"
  Delete "$INSTDIR\${EXE}"
  Delete "$INSTDIR\brctool.exe"
  Delete "$INSTDIR\*.dll"
  Delete "$INSTDIR\LICENSE.txt"
  Delete "$INSTDIR\README.md"
  Delete "$INSTDIR\openbrc-install.ini"
  RMDir /r "$INSTDIR\licenses"
  Delete "$INSTDIR\Uninstall.exe"
  RMDir "$INSTDIR"
  Delete "$SMPROGRAMS\${PRODUCT}\${PRODUCT}.lnk"
  Delete "$SMPROGRAMS\${PRODUCT}\Uninstall ${PRODUCT}.lnk"
  RMDir "$SMPROGRAMS\${PRODUCT}"
  Delete "$DESKTOP\${PRODUCT}.lnk"
  DeleteRegKey HKLM "${UNINST_KEY}"
  DeleteRegKey HKLM "Software\${PRODUCT}"
  ; Saved games and settings live in the user's profile; keep them unless asked.
  SetShellVarContext current
  MessageBox MB_YESNO|MB_DEFBUTTON2|MB_ICONQUESTION "Also delete your saved games, photos and settings ($APPDATA\OpenBRC and $LOCALAPPDATA\OpenBRC)?" /SD IDNO IDNO keep
    RMDir /r "$APPDATA\OpenBRC"
    RMDir /r "$LOCALAPPDATA\OpenBRC"
  keep:
SectionEnd
