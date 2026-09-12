#!/usr/bin/env python3
"""SuperAstra-style companion for the snesrecomp Lua TCP bridge.

Interface structure and visual styling are adapted from SuperAstra, MIT:
https://github.com/ScottStevenson/SuperAstra
"""

from __future__ import annotations

import json
import os
import queue
import sys
import threading
from pathlib import Path
import tkinter as tk
from tkinter import filedialog, messagebox, ttk


HERE = Path(__file__).resolve().parent
SOURCE_ROOT = HERE.parents[1] if len(HERE.parents) > 1 else HERE
SNESRECOMP_TOOLS = SOURCE_ROOT / "snesrecomp" / "tools"
if SNESRECOMP_TOOLS.exists():
    sys.path.insert(0, str(SNESRECOMP_TOOLS))

try:
    from lua_tcp import LuaClient
except ImportError as exc:  # pragma: no cover - only hit by broken packaging.
    raise SystemExit(
        "lua_console.py needs lua_tcp.py beside it, or snesrecomp/tools/lua_tcp.py "
        "when run from the source tree."
    ) from exc


BG, PANEL, FG, MUTED, ACCENT = "#080d24", "#1b2865", "#f4f1ff", "#929bc3", "#f4d38b"
DEFAULT_LUA = """return emu.framecount(), emu.getsystemid()"""


