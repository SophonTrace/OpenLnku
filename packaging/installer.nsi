Unicode True
!include "MUI2.nsh"
!include "x64.nsh"
!include "WinVer.nsh"
!include "LogicLib.nsh"
!include "FileFunc.nsh"
!include "StrFunc.nsh"
!include "nsDialogs.nsh"
!ifdef ISOLATED_FILES_ONLY
  !ifndef ISOLATED_ACCEPTANCE
    !error "File-only tests require isolated acceptance"
  !endif
!endif
${StrStr}
${UnStrStr}
!ifndef APP_ID
  !define APP_ID "OpenLnku"
!endif
!ifndef LEGACY_ID
  !define LEGACY_ID "OpenNex"
!endif
!ifndef APP_TITLE
  !define APP_TITLE "OpenLnku"
!endif
!ifndef APP_VERSION
  !error "APP_VERSION is required"
!endif
!ifndef PACKAGE_DIR
  !error "PACKAGE_DIR is required"
!endif
!ifndef MANIFEST_FILE
  !error "MANIFEST_FILE is required"
!endif
!ifndef OUTPUT_FILE
  !error "OUTPUT_FILE is required"
!endif
!define UNINSTALL_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP_ID}"
!define SETTINGS_KEY "Software\${APP_ID}\${APP_ID}"
!ifdef ISOLATED_ACCEPTANCE
  !define SHORTCUT_DIR "$INSTDIR\..\shortcuts"
  !define LEGACY_SHORTCUT_DIR "$INSTDIR\..\legacy-shortcuts"
  !define AUTORUN_ARGS '--demo --tray --test-identity ${APP_ID} --settings-file $\"$INSTDIR\..\demo.ini$\"'
  !define LAUNCH_ARGS '--demo --test-identity ${APP_ID} --settings-file $\"$INSTDIR\..\demo.ini$\"'
!else
  !define SHORTCUT_DIR "$SMPROGRAMS\${APP_ID}"
  !define LEGACY_SHORTCUT_DIR "$SMPROGRAMS\${LEGACY_ID}"
  !define AUTORUN_ARGS '--tray'
  !define LAUNCH_ARGS ''
!endif
Name "${APP_TITLE}"
OutFile "${OUTPUT_FILE}"
InstallDir "$LOCALAPPDATA\Programs\${APP_ID}"
RequestExecutionLevel user
ManifestDPIAware True
ManifestSupportedOS Win10
SetCompressor /SOLID lzma
VIProductVersion "${APP_VERSION}.0"
VIAddVersionKey "ProductName" "${APP_TITLE}"
VIAddVersionKey "FileDescription" "${APP_TITLE} Setup"
VIAddVersionKey "FileVersion" "${APP_VERSION}"
VIAddVersionKey "ProductVersion" "${APP_VERSION}"
VIAddVersionKey "LegalCopyright" "OpenLnku"
!define MUI_ABORTWARNING
!define MUI_ICON "..\assets\branding\app.ico"
!define MUI_UNICON "..\assets\branding\app.ico"
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "..\LICENSE"
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!define MUI_FINISHPAGE_RUN "$INSTDIR\OpenLnku.exe"
!define MUI_FINISHPAGE_RUN_PARAMETERS "${LAUNCH_ARGS}"
!define MUI_FINISHPAGE_RUN_FUNCTION LaunchApplication
!define MUI_FINISHPAGE_RUN_NOTCHECKED
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
UninstPage custom un.SettingsPage un.SettingsLeave
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "SimpChinese"
Var InstalledLocation
Var InstalledVersion
Var ManifestEntry
Var PurgeSettings
Var PurgeCheckbox
Var InstallMutex

!macro ReadBool VALUE KEY NAME
  ClearErrors
  ReadRegDWORD ${VALUE} HKCU "${KEY}" "${NAME}"
  ${If} ${Errors}
    ClearErrors
    ReadRegStr ${VALUE} HKCU "${KEY}" "${NAME}"
    ${If} ${Errors}
      StrCpy ${VALUE} "OpenLnkuMissingBoolean"
    ${EndIf}
  ${EndIf}
  ; LogicLib's Errors condition consumes the flag. Return validity only after parsing.
  ${If} ${VALUE} == "false"
  ${OrIf} ${VALUE} == "0"
    StrCpy ${VALUE} 0
  ${ElseIf} ${VALUE} == "true"
  ${OrIf} ${VALUE} == "1"
    StrCpy ${VALUE} 1
  ${Else}
    SetErrors
  ${EndIf}
