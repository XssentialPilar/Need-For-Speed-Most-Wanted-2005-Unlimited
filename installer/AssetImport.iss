type
  TAsset = record
    Source: string;
    Relative: string;
    BeforeHash: string;
    AfterHash: string;
  end;

const
  ReplaceAndFlush = 9;

var
  AssetPage: TWizardPage;
  AssetEdits: array[0..1] of TNewEdit;
  Assets: array of TAsset;
  AssetImportError: Boolean;

function MoveAsset(Source, Target: string; Flags: LongWord): LongWord;
  external 'MoveFileExW@kernel32.dll stdcall setuponly';
function CreateAssetStage(Path: string; SecurityAttributes: LongWord): LongWord;
  external 'CreateDirectoryW@kernel32.dll stdcall setuponly';
function VerifyReplacementMovie(Path: string; Pal: LongWord): LongWord;
  external 'VerifyReplacementMovie@files:unlimited-setup-private.dll stdcall delayload setuponly';

procedure BrowseAssetFolder(Sender: TObject);
var
  Button: TNewButton;
  Folder: string;
begin
  Button := TNewButton(Sender);
  Folder := AssetEdits[Button.Tag].Text;
  if BrowseForFolder(SetupMessage(msgBrowseDialogTitle), Folder, False) then
    AssetEdits[Button.Tag].Text := Folder;
end;

procedure InitializeWizard;
var
  Instructions, LabelText: TNewStaticText;
  Browse: TNewButton;
  Top, I: Integer;
begin
  AssetPage := CreateCustomPage(wpSelectDir, CustomMessage('AssetTitle'), CustomMessage('AssetSubtitle'));
  Instructions := TNewStaticText.Create(AssetPage);
  Instructions.Parent := AssetPage.Surface;
  Instructions.AutoSize := False;
  Instructions.Width := AssetPage.SurfaceWidth;
  Instructions.WordWrap := True;
  #ifdef LocalHudPayload
  Instructions.Caption := CustomMessage('EmbeddedHudInstructions');
  #else
  Instructions.Caption := CustomMessage('AssetInstructions');
  #endif
  WizardForm.AdjustLabelHeight(Instructions);
  Top := Instructions.Height + ScaleY(12);

  for I := 0 to 1 do begin
    LabelText := TNewStaticText.Create(AssetPage);
    LabelText.Parent := AssetPage.Surface;
    LabelText.Top := Top;
    if I = 0 then LabelText.Caption := CustomMessage('UpscaledHudSource')
    else LabelText.Caption := CustomMessage('HDCutsceneSource');
    Top := Top + LabelText.Height + ScaleY(4);
    Browse := TNewButton.Create(AssetPage);
    Browse.Parent := AssetPage.Surface;
    Browse.Caption := SetupMessage(msgButtonBrowse);
    Browse.SetBounds(AssetPage.SurfaceWidth - ScaleX(96), Top, ScaleX(96), ScaleY(24));
    Browse.Tag := I;
    Browse.OnClick := @BrowseAssetFolder;
    #ifdef LocalHudPayload
    if I = 0 then Browse.Enabled := False;
    #endif
    AssetEdits[I] := TNewEdit.Create(AssetPage);
    AssetEdits[I].Parent := AssetPage.Surface;
    AssetEdits[I].SetBounds(0, Top + ScaleY(1), Browse.Left - ScaleX(8), ScaleY(22));
    LabelText.FocusControl := AssetEdits[I];
    Top := Top + ScaleY(36);
  end;
  AssetEdits[0].Text := ExpandConstant('{param:HDHUD|}');
  AssetEdits[1].Text := ExpandConstant('{param:HDMOVIES|}');
  #ifdef LocalHudPayload
  AssetEdits[0].Enabled := False;
  AssetEdits[0].Text := CustomMessage('EmbeddedHud');
  #endif
end;

function AssetDigest(Path: string): string;
begin
  Result := '';
  if FileExists(Path) then Result := Lowercase(GetSHA256OfFile(Path));
end;

procedure QueueAsset(Source, Relative, BeforeHash, AfterHash: string);
var
  Count: Integer;
