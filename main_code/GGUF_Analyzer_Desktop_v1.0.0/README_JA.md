# GGUF Analyzer Desktop v1.0.0（CPU Edition）

Windows用のPython/Tkinter GUIと、CPU専用C++20解析エンジンを組み合わせたデスクトップアプリです。

## 機能

- GGUF v2/v3のヘッダー、メタデータ、テンソル記述子の解析
- テンソル範囲、アラインメント、重複、オーバーラップ、ギャップの検査
- チャンク単位の全ファイル走査とバイト統計（ゼロ/非ゼロ、合計、最小/最大、256-bin histogram、Shannon entropy）
- SHA-256（有効時はファイルを別途ストリーミング読み取り）
- JSONレポート出力
- GUIでサマリー、メタデータ、テンソル、問題一覧を表示
- CUDAをビルド・実行しないCPU専用構成

> このツールは構造と物理バイトを検査します。量子化テンソル値の復号や推論は実装していません。100%走査は全バイトを読んだことを意味し、意味論的な完全正当性を保証するものではありません。

## 必要環境（ビルド時）

- Windows 10/11 x64
- Python 3.10+（PATHに登録）
- CMake 3.20+
- Visual Studio 2022 Build Tools / MSVC C++ x64
- インターネット接続（PyInstallerの初回インストール時のみ）

実行時はPythonをインストールしていないPCでも使えるよう、PyInstallerでGUIをパッケージ化できます。MSVCランタイムが対象PCにない場合、Visual C++ Redistributableが必要になる場合があります。

## 1. ZIPを展開してPowerShellを開く

`GGUF_Analyzer_Desktop_v1.0.0` フォルダに移動します。

```powershell
cd .\GGUF_Analyzer_Desktop_v1.0.0
```

## 2. CPU専用C++エンジンをビルド

```powershell
Set-ExecutionPolicy -Scope Process Bypass
.\build.ps1
```

このCMakeプロジェクトはCXXのみを有効にし、CUDAコンパイラの検出や `.cu` ファイルのビルドを行いません。

ビルド後の実行ファイルは通常、次の場所です。

`engine\build_cpu\Release\gguf_analyzer.exe`

## 3. Python GUIをソースから起動してテスト

```powershell
python .\gui.py
```

GUIでGGUFファイルを選び、解析を開始します。SHA-256を有効にするとファイルをもう一度読み取るため、追加時間がかかります。進行状況バーは処理中表示であり、正確なパーセント進捗ではありません（C++エンジンが途中経過を標準出力へ送らないため）。

## 4. GUIをWindows EXEにビルド

```powershell
.\build.ps1 -BuildGuiExe
```

出力先：

`dist\GGUF Analyzer\GGUF Analyzer.exe`

`--onedir` 方式で、エンジン実行ファイルをアプリの同梱データとして配置します。配布時は `dist\GGUF Analyzer` フォルダ全体を渡してください。EXEだけを単独で移動しないでください。

## 5. スモークテスト（合成GGUF）

ビルド後、以下を実行します。

```powershell
$engine = Join-Path $PWD 'engine\build_cpu\Release\gguf_analyzer.exe'
if (-not (Test-Path $engine)) { $engine = Join-Path $PWD 'engine\build_cpu\gguf_analyzer.exe' }
$out = Join-Path $env:TEMP 'gguf_minimal_cpu_report.json'
& $engine (Join-Path $PWD 'engine\tests\minimal.gguf') --out $out --chunk-mib 1 --backend cpu
$rc = $LASTEXITCODE
Write-Host "Exit code: $rc"
Write-Host "JSON exists: $(Test-Path $out)"
if ($rc -ne 0 -or -not (Test-Path $out)) { throw 'Smoke test failed.' }
Get-Content $out -Raw | ConvertFrom-Json | Select-Object structure_status, file_size, sha256
```

さらに、CPU専用であることを確認します。CUDA指定は拒否される必要があります。

```powershell
& $engine (Join-Path $PWD 'engine\tests\minimal.gguf') --backend cuda
Write-Host "CUDA rejection exit code: $LASTEXITCODE"
```

## 6. 実際のモデルを解析

GUIを起動してファイルを選択するか、エンジンを直接呼び出します。

```powershell
$gguf = 'D:\models\model.gguf'
$out = Join-Path $env:TEMP 'model_report.json'
& $engine $gguf --out $out --chunk-mib 64 --backend cpu
```

## 既知の制限

- `tensor_value_decode_status` は `not_implemented` のままです。
- メタデータ値はJSONネイティブ型ではなく表示用文字列です。
- エンジンは途中経過を出力しないため、GUIは正確な進捗率を表示しません。
- C++のSHA-256実装のローカル1 MiBバッファはヒープへ移動し、Windowsのスタック使用量を抑えています。環境でのビルド・テストは実行して検証してください。