!macroend

!macro RequireStopped
  runningCheck:
    System::Call 'kernel32::OpenMutexW(i 0x100000, i 0, w "Local\${LEGACY_ID}.App.Live") p.r0'
    ${If} $0 != 0
      System::Call 'kernel32::CloseHandle(p r0)'
      Goto appBusy
    ${EndIf}
    System::Call 'kernel32::OpenMutexW(i 0x100000, i 0, w "Local\${APP_ID}.App.Live") p.r0'
    ${If} $0 != 0
      System::Call 'kernel32::CloseHandle(p r0)'
      Goto appBusy
    ${EndIf}
    System::Call 'kernel32::OpenMutexW(i 0x100000, i 0, w "Local\${APP_ID}.App.Demo") p.r0'
    ${If} $0 != 0
      System::Call 'kernel32::CloseHandle(p r0)'
      Goto appBusy
    ${EndIf}
    !ifndef ISOLATED_ACCEPTANCE
      FindWindow $0 "" "OpenLnku"
      ${If} $0 != 0
        Goto appBusy
      ${EndIf}
      FindWindow $0 "" "OpenLnku · 离线演示"
      ${If} $0 != 0
        Goto appBusy
      ${EndIf}
    !endif
    Goto appStopped
  appBusy:
    IfSilent appBusySilent
    MessageBox MB_RETRYCANCEL|MB_ICONEXCLAMATION "请先从托盘退出 ${APP_TITLE}，然后重试。" IDRETRY runningCheck
  appBusySilent:
    SetErrorLevel 17
    Quit
  appStopped:
!macroend

Function .onInit
  ${IfNot} ${RunningX64}
    SetErrorLevel 2
    MessageBox MB_ICONSTOP "需要 64 位 Windows 10 或更新版本。"
    Abort
  ${EndIf}
  ${IfNot} ${AtLeastWin10}
    SetErrorLevel 2
    MessageBox MB_ICONSTOP "需要 Windows 10 或更新版本。"
    Abort
  ${EndIf}
  SetRegView 64
  ReadRegStr $InstalledLocation HKCU "${UNINSTALL_KEY}" "InstallLocation"
  ReadRegStr $InstalledVersion HKCU "${UNINSTALL_KEY}" "DisplayVersion"
  ${If} $InstalledLocation == ""
    ReadRegStr $InstalledLocation HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\${LEGACY_ID}" "InstallLocation"
    ReadRegStr $InstalledVersion HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\${LEGACY_ID}" "DisplayVersion"
  ${EndIf}
  ${If} $InstalledLocation != ""
    StrCpy $INSTDIR $InstalledLocation
  ${EndIf}
  System::Call 'kernel32::CreateMutexW(p 0, i 0, w "Local\${APP_ID}.Installer") p.s ?e'
  Pop $0
  Pop $InstallMutex
  ${If} $0 == 183
    SetErrorLevel 18
    Abort
  ${EndIf}
  ${If} $InstallMutex == 0
    SetErrorLevel 18
    Abort
  ${EndIf}
  !insertmacro RequireStopped
FunctionEnd

Function LaunchApplication
  System::Call 'kernel32::CloseHandle(p $InstallMutex)'
  StrCpy $InstallMutex 0
  Exec '"$INSTDIR\OpenLnku.exe" ${LAUNCH_ARGS}'
FunctionEnd

Function .onVerifyInstDir
  ${GetRoot} "$INSTDIR" $0
  StrCmp "$INSTDIR" "$0\" badDirectory
  StrCmp "$INSTDIR" "$WINDIR" badDirectory
  StrCmp "$INSTDIR" "$PROGRAMFILES" badDirectory
  StrCmp "$INSTDIR" "$LOCALAPPDATA" badDirectory
  IfFileExists "$INSTDIR\.opennex-installed" ownedDirectory
  StrCmp $INSTDIR $InstalledLocation legacyDirectory
  FindFirst $0 $1 "$INSTDIR\*"
  directoryLoop:
    StrCmp $1 "" directoryEmpty
    StrCmp $1 "." directoryNext
    StrCmp $1 ".." directoryNext
    FindClose $0
    Goto badDirectory
  directoryNext:
    FindNext $0 $1
    Goto directoryLoop
  directoryEmpty:
    FindClose $0
    Return
  legacyDirectory:
    IfFileExists "$INSTDIR\OpenLnku.exe" directoryValid badDirectory
  ownedDirectory:
    FileOpen $0 "$INSTDIR\.opennex-installed" r
    FileRead $0 $1
    FileClose $0
    StrCmp $1 "${APP_ID}" directoryValid
    StrCmp $1 "${LEGACY_ID}" directoryValid badDirectory
  badDirectory:
    SetErrorLevel 19
    IfSilent +2
    MessageBox MB_ICONEXCLAMATION "请选择空目录或现有 ${APP_TITLE} 安装目录。"
    Abort
  directoryValid:
