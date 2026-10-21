from __future__ import annotations

import json
import os
import queue
import subprocess
import sys
import threading
import time
import tkinter as tk
from pathlib import Path
from tkinter import filedialog, messagebox, ttk

APP_NAME = "GGUF Analyzer"
APP_VERSION = "1.2.0"
BG = "#0b1020"
PANEL = "#121a2d"
PANEL2 = "#18233b"
BORDER = "#26344f"
TEXT = "#e8eefc"
MUTED = "#9aa9c5"
ACCENT = "#58d6c7"
ACCENT_DARK = "#143f45"
BLUE = "#7aa8ff"
RED = "#ff7785"
GREEN = "#65d6a5"


def resource_root() -> Path:
    if getattr(sys, "frozen", False) and hasattr(sys, "_MEIPASS"):
        return Path(sys._MEIPASS)
    return Path(__file__).resolve().parent


def format_bytes(value) -> str:
    try:
        n = float(value)
    except (TypeError, ValueError):
        return "—"
    units = ("B", "KiB", "MiB", "GiB", "TiB")
    for unit in units:
        if abs(n) < 1024 or unit == units[-1]:
            return f"{n:,.2f} {unit}" if unit != "B" else f"{n:,.0f} B"
        n /= 1024
    return f"{n:,.2f} TiB"


