; =============================================================================
; TriLayerReharm - Inno Setup Installer Script (Phase 11)
; Publisher : Universal Make Associates (U.M.A)
; =============================================================================

#define MyAppName      "TriLayerReharm"
#define MyAppVersion   "1.0.0"
#define MyAppPublisher "Universal Make Associates"
#define MyAppCopyright "© 2026 Universal Make Associates"

; CMake の Release ビルド出力先（.iss ファイルをプロジェクトルートに置いた場合の相対パス）
#define Vst3BundleSourceDir "build\TriLayerReharm_artefacts\Release\VST3\TriLayerReharm.vst3"

[Setup]
; アプリケーション識別用の一意なGUID（アップデート時の上書き判定に使用）
AppId={{A74D9E12-63F8-4B29-9D81-5C4E0F12A9B3}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppVerName={#MyAppName} v{#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppCopyright={#MyAppCopyright}

; 64bit環境専用プラグインとして動作・インストール
ArchitecturesAllowed=x64
ArchitecturesInstallIn64BitMode=x64

; Windows VST3 標準ディレクトリ配下のベンダーフォルダへ固定インストール
; {commoncf64} は "C:\Program Files\Common Files" に展開されます
DefaultDirName={commoncf64}\VST3\{#MyAppPublisher}

; ユーザーにインストール先フォルダやスタートメニュー作成を選ばせない（VST3標準仕様）
DisableDirPage=yes
DisableProgramGroupPage=yes

; EULA（使用許諾契約書）を表示する場合は以下のコメントアウトを外して LICENSE.txt を配置してください
; LicenseFile=LICENSE.txt

; 出力インストーラーの設定と高圧縮指定
OutputDir=InstallerOutput
OutputBaseFilename=TriLayerReharm_Installer_v1.0.0
Compression=lzma2/ultra64
SolidCompression=yes

; Program Files 配下へ書き込むため管理者権限を要求
PrivilegesRequired=admin
WizardStyle=modern
UninstallDisplayName={#MyAppName} (VST3 Plugin)

[Languages]
Name: "japanese"; MessagesFile: "compiler:Languages\Japanese.isl"
Name: "english";  MessagesFile: "compiler:Default.isl"

[Files]
; VST3バンドル（TriLayerReharm.vst3 フォルダ構造一式）をそのまま維持してコピー
Source: "{#Vst3BundleSourceDir}\*"; DestDir: "{app}\TriLayerReharm.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs

[UninstallDelete]
; アンインストール時にバンドルフォルダおよび空になったベンダーフォルダをクリーンアップ
Type: filesandordirs; Name: "{app}\TriLayerReharm.vst3"
Type: dirifempty;     Name: "{app}"
