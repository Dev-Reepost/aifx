; SPDX-License-Identifier: BSD-3-Clause
; Copyright AIFX contributors.
;
; AIFX Windows installer (Inno Setup 6).
;
; A native Setup.exe wizard, the Windows counterpart of the macOS .dmg wizard
; at installer/macos/. It:
;   1. lets the user choose per-user vs all-users install location
;   2. collects site configuration (ComfyUI server address/port + the two
;      shared-folder mount paths + job timeout + processing toggle) on a
;      custom wizard page
;   3. copies the seven .ofx.bundle payloads into the OFX plugin directory
;   4. rewrites each bundle's Contents\Resources\config\defaults.json with the
;      collected site config (via patch-defaults.ps1) unless the user opts to
;      keep the bundled defaults
;
; The seven bundle payloads are staged into Bundles\ by
; tools/release-windows-installer.ps1 before this script is compiled with ISCC.
;
; AppVersion is injected by the release script:  ISCC /DAppVersion=0.1.13 ...
; It falls back to "dev" for a manual compile.

#ifndef AppVersion
  #define AppVersion "dev"
#endif

#define AppName "AIFX"
#define AppPublisher "MaGMa for Reepost Studio"
#define AppURL "https://dev-reepost.github.io/aifx/"
#define DocsURL "https://dev-reepost.github.io/aifx/"

