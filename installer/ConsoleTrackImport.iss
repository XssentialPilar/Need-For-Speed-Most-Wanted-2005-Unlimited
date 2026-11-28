function StageConsoleTrack(Source, Delta, Output: string): LongWord;
  external 'StageConsoleTrack@files:unlimited-setup-private.dll stdcall delayload setuponly';

procedure InstallConsoleTrack;
var
  Root, Source, Target, Stage, Temporary: string;
begin
  Root := AddBackslash(WizardDirValue);
  Source := Root + 'TRACKS\STREAML2RA.BUN';
  Target := Root + 'TRACKS\STREAML2X6.BUN';
  if FileExists(Target) then begin
    Log('Existing alternate console track retained.');
    Exit;
  end;
  if not FileExists(Source) then begin
    Log('Optional console track skipped: the stock PC track archive is missing.');
    Exit;
  end;
  ExtractTemporaryFile('ConsoleTrack.ups');
  Stage := Root + 'UnlimitedCommunity\SetupTrack-' + GetDateTimeString('yyyymmddhhnnss', '/', ':');
  if CreateAssetStage(Stage, 0) = 0 then begin
    Log('Optional console track skipped: could not create a staging directory.');
    Exit;
  end;
  Temporary := AddBackslash(Stage) + 'track.new';
  try
    if (StageConsoleTrack(Source, ExpandConstant('{tmp}\ConsoleTrack.ups'), Temporary) <> 0) and
      (MoveAsset(Temporary, Target, 8) <> 0) then
      Log('Verified optional console track generated from the stock PC track archive.')
    else
      Log('Optional console track skipped: incompatible source archive or write failure.');
  finally
    DeleteFile(Temporary);
    RemoveDir(Stage);
  end;
end;