FunctionEnd

Function un.onInit
  SetRegView 64
  System::Call 'kernel32::CreateMutexW(p 0, i 0, w "Local\${APP_ID}.Installer") p.s ?e'
  Pop $0
  Pop $InstallMutex
  ${If} $0 == 183
    SetErrorLevel 18
    Abort
  ${EndIf}
  ${If} $InstallMutex == 0
    SetErrorLevel 18
    Abort
  ${EndIf}
  StrCpy $PurgeSettings 0
  ${GetParameters} $0
  ClearErrors
  ${GetOptions} $0 "/PURGESETTINGS" $1
  ${IfNot} ${Errors}
    StrCpy $PurgeSettings 1
  ${EndIf}
  FileOpen $0 "$INSTDIR\.opennex-installed" r
  IfErrors invalidUninstall
  FileRead $0 $1
  FileClose $0
  StrCmp $1 "${APP_ID}" validUninstall
  invalidUninstall:
    SetErrorLevel 19
    Abort
  validUninstall:
    !insertmacro RequireStopped
FunctionEnd

Function un.SettingsPage
  nsDialogs::Create 1018
  Pop $0
  ${NSD_CreateCheckbox} 0 14u 100% 24u "同时删除保存的账号、密码与偏好"
  Pop $PurgeCheckbox
  ${NSD_SetState} $PurgeCheckbox $PurgeSettings
  nsDialogs::Show
FunctionEnd

Function un.SettingsLeave
  ${NSD_GetState} $PurgeCheckbox $PurgeSettings
FunctionEnd

!macro ManifestHelpers PREFIX SEARCH
Function ${PREFIX}TrimEntry
  trimLoop:
    StrCpy $0 $ManifestEntry 1 -1
    StrCmp $0 "$\r" trimChar
    StrCmp $0 "$\n" trimChar trimDone
  trimChar:
    StrCpy $ManifestEntry $ManifestEntry -1
    Goto trimLoop
  trimDone:
FunctionEnd
Function ${PREFIX}ValidateEntry
  StrCmp $ManifestEntry "" invalid
  ${${SEARCH}} $0 $ManifestEntry ".."
  StrCmp $0 "" +2
  Goto invalid
  ${${SEARCH}} $0 $ManifestEntry ":"
  StrCmp $0 "" +2
  Goto invalid
  ${${SEARCH}} $0 $ManifestEntry "/"
  StrCmp $0 "" +2
  Goto invalid
  StrCpy $0 $ManifestEntry 1
  StrCmp $0 "\" invalid
  ClearErrors
  Return
  invalid:
    SetErrors
FunctionEnd
Function ${PREFIX}RemoveEmptyParents
  ${GetParent} "$INSTDIR\$ManifestEntry" $R2
  parentLoop:
    StrCmp $R2 "$INSTDIR" parentDone
    StrCmp $R2 "" parentDone
    ClearErrors
    RMDir "$R2"
    IfErrors parentDone
    ${GetParent} "$R2" $R2
    Goto parentLoop
  parentDone:
    ClearErrors
FunctionEnd
!macroend
!insertmacro ManifestHelpers "" "StrStr"
!insertmacro ManifestHelpers "un." "UnStrStr"