begin
  if BeforeHash = AfterHash then Exit;
  Count := GetArrayLength(Assets);
  SetArrayLength(Assets, Count + 1);
  Assets[Count].Source := Source;
  Assets[Count].Relative := Relative;
  Assets[Count].BeforeHash := BeforeHash;
  Assets[Count].AfterHash := AfterHash;
end;

function PrepareHudImport(SourceRoot, GameRoot: string): string;
var
  Manifest: TArrayOfString;
  Relative, Source, Original, Expected, Current, Line: string;
  I, TabAt: Integer;
  Embedded, Extracted: Boolean;
begin
  ExtractTemporaryFile('upscaled-hud-checksums.tsv');
  if not LoadStringsFromFile(ExpandConstant('{tmp}\upscaled-hud-checksums.tsv'), Manifest) then
    RaiseException(CustomMessage('AssetImportFailed'));
  Embedded := SourceRoot = '';
  Extracted := False;
  if not Embedded then SourceRoot := AddBackslash(SourceRoot);
  for I := 0 to GetArrayLength(Manifest) - 1 do begin
    Line := Manifest[I];
    if Line = '' then Continue;
    TabAt := Pos(#9, Line);
    Relative := Copy(Line, 1, TabAt - 1);
    Original := Copy(Line, TabAt + 1, 64);
    Expected := Copy(Line, TabAt + 66, 64);
    Current := AssetDigest(GameRoot + Relative);
    if Current = Expected then Continue;
    if Current <> Original then begin
      Result := FmtMessage(CustomMessage('AssetConflict'), [Relative]);
      Exit;
    end;
    if Embedded then begin
      if not Extracted then begin
        ExtractTemporaryFiles('*.hdasset');
        Extracted := True;
      end;
      Source := ExpandConstant('{tmp}\') + Expected + '.hdasset';
    end else
      Source := SourceRoot + Relative;
    if AssetDigest(Source) <> Expected then begin
      Result := FmtMessage(CustomMessage('WrongHudSource'), [Relative]);
      Exit;
    end;
    QueueAsset(Source, Relative, Current, Expected);
  end;
  Result := '';
end;

function PrepareMovieImport(SourceRoot, GameRoot: string): string;
var
  Match: TFindRec;
  Name, Source, Relative, Expected: string;
  Pal, Valid, Found: Boolean;
begin
  Result := FmtMessage(CustomMessage('WrongMovieSource'), [SourceRoot]);
  SourceRoot := AddBackslash(SourceRoot);
  Found := False;
  if not FindFirst(SourceRoot + '*.vp6', Match) then Exit;
  try
    repeat
      if (Match.Attributes and FILE_ATTRIBUTE_DIRECTORY) = 0 then begin
        Name := Lowercase(Match.Name);
        Source := SourceRoot + Match.Name;
        Pal := Copy(Name, Length(Name) - 7, 8) = '_pal.vp6';
        Valid := Pal or (Copy(Name, Length(Name) - 8, 9) = '_ntsc.vp6');
        if not Valid or not FileExists(GameRoot + 'MOVIES\' + Match.Name) or
          (VerifyReplacementMovie(Source, Ord(Pal)) = 0) then begin
          Result := FmtMessage(CustomMessage('WrongMovieSource'), [Match.Name]);
          Exit;
        end;
        Found := True;
        Relative := 'MOVIES_HD\' + Match.Name;
        Expected := AssetDigest(Source);
        QueueAsset(Source, Relative, AssetDigest(GameRoot + Relative), Expected);
      end;
    until not FindNext(Match);
  finally
    FindClose(Match);
  end;
  if Found then Result := '';
end;

function PrepareAssetImport: string;
var
  GameRoot: string;
begin
  SetArrayLength(Assets, 0);
  Result := '';
  GameRoot := AddBackslash(WizardDirValue);
  try
    #ifdef LocalHudPayload
    Result := PrepareHudImport('', GameRoot);
    #else
    if Trim(AssetEdits[0].Text) <> '' then
      Result := PrepareHudImport(Trim(AssetEdits[0].Text), GameRoot);
    #endif
    if (Result = '') and (Trim(AssetEdits[1].Text) <> '') then
      Result := PrepareMovieImport(Trim(AssetEdits[1].Text), GameRoot);
  except
    Result := CustomMessage('AssetImportFailed');
  end;
end;

procedure ApplyAssetImport;
var
  GameRoot, Stage, Target, Backup, Temporary, Previous: string;
  Status: TOutputProgressWizardPage;
  I, Count, Applied: Integer;
  KeepStage: Boolean;
begin
  Count := GetArrayLength(Assets);
  if Count = 0 then Exit;
  GameRoot := AddBackslash(WizardDirValue);
  Stage := GameRoot + 'UnlimitedCommunity\SetupAssets-' + GetDateTimeString('yyyymmddhhnnss', '/', ':');
  if CreateAssetStage(Stage, 0) = 0 then RaiseException(CustomMessage('AssetImportFailed'));
  Applied := 0;
  KeepStage := False;
  Status := CreateOutputProgressPage(CustomMessage('AssetTitle'), CustomMessage('AssetProgress'));
  Status.Show;
  try
    try
      // Copy and verify every asset before changing any installed file.
      for I := 0 to Count - 1 do begin
        Status.SetText(CustomMessage('AssetProgress'), Assets[I].Relative);
        Status.SetProgress(I, Count * 2);
        Target := GameRoot + Assets[I].Relative;
        Temporary := AddBackslash(Stage) + IntToStr(I) + '.new';
        if not CopyFile(Assets[I].Source, Temporary, True) or
          (AssetDigest(Temporary) <> Assets[I].AfterHash) or
          (AssetDigest(Target) <> Assets[I].BeforeHash) then
          RaiseException(FmtMessage(CustomMessage('AssetConflict'), [Assets[I].Relative]));
        if Assets[I].BeforeHash <> '' then begin
          Previous := AddBackslash(Stage) + IntToStr(I) + '.old';
          Backup := GameRoot + 'UnlimitedCommunity\BeforeUnlimited\' + Assets[I].Relative;
          if not CopyFile(Target, Previous, True) or
            (AssetDigest(Previous) <> Assets[I].BeforeHash) or
            (not FileExists(Backup) and
            (not ForceDirectories(ExtractFileDir(Backup)) or not CopyFile(Previous, Backup, True))) then
            RaiseException(FmtMessage(CustomMessage('BackupFailed'), [Assets[I].Relative]));
        end;
      end;
      for I := 0 to Count - 1 do begin
        Status.SetText(CustomMessage('AssetProgress'), Assets[I].Relative);
        Status.SetProgress(Count + I, Count * 2);
        Target := GameRoot + Assets[I].Relative;
        Temporary := AddBackslash(Stage) + IntToStr(I) + '.new';
        if (AssetDigest(Target) <> Assets[I].BeforeHash) or
          not ForceDirectories(ExtractFileDir(Target)) or
          (MoveAsset(Temporary, Target, ReplaceAndFlush) = 0) then
          RaiseException(FmtMessage(CustomMessage('AssetConflict'), [Assets[I].Relative]));
        Applied := Applied + 1;
      end;
      Log('Verified HD assets installed: ' + IntToStr(Applied));
    except
      for I := Applied - 1 downto 0 do begin
        Target := GameRoot + Assets[I].Relative;
        Previous := AddBackslash(Stage) + IntToStr(I) + '.old';
        if Assets[I].BeforeHash <> '' then begin
          if MoveAsset(Previous, Target, ReplaceAndFlush) = 0 then KeepStage := True;
        end else if not DeleteFile(Target) then KeepStage := True;
      end;
      RaiseException(GetExceptionMessage);
    end;
  finally
    Status.Hide;
    if KeepStage then
      Log('Recovery files retained in ' + Stage)
    else begin
      for I := 0 to Count - 1 do begin
        DeleteFile(AddBackslash(Stage) + IntToStr(I) + '.new');
        DeleteFile(AddBackslash(Stage) + IntToStr(I) + '.old');
      end;
      RemoveDir(Stage);
    end;
  end;
end;

procedure CurStepChanged(Step: TSetupStep);
begin
  if Step = ssPostInstall then
    try
      ApplyAssetImport;
    except
      AssetImportError := True;
      Log('HD asset import failed: ' + GetExceptionMessage);
      SuppressibleMsgBox(GetExceptionMessage, mbError, MB_OK, IDOK);
    end;
end;

function GetCustomSetupExitCode: Integer;
begin
  Result := 0;
  if AssetImportError then Result := 10;
end;
