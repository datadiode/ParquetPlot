; Compile with ISCC /DAppVersion=<version> /DAppSourceDir=<bundle> /O<output> ParquetPlot.iss
#ifndef AppVersion
  #error AppVersion must be supplied to the compiler
#endif
#ifndef AppSourceDir
  #error AppSourceDir must point to the bundled application directory
#endif

[Setup]
; Keep this ID stable so future installers upgrade the existing installation.
AppId={{FEFE138C-03CA-4777-94C7-06CD60A57628}
AppName=ParquetPlot
AppVersion={#AppVersion}
DefaultDirName={autopf}\ParquetPlot
PrivilegesRequired=admin
ArchitecturesInstallIn64BitMode=x64
MinVersion=10.0
DisableProgramGroupPage=yes
UninstallDisplayName=ParquetPlot
UninstallDisplayIcon={app}\ParquetPlot.exe
LicenseFile={#AppSourceDir}\LICENSE
OutputBaseFilename=ParquetPlot-Windows-x64-Setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
ChangesAssociations=yes

[Tasks]
Name: "desktopicon"; Description: "Create a &desktop shortcut"; GroupDescription: "Shortcuts:"; Flags: unchecked

[Files]
Source: "{#AppSourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{commonprograms}\ParquetPlot"; Filename: "{app}\ParquetPlot.exe"; WorkingDir: "{app}"
Name: "{commondesktop}\ParquetPlot"; Filename: "{app}\ParquetPlot.exe"; WorkingDir: "{app}"; Tasks: desktopicon

[Registry]
; Register as an Open With option without taking over the user's default app.
Root: HKLM; Subkey: "Software\Classes\.parquet\OpenWithProgids"; ValueType: string; ValueName: "ParquetPlot.Parquet"; ValueData: ""; Flags: uninsdeletevalue uninsdeletekeyifempty
Root: HKLM; Subkey: "Software\Classes\ParquetPlot.Parquet"; ValueType: string; ValueData: "Parquet data file"; Flags: uninsdeletekey
Root: HKLM; Subkey: "Software\Classes\ParquetPlot.Parquet\DefaultIcon"; ValueType: string; ValueData: """{app}\ParquetPlot.exe"",0"
Root: HKLM; Subkey: "Software\Classes\ParquetPlot.Parquet\shell\open\command"; ValueType: string; ValueData: """{app}\ParquetPlot.exe"" ""%1"""
Root: HKLM; Subkey: "Software\Microsoft\Windows\CurrentVersion\App Paths\ParquetPlot.exe"; ValueType: string; ValueData: "{app}\ParquetPlot.exe"; Flags: uninsdeletekey

[Run]
Filename: "{app}\ParquetPlot.exe"; Description: "Launch ParquetPlot"; Flags: nowait postinstall skipifsilent runasoriginaluser

[Code]
{ ---- VCRedist processing code ----- }
// Marginally modified from https://www.stefanobordoni.cloud/howto-include-visual-c-rtl-in-a-inno-setup-kit/
// minimal RTL version required (default: 14.23.27820.0)
// minimal VC2019 RTL version: 14.29.30133.0
// minimal VC2022 RTL version: 14.32.31326.0
const
  VCRTL_MIN_V1 = 14;
const
  VCRTL_MIN_V2 = 32;
const
  VCRTL_MIN_V3 = 31326;
const
  VCRTL_MIN_V4 = 0;

// check if the needed RTL 32/64 bits is installed (by looking the registry)
function RTL_IsNeeded(bUse64BitsRTL: Boolean): Boolean;
var
  sRegKey: string;
  v1: Cardinal;
  v2: Cardinal;
  v3: Cardinal;
  v4: Cardinal;
begin
  // is it running on 64 bits OS?
  if IsWin64 then begin
    sRegKey := 'SOFTWARE\WOW6432Node\Microsoft\VisualStudio\14.0\VC\Runtimes\';
    // need 64 bits RTL?
    if bUse64BitsRTL then
      sRegKey := sRegKey + 'x64'
    else
      sRegKey := sRegKey + 'x86'
  end
  // else we have only 32 bits RTL on 32 bits OS...
  else if not bUse64BitsRTL then
    sRegKey := 'SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\X86';
  // if we have a key, let's check it
  if ((sRegKey <> '')
      and RegQueryDWordValue(HKEY_LOCAL_MACHINE, sRegKey, 'Major', v1)
      and RegQueryDWordValue(HKEY_LOCAL_MACHINE, sRegKey, 'Minor', v2)
      and RegQueryDWordValue(HKEY_LOCAL_MACHINE, sRegKey, 'Bld', v3)
      and RegQueryDWordValue(HKEY_LOCAL_MACHINE, sRegKey, 'RBld', v4)) then
  begin
    Log('VC 2015-2022 Redist version: ' + IntToStr(v1) + '.' + IntToStr(v2) + '.' + IntToStr(v3) + '.' + IntToStr(v4));
    { Version info was found. Return true if later or equal to our
       minimal required version RTL_MIN_Vx }
    Result :=
        not ((v1 > VCRTL_MIN_V1)
            or ((v1 = VCRTL_MIN_V1)
                and ((v2 > VCRTL_MIN_V2)
                    or ((v2 = VCRTL_MIN_V2)
                        and ((v3 > VCRTL_MIN_V3) or ((v3 = VCRTL_MIN_V3) and (v4 >= VCRTL_MIN_V4)))))));
  end
  else
    Result := TRUE;
end;

// onDownload event handler
function OnDownloadProgress(const Url, FileName: string; const Progress, ProgressMax: Int64): Boolean;
begin
  if Progress = ProgressMax then
    Log(Format('Successfully downloaded file to {tmp}: %s', [FileName]));
  Result := TRUE;
end;

// download and install the requested RTL 32/64 bits
function RTL_DownloadAndInstall(bUse64BitsRTL: Boolean; var NeedsRestart: Boolean): string;
var
  ret_code: Integer;
  sFile: string;
  sSetupFile: string;
  DownloadPage: TDownloadWizardPage;
begin
  DownloadPage :=
      CreateDownloadPage(
          SetupMessage(msgWizardPreparing),
          'Needed Visual Studio C++ Runtime files...',
          @OnDownloadProgress
      );
  DownloadPage.Clear;
  if bUse64BitsRTL then
    sSetupFile := 'VC_redist.x64.exe'
  else
    sSetupfile := 'VC_redist.x86.exe';
  DownloadPage.Add('https://aka.ms/vs/17/release/' + sSetupFile, sSetupFile, '');
  DownloadPage.Show;
  try
    try
      DownloadPage.Download;
      { run the downloaded setup files }
      sFile := ExpandConstant('{tmp}\' + sSetupFile);
      Log(Format('- starting: %s', [sFile]));
      DownloadPage.SetText('installing package...', sSetupFile);
      if not Exec(sFile, '/install /passive /norestart', '', SW_SHOW, ewWaitUntilTerminated, ret_code)
          or ((ret_code <> 0) and (ret_code <> 1638) and (ret_code <> 3010)) then
        Result := Format('VC++ runtime installation failed (code %d).', [ret_code])
      else if ret_code = 3010 then
        NeedsRestart := True;
      Log(Format('- done : %u', [ret_code]));
    except
      Result := AddPeriod(GetExceptionMessage);
    end;
  finally
    DownloadPage.Hide;
  end;
end;

function PrepareToInstall(var NeedsRestart: Boolean): string;
begin
  if RTL_IsNeeded(Is64BitInstallMode) then
    Result := RTL_DownloadAndInstall(Is64BitInstallMode, NeedsRestart);
end;
