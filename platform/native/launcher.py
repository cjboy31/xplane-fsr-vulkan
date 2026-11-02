#!/usr/bin/env python3
"""Native preview launcher. SPDX-License-Identifier: MIT"""
import argparse
import platform
from pathlib import Path
import shlex
import sys
import core

PACKAGE = Path(__file__).resolve().parent
SYSTEM = platform.system()


def gui(root, saved):
    try:
        import tkinter as tk
        from tkinter import filedialog, messagebox, ttk
    except ImportError:
        raise SystemExit("Install Python 3 with Tk (Ubuntu: sudo apt install python3-tk), or use --help for CLI launch.")
    window = tk.Tk()
    window.title("FSR 3/4 + Frame Gen • Native platform preview")
    window.geometry("850x760")
    window.minsize(680, 650)
    window.configure(bg="#11151c")
    style = ttk.Style(window)
    style.theme_use("clam")
    style.configure("TFrame", background="#11151c")
    style.configure("TLabel", background="#11151c", foreground="#eee9df", font=("Helvetica", 11))
    style.configure("TButton", background="#303745", foreground="#eee9df", padding=10)
    style.map("TButton", background=[("pressed", "#4e5768"), ("active", "#3b4556")],
              foreground=[("active", "#ffffff"), ("pressed", "#ffffff")])
    style.configure("TEntry", fieldbackground="#242b36", foreground="#eee9df", padding=8)
    style.configure("TCombobox", fieldbackground="#242b36", foreground="#eee9df", padding=8)
    style.map("TCombobox", fieldbackground=[("readonly", "#242b36"), ("disabled", "#242b36")],
              foreground=[("readonly", "#eee9df"), ("disabled", "#d9c394")],
              selectbackground=[("readonly", "#242b36")], selectforeground=[("readonly", "#eee9df")])
    window.option_add("*TCombobox*Listbox.background", "#242b36")
    window.option_add("*TCombobox*Listbox.foreground", "#eee9df")
    body = ttk.Frame(window, padding=26)
    body.pack(fill="both", expand=True)
    ttk.Label(body, text="NATIVE PLATFORM PREVIEW", foreground="#d9c394", font=("Helvetica", 10, "bold")).pack(anchor="w")
    ttk.Label(body, text="FSR for X-Plane", font=("Helvetica", 24, "bold")).pack(anchor="w", pady=(8, 14))
    explanation = ("Linux x64 • FSR 3.1 Vulkan preview. Community flight testing pending."
                   if SYSTEM == "Linux" else "macOS • Compatibility probe only. Metal FSR rendering is not implemented.")
    ttk.Label(body, text=explanation, wraplength=740).pack(anchor="w", pady=(0, 16))
    fields = {k: tk.StringVar(value=saved[k]) for k in ("sim", "mode", "arguments")}
    state = {"process": None, "session": None}
    save_state = tk.StringVar(value="Saved choices load automatically on reopening.")

    def persist(*_):
        saved.update({k: v.get() for k, v in fields.items()})
        try:
            core.save_settings(root, saved)
            save_state.set("All choices saved.")
        except (OSError, ValueError) as e:
            save_state.set("SAVE FAILED: " + str(e))

    def choose():
        folder = filedialog.askdirectory(title="Choose your X-Plane 12 folder")
        if folder:
            fields["sim"].set(folder)

    ttk.Label(body, text="X-Plane 12 folder" + (" (found automatically)" if saved["sim"] and saved["sim"] == core.detect_simulator(SYSTEM) else "")).pack(anchor="w")
    row = ttk.Frame(body)
    row.pack(fill="x", pady=(6, 16))
    ttk.Entry(row, textvariable=fields["sim"]).pack(side="left", fill="x", expand=True)
    ttk.Button(row, text="Browse…", command=choose).pack(side="right", padx=(10, 0))
    ttk.Label(body, text="Rendering mode").pack(anchor="w")
    mode_display = fields["mode"] if SYSTEM == "Linux" else tk.StringVar(value="Compatibility probe only — FSR unavailable")
    ttk.Combobox(body, textvariable=mode_display, values=list(core.MODES), state="readonly" if SYSTEM == "Linux" else "disabled").pack(fill="x", pady=(6, 4))
    ttk.Button(body, text="ⓘ  Which mode should I use?", command=lambda: messagebox.showinfo("Rendering mode", "FSR 3.1 upscaling replaces X-Plane's FSR 1 and adds temporal anti-aliasing.\n\nCompare starts the sim without our Vulkan layer. Observe runs FSR into private images but displays X-Plane FSR 1; use only for diagnostics.\n\nFSR 4, built-in frame generation, VR and native-resolution AA are unavailable in this Linux preview. macOS currently supports diagnostics only.")).pack(anchor="w", pady=(0, 12))
    ttk.Label(body, text="Simulator arguments (optional)").pack(anchor="w")
    ttk.Entry(body, textvariable=fields["arguments"]).pack(fill="x", pady=(6, 12))
    ttk.Label(body, text="SIM SETUP", foreground="#d9c394", font=("Helvetica", 10, "bold")).pack(anchor="w")
    guide = ("Set the sim's output/display resolution to your monitor (e.g. 3840×2160). Enable Rendering Resolution / FSR Supersampling; start at 1.5×. Turn MSAA off; FXAA off is recommended. Load a flight and wait for ACTIVE below. This launcher does not change simulator preferences."
             if SYSTEM == "Linux" else "Close X-Plane, install the optional probe below, then launch a test session. It checks native plugin loading and camera data. It does not change rendering, controller assignments or simulator preferences.")
    ttk.Label(body, text=guide, wraplength=740).pack(anchor="w", pady=(7, 14))
    status = tk.Text(body, height=6, bg="#1b222c", fg="#eee9df", relief="flat", wrap="word", font=("Helvetica", 11), padx=12, pady=12)
    status.pack(fill="both", expand=True)

    def show(text):
        status.configure(state="normal")
        status.delete("1.0", "end")
        status.insert("1.0", text)
        status.configure(state="disabled")

    def start():
        if state["process"] is not None and state["process"].poll() is None:
            messagebox.showinfo("Already running", "The session launched by this window is still running.")
            return
        persist()
        try:
            process, session = core.launch(PACKAGE, root, saved, shlex.split(fields["arguments"].get()))
            state.update(process=process, session=session)
        except (OSError, ValueError) as e:
            messagebox.showerror("Unable to start", str(e))

    def probe(remove=False):
        if state["process"] is not None and state["process"].poll() is None:
            messagebox.showerror("Close X-Plane first", "Close the test session before changing the probe installation.")
            return
        try:
            if remove:
                core.uninstall_probe(fields["sim"].get())
            else:
                core.install_probe(PACKAGE, fields["sim"].get(), SYSTEM)
            messagebox.showinfo("Probe", "Probe removed." if remove else "Diagnostic probe installed. FSR rendering is not enabled by this probe.")
        except (OSError, ValueError) as e:
            messagebox.showerror("Probe", str(e))

    ttk.Label(body, textvariable=save_state, wraplength=740).pack(anchor="w", pady=10)
    actions = ttk.Frame(body)
    actions.pack(fill="x")
    ttk.Button(actions, text="Launch test session" if SYSTEM == "Darwin" else "Launch X-Plane", command=start).pack(side="left")
    ttk.Button(actions, text="Install probe", command=probe).pack(side="left", padx=8)
    ttk.Button(actions, text="Remove probe", command=lambda: probe(True)).pack(side="left")
    show("NOT RUNNING — no injection has been confirmed.\nFSR 4 and frame generation are unavailable in this native preview.")

    def poll():
        if state["session"]:
            try:
                show(core.renderer_status(state["session"], state["process"].poll() is None, SYSTEM))
            except OSError as e:
                show("STATUS UNAVAILABLE: " + str(e))
        window.after(1000, poll)

    for variable in fields.values():
        variable.trace_add("write", persist)
    def resize(event):
        if event.widget is body:
            for widget in body.winfo_children():
                if isinstance(widget, ttk.Label):
                    widget.configure(wraplength=max(200, event.width - 52))
    body.bind("<Configure>", resize)
    window.after(1000, poll)
    window.mainloop()


