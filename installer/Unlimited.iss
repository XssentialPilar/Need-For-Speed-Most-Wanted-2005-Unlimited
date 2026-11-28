#ifndef BuildPayload
  #error BuildPayload must point to the staged game and setup DLLs.
#endif
#if !Defined(EmbeddedHudPayload) && !Defined(RuntimeOnlyBuild) && !Defined(ConsoleUpdateOnly)
  #error The standard installer requires an embedded HD payload.
#endif

[Setup]
AppId=NFSMW-Unlimited
AppName=Need for Speed Most Wanted 2005 Unlimited
AppVersion=1.1.0-beta.1
AppVerName=Need for Speed Most Wanted 2005 Unlimited 1.1.0-beta.1
VersionInfoVersion=1.1.0.1
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
#ifdef ConsoleUpdateOnly
OutputBaseFilename=NFSMW-Unlimited-Console-Update
VersionInfoDescription=Unlimited Xbox 360 Integration Update
Compression=lzma2/fast
#elif Defined(CompactBuild)
OutputBaseFilename=NFSMW-Unlimited-Compact-Setup
VersionInfoDescription=Unlimited Compact Setup with Xbox 360 Integration
Compression=lzma2/max
#elif Defined(RuntimeOnlyBuild)
OutputBaseFilename=NFSMW-Unlimited-Runtime-Setup
VersionInfoDescription=Unlimited Runtime Setup
Compression=lzma2/max
#else
OutputBaseFilename=NFSMW-Unlimited-Setup
VersionInfoDescription=Unlimited Setup with HD Interface
Compression=lzma2/fast
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
#ifndef ConsoleUpdateOnly
Source: "upscaled-hud-checksums.tsv"; Flags: dontcopy
#endif
#ifdef EmbeddedHudPayload
#include EmbeddedHudPayload
#endif
#ifdef EmbeddedTextures
Source: "{#BuildPayload}\UnlimitedTextures.bin"; DestDir: "{app}\GLOBAL"; Flags: ignoreversion solidbreak
#endif
#ifdef EmbeddedConsolePack
Source: "{#BuildPayload}\UnlimitedXboxTextures.bin"; DestDir: "{app}\GLOBAL"; Flags: ignoreversion solidbreak
#ifndef CompactBuild
Source: "{#BuildPayload}\X3_Music.mpf"; DestDir: "{app}\SOUND\PFDATA"; Flags: ignoreversion
Source: "{#BuildPayload}\X3_Music.mus"; DestDir: "{app}\SOUND\PFDATA"; Flags: ignoreversion solidbreak
#endif
Source: "{#BuildPayload}\console-engine-checksums.tsv"; Flags: dontcopy
Source: "{#BuildPayload}\*.xgin"; Flags: dontcopy
#ifndef CompactBuild
Source: "{#BuildPayload}\Xbox360Stuff-LICENSE.txt"; DestDir: "{app}\UnlimitedCommunity"; Flags: ignoreversion
#endif
#endif
#ifdef CompactBuild
Source: "{#BuildPayload}\ConsoleTrack.ups"; Flags: dontcopy
#endif
#ifdef EmbeddedConsoleTrack
Source: "{#BuildPayload}\STREAML2X6.BUN"; DestDir: "{app}\TRACKS"; Flags: ignoreversion solidbreak
#endif
#ifdef EmbeddedConsolePlatforms
Source: "{#BuildPayload}\WorldRenderer\World360.dll"; DestDir: "{app}\UnlimitedCommunity"; Flags: ignoreversion
Source: "{#BuildPayload}\WorldRenderer\World360.ini"; DestDir: "{app}\UnlimitedCommunity"; Flags: onlyifdoesntexist
Source: "{#BuildPayload}\WorldRenderer\Xbox360Stuff-LICENSE.txt"; DestDir: "{app}\UnlimitedCommunity\World360"; Flags: ignoreversion
Source: "{#BuildPayload}\WorldRenderer\FX\*"; DestDir: "{app}\FX"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#BuildPayload}\GLOB36B.LZC"; DestDir: "{app}\GLOBAL"; Flags: ignoreversion solidbreak
Source: "{#BuildPayload}\INGAM36A.BUN"; DestDir: "{app}\GLOBAL"; Flags: ignoreversion solidbreak
Source: "{#BuildPayload}\DYNTEX36.BIN"; DestDir: "{app}\GLOBAL"; Flags: ignoreversion solidbreak
Source: "{#BuildPayload}\ConsolePlatforms\*.BIN"; DestDir: "{app}\FRONTEND\PLATFORMS\Unlimited360"; Flags: ignoreversion solidbreak
#endif
#ifdef EmbeddedConsoleVault
Source: "{#BuildPayload}\ATTRIBUTE360.BIN"; DestDir: "{app}\GLOBAL"; Flags: ignoreversion
Source: "{#BuildPayload}\ATTRIBX36ALT.BIN"; DestDir: "{app}\GLOBAL"; Flags: ignoreversion
#endif
Source: "{#BuildPayload}\d3d9.dll"; DestDir: "{app}"; Flags: ignoreversion; BeforeInstall: BackupOriginal('d3d9.dll')
Source: "{#BuildPayload}\Discx\*"; DestDir: "{app}\UnlimitedCommunity\Discx"; Flags: ignoreversion recursesubdirs
Source: "..\LICENSE"; DestDir: "{app}\UnlimitedCommunity"; DestName: "Unlimited-LICENSE.txt"; Flags: ignoreversion
Source: "..\third-party\split-screen\LICENSE"; DestDir: "{app}\UnlimitedCommunity"; DestName: "SplitScreen-LICENSE.txt"; Flags: ignoreversion
#ifndef ConsoleUpdateOnly
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

[Dirs]
Name: "{app}\UnlimitedMusic"; Permissions: users-modify; Flags: uninsneveruninstall
#endif

[Code]
function VerifyGameDirectory(Dir: string): LongWord;
  external 'VerifyGameDirectory@files:unlimited-setup-private.dll stdcall delayload setuponly';

procedure BackupOriginal(FileName: string); forward;

#ifndef ConsoleUpdateOnly
#include "AssetImport.iss"
#endif

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
  #ifdef ConsoleUpdateOnly
  if (Result = '') and
     ((not FileExists(AddBackslash(Dir) + 'NFSMW-Unlimited.ini')) or
      (not FileExists(AddBackslash(Dir) + 'GLOBAL\UnlimitedXboxTextures.bin'))) then
    Result := 'Install Unlimited with the Xbox 360 asset pack before applying this update.';
  #endif
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
  #ifndef ConsoleUpdateOnly
  if Result = '' then
    Result := PrepareAssetImport;
  #endif
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