Function CleanupObsolete
  IfFileExists "$INSTDIR\.opennex-files.txt" 0 cleanupDone
  FileOpen $R3 "$INSTDIR\.opennex-files.txt" r
  IfErrors oldInvalid
  FileOpen $R4 "$INSTDIR\.opennex-files.new" r
  IfErrors oldInvalid
  StrCpy $R7 0
  StrCpy $R5 ""
  oldLoop:
    ClearErrors
    FileRead $R3 $ManifestEntry
    IfErrors oldDone
    Call TrimEntry
    StrCmp $ManifestEntry "" oldLoop
    Call ValidateEntry
    IfErrors oldInvalid
    StrCmp $R7 1 removedFile
    StrCmp $R5 "" newLoop compareEntries
  newLoop:
      ClearErrors
      FileRead $R4 $R5
      IfErrors newEnd
      Push $ManifestEntry
      StrCpy $ManifestEntry $R5
      Call TrimEntry
      StrCpy $R5 $ManifestEntry
      Pop $ManifestEntry
  compareEntries:
      System::Call 'kernel32::CompareStringOrdinal(w "$ManifestEntry", i -1, w "$R5", i -1, i 1) i.s'
      Pop $R6
      StrCmp $R6 0 oldInvalid
      StrCmp $R6 2 oldLoop
      StrCmp $R6 3 newLoop removedFile
  newEnd:
      StrCpy $R7 1
    removedFile:
      StrCpy $0 $ManifestEntry 1 -1
      StrCmp $0 "\" removedDirectory
      IfFileExists "$INSTDIR\$ManifestEntry" 0 oldLoop
      ClearErrors
      Delete "$INSTDIR\$ManifestEntry"
      IfErrors oldInvalid
      Call RemoveEmptyParents
      Goto oldLoop
    removedDirectory:
      RMDir "$INSTDIR\$ManifestEntry"
      Call RemoveEmptyParents
      Goto oldLoop
  oldInvalid:
    FileClose $R4
    FileClose $R3
    SetErrorLevel 20
    Abort "旧文件清理失败。请关闭占用文件的程序后重新安装。"
  oldDone:
    FileClose $R4
    FileClose $R3
    ClearErrors
  cleanupDone:
FunctionEnd