class AnalyzerApp(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title(f"{APP_NAME}  ·  CPU Edition")
        self.geometry("1180x790")
        self.minsize(980, 680)
        self.configure(bg=BG)

        self.events: queue.Queue = queue.Queue()
        self.proc = None
        self.worker = None
        self.last_report_path: Path | None = None
        self.report_data = None
        self.input_var = tk.StringVar()
        self.output_var = tk.StringVar()
        self.chunk_var = tk.StringVar(value="64")
        self.hash_var = tk.BooleanVar(value=True)
        self.status_var = tk.StringVar(value="待機中")
        self.file_info_var = tk.StringVar(value="GGUFファイルを選択してください")
        self.backend_var = tk.StringVar(value="CPU ONLY")
        self._configure_styles()
        self._build_ui()
        self.after(120, self._drain_events)

    def _configure_styles(self):
        style = ttk.Style(self)
        try:
            style.theme_use("clam")
        except tk.TclError:
            pass
        style.configure(".", font=("Segoe UI", 10))
        style.configure("TFrame", background=BG)
        style.configure("Panel.TFrame", background=PANEL)
        style.configure("Panel2.TFrame", background=PANEL2)
        style.configure("TLabel", background=BG, foreground=TEXT)
        style.configure("Muted.TLabel", background=BG, foreground=MUTED)
        style.configure("Panel.TLabel", background=PANEL, foreground=TEXT)
        style.configure("PanelMuted.TLabel", background=PANEL, foreground=MUTED)
        style.configure("Brand.TLabel", background=BG, foreground=TEXT, font=("Segoe UI Semibold", 19))
        style.configure("Section.TLabel", background=PANEL, foreground=TEXT, font=("Segoe UI Semibold", 11))
        style.configure("Metric.TLabel", background=PANEL, foreground=TEXT, font=("Segoe UI Semibold", 19))
        style.configure("MetricName.TLabel", background=PANEL, foreground=MUTED, font=("Segoe UI", 9))
        style.configure("TButton", background=PANEL2, foreground=TEXT, borderwidth=0, padding=(12, 9))
        style.map("TButton", background=[("active", "#263858"), ("disabled", "#171f31")],
                  foreground=[("disabled", "#66738c")])
        style.configure("Accent.TButton", background=ACCENT, foreground="#06181b",
                         font=("Segoe UI Semibold", 10), padding=(16, 11))
        style.map("Accent.TButton", background=[("active", "#7ce7da"), ("disabled", "#35565b")])
        style.configure("Danger.TButton", background="#47232f", foreground="#ffdfe4", padding=(10, 8))
        style.map("Danger.TButton", background=[("active", "#653040")])
        style.configure("TEntry", fieldbackground="#0e1628", foreground=TEXT,
                        insertcolor=TEXT, bordercolor=BORDER, padding=8)
        style.configure("TCombobox", fieldbackground="#0e1628", background=PANEL2,
                        foreground=TEXT, arrowcolor=TEXT, padding=7)
        style.map("TCombobox", fieldbackground=[("readonly", "#0e1628")],
                  foreground=[("readonly", TEXT)])
        style.configure("TCheckbutton", background=PANEL, foreground=TEXT)
        style.map("TCheckbutton", background=[("active", PANEL)])
        style.configure("TNotebook", background=BG, borderwidth=0)
        style.configure("TNotebook.Tab", background=PANEL2, foreground=MUTED, padding=(15, 9))
        style.map("TNotebook.Tab", background=[("selected", PANEL)], foreground=[("selected", ACCENT)])
        style.configure("Treeview", background="#0e1628", fieldbackground="#0e1628",
                        foreground=TEXT, rowheight=27, borderwidth=0)
        style.configure("Treeview.Heading", background=PANEL2, foreground=MUTED,
                        relief="flat", font=("Segoe UI Semibold", 9))
        style.map("Treeview", background=[("selected", "#24424c")], foreground=[("selected", TEXT)])
        style.configure("Horizontal.TProgressbar", troughcolor="#0e1628",
                        background=ACCENT, bordercolor="#0e1628", lightcolor=ACCENT, darkcolor=ACCENT)

    def _build_ui(self):
        outer = ttk.Frame(self, padding=(22, 18, 22, 16))
        outer.pack(fill="both", expand=True)

        header = ttk.Frame(outer)
        header.pack(fill="x", pady=(0, 16))
        left = ttk.Frame(header)
        left.pack(side="left", fill="x", expand=True)
        ttk.Label(left, text="GGUF ANALYZER", style="Brand.TLabel").pack(anchor="w")
        ttk.Label(left, text="ローカル解析ワークベンチ  /  C++ Engine + Python GUI",
                  style="Muted.TLabel").pack(anchor="w", pady=(3, 0))
        right = ttk.Frame(header)
        right.pack(side="right")
        badge = tk.Label(right, text="  CPU ONLY  ", bg=ACCENT_DARK, fg=ACCENT,
                         font=("Segoe UI Semibold", 10), padx=8, pady=7)
        badge.pack(side="right", padx=(12, 0))
        tk.Label(right, text=f"DESKTOP  v{APP_VERSION}", bg=BG, fg=MUTED,
                 font=("Segoe UI", 9)).pack(side="right", pady=8)

        # File selection panel
        file_panel = ttk.Frame(outer, style="Panel.TFrame", padding=16)
        file_panel.pack(fill="x", pady=(0, 13))
        ttk.Label(file_panel, text="解析対象", style="Section.TLabel").grid(row=0, column=0, sticky="w", columnspan=3)
        ttk.Label(file_panel, text="入力 GGUF", style="PanelMuted.TLabel").grid(row=1, column=0, sticky="w", pady=(13, 5))
        self.input_entry = ttk.Entry(file_panel, textvariable=self.input_var)
        self.input_entry.grid(row=2, column=0, sticky="ew", padx=(0, 9))
        ttk.Button(file_panel, text="ファイルを選択", command=self._browse_input).grid(row=2, column=1, sticky="ew")
        ttk.Label(file_panel, text="出力 JSON", style="PanelMuted.TLabel").grid(row=3, column=0, sticky="w", pady=(11, 5))
        ttk.Entry(file_panel, textvariable=self.output_var).grid(row=4, column=0, sticky="ew", padx=(0, 9))
        ttk.Button(file_panel, text="保存先", command=self._browse_output).grid(row=4, column=1, sticky="ew")
        ttk.Label(file_panel, textvariable=self.file_info_var, style="PanelMuted.TLabel").grid(
            row=5, column=0, columnspan=2, sticky="w", pady=(10, 0))
        file_panel.columnconfigure(0, weight=1)

        # Settings + actions
        controls = ttk.Frame(outer)
        controls.pack(fill="x", pady=(0, 13))
        settings = ttk.Frame(controls, style="Panel.TFrame", padding=(16, 12))
        settings.pack(side="left", fill="x", expand=True, padx=(0, 12))
        ttk.Label(settings, text="解析設定", style="Section.TLabel").grid(row=0, column=0, sticky="w", columnspan=4)
        ttk.Label(settings, text="チャンクサイズ", style="PanelMuted.TLabel").grid(row=1, column=0, sticky="w", pady=(10, 4))
        chunk = ttk.Combobox(settings, textvariable=self.chunk_var, values=("8", "16", "32", "64", "128", "256"),
                             width=8, state="readonly")
        chunk.grid(row=2, column=0, sticky="w")
        ttk.Label(settings, text="MiB", style="PanelMuted.TLabel").grid(row=2, column=1, sticky="w", padx=(7, 18))
        ttk.Checkbutton(settings, text="SHA-256を計算する（追加のファイル読み取り）",
                        variable=self.hash_var).grid(row=2, column=2, sticky="w")
        ttk.Label(settings, text="CPU専用・最大256 MiB/チャンク", style="PanelMuted.TLabel").grid(
            row=3, column=0, columnspan=4, sticky="w", pady=(9, 0))
        actions = ttk.Frame(controls, style="Panel.TFrame", padding=12)
        actions.pack(side="right", fill="y")
        self.run_button = ttk.Button(actions, text="▶  解析を開始", style="Accent.TButton", command=self._start)
        self.run_button.pack(fill="x")
        self.cancel_button = ttk.Button(actions, text="■  キャンセル", style="Danger.TButton",
                                        command=self._cancel, state="disabled")
        self.cancel_button.pack(fill="x", pady=(8, 0))

        # Status bar / indeterminate progress, not fake percentage.
        status_frame = ttk.Frame(outer, style="Panel2.TFrame", padding=(13, 9))
        status_frame.pack(fill="x", pady=(0, 13))
        self.status_dot = tk.Label(status_frame, text="●", bg=PANEL2, fg=ACCENT, font=("Segoe UI", 10))
        self.status_dot.pack(side="left", padx=(0, 8))
        ttk.Label(status_frame, textvariable=self.status_var, background=PANEL2, foreground=TEXT).pack(side="left")
        self.progress = ttk.Progressbar(status_frame, mode="indeterminate", length=180)
        self.progress.pack(side="right", padx=(12, 0))

        # Metrics cards
        self.metric_vars = {}
        metrics = ttk.Frame(outer)
        metrics.pack(fill="x", pady=(0, 13))
        cards = [
            ("size", "ファイルサイズ", "—"),
            ("coverage", "物理走査率", "—"),
            ("speed", "走査速度", "—"),
            ("entropy", "バイトエントロピー", "—"),
            ("tensors", "テンソル", "—"),
            ("metadata", "メタデータ", "—"),
        ]
        for i, (key, label, value) in enumerate(cards):
            card = ttk.Frame(metrics, style="Panel.TFrame", padding=(13, 12))
            card.grid(row=0, column=i, sticky="nsew", padx=(0 if i == 0 else 6, 0 if i == 5 else 6))
            ttk.Label(card, text=label, style="MetricName.TLabel").pack(anchor="w")
            var = tk.StringVar(value=value)
            self.metric_vars[key] = var
            ttk.Label(card, textvariable=var, style="Metric.TLabel").pack(anchor="w", pady=(7, 0))
            metrics.columnconfigure(i, weight=1, uniform="metric")

        notebook = ttk.Notebook(outer)
        notebook.pack(fill="both", expand=True)
        self.overview_tab = ttk.Frame(notebook, style="Panel.TFrame", padding=14)
        self.metadata_tab = ttk.Frame(notebook, style="Panel.TFrame", padding=10)
        self.tensors_tab = ttk.Frame(notebook, style="Panel.TFrame", padding=10)
        self.issues_tab = ttk.Frame(notebook, style="Panel.TFrame", padding=10)
        self.log_tab = ttk.Frame(notebook, style="Panel.TFrame", padding=10)
        notebook.add(self.overview_tab, text="概要")
        notebook.add(self.metadata_tab, text="メタデータ")
        notebook.add(self.tensors_tab, text="テンソル")
        notebook.add(self.issues_tab, text="問題一覧")
        notebook.add(self.log_tab, text="実行ログ")
        self.notebook = notebook
        self._build_overview()
        self.metadata_tree = self._tree(self.metadata_tab, ("key", "type", "value"),
                                        ("キー", "型", "値"), (280, 100, 600))
        decode_bar = ttk.Frame(self.tensors_tab, style="Panel.TFrame")
        decode_bar.pack(fill="x", pady=(0, 8))
        ttk.Label(decode_bar, text="先頭から復号する要素数", style="PanelMuted.TLabel").pack(side="left", padx=(0, 8))
        self.decode_count_var = tk.StringVar(value="256")
        ttk.Entry(decode_bar, textvariable=self.decode_count_var, width=10).pack(side="left")
        ttk.Button(decode_bar, text="選択テンソルを復号", style="Accent.TButton", command=self._decode_selected).pack(side="left", padx=10)
        ttk.Label(decode_bar, text="対応型: F32/F16/BF16・整数・Q4_0/Q4_1/Q5_0/Q5_1/Q8_0/Q3_K/Q4_K/Q5_K/Q6_K", style="PanelMuted.TLabel").pack(side="left", padx=8)
        self.tensor_tree = self._tree(self.tensors_tab, ("name", "type", "dims", "elements", "offset"),
                                      ("テンソル名", "型", "次元", "要素数", "絶対オフセット"),
                                      (360, 100, 150, 130, 170))
        self.issue_tree = self._tree(self.issues_tab, ("severity", "code", "message", "offset"),
                                     ("重要度", "コード", "説明", "オフセット"), (90, 180, 570, 130))
        self.log_text = tk.Text(self.log_tab, bg="#0e1628", fg=TEXT, insertbackground=TEXT,
                                relief="flat", wrap="word", font=("Cascadia Mono", 9),
                                padx=12, pady=12)
        self.log_text.pack(fill="both", expand=True)
        self.log_text.insert("end", "GGUF Analyzer Desktop ready.\nCPU-only engine will be used.\n")
        self.log_text.configure(state="disabled")

        footer = ttk.Frame(outer)
        footer.pack(fill="x", pady=(10, 0))
        ttk.Label(footer, text="ローカル処理のみ  ·  モデルファイルは外部へ送信しません",
                  style="Muted.TLabel").pack(side="left")
        self.open_report_button = ttk.Button(footer, text="JSONレポートを開く",
                                             command=self._open_report, state="disabled")
        self.open_report_button.pack(side="right")

    def _build_overview(self):
        frame = self.overview_tab
        ttk.Label(frame, text="解析サマリー", style="Section.TLabel").pack(anchor="w")
        self.summary_text = tk.Text(frame, height=8, bg=PANEL, fg=TEXT, insertbackground=TEXT,
                                    relief="flat", wrap="word", font=("Segoe UI", 10), padx=2, pady=10)
        self.summary_text.pack(fill="x")
        self.summary_text.insert("end", "解析を実行すると、GGUFヘッダー、構造検証、全ファイル走査、統計情報がここに表示されます。")
        self.summary_text.configure(state="disabled")
        ttk.Label(frame, text="SHA-256", style="Section.TLabel").pack(anchor="w", pady=(12, 5))
        self.sha_text = tk.Text(frame, height=2, bg="#0e1628", fg=ACCENT, insertbackground=TEXT,
                                relief="flat", wrap="word", font=("Cascadia Mono", 9), padx=10, pady=8)
        self.sha_text.pack(fill="x")
        self.sha_text.insert("end", "未計算")
        self.sha_text.configure(state="disabled")

    def _tree(self, parent, columns, headings, widths):
        wrapper = ttk.Frame(parent, style="Panel.TFrame")
        wrapper.pack(fill="both", expand=True)
        tree = ttk.Treeview(wrapper, columns=columns, show="headings")
        yscroll = ttk.Scrollbar(wrapper, orient="vertical", command=tree.yview)
        xscroll = ttk.Scrollbar(wrapper, orient="horizontal", command=tree.xview)
        tree.configure(yscrollcommand=yscroll.set, xscrollcommand=xscroll.set)
        for col, heading, width in zip(columns, headings, widths):
            tree.heading(col, text=heading)
            tree.column(col, width=width, minwidth=70, anchor="w", stretch=True)
        tree.grid(row=0, column=0, sticky="nsew")
        yscroll.grid(row=0, column=1, sticky="ns")
        xscroll.grid(row=1, column=0, sticky="ew")
        wrapper.rowconfigure(0, weight=1)
        wrapper.columnconfigure(0, weight=1)
        return tree

    def _browse_input(self):
        path = filedialog.askopenfilename(title="解析するGGUFファイルを選択",
                                          filetypes=[("GGUF files", "*.gguf"), ("All files", "*.*")])
        if path:
            self.input_var.set(path)
            p = Path(path)
            size = p.stat().st_size if p.exists() else 0
            self.file_info_var.set(f"{p.name}  ·  {format_bytes(size)}")
            if not self.output_var.get().strip():
                self.output_var.set(str(p.with_suffix(".report.json")))

    def _browse_output(self):
        path = filedialog.asksaveasfilename(title="JSONレポートの保存先",
                                            defaultextension=".json",
                                            filetypes=[("JSON report", "*.json"), ("All files", "*.*")])
        if path:
            self.output_var.set(path)

    def _append_log(self, text):
        self.log_text.configure(state="normal")
        self.log_text.insert("end", text.rstrip() + "\n")
        self.log_text.see("end")
        self.log_text.configure(state="disabled")

    def _set_text(self, widget, text):
        widget.configure(state="normal")
        widget.delete("1.0", "end")
        widget.insert("end", text)
        widget.configure(state="disabled")

    def _start(self):
        if self.worker and self.worker.is_alive():
            return
        input_path = Path(self.input_var.get().strip().strip('"'))
        if not input_path.is_file():
            messagebox.showerror("入力エラー", "有効なGGUFファイルを選択してください。")
            return
        try:
            chunk = int(self.chunk_var.get())
            if chunk < 1 or chunk > 256:
                raise ValueError
        except ValueError:
            messagebox.showerror("設定エラー", "チャンクサイズは1〜256 MiBで指定してください。")
            return
        out_text = self.output_var.get().strip()
        if not out_text:
            out_text = str(input_path.with_suffix(".report.json"))
            self.output_var.set(out_text)
        output_path = Path(out_text).expanduser()
        try:
            output_path.parent.mkdir(parents=True, exist_ok=True)
        except OSError as exc:
            messagebox.showerror("出力先エラー", str(exc))
            return
        exe = resource_root() / "engine" / "build_cpu" / "Release" / "gguf_analyzer.exe"
        if not exe.is_file():
            messagebox.showerror("エンジンがありません",
                "C++バックエンドが見つかりません。\n\n先に build.ps1 を実行してエンジンをビルドしてください。")
            return
        args = [str(exe), str(input_path), "--out", str(output_path),
                "--chunk-mib", str(chunk), "--backend", "cpu"]
        if not self.hash_var.get():
            args.append("--no-hash")

        self.last_report_path = output_path
        self.report_data = None
        self.open_report_button.configure(state="disabled")
        self.run_button.configure(state="disabled")
        self.cancel_button.configure(state="normal")
        self.progress.start(12)
        self.status_var.set("解析中 — 実際の進捗率ではなく、処理中を示しています")
        self.status_dot.configure(fg=BLUE)
        self._append_log("\n" + "=" * 72)
        self._append_log("開始: " + time.strftime("%Y-%m-%d %H:%M:%S"))
        self._append_log("入力: " + str(input_path))
        self._append_log("出力: " + str(output_path))
        self._append_log(f"設定: CPU / {chunk} MiB chunks / SHA-256={'ON' if self.hash_var.get() else 'OFF'}")
        self.worker = threading.Thread(target=self._run_worker, args=(args, output_path), daemon=True)
        self.worker.start()

    def _run_worker(self, args, output_path):
        try:
            creationflags = getattr(subprocess, "CREATE_NO_WINDOW", 0)
            self.proc = subprocess.Popen(args, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                         text=True, encoding="utf-8", errors="replace",
                                         creationflags=creationflags)
            output, _ = self.proc.communicate()
            self.events.put(("finished", self.proc.returncode, output, str(output_path)))
        except Exception as exc:
            self.events.put(("failed", str(exc)))

    def _cancel(self):
        if self.proc and self.proc.poll() is None:
            try:
                self.proc.terminate()
                self.status_var.set("キャンセル要求を送信しました")
            except OSError as exc:
                messagebox.showerror("キャンセルエラー", str(exc))

    def _drain_events(self):
        try:
            while True:
                event = self.events.get_nowait()
                if event[0] == "finished":
                    _, code, output, report_path = event
                    self.progress.stop()
                    self.run_button.configure(state="normal")
                    self.cancel_button.configure(state="disabled")
                    self._append_log(output or "(エンジンから標準出力はありません)")
                    self._append_log(f"終了コード: {code}")
                    if code == 0 and Path(report_path).is_file():
                        try:
                            data = json.loads(Path(report_path).read_text(encoding="utf-8-sig"))
                            self.report_data = data
                            self._show_report(data)
                            self.status_var.set("解析完了 — JSONレポートを生成しました")
                            self.status_dot.configure(fg=GREEN)
                            self.open_report_button.configure(state="normal")
                            self._append_log("結果JSONの読み込みに成功しました。")
                        except Exception as exc:
                            self.status_var.set("JSONレポートの読み込みに失敗しました")
                            self.status_dot.configure(fg=RED)
                            self._append_log(f"JSON error: {exc}")
                            messagebox.showerror("レポートエラー", f"JSONを読み込めませんでした:\n{exc}")
                    else:
                        self.status_var.set(f"解析失敗またはキャンセル（終了コード {code}）")
                        self.status_dot.configure(fg=RED)
                        messagebox.showerror("解析エラー",
                            f"解析エンジンが正常終了しませんでした。\n終了コード: {code}\n\n実行ログタブを確認してください。")
                elif event[0] == "failed":
                    self.progress.stop()
                    self.run_button.configure(state="normal")
                    self.cancel_button.configure(state="disabled")
                    self.status_var.set("エンジン起動に失敗しました")
                    self.status_dot.configure(fg=RED)
                    self._append_log("起動エラー: " + event[1])
                    messagebox.showerror("起動エラー", event[1])
        except queue.Empty:
            pass
        self.after(120, self._drain_events)

    def _show_report(self, d):
        gguf = d.get("gguf", {})
        stats = d.get("byte_statistics", {})
        perf = d.get("performance", {})
        scan = d.get("physical_scan", {})
        self.metric_vars["size"].set(format_bytes(d.get("file_size")))
        self.metric_vars["coverage"].set(f"{scan.get('coverage_percent', 0):.2f}%")
        self.metric_vars["speed"].set(f"{perf.get('scan_mib_per_second', 0):,.1f} MiB/s")
        self.metric_vars["entropy"].set(f"{stats.get('entropy_bits_per_byte', 0):.4f}")
        self.metric_vars["tensors"].set(f"{gguf.get('parsed_tensor_count', 0):,}")
        self.metric_vars["metadata"].set(f"{gguf.get('parsed_metadata_count', 0):,}")
        backend = d.get("backend", {})
        quant = d.get("quantization", {})
        declared_profile = quant.get("declared_profile")
        type_counts = quant.get("tensor_type_counts", {})
        type_mix = " / ".join(f"{name}:{count}" for name, count in sorted(type_counts.items(), key=lambda item: (-item[1], item[0])))
        if not declared_profile:
            declared_profile = "メタデータ未設定"
        summary = (
            f"量子化プロファイル: {declared_profile}（general.file_type={quant.get('declared_file_type_id', '—')}）\n"
            f"テンソル形式内訳: {type_mix or '—'}\n"
            f"構造状態: {d.get('structure_status', 'unknown')}\n"
            f"GGUFバージョン: {gguf.get('version', '—')}    "
            f"アラインメント: {gguf.get('alignment', '—')} bytes\n"
            f"物理走査: {scan.get('bytes_scanned', 0):,} / {d.get('file_size', 0):,} bytes\n"
            f"走査時間: {perf.get('scan_seconds', 0):.3f} 秒\n"
            f"バックエンド: {backend.get('active', 'cpu')}    "
            f"GPU処理チャンク: {backend.get('gpu_chunks_processed', 0)}\n"
            f"問題件数: {len(d.get('issues', []))}    "
            f"ギャップ/未割当領域: {len(d.get('unparsed_gaps', []))}\n"
            f"テンソル値のデコード: {d.get('tensor_value_decode_status', 'partial_supported')}"
        )
        self._set_text(self.summary_text, summary)
        self._set_text(self.sha_text, d.get("sha256") or "未計算（設定でSHA-256を無効化）")
        for tree in (self.metadata_tree, self.tensor_tree, self.issue_tree):
            tree.delete(*tree.get_children())
        for m in d.get("metadata", []):
            self.metadata_tree.insert("", "end", values=(m.get("key", ""), m.get("type", ""), str(m.get("value", ""))[:500]))
        self.tensor_lookup = {}
        for t in d.get("tensors", []):
            dims = " × ".join(str(x) for x in t.get("dimensions", []))
            iid = self.tensor_tree.insert("", "end", values=(t.get("name", ""), t.get("type", ""), dims,
                                                       f"{t.get('element_count', 0):,}", f"{t.get('absolute_offset', 0):,}"))
            self.tensor_lookup[iid] = t
        for issue in d.get("issues", []):
            self.issue_tree.insert("", "end", values=(issue.get("severity", ""), issue.get("code", ""),
                                                      issue.get("message", ""), issue.get("offset", "")))
        self.notebook.select(self.overview_tab)
        self._append_log(f"構造: {d.get('structure_status')}; issues={len(d.get('issues', []))}; "
                         f"sha256={'available' if d.get('sha256') else 'not calculated'}")

    def _decode_selected(self):
        selected = self.tensor_tree.selection()
        if not selected:
            messagebox.showinfo("テンソル未選択", "テンソル一覧から復号する行を選択してください。")
            return
        if not self.input_var.get().strip() or not Path(self.input_var.get().strip()).is_file():
            messagebox.showerror("入力ファイルなし", "先にGGUFファイルを選択し、解析を完了してください。")
            return
        t = self.tensor_lookup.get(selected[0])
        if not t: return
        try:
            count = int(self.decode_count_var.get())
            if not 1 <= count <= 1000000: raise ValueError
        except ValueError:
            messagebox.showerror("要素数エラー", "復号要素数は1〜1,000,000の整数で指定してください。")
            return
        engine = self._engine_path()
        if not engine.is_file():
            messagebox.showerror("エンジンなし", f"CPUエンジンが見つかりません。先に build.ps1 を実行してください。\n{engine}")
            return
        out_path = Path(self.output_var.get().strip() or "gguf_report.json").with_name("decoded_" + Path(self.input_var.get()).stem + "_values.json")
        args = [str(engine), self.input_var.get().strip(), "--decode-at", str(t.get("absolute_offset", 0)),
                "--decode-type", str(t.get("type_id", 0)), "--decode-total", str(t.get("element_count", 0)),
                "--decode-count", str(count), "--decode-out", str(out_path)]
        try:
            cp = subprocess.run(args, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                                encoding="utf-8", errors="replace", timeout=120,
                                creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
            self._append_log(cp.stdout or "")
            if cp.returncode != 0:
                messagebox.showerror("テンソル復号エラー", cp.stdout or f"終了コード {cp.returncode}")
                return
            data = json.loads(out_path.read_text(encoding="utf-8"))
            win = tk.Toplevel(self); win.title("テンソル復号結果"); win.geometry("760x560"); win.configure(bg=BG)
            ttk.Label(win, text=f"{t.get('name')}  /  {t.get('type')}  /  {data['decoded_elements']:,} / {data['total_elements']:,} 要素",
                      style="Section.TLabel").pack(anchor="w", padx=14, pady=12)
            txt = tk.Text(win, bg="#0e1628", fg=TEXT, insertbackground=TEXT, wrap="none", font=("Cascadia Mono", 10))
            txt.pack(fill="both", expand=True, padx=14, pady=(0, 14))
            txt.insert("1.0", "index,value\n" + "\n".join(f"{i},{v}" for i, v in enumerate(data["values"])))
            txt.configure(state="disabled")
            self.status_var.set(f"テンソル復号完了: {data['decoded_elements']:,} values")
            self._append_log(f"Decoded tensor={t.get('name')} type={t.get('type')} values={data['decoded_elements']} output={out_path}")
        except Exception as exc:
            messagebox.showerror("テンソル復号エラー", str(exc))

    def _engine_path(self) -> Path:
        candidates = [resource_root() / "engine" / "build_cpu" / "Release" / "gguf_analyzer.exe",
                      resource_root() / "engine" / "build_cpu" / "gguf_analyzer.exe",
                      Path(__file__).resolve().parent / "engine" / "build_cpu" / "Release" / "gguf_analyzer.exe",
                      Path(__file__).resolve().parent / "engine" / "build_cpu" / "gguf_analyzer.exe"]
        return next((p for p in candidates if p.is_file()), candidates[0])

    def _open_report(self):
        if self.last_report_path and self.last_report_path.is_file():
            try:
                os.startfile(str(self.last_report_path))
            except Exception as exc:
                messagebox.showerror("ファイルを開けません", str(exc))


if __name__ == "__main__":
    app = AnalyzerApp()
    app.mainloop()
