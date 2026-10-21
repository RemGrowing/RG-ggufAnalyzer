# GGUF Analyzer Desktop v1.4.0（CPU Edition / Single EXE）

Windows用のPython/Tkinter GUIと、CPU専用C++20解析エンジンを組み合わせたデスクトップアプリです。

## 機能

- GGUF v2/v3のヘッダー、メタデータ、テンソル記述子の解析
- テンソル範囲、アラインメント、重複、オーバーラップ、ギャップの検査
- チャンク単位の全ファイル走査とバイト統計（ゼロ/非ゼロ、合計、最小/最大、256-bin histogram、Shannon entropy）
- SHA-256（有効時はファイルを別途ストリーミング読み取り）
- JSONレポート出力
- GUIでサマリー、メタデータ、テンソル、問題一覧を表示
- CUDAをビルド・実行しないCPU専用構成
- 量子化テンソルの値復号：Q4_0 / Q4_1 / Q5_0 / Q5_1 / Q8_0 / Q3_K / Q4_K / Q5_K / Q6_K
- F32 / F16 / BF16 / I8 / I16 / I32 / I64 / F64 の値復号
- `general.file_type` に基づく Q4_K_M などの量子化プロファイル認識
- 実際のテンソル形式別の本数をサマリーに表示

> 量子化値の復号は、GUIのテンソル一覧からテンソルを選択して実行します。復号出力は選択した先頭要素（最大1,000,000要素）です。これはテンソル値の可視化であり、モデル推論機能ではありません。100%走査は全バイトを読んだことを意味し、意味論的な完全正当性を保証するものではありません。

## 必要環境（ビルド時）

- Windows 10/11 x64
- Python 3.10+（PATHに登録）
- CMake 3.20+
- Visual Studio 2022 Build Tools / MSVC C++ x64
- インターネット接続（PyInstallerの初回インストール時のみ）

実行時はPythonをインストールしていないPCでも使えるよう、PyInstallerでGUIをパッケージ化できます。C++エンジンはMSVCランタイムを静的リンクする設定です。

## 1. ZIPを展開してPowerShellを開く

`GGUF_Analyzer_Desktop_v1.4.0` フォルダに移動します。

```powershell
cd .\GGUF_Analyzer_Desktop_v1.4.0
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

## 4. GUIを単体Windows EXEにビルド

```powershell
.\build.ps1 -BuildGuiExe
```

出力先：

`dist\GGUF Analyzer.exe`

`--onefile` 方式です。GUI、Pythonランタイム、C++エンジンを1つのEXEに梱包するため、配布時に `_internal` フォルダや別のエンジンEXEは不要です。配布対象は `GGUF Analyzer.exe` 単体です。起動時、PyInstallerは同梱物をユーザーの一時ディレクトリへ展開してエンジンを実行し、終了後に一時展開物を片付けます。これはEXEの隣に `_internal` フォルダを作らないという意味であり、実行中の一時展開までゼロにする方式ではありません。

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

- テンソル値デコードは一部形式に対応しています。未対応の量子化形式は明示的にエラーになります。
- メタデータ値はJSONネイティブ型ではなく表示用文字列です。
- エンジンは途中経過を出力しないため、GUIは正確な進捗率を表示しません。
- C++のSHA-256実装のローカル1 MiBバッファはヒープへ移動し、Windowsのスタック使用量を抑えています。環境でのビルド・テストは実行して検証してください。


## v1.1.0: テンソル値デコード

テンソル一覧で行を選択し、先頭から最大1,000,000要素をCPUで復号できます。現在のデコーダ対応型は F32、F16、BF16、I8/I16/I32/I64、F64、Q4_0、Q4_1、Q5_0、Q5_1、Q8_0、Q3_K、Q4_K、Q5_K、Q6_K です。Q2_K、Q8_1/Q8_K、IQ系などは未対応で、エラーとして明示します。量子化型の復号値は量子化後に保持された情報からの近似値です。巨大テンソルの全要素を一度に展開せず、必要な先頭要素だけを読み取ります。復号結果はプロジェクト作業ディレクトリ内の `decoded_<model>_values.json` に保存します。

## v1.2.0: Q3_K / Q4_K と単体EXE

- `Q3_K`（GGML type ID 11、256要素/110 bytes）と `Q4_K`（type ID 12、256要素/144 bytes）のブロック復号を追加。
- GUIの対応型表示を更新。
- PyInstallerを `--onefile` に変更し、C++エンジンをEXE内へ同梱。
- MSVCビルドではC++ランタイムを静的リンク。
- 合成ブロックの回帰テストを `engine\tests\test_quant_decode.py` に追加。

Windowsでの回帰テスト実行例：

```powershell
$engine = '.\engine\build_cpu\Release\gguf_analyzer.exe'
python .\engine\tests\test_quant_decode.py $engine
```

Q3_K/Q4_K/Q5_K/Q6_Kのテストは合成データを用いた復号ロジックのテストです。実際の大型GGUFを用いたWindows環境での全テンソル検証は、利用者側の環境で別途実施してください。


## v1.3.0: Q5_K / Q6_K 追加

- `Q5_K`（GGML type ID 13、256要素/176 bytes）を追加。
- `Q6_K`（GGML type ID 14、256要素/210 bytes）を追加。
- GUIの対応型表示を更新。
- 合成データによるQ3_K/Q4_K/Q5_K/Q6_Kの回帰テストを拡張。
- PyInstaller `--onefile` による単体EXEビルド方式は維持。

テスト実行例：

```powershell
$engine = '.\engine\build_cpu\Release\gguf_analyzer.exe'
python .\engine\tests\test_quant_decode.py $engine
```

このテストは合成ブロックを使った復号ロジックの確認です。Windowsでの実ビルドおよび実モデルの全テンソルとの照合は、利用環境で別途確認してください。


## v1.4.0: Q4_K_M プロファイル認識

- GGUF の `general.file_type` をファイルタイプ列挙値として解釈し、ID 15 を `Q4_K_M` と表示します。
- 概要タブに宣言された量子化プロファイルと、実テンソルの形式別本数を表示します。
- Q4_K_M は単独のテンソル形式ではなく混合量子化プロファイルです。実際の復号可否は個々のテンソル型に依存します。
- v1.3.0 の Q3_K/Q4_K/Q5_K/Q6_K の値復号対応を維持します。Q2_K、IQ系などの未実装形式を新たに復号可能にしたものではありません。