Section "OpenLnku"
  Call .onVerifyInstDir
  !insertmacro RequireStopped
  SetOutPath "$INSTDIR"
  ClearErrors
  File /r "${PACKAGE_DIR}\*"
  IfErrors installFailed
  File /oname=.opennex-files.new "${MANIFEST_FILE}"
  Call CleanupObsolete
  Delete "$INSTDIR\.opennex-files.txt"
  Rename "$INSTDIR\.opennex-files.new" "$INSTDIR\.opennex-files.txt"
  IfErrors installFailed
  FileOpen $0 "$INSTDIR\.opennex-installed" w
  FileWrite $0 "${APP_ID}"
  FileClose $0
  WriteUninstaller "$INSTDIR\Uninstall.exe"
  IfErrors installFailed
  SetShellVarContext current
  ClearErrors
  CreateDirectory "${SHORTCUT_DIR}"
  CreateShortcut "${SHORTCUT_DIR}\OpenLnku.lnk" "$INSTDIR\OpenLnku.exe" "${LAUNCH_ARGS}"
  CreateShortcut "${SHORTCUT_DIR}\Uninstall.lnk" "$INSTDIR\Uninstall.exe"
  IfErrors installFailed
  !ifndef ISOLATED_FILES_ONLY
  WriteRegStr HKCU "${UNINSTALL_KEY}" "DisplayName" "${APP_TITLE}"
  WriteRegStr HKCU "${UNINSTALL_KEY}" "DisplayVersion" "${APP_VERSION}"
  WriteRegStr HKCU "${UNINSTALL_KEY}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKCU "${UNINSTALL_KEY}" "UninstallString" '$\"$INSTDIR\Uninstall.exe$\"'
  WriteRegStr HKCU "${UNINSTALL_KEY}" "QuietUninstallString" '$\"$INSTDIR\Uninstall.exe$\" /S'
  WriteRegStr HKCU "${UNINSTALL_KEY}" "DisplayIcon" "$INSTDIR\OpenLnku.exe"
  WriteRegDWORD HKCU "${UNINSTALL_KEY}" "NoModify" 1
  WriteRegDWORD HKCU "${UNINSTALL_KEY}" "NoRepair" 1
  IfErrors installFailed
  ; New installs default all three connection preferences on; upgrades preserve selections.
  !insertmacro ReadBool $0 "${SETTINGS_KEY}" "autoLogin"
  ${If} ${Errors}
    StrCpy $0 1
      !insertmacro ReadBool $1 "Software\${LEGACY_ID}\${LEGACY_ID}" "autoLogin"
      ${If} $1 == 0
        StrCpy $0 0
      ${EndIf}
    ClearErrors
    WriteRegDWORD HKCU "${SETTINGS_KEY}" "autoLogin" $0
    IfErrors installFailed
  ${EndIf}
  !insertmacro ReadBool $0 "${SETTINGS_KEY}" "autoReconnect"
  ${If} ${Errors}
    StrCpy $0 1
    !insertmacro ReadBool $1 "Software\${LEGACY_ID}\${LEGACY_ID}" "autoReconnect"
    ${If} $1 == 0
      StrCpy $0 0
    ${EndIf}
    ClearErrors
    WriteRegDWORD HKCU "${SETTINGS_KEY}" "autoReconnect" $0
    IfErrors installFailed
  ${EndIf}
  !insertmacro ReadBool $0 "${SETTINGS_KEY}" "startWithWindows"
  ${If} ${Errors}
    StrCpy $0 1
    !insertmacro ReadBool $1 "Software\${LEGACY_ID}\${LEGACY_ID}" "startWithWindows"
    ${IfNot} ${Errors}
      StrCpy $0 $1
    ${ElseIf} $InstalledLocation != ""
      ReadRegStr $1 HKCU "Software\Microsoft\Windows\CurrentVersion\Run" "${APP_ID}"
      ReadRegStr $2 HKCU "Software\Microsoft\Windows\CurrentVersion\Run" "${LEGACY_ID}"
      ${If} $1 == ""
      ${AndIf} $2 == ""
        StrCpy $0 0
      ${EndIf}
    ${Else}
        EnumRegValue $1 HKCU "Software\${LEGACY_ID}\${LEGACY_ID}" 0
        ${If} $1 != ""
          ReadRegStr $1 HKCU "Software\Microsoft\Windows\CurrentVersion\Run" "${LEGACY_ID}"
          ReadRegStr $2 HKCU "Software\Microsoft\Windows\CurrentVersion\Run" "${APP_ID}"
          ${If} $1 == ""
          ${AndIf} $2 == ""
            StrCpy $0 0
          ${EndIf}
        ${EndIf}
    ${EndIf}
    ClearErrors
    WriteRegDWORD HKCU "${SETTINGS_KEY}" "startWithWindows" $0
    IfErrors installFailed
  ${EndIf}
  ${If} $0 == 1
    ClearErrors
    WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Run" "${APP_ID}" '$\"$INSTDIR\OpenLnku.exe$\" ${AUTORUN_ARGS}'
    IfErrors installFailed
    DeleteRegValue HKCU "Software\Microsoft\Windows\CurrentVersion\Run" "${LEGACY_ID}"
  ${Else}
    ReadRegStr $1 HKCU "Software\Microsoft\Windows\CurrentVersion\Run" "${APP_ID}"
    StrLen $2 '$\"$INSTDIR\OpenLnku.exe$\"'
    StrCpy $1 $1 $2
    ${If} $1 == '$\"$INSTDIR\OpenLnku.exe$\"'
      ClearErrors
      DeleteRegValue HKCU "Software\Microsoft\Windows\CurrentVersion\Run" "${APP_ID}"
      IfErrors installFailed
    ${EndIf}
    !insertmacro ReadBool $1 "Software\${LEGACY_ID}\${LEGACY_ID}" "startWithWindows"
    ${IfNot} ${Errors}
      ${If} $1 == 0
        DeleteRegValue HKCU "Software\Microsoft\Windows\CurrentVersion\Run" "${LEGACY_ID}"
      ${EndIf}
    ${EndIf}
  ${EndIf}
  ClearErrors
  ; Respect an explicit disabled new preference even when the old boolean was absent.
  ; Remove only a legacy command pointing to the managed executable in this directory.
  ReadRegStr $1 HKCU "Software\Microsoft\Windows\CurrentVersion\Run" "${LEGACY_ID}"
  StrLen $2 '$\"$INSTDIR\OpenNex.exe$\"'
  StrCpy $1 $1 $2
  ${If} $1 == '$\"$INSTDIR\OpenNex.exe$\"'
    DeleteRegValue HKCU "Software\Microsoft\Windows\CurrentVersion\Run" "${LEGACY_ID}"
  ${EndIf}
  ClearErrors
  WriteRegDWORD HKCU "${SETTINGS_KEY}" "startupConfigured" 1
  IfErrors installFailed
  WriteRegDWORD HKCU "${SETTINGS_KEY}" "brandStartupMigrated" 1
  IfErrors installFailed
  ReadRegStr $1 HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\${LEGACY_ID}" "InstallLocation"
  ${If} $1 == $INSTDIR
    DeleteRegKey HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\${LEGACY_ID}"
    Delete "${LEGACY_SHORTCUT_DIR}\${LEGACY_ID}.lnk"
    Delete "${LEGACY_SHORTCUT_DIR}\Uninstall.lnk"
    RMDir "${LEGACY_SHORTCUT_DIR}"
  ${EndIf}
  !endif
  SetErrorLevel 0
  Goto installDone
  installFailed:
    SetErrorLevel 20
    Abort "安装未完成。请关闭占用文件的程序后重试。"
  installDone:
