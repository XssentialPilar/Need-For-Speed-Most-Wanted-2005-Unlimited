#ifndef BuildPayload
  #error BuildPayload must point to the staged game and setup DLLs.
#endif

[Setup]
AppId=NFSMW-Unlimited
AppName=Need for Speed Most Wanted 2005 Unlimited
AppVersion=1.5
AppVerName=Need for Speed Most Wanted 2005 Unlimited
AppPublisher=XssentialPilar
AppPublisherURL=https://github.com/XssentialPilar/Need-For-Speed-Most-Wanted-2005-Unlimited
MinVersion=6.1sp1
DefaultDirName={code:FindGameFolder}
DisableDirPage=no
DisableProgramGroupPage=yes
DisableWelcomePage=no
DirExistsWarning=no
UsePreviousAppDir=no
ShowLanguageDialog=yes
LanguageDetectionMethod=uilanguage
PrivilegesRequired=admin
PrivilegesRequiredOverridesAllowed=commandline
CloseApplications=no
RestartApplications=no
Uninstallable=no
#ifdef LocalHudPayload
OutputBaseFilename=NFSMW-Unlimited-Local-HD-Setup
Compression=lzma2/fast
#else
OutputBaseFilename=NFSMW-Unlimited-Setup
Compression=lzma2/max
#endif
SolidCompression=yes
WizardStyle=modern

[Languages]
Name: "en"; MessagesFile: "compiler:Default.isl"
Name: "es"; MessagesFile: "compiler:Languages\Spanish.isl"
Name: "fr"; MessagesFile: "compiler:Languages\French.isl"
Name: "de"; MessagesFile: "compiler:Languages\German.isl"
Name: "it"; MessagesFile: "compiler:Languages\Italian.isl"
Name: "nl"; MessagesFile: "compiler:Languages\Dutch.isl"
Name: "pt"; MessagesFile: "compiler:Languages\Portuguese.isl"
Name: "pl"; MessagesFile: "compiler:Languages\Polish.isl"
Name: "ru"; MessagesFile: "compiler:Languages\Russian.isl"
Name: "ja"; MessagesFile: "compiler:Languages\Japanese.isl"
Name: "sv"; MessagesFile: "compiler:Languages\Swedish.isl"
Name: "da"; MessagesFile: "compiler:Languages\Danish.isl"

#include "Messages.iss"

[Files]
Source: "{#BuildPayload}\unlimited-setup-private.dll"; Flags: dontcopy
Source: "upscaled-hud-checksums.tsv"; Flags: dontcopy
#ifdef LocalHudPayload
#include LocalHudPayload
#endif
Source: "{#BuildPayload}\d3d9.dll"; DestDir: "{app}"; Flags: ignoreversion; BeforeInstall: BackupOriginal('d3d9.dll')
Source: "..\third-party\controllers\dinput8.dll"; DestDir: "{app}"; Flags: ignoreversion; BeforeInstall: BackupOriginal('dinput8.dll')
Source: "..\third-party\controllers\Xidi.32.dll"; DestDir: "{app}"; Flags: ignoreversion; BeforeInstall: BackupOriginal('Xidi.32.dll')
Source: "..\third-party\controllers\SDL3.dll"; DestDir: "{app}"; Flags: ignoreversion; BeforeInstall: BackupOriginal('SDL3.dll')
Source: "..\third-party\controllers\Xidi.ini"; DestDir: "{app}"; Flags: onlyifdoesntexist
Source: "NFSMW-Unlimited.ini"; DestDir: "{app}"; Flags: onlyifdoesntexist
Source: "..\third-party\controllers\Licenses\*.txt"; DestDir: "{app}\UnlimitedCommunity\ControllerLicenses"; Flags: ignoreversion
Source: "..\third-party\xenon\NFSMW_XenonEffects.asi"; DestDir: "{app}\UnlimitedCommunity"; Flags: ignoreversion
Source: "..\third-party\xenon\XenonEffects.tpk"; DestDir: "{app}\GLOBAL"; Flags: ignoreversion
Source: "..\third-party\xenon\NFSMW_XenonEffects.ini"; DestDir: "{app}"; Flags: onlyifdoesntexist
Source: "..\third-party\xenon\LICENSE"; DestDir: "{app}\UnlimitedCommunity"; DestName: "NFSMW_XenonEffects-LICENSE.txt"; Flags: ignoreversion
Source: "{#LegacyUcrt}\*.dll"; DestDir: "{app}"; Flags: onlyifdoesntexist; OnlyBelowVersion: 10.0

[Code]
function VerifyGameDirectory(Dir: string): LongWord;
  external 'VerifyGameDirectory@files:unlimited-setup-private.dll stdcall delayload setuponly';

#include "AssetImport.iss"

function FindGameFolder(Param: string): string;
begin
  if not RegQueryStringValue(HKLM32, 'SOFTWARE\EA GAMES\Need for Speed Most Wanted', 'Install Dir', Result) then
    Result := ExpandConstant('{pf32}\Need for Speed Most Wanted 2005');
end;

function GameError(Dir: string): string;
begin
  case VerifyGameDirectory(Dir) of
    0: Result := '';
    2: Result := CustomMessage('GameOpen');
  else
    Result := CustomMessage('WrongGame');
  end;
end;

function NextButtonClick(PageID: Integer): Boolean;
var
  ErrorText: string;
begin
  Result := True;
  if PageID = wpSelectDir then begin
    ErrorText := GameError(WizardDirValue);
    Result := ErrorText = '';
    if not Result then
      SuppressibleMsgBox(ErrorText, mbError, MB_OK, IDOK);
  end;
end;

function PrepareToInstall(var NeedsRestart: Boolean): string;
begin
  Log('Unlimited setup language: ' + ActiveLanguage);
  Result := GameError(WizardDirValue);
  if Result = '' then
    Result := PrepareAssetImport;
end;

procedure BackupOriginal(FileName: string);
var
  Original, BackupDir, Backup: string;
begin
  Original := AddBackslash(WizardDirValue) + FileName;
  if not FileExists(Original) then Exit;
  BackupDir := AddBackslash(WizardDirValue) + 'UnlimitedCommunity\BeforeUnlimited';
  Backup := AddBackslash(BackupDir) + FileName;
  if FileExists(Backup) then Exit;
  if not ForceDirectories(BackupDir) or not CopyFile(Original, Backup, True) then
    RaiseException(FmtMessage(CustomMessage('BackupFailed'), [FileName]));
end;
