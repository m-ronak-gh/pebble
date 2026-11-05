#define MyAppName "PebbleBit"
#define MyAppVersion "1.0.0"
#define MyAppDisplayVersion "v1.0.0"

[Setup]
AppId={{B72A8F19-6D34-4C1B-9E52-0F83A6D4172B}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppVerName={#MyAppName} {#MyAppDisplayVersion}
AppPublisher=PebbleBit Project
AppPublisherURL=https://github.com/m-ronak-gh/pebblebit
DefaultDirName={localappdata}\Programs\PebbleBit
DefaultGroupName=PebbleBit
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64os
ArchitecturesInstallIn64BitMode=x64os
WizardStyle=modern
UninstallDisplayName={#MyAppName} {#MyAppDisplayVersion}
OutputDir=..\..\dist\windows
OutputBaseFilename=PebbleBit-Setup-1.0.0
Compression=lzma2
SolidCompression=yes
SetupLogging=yes
Uninstallable=yes
CloseApplications=no

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "addtopath"; Description: "Add PebbleBit to your user PATH (recommended)"; GroupDescription: "Additional options:"; Flags: checkedonce

[Files]
Source: "..\..\build\windows-stage\bin\*"; DestDir: "{app}\bin"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "..\..\build\windows-stage\include\*"; DestDir: "{app}\include"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "..\..\build\windows-stage\lib\*"; DestDir: "{app}\lib"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "..\..\build\windows-stage\runtime\*"; DestDir: "{app}\runtime"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "..\..\build\windows-stage\licenses\*"; DestDir: "{app}\licenses"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "..\..\build\windows-stage\README.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\..\build\windows-stage\pebblebit-language-spec.md"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\PebbleBit README"; Filename: "{app}\README.md"
Name: "{group}\PebbleBit Language Specification"; Filename: "{app}\pebblebit-language-spec.md"
Name: "{group}\Uninstall PebbleBit"; Filename: "{uninstallexe}"

[Registry]
Root: HKCU; Subkey: "Environment"; ValueType: expandsz; ValueName: "Path"; ValueData: "{olddata};{app}\bin"; Flags: preservestringtype; Check: ShouldAddPath

[Code]
var
  PathEntryAddedByInstaller: Boolean;

function NormalizePathEntry(Value: string): string;
begin
  Result := Trim(Value);
  if (Length(Result) >= 2) and
     (Result[1] = '"') and
     (Result[Length(Result)] = '"') then
    Result := Copy(Result, 2, Length(Result) - 2);

  while (Length(Result) > 3) and
        (Result[Length(Result)] = '\') do
    Delete(Result, Length(Result), 1);
end;

function PathHasEntry(PathValue, Entry: string): Boolean;
var
  Remaining, Part: string;
  Separator: Integer;
begin
  Result := False;
  Remaining := PathValue;

  while Remaining <> '' do
  begin
    Separator := Pos(';', Remaining);

    if Separator = 0 then
    begin
      Part := Remaining;
      Remaining := '';
    end
    else
    begin
      Part := Copy(Remaining, 1, Separator - 1);
      Delete(Remaining, 1, Separator);
    end;

    if CompareText(NormalizePathEntry(Part),
                   NormalizePathEntry(Entry)) = 0 then
    begin
      Result := True;
      Exit;
    end;
  end;
end;

function ShouldAddPath: Boolean;
var
  ExistingPath, InstallBin: string;
begin
  ExistingPath := '';
  RegQueryStringValue(HKEY_CURRENT_USER, 'Environment', 'Path',
                      ExistingPath);

  InstallBin := ExpandConstant('{app}\bin');

  Result := WizardIsTaskSelected('addtopath') and
            (not PathHasEntry(ExistingPath, InstallBin));

  PathEntryAddedByInstaller := Result;
end;

procedure CurStepChanged(CurStep: TSetupStep);
var
  ExistingPath, InstallBin: string;
begin
  if (CurStep = ssPostInstall) and PathEntryAddedByInstaller then
  begin
    ExistingPath := '';
    InstallBin := ExpandConstant('{app}\bin');

    if RegQueryStringValue(HKEY_CURRENT_USER, 'Environment', 'Path',
                           ExistingPath) and
       PathHasEntry(ExistingPath, InstallBin) then
    begin
      if not RegWriteStringValue(HKEY_CURRENT_USER,
                                 'Software\PebbleBit\Installer',
                                 'PathEntryOwned', InstallBin) then
        MsgBox('PebbleBit was added to PATH, but the installer could not record PATH ownership. The entry may need manual removal after uninstall.',
               mbError, MB_OK);
    end;
  end;
end;

procedure RemoveInstallPath;
var
  OwnedPath, ExistingPath, Remaining, Part, NewPath: string;
  Separator: Integer;
  RemovedOwnedEntry, FirstPart, HasRemainingParts: Boolean;
begin
  OwnedPath := '';

  if not RegQueryStringValue(HKEY_CURRENT_USER,
                             'Software\PebbleBit\Installer',
                             'PathEntryOwned', OwnedPath) then
    Exit;

  ExistingPath := '';

  if RegQueryStringValue(HKEY_CURRENT_USER, 'Environment', 'Path',
                         ExistingPath) then
  begin
    Remaining := ExistingPath;
    NewPath := '';
    RemovedOwnedEntry := False;
    FirstPart := True;
    HasRemainingParts := False;

    while True do
    begin
      Separator := Pos(';', Remaining);

      if Separator = 0 then
      begin
        Part := Remaining;
        Remaining := '';
      end
      else
      begin
        Part := Copy(Remaining, 1, Separator - 1);
        Delete(Remaining, 1, Separator);
      end;

      if (not RemovedOwnedEntry) and
         (CompareText(NormalizePathEntry(Part),
                      NormalizePathEntry(OwnedPath)) = 0) then
        RemovedOwnedEntry := True
      else
      begin
        if not FirstPart then
          NewPath := NewPath + ';';

        NewPath := NewPath + Part;
        FirstPart := False;
        HasRemainingParts := True;
      end;

      if Separator = 0 then
        Break;
    end;

    if RemovedOwnedEntry then
    begin
      if HasRemainingParts then
        RegWriteExpandStringValue(HKEY_CURRENT_USER, 'Environment',
                                  'Path', NewPath)
      else
        RegDeleteValue(HKEY_CURRENT_USER, 'Environment', 'Path');
    end;
  end;

  RegDeleteValue(HKEY_CURRENT_USER, 'Software\PebbleBit\Installer',
                 'PathEntryOwned');
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usPostUninstall then
    RemoveInstallPath;
end;