def main():
    parser = argparse.ArgumentParser(description="Native Linux FSR 3.1 preview / macOS compatibility probe. No FSR 4 or frame generation.")
    parser.add_argument("--sim", help="X-Plane 12 installation folder")
    parser.add_argument("--mode", choices=list(core.MODES))
    parser.add_argument("--launch", action="store_true", help="Start without the GUI and wait for the simulator")
    parser.add_argument("--install-probe", action="store_true")
    parser.add_argument("--remove-probe", action="store_true")
    parser.add_argument("arguments", nargs=argparse.REMAINDER, help="Simulator arguments after --")
    args = parser.parse_args()
    root = core.user_root()
    try:
        saved = core.load_settings(root)
        if args.sim:
            saved["sim"] = args.sim
        elif not saved["sim"]:
            # One-click setup: use the folder X-Plane itself recorded.
            saved["sim"] = core.detect_simulator(SYSTEM)
        if args.mode:
            saved["mode"] = args.mode
        if args.sim or args.mode:
            core.save_settings(root, saved)
        if args.install_probe:
            print(core.install_probe(PACKAGE, saved["sim"], SYSTEM))
        elif args.remove_probe:
            core.uninstall_probe(saved["sim"])
        elif args.launch:
            arguments = args.arguments[1:] if args.arguments[:1] == ["--"] else args.arguments
            process, session = core.launch(PACKAGE, root, saved, arguments or shlex.split(saved["arguments"]))
            print("Fresh session:", session, flush=True)
            print("macOS probe only; no FSR injection." if SYSTEM == "Darwin" else "Launch requested; ACTIVE in status.txt is the renderer confirmation.", flush=True)
            return process.wait()
        else:
            gui(root, saved)
    except (OSError, ValueError) as e:
        parser.exit(1, f"Error: {e}\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