class SnesrecompAstra:
    def __init__(self, root: tk.Tk):
        self.root = root
        root.title("SUPERASTRA / snesrecomp Lua")
        root.geometry("920x880")
        root.minsize(780, 760)
        root.configure(bg=BG)

        self.events: queue.Queue[tuple[str, object]] = queue.Queue()
        self.busy = False
        self.client: LuaClient | None = None
        self.client_key: tuple[str, int] | None = None
        self.mode = tk.StringVar(value=os.environ.get("SNESRECOMP_LUA_CONSOLE_MODE", "Eval"))
        self.status = tk.StringVar(value="NO SNESRECOMP LUA BRIDGE CONNECTED")
        self.activity = tk.StringVar(value="Start SMW with SNESRECOMP_LUA_PORT=4380, then cast Lua.")
        self.host_var = tk.StringVar(value="127.0.0.1")
        self.port_var = tk.StringVar(value="4380")

        from tkinter import font as tkfont

        families = set(tkfont.families(root))
        self.mono = next((f for f in ("Consolas", "DejaVu Sans Mono", "Courier New") if f in families), "Courier")
        self.menu_font = (self.mono, 10, "bold")
        self._configure_style()
        self._build_ui()
        root.bind("<Configure>", self.resize_labels, add="+")
        root.after(100, self.drain)
        root.after(700, self.poll)
        root.protocol("WM_DELETE_WINDOW", self.close)

    def _configure_style(self) -> None:
        style = ttk.Style()
        style.theme_use("clam")
        style.configure("TFrame", background=BG)
        style.configure("TLabel", background=BG, foreground=FG, font=(self.mono, 10))
        style.configure(
            "TButton",
            background="#293571",
            foreground=FG,
            bordercolor="#6575b5",
            lightcolor="#8291cb",
            darkcolor="#111a46",
            borderwidth=2,
            padding=(14, 10),
            font=self.menu_font,
        )
        style.map(
            "TButton",
            background=[("active", "#414f94"), ("pressed", "#202955")],
            foreground=[("disabled", "#7b83a8")],
        )
        style.configure(
            "Accent.TButton",
            background=ACCENT,
            foreground="#251d38",
            bordercolor="#fff2bf",
            lightcolor="#fff6d4",
            darkcolor="#a67b42",
        )
        style.map(
            "Accent.TButton",
            background=[("active", "#ffe9ad"), ("pressed", "#d3ac65")],
            foreground=[("disabled", "#766544")],
        )
        style.configure("Menu.TButton", background=PANEL, anchor="w", padding=(10, 10), borderwidth=0)
        style.map("Menu.TButton", background=[("active", "#3d4b96"), ("pressed", "#101b51")])
        style.configure("TRadiobutton", background=BG, foreground=MUTED, font=(self.mono, 9))
        style.map("TRadiobutton", foreground=[("selected", FG)])
        style.configure("TCheckbutton", background=BG, foreground=FG)
        style.configure("TEntry", fieldbackground=PANEL, foreground=FG, insertcolor=ACCENT, padding=6)
        style.configure(
            "Vertical.TScrollbar",
            background="#5363a0",
            troughcolor=PANEL,
            bordercolor=PANEL,
            arrowcolor=FG,
        )

    def panel(self, parent):
        shadow = tk.Frame(parent, bg="#030617", padx=0, pady=0)
        edge = tk.Frame(shadow, bg="#b9c5ef", padx=2, pady=2)
        edge.pack(fill="both", expand=True, padx=(0, 4), pady=(0, 4))
        inset = tk.Frame(edge, bg="#5364a6", padx=2, pady=2)
        inset.pack(fill="both", expand=True)
        inner = tk.Frame(inset, bg=PANEL, padx=16, pady=12)
        inner.pack(fill="both", expand=True)
        return shadow, inner

    def _build_ui(self) -> None:
        outer = tk.Frame(self.root, bg=BG, padx=24, pady=16)
        outer.pack(fill="both", expand=True)

        header = tk.Canvas(outer, height=138, bg=BG, highlightthickness=0)
        header.pack(fill="x")
        for x, y, size, color in (
            (14, 30, 3, "#7884cb"),
            (562, 31, 3, "#9a85d1"),
            (577, 91, 2, "#5666a0"),
            (20, 113, 2, "#5666a0"),
        ):
            header.create_rectangle(x, y, x + size, y + size, fill=color, outline="")
        header.create_text(24, 58, anchor="w", text="SUPERASTRA", fill=FG, font=(self.mono, 35, "bold"))
        header.create_text(24, 96, anchor="w", text="snesrecomp Lua bridge", fill=ACCENT, font=(self.mono, 14, "bold"))
        header.create_text(286, 122, text="Change the game.", fill=MUTED, font=(self.mono, 10))
        settings = ttk.Button(header, text="CONNECTION", command=self.settings)
        settings.place(relx=1.0, x=-4, y=92, anchor="ne")

        status_frame, status_inner = self.panel(outer)
        status_frame.pack(fill="x", pady=(0, 12))
        status_inner.configure(pady=8)
        tk.Label(status_inner, text="CARTRIDGE", bg=PANEL, fg="#b3a0e5", font=(self.mono, 9, "bold")).pack(
            side="left", padx=(0, 16)
        )
        self.status_label = tk.Label(
            status_inner,
            textvariable=self.status,
            bg=PANEL,
            fg=FG,
            font=(self.mono, 10),
            anchor="w",
            justify="left",
            wraplength=640,
        )
        self.status_label.pack(side="left", fill="x", expand=True)

        modes = ttk.Frame(outer)
        modes.pack(fill="x", pady=(0, 10))
        ttk.Radiobutton(modes, text="EVAL", variable=self.mode, value="Eval").pack(side="left")
        ttk.Radiobutton(modes, text="RUN SCRIPT", variable=self.mode, value="Run").pack(side="left", padx=(24, 0))
        ttk.Radiobutton(modes, text="LOCAL SHORTCUTS", variable=self.mode, value="Local").pack(side="left", padx=(24, 0))
        ttk.Label(modes, text="CTRL + ENTER TO CAST", foreground=MUTED, font=(self.mono, 9)).pack(
            side="right", padx=(0, 4)
        )

        prompt_frame, prompt_inner = self.panel(outer)
        prompt_frame.pack(fill="x")
        prompt_title = tk.Frame(prompt_inner, bg=PANEL)
        prompt_title.pack(fill="x", pady=(0, 8))
        tk.Label(prompt_title, text="YOUR COMMAND", bg=PANEL, fg=ACCENT, font=(self.mono, 11, "bold")).pack(
            side="left"
        )
        tk.Label(prompt_title, text="WRITE A LITTLE MAGIC", bg=PANEL, fg="#aab5ed", font=(self.mono, 9)).pack(
            side="right"
        )
        self.prompt = tk.Text(
            prompt_inner,
            height=5,
            bg=PANEL,
            fg=FG,
            insertbackground=ACCENT,
            insertwidth=3,
            relief="flat",
            borderwidth=0,
            highlightthickness=0,
            padx=0,
            pady=6,
            font=(self.mono, 16),
            wrap="word",
            selectbackground="#6658a5",
            selectforeground="#ffffff",
        )
        self.prompt.pack(fill="x")
        self.prompt.insert("1.0", os.environ.get("SNESRECOMP_LUA_CONSOLE_PROMPT", DEFAULT_LUA))
        self.prompt.bind("<Control-Return>", lambda _event: (self.send(), "break")[1])
        arrow = tk.Canvas(prompt_inner, height=9, bg=PANEL, highlightthickness=0)
        arrow.pack(fill="x")
        arrow.bind(
            "<Configure>",
            lambda e: (
                arrow.delete("all"),
                arrow.create_polygon(e.width - 16, 0, e.width - 2, 0, e.width - 9, 7, fill=FG, outline=""),
            ),
        )

        row = ttk.Frame(outer)
        row.pack(fill="x", pady=(12, 16))
        self.send_button = ttk.Button(row, text="CAST LUA", style="Accent.TButton", command=self.send)
        self.send_button.pack(side="left")
        ttk.Button(row, text="STOP EFFECTS", command=lambda: self.raw_command("reset")).pack(side="left", padx=10)
        for title, source, mode in (
            ("STATUS", 'return smw and smw.status() or emu.framecount()', "Eval"),
            ("FIRE STREAM", "fire stream 100", "Local"),
            ("LOAD SMW HELPERS", "", "Run"),
        ):
            ttk.Button(row, text=title, command=lambda s=source, m=mode: self.fill(s, m)).pack(
                side="right", padx=(8, 4)
            )

        lower = tk.Frame(outer, bg=BG)
        lower.pack(fill="both", expand=True)
        menu_frame, menu_inner = self.panel(lower)
        menu_frame.pack(side="right", fill="y", padx=(14, 0))
        menu_inner.configure(padx=8)
        tk.Label(menu_inner, text="MENU", bg=PANEL, fg=ACCENT, font=(self.mono, 11, "bold")).pack(
            anchor="w", padx=10, pady=(0, 8)
        )
        for title, action in (
            ("> STATUS", lambda: self.raw_command("status")),
            ("> PAUSE", lambda: self.raw_command("pause")),
            ("> RESUME", lambda: self.raw_command("resume")),
            ("> STEP FRAME", lambda: self.run_client("step", lambda c: c.step(1))),
            ("> OPEN LUA FILE", self.open_file),
            ("> LOAD SMW HELPERS", self.load_smw_helpers),
            ("> CLEAR PROMPT", lambda: self.fill("", self.mode.get())),
            ("> RESET LUA VM", lambda: self.raw_command("reset")),
        ):
            ttk.Button(menu_inner, text=title, style="Menu.TButton", command=action).pack(fill="x", pady=1)

        log_frame, log_inner = self.panel(lower)
        log_frame.pack(side="left", fill="both", expand=True)
        tk.Label(log_inner, text="SESSION LOG", bg=PANEL, fg="#b3a0e5", font=(self.mono, 10, "bold")).pack(
            anchor="w", pady=(0, 10)
        )
        text_row = tk.Frame(log_inner, bg=PANEL)
        text_row.pack(fill="both", expand=True)
        self.transcript = tk.Text(
            text_row,
            height=7,
            bg=PANEL,
            fg=FG,
            relief="flat",
            borderwidth=0,
            highlightthickness=0,
            padx=0,
            pady=3,
            font=(self.mono, 11),
            spacing1=3,
            spacing3=5,
            wrap="word",
            state="disabled",
        )
        scroll = ttk.Scrollbar(text_row, orient="vertical", command=self.transcript.yview)
        scroll.pack(side="right", fill="y", padx=(8, 0))
        self.transcript.configure(yscrollcommand=scroll.set)
        self.transcript.pack(side="left", fill="both", expand=True)
        self.transcript.tag_configure("you", foreground=ACCENT, font=(self.mono, 10, "bold"))
        self.transcript.tag_configure("detail", foreground="#c6b3fc", font=(self.mono, 10, "bold"))
        self.log("Astra", "What Lua would you like to send?")
        self.footer = ttk.Label(outer, textvariable=self.activity, foreground=MUTED, font=(self.mono, 9), wraplength=850)
        self.footer.pack(anchor="w", pady=(10, 0))

    def resize_labels(self, event) -> None:
        if event.widget == self.root:
            self.status_label.configure(wraplength=max(420, event.width - 215))
            self.footer.configure(wraplength=max(500, event.width - 65))

    def log(self, speaker: str, text: str) -> None:
        self.transcript.configure(state="normal")
        self.transcript.insert("end", speaker.upper() + "\n", "you" if speaker == "You" else "detail")
        self.transcript.insert("end", text + "\n\n")
        self.transcript.see("end")
        self.transcript.configure(state="disabled")

    def fill(self, source: str, mode: str | None = None) -> None:
        if mode:
            self.mode.set(mode)
        if source:
            self.prompt.delete("1.0", "end")
            self.prompt.insert("1.0", source)
        elif mode == "Run":
            self.load_smw_helpers()
        else:
            self.prompt.delete("1.0", "end")
        self.prompt.focus_set()

    def settings(self) -> None:
        dialog = tk.Toplevel(self.root)
        dialog.title("SUPERASTRA / snesrecomp connection")
        dialog.configure(bg=BG)
        dialog.transient(self.root)
        dialog.geometry("+" + str(self.root.winfo_rootx() + 100) + "+" + str(self.root.winfo_rooty() + 100))
        frame = ttk.Frame(dialog, padding=22)
        frame.pack(fill="both", expand=True)
        ttk.Label(frame, text="Host").pack(anchor="w")
        host = ttk.Entry(frame, width=56)
        host.insert(0, self.host_var.get())
        host.pack(fill="x", pady=(6, 14))
        ttk.Label(frame, text="Port").pack(anchor="w")
        port = ttk.Entry(frame, width=12)
        port.insert(0, self.port_var.get())
        port.pack(anchor="w", pady=(6, 14))
        ttk.Label(
            frame,
            text="Start the game with SNESRECOMP_LUA_PORT set to the same port. "
            "The bridge listens only on localhost.",
            foreground=MUTED,
            wraplength=510,
            justify="left",
        ).pack(anchor="w", pady=14)

        def apply() -> None:
            if self.busy:
                messagebox.showinfo("Command running", "Wait for the current command to finish before changing ports.")
                return
            try:
                value = int(port.get())
                if value < 1 or value > 65535:
                    raise ValueError()
            except ValueError:
                messagebox.showerror("Port", "Enter a whole number from 1 to 65535.")
                return
            self.host_var.set(host.get().strip() or "127.0.0.1")
            self.port_var.set(str(value))
            self.disconnect()
            dialog.destroy()
            self.raw_command("status")

        ttk.Button(frame, text="Use connection", command=apply, style="Accent.TButton").pack(anchor="e")

    def client_or_connect(self) -> LuaClient:
        host = self.host_var.get().strip() or "127.0.0.1"
        port = int(self.port_var.get())
        key = (host, port)
        if self.client is not None and self.client_key == key:
            return self.client
        self.disconnect()
        self.client = LuaClient(host=host, port=port, timeout=10)
        self.client_key = key
        return self.client

    def disconnect(self) -> None:
        if self.client is not None:
            try:
                self.client.close()
            except Exception:
                pass
        self.client = None
        self.client_key = None

    def prompt_text(self) -> str:
        return self.prompt.get("1.0", "end").strip()

    def local_shortcut(self, prompt: str) -> str:
        text = prompt.lower().strip().rstrip(".!?")
        if text in ("status", "smw status", "what is happening"):
            return 'return smw and smw.status() or "run smw.lua first"'
        if text in ("stop", "stop effects", "reset effects"):
            return 'if smw then smw.stop() end; return "stopped"'
        if text.startswith("fire stream"):
            rate = "".join(ch for ch in text if ch.isdigit()) or "100"
            return f"return smw.fire_stream({rate})"
        if text in ("hold fire", "holdfire", "drop a star"):
            return "return smw.holdfire(100)"
        if text in ("invincible", "make me invincible"):
            return "return smw.invincible(true)"
        raise ValueError(
            "Local shortcuts know: status, fire stream 100, 100 fireballs, "
            "hold fire, invincible, stop effects."
        )

    def looks_like_fireball_request(self, prompt: str) -> bool:
        text = prompt.lower()
        fire_words = ("fireball", "fireballs", "fire power", "fire mario", "firing")
        rate_words = ("100", "hundred", "per second", "always", "maximum", "spawn", "entities")
        return any(word in text for word in fire_words) and any(word in text for word in rate_words)

    def always_fire_source(self, rate: int = 100) -> str:
        return f"""
event.unregisterbyname("astra.always_fire_100")
game.command("fire_stream_reset")
local active = false
event.onframestart(function()
    local playable = mainmemory.read_u8(0x100) == 0x14
        and mainmemory.read_u8(0x109) == 0
        and mainmemory.read_u8(0x71) == 0
    if playable then
        mainmemory.write_u8(0x19, 3)
        if not active then
            game.command("fire_stream", "{rate}")
            active = true
        end
    elseif active then
        game.command("fire_stream", "0")
        active = false
    end
end, "astra.always_fire_100")
print("Always-fire {rate}/sec loaded. Enter a level; Mario will stay fire powered and stream fireballs.")
"""

    def send(self) -> None:
        if self.busy:
            return
        source = self.prompt_text()
        if not source:
            return
        mode = self.mode.get()
        self.log("You", source)
        if mode == "Run":
            self.run_client("run", lambda c: c.run(source))
        elif mode == "Local":
            self.run_client("local shortcut", lambda c: self.run_local_shortcut(c, source))
        else:
            self.run_client("eval", lambda c: c.eval(source))

    def smw_helper_source(self) -> str:
        for path in (HERE / "smw.lua", SOURCE_ROOT / "tools" / "lua" / "smw.lua"):
            if path.exists():
                return path.read_text(encoding="utf-8")
        raise FileNotFoundError("Could not find smw.lua")

    def run_local_shortcut(self, client: LuaClient, prompt: str) -> dict:
        text = prompt.lower().strip().rstrip(".!?")
        if self.looks_like_fireball_request(prompt):
            return client.run(self.always_fire_source(100))
        if text in ("100 fireballs", "one hundred fireballs", "load 100 fireballs"):
            example = HERE / "100_fireballs.lua"
            if not example.exists():
                example = SOURCE_ROOT / "lua" / "100_fireballs.lua"
            if not example.exists():
                raise FileNotFoundError("Could not find 100_fireballs.lua")
            return client.run(example.read_text(encoding="utf-8"))
        client.run(self.smw_helper_source())
        return client.eval(self.local_shortcut(prompt))

    def raw_command(self, command: str) -> None:
        self.run_client(command, lambda c: c.command(command))

    def run_client(self, label: str, operation) -> None:
        if self.busy:
            return
        self.busy = True
        self.send_button.configure(state="disabled")
        self.activity.set("Astra -> snesrecomp: " + label)

        def worker() -> None:
            try:
                result = operation(self.client_or_connect())
                self.events.put(("answer", result))
            except Exception as exc:
                self.disconnect()
                self.events.put(("error", str(exc)))
            finally:
                self.events.put(("done", ""))

        threading.Thread(target=worker, daemon=True).start()

    def open_file(self) -> None:
        if self.busy:
            return
        path = filedialog.askopenfilename(
            title="Open Lua file",
            filetypes=(("Lua files", "*.lua"), ("All files", "*.*")),
        )
        if not path:
            return
        self.mode.set("Run")
        self.prompt.delete("1.0", "end")
        self.prompt.insert("1.0", Path(path).read_text(encoding="utf-8"))
        self.prompt.focus_set()

    def load_smw_helpers(self) -> None:
        for path in (HERE / "smw.lua", SOURCE_ROOT / "tools" / "lua" / "smw.lua"):
            if path.exists():
                self.mode.set("Run")
                self.prompt.delete("1.0", "end")
                self.prompt.insert("1.0", path.read_text(encoding="utf-8"))
                self.prompt.focus_set()
                return
        messagebox.showerror("Lua bridge", "Could not find smw.lua")

    def drain(self) -> None:
        while True:
            try:
                kind, message = self.events.get_nowait()
            except queue.Empty:
                break
            if kind == "answer":
                self.show_result(message)
            elif kind == "error":
                self.log("Could not complete", str(message))
                self.activity.set("Check the session message above.")
                self.status.set("NO SNESRECOMP LUA BRIDGE CONNECTED")
            elif kind == "done":
                self.busy = False
                self.send_button.configure(state="normal")
        self.root.after(100, self.drain)

    def show_result(self, result: dict) -> None:
        values = result.get("values") or []
        output = result.get("output") or ""
        error = result.get("error") or ""
        parts = []
        if output:
            parts.append(output.strip())
        if values:
            parts.append("values: " + json.dumps(values, ensure_ascii=False))
        if error:
            parts.append("last error: " + error)
        if not parts:
            parts.append(json.dumps(result, indent=2))
        self.log("Lua", "\n".join(parts))
        paused = "paused" if result.get("paused") else "live"
        running = "running" if result.get("running") else "idle"
        self.status.set(f"Connected: SNES Lua bridge   |   Frame: {result.get('frame')}   |   {paused}, {running}")
        self.activity.set("Ready when you are.")

    def poll(self) -> None:
        if not self.busy and self.client is not None:
            try:
                result = self.client.command("status")
                paused = "paused" if result.get("paused") else "live"
                running = "running" if result.get("running") else "idle"
                self.status.set(f"Connected: SNES Lua bridge   |   Frame: {result.get('frame')}   |   {paused}, {running}")
            except Exception:
                self.disconnect()
                self.status.set("NO SNESRECOMP LUA BRIDGE CONNECTED")
        self.root.after(1000, self.poll)

    def close(self) -> None:
        self.disconnect()
        self.root.destroy()


def main() -> None:
    root = tk.Tk()
    SnesrecompAstra(root)
    root.mainloop()


if __name__ == "__main__":
    main()