[Setup]
; A stable AppId keeps upgrades/uninstall tracking consistent across versions.
AppId={{B7E6F2A1-3C9D-4E58-9A2F-1C0FFEE54321}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher={#AppPublisher}
AppPublisherURL={#AppURL}
AppSupportURL={#AppURL}
VersionInfoVersion=0.0.0
VersionInfoDescription=AIFX OpenFX plugins installer
WizardStyle=modern
; The plugins are x64; install as a 64-bit app so {commoncf} resolves to
; C:\Program Files\Common Files (not the WOW64 redirect).
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
; Default to a per-user install (no admin); let the user elevate to all-users
; from the standard "install mode" dialog.
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
DefaultDirName={code:GetDefaultDirName}
; This is a shared plugin directory, not a dedicated app folder - no Start menu
; group, and don't refuse to install into a non-empty directory.
DisableProgramGroupPage=yes
DisableDirPage=no
DirExistsWarning=no
AllowNoIcons=yes
UninstallDisplayName={#AppName} {#AppVersion} (OpenFX plugins)
OutputBaseFilename=aifx-{#AppVersion}-windows-setup
OutputDir=Output
SolidCompression=yes
Compression=lzma2/max
; Optional: a setup icon, only used if installer/windows/app/AppIcon.ico exists.
#if FileExists("app\AppIcon.ico")
SetupIconFile=app\AppIcon.ico
#endif

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Files]
; The seven .ofx.bundle directories, staged by the release script.
Source: "Bundles\*"; DestDir: "{app}"; Flags: recursesubdirs createallsubdirs ignoreversion
; The JSON patcher runs from {tmp} post-install; never installed permanently.
Source: "patch-defaults.ps1"; Flags: dontcopy

[Run]
; Offer to open the documentation when finished (unchecked by default).
Filename: "{#DocsURL}"; Description: "Open the AIFX documentation in your browser"; \
    Flags: postinstall shellexec skipifsilent unchecked nowait

[Code]
var
  ConfigPage: TInputQueryWizardPage;
  KeepDefaultsCheck: TNewCheckBox;
  EnableProcessingCheck: TNewCheckBox;

const
  IDX_ADDRESS     = 0;
  IDX_PORT        = 1;
  IDX_LOCALMOUNT  = 2;
  IDX_SERVERMOUNT = 3;
  IDX_TIMEOUT     = 4;

{ Default install directory: all-users -> Common Files\OFX\Plugins,
  per-user -> %LOCALAPPDATA%\OFX\Plugins. Mirrors the table in
  docs/installation.md. }
function GetDefaultDirName(Param: String): String;
begin
  if IsAdminInstallMode then
    Result := ExpandConstant('{commoncf}\OFX\Plugins')
  else
    Result := ExpandConstant('{localappdata}\OFX\Plugins');
end;

{ Enable/disable the site-config edits when the "keep bundled defaults"
  checkbox is toggled. }
procedure UpdateConfigEnabled;
var
  Keep: Boolean;
  I: Integer;
begin
  Keep := KeepDefaultsCheck.Checked;
  for I := 0 to 4 do
  begin
    ConfigPage.Edits[I].Enabled := not Keep;
    ConfigPage.PromptLabels[I].Enabled := not Keep;
  end;
  EnableProcessingCheck.Enabled := not Keep;
end;

procedure KeepDefaultsClick(Sender: TObject);
begin
  UpdateConfigEnabled;
end;

procedure InitializeWizard;
var
  Y: Integer;
begin
  ConfigPage := CreateInputQueryPage(wpSelectDir,
    'Site configuration',
    'Point AIFX at your ComfyUI server and shared storage.',
    'These values are baked into each plugin''s defaults.json. You can still ' +
    'override any of them per-clip in your host''s parameter panel later.');

  ConfigPage.Add('ComfyUI server address (hostname or IP):', False);
  ConfigPage.Add('ComfyUI server port:', False);
  ConfigPage.Add('This PC''s view of the shared folder (Local Storage Mount; leave blank to reuse the server view):', False);
  ConfigPage.Add('ComfyUI server''s view of the shared folder (UNC path):', False);
  ConfigPage.Add('Job timeout (seconds):', False);

  ConfigPage.Values[IDX_ADDRESS]     := '127.0.0.1';
  ConfigPage.Values[IDX_PORT]        := '8188';
  ConfigPage.Values[IDX_LOCALMOUNT]  := '\\COMFYUI-HOST\silo\AIFX';
  ConfigPage.Values[IDX_SERVERMOUNT] := '\\COMFYUI-HOST\silo\AIFX';
  ConfigPage.Values[IDX_TIMEOUT]     := '600';

  { Place the two checkboxes below the last edit field. }
  Y := ConfigPage.Edits[IDX_TIMEOUT].Top + ConfigPage.Edits[IDX_TIMEOUT].Height + ScaleY(12);

  EnableProcessingCheck := TNewCheckBox.Create(ConfigPage);
  EnableProcessingCheck.Parent := ConfigPage.Surface;
  EnableProcessingCheck.Left := ConfigPage.Edits[IDX_TIMEOUT].Left;
  EnableProcessingCheck.Top := Y;
  EnableProcessingCheck.Width := ConfigPage.SurfaceWidth - ConfigPage.Edits[IDX_TIMEOUT].Left;
  EnableProcessingCheck.Height := ScaleY(17);
  EnableProcessingCheck.Caption := 'Enable processing by default (otherwise artists flip the toggle per clip in the host)';
  EnableProcessingCheck.Checked := False;

  Y := Y + EnableProcessingCheck.Height + ScaleY(8);

  KeepDefaultsCheck := TNewCheckBox.Create(ConfigPage);
  KeepDefaultsCheck.Parent := ConfigPage.Surface;
  KeepDefaultsCheck.Left := ConfigPage.Edits[IDX_TIMEOUT].Left;
  KeepDefaultsCheck.Top := Y;
  KeepDefaultsCheck.Width := ConfigPage.SurfaceWidth - ConfigPage.Edits[IDX_TIMEOUT].Left;
  KeepDefaultsCheck.Height := ScaleY(17);
  KeepDefaultsCheck.Caption := 'Skip - keep each plugin''s bundled defaults (configure later in the host UI)';
  KeepDefaultsCheck.Checked := False;
  KeepDefaultsCheck.OnClick := @KeepDefaultsClick;
end;

{ Validate the site config before leaving the page. }
function NextButtonClick(CurPageID: Integer): Boolean;
var
  Port, Timeout: Integer;
begin
  Result := True;
  if CurPageID <> ConfigPage.ID then
    Exit;
  if KeepDefaultsCheck.Checked then
    Exit;

  if Trim(ConfigPage.Values[IDX_ADDRESS]) = '' then
  begin
    MsgBox('Please enter the ComfyUI server address (or tick "keep bundled defaults").',
      mbError, MB_OK);
    Result := False;
    Exit;
  end;

  Port := StrToIntDef(Trim(ConfigPage.Values[IDX_PORT]), -1);
  if (Port < 1) or (Port > 65535) then
  begin
    MsgBox('The server port must be a number between 1 and 65535.', mbError, MB_OK);
    Result := False;
    Exit;
  end;

  if Trim(ConfigPage.Values[IDX_SERVERMOUNT]) = '' then
  begin
    MsgBox('Please enter the ComfyUI server''s view of the shared folder (UNC path).',
      mbError, MB_OK);
    Result := False;
    Exit;
  end;

  Timeout := StrToIntDef(Trim(ConfigPage.Values[IDX_TIMEOUT]), -1);
  if (Timeout < 1) then
  begin
    MsgBox('The job timeout must be a positive number of seconds.', mbError, MB_OK);
    Result := False;
    Exit;
  end;
end;

{ Write the collected config to a key=value file the patcher reads. Using a
  file (rather than command-line args) sidesteps all quoting issues with UNC
  paths and spaces. }
procedure WriteSiteConfigFile(const Path: String);
var
  S: String;
  Enable: String;
begin
  if EnableProcessingCheck.Checked then Enable := '1' else Enable := '0';
  S :=
    'serverAddress='    + Trim(ConfigPage.Values[IDX_ADDRESS])     + #13#10 +
    'serverPort='       + Trim(ConfigPage.Values[IDX_PORT])        + #13#10 +
    'localMountPath='   + Trim(ConfigPage.Values[IDX_LOCALMOUNT])  + #13#10 +
    'serverMountPath='  + Trim(ConfigPage.Values[IDX_SERVERMOUNT]) + #13#10 +
    'timeout='          + Trim(ConfigPage.Values[IDX_TIMEOUT])     + #13#10 +
    'enableProcessing=' + Enable                                   + #13#10;
  SaveStringToFile(Path, S, False);
end;

procedure CurStepChanged(CurStep: TSetupStep);
var
  ConfigFile, ScriptPath, Params: String;
  ResultCode: Integer;
begin
  if CurStep <> ssPostInstall then
    Exit;
  if KeepDefaultsCheck.Checked then
    Exit;

  ConfigFile := ExpandConstant('{tmp}\aifx-site-config.txt');
  WriteSiteConfigFile(ConfigFile);

  // patch-defaults.ps1 ships with the "dontcopy" flag; pull it into the temp
  // dir so we can run it.
  ExtractTemporaryFile('patch-defaults.ps1');
  ScriptPath := ExpandConstant('{tmp}\patch-defaults.ps1');

  Params := '-NoProfile -ExecutionPolicy Bypass -File "' + ScriptPath + '"' +
            ' -PluginsDir "' + ExpandConstant('{app}') + '"' +
            ' -ConfigFile "' + ConfigFile + '"';

  if not Exec('powershell.exe', Params, '', SW_HIDE, ewWaitUntilTerminated, ResultCode) then
  begin
    MsgBox('Could not launch PowerShell to write the site configuration.' + #13#10 +
           'The plugins are installed but still carry their bundled defaults - ' +
           'edit each defaults.json by hand, or configure values in your host UI.',
           mbError, MB_OK);
  end
  else if ResultCode <> 0 then
  begin
    MsgBox('Writing the site configuration failed (exit code ' + IntToStr(ResultCode) + ').' + #13#10 +
           'The plugins are installed but may still carry their bundled defaults - ' +
           'configure values in your host UI.',
           mbError, MB_OK);
  end;
end;