SectionEnd

Function un.DeleteSettingsKey
  Pop $R8
  System::Call 'advapi32::RegOpenKeyExW(p 0x80000001, w "$R8", i 0, i 0x20119, *p.r1) i.r0'
  ${If} $0 == 2
    ClearErrors
    Return
  ${EndIf}
  ${If} $0 != 0
    SetErrors
    Return
  ${EndIf}
  System::Call 'advapi32::RegCloseKey(p r1)'
  ClearErrors
  DeleteRegKey HKCU "$R8"
FunctionEnd

Section "Uninstall"
  SetRegView 64
  SetShellVarContext current
  !insertmacro RequireStopped
  FileOpen $R3 "$INSTDIR\.opennex-files.txt" r
  IfErrors unFailure
  unLoop:
    ClearErrors
    FileRead $R3 $ManifestEntry
    IfErrors unFilesDone
    Call un.TrimEntry
    StrCmp $ManifestEntry "" unLoop
    Call un.ValidateEntry
    IfErrors unInvalid
    StrCpy $0 $ManifestEntry 1 -1
    StrCmp $0 "\" unLoop
    IfFileExists "$INSTDIR\$ManifestEntry" 0 unLoop
    ClearErrors
    Delete "$INSTDIR\$ManifestEntry"
    IfErrors unInvalid
    Call un.RemoveEmptyParents
    Goto unLoop
  unInvalid:
    FileClose $R3
    Goto unFailure
  unFilesDone:
    FileClose $R3
    ClearErrors
    FileOpen $R3 "$INSTDIR\.opennex-files.txt" r
    IfErrors unFailure
  unDirectoriesLoop:
    ClearErrors
    FileRead $R3 $ManifestEntry
    IfErrors unDirectoriesDone
    Call un.TrimEntry
    StrCmp $ManifestEntry "" unDirectoriesLoop
    Call un.ValidateEntry
    IfErrors unInvalid
    StrCpy $0 $ManifestEntry 1 -1
    StrCmp $0 "\" 0 unDirectoriesLoop
    RMDir "$INSTDIR\$ManifestEntry"
    Call un.RemoveEmptyParents
    Goto unDirectoriesLoop
  unDirectoriesDone:
    FileClose $R3
    !ifndef ISOLATED_FILES_ONLY
    ReadRegStr $0 HKCU "Software\Microsoft\Windows\CurrentVersion\Run" "${APP_ID}"
    StrLen $1 '$\"$INSTDIR\OpenLnku.exe$\"'
    StrCpy $2 $0 $1
    ${If} $2 == '$\"$INSTDIR\OpenLnku.exe$\"'
      ClearErrors
      DeleteRegValue HKCU "Software\Microsoft\Windows\CurrentVersion\Run" "${APP_ID}"
      IfErrors unFailure
    ${EndIf}
    ${If} $PurgeSettings == 1
      Push "${SETTINGS_KEY}"
      Call un.DeleteSettingsKey
      IfErrors unFailure
      Push "Software\${LEGACY_ID}\${LEGACY_ID}"
      Call un.DeleteSettingsKey
      IfErrors unFailure
    ${EndIf}
    ReadRegStr $0 HKCU "${UNINSTALL_KEY}" "InstallLocation"
    ${If} $0 == $INSTDIR
      ClearErrors
      DeleteRegKey HKCU "${UNINSTALL_KEY}"
      IfErrors unFailure
    ${EndIf}
    !endif
    Delete "${SHORTCUT_DIR}\OpenLnku.lnk"
    Delete "${SHORTCUT_DIR}\Uninstall.lnk"
    RMDir "${SHORTCUT_DIR}"
    Delete "$INSTDIR\.opennex-files.txt"
    Delete "$INSTDIR\.opennex-installed"
    Delete "$INSTDIR\Uninstall.exe"
    RMDir "$INSTDIR"
    SetErrorLevel 0
    Goto unDone
  unFailure:
    SetErrorLevel 20
    IfSilent +2
    MessageBox MB_ICONEXCLAMATION "卸载未完成。请关闭占用文件的程序后重新卸载。"
    Quit
  unDone:
SectionEnd
