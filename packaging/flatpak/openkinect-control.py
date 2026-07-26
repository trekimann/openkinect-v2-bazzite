#!/usr/bin/env python3
import json
import os
import shutil
import subprocess
import sys
import threading
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

HOSTCTL_CANDIDATES = [
        os.environ.get("OPENKINECT_HOSTCTL"),
        "/usr/local/libexec/openkinect-v2/openkinect-v2-hostctl.sh",
        "/usr/libexec/openkinect-v2/openkinect-v2-hostctl.sh",
]

DEFAULT_DEPTH_COLORS = {
        "DEPTH_NEAR_COLOR": "#0000FF",
        "DEPTH_MID_COLOR": "#00FF00",
        "DEPTH_FAR_COLOR": "#FF0000",
}

DEFAULT_CONTROL_PORT = int(os.environ.get("OPENKINECT_CONTROL_PORT", "40123"))

HTML = """<!DOCTYPE html>
<html lang=\"en\">
<head>
    <meta charset=\"utf-8\">
    <meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">
    <title>OpenKinect v2 Control</title>
    <style>
        :root {
            color-scheme: light dark;
            --bg: #0e1116;
            --panel: #171c24;
            --border: #2b3545;
            --text: #ecf2f8;
            --muted: #9db0c3;
            --accent: #4ad9a8;
            --accent-2: #5cc8ff;
            --danger: #ff7f7f;
        }
        body {
            margin: 0;
            font-family: "IBM Plex Sans", "Segoe UI", sans-serif;
            background: radial-gradient(circle at top, #1d2735 0%, var(--bg) 55%);
            color: var(--text);
        }
        main {
            max-width: 960px;
            margin: 0 auto;
            padding: 32px 20px 56px;
        }
        h1 {
            margin: 0 0 8px;
            font-size: 2rem;
        }
        p.lead {
            margin: 0 0 28px;
            color: var(--muted);
            line-height: 1.5;
        }
        .grid {
            display: grid;
            gap: 20px;
            grid-template-columns: repeat(auto-fit, minmax(280px, 1fr));
        }
        .panel {
            background: color-mix(in srgb, var(--panel) 88%, black 12%);
            border: 1px solid var(--border);
            border-radius: 18px;
            padding: 20px;
            box-shadow: 0 18px 48px rgba(0, 0, 0, 0.18);
        }
        .panel h2 {
            margin: 0 0 12px;
            font-size: 1.1rem;
        }
        .row {
            display: flex;
            flex-wrap: wrap;
            gap: 12px;
            align-items: center;
        }
        .field {
            display: grid;
            gap: 8px;
            margin: 14px 0;
        }
        label {
            color: var(--muted);
            font-size: 0.92rem;
        }
        select, button, input[type=\"color\"] {
            border-radius: 12px;
            border: 1px solid var(--border);
            background: #0f141c;
            color: var(--text);
            font: inherit;
        }
        select, button {
            padding: 12px 14px;
        }
        input[type=\"color\"] {
            width: 100%;
            min-height: 52px;
            padding: 4px;
            background: #0b0f15;
        }
        button.primary {
            background: linear-gradient(135deg, var(--accent), var(--accent-2));
            color: #04131a;
            border: none;
            font-weight: 700;
        }
        button.secondary {
            background: transparent;
        }
        button.warn {
            color: var(--danger);
        }
        .status {
            padding: 10px 12px;
            border-radius: 12px;
            background: rgba(255, 255, 255, 0.04);
            color: var(--muted);
            min-height: 24px;
        }
        pre {
            margin: 0;
            white-space: pre-wrap;
            word-break: break-word;
            font-family: "IBM Plex Mono", "Cascadia Code", monospace;
            font-size: 0.85rem;
            color: var(--muted);
            max-height: 320px;
            overflow: auto;
        }
        .palette {
            display: grid;
            gap: 12px;
            grid-template-columns: repeat(3, minmax(0, 1fr));
        }
        .chip {
            display: grid;
            gap: 8px;
        }
        .hint {
            margin-top: 16px;
            color: var(--muted);
            font-size: 0.9rem;
            line-height: 1.45;
        }
    </style>
</head>
<body>
    <main>
        <h1>OpenKinect v2 Control</h1>
        <p class=\"lead\">Adjust stream mode and live depth palette settings for the Bazzite host service. Mode changes restart the service. Depth palette changes reload live.</p>
        <div class=\"grid\">
            <section class=\"panel\">
                <h2>Stream Mode</h2>
                <div class=\"field\">
                    <label for=\"mode\">Active stream preset</label>
                    <select id=\"mode\">
                        <option value=\"color\">Color</option>
                        <option value=\"ir\">IR</option>
                        <option value=\"depth\">Depth</option>
                        <option value=\"all\">All</option>
                    </select>
                </div>
                <div class=\"row\">
                    <button class=\"primary\" id=\"apply-mode\">Apply Mode</button>
                    <button class=\"secondary warn\" id=\"restart\">Restart Service</button>
                </div>
                <p class=\"hint\">The current host setup exposes labeled loopback devices such as Kinect_Color, Kinect_IR, and Kinect_Depth. OBS may cache old device selections after a restart.</p>
            </section>
            <section class=\"panel\">
                <h2>Depth Palette</h2>
                <div class=\"palette\">
                    <div class=\"chip\">
                        <label for=\"near\">Near</label>
                        <input type=\"color\" id=\"near\" value=\"#0000FF\">
                    </div>
                    <div class=\"chip\">
                        <label for=\"mid\">Middle</label>
                        <input type=\"color\" id=\"mid\" value=\"#00FF00\">
                    </div>
                    <div class=\"chip\">
                        <label for=\"far\">Far</label>
                        <input type=\"color\" id=\"far\" value=\"#FF0000\">
                    </div>
                </div>
                <div class=\"row\" style=\"margin-top: 16px;\">
                    <button class=\"primary\" id=\"apply-palette\">Apply Palette Live</button>
                    <button class=\"secondary\" id=\"refresh\">Refresh</button>
                </div>
                <p class=\"hint\">Palette changes update the running depth output with a live config reload. No service restart is required for this control path.</p>
            </section>
        </div>
        <section class=\"panel\" style=\"margin-top: 20px;\">
            <h2>Host Service</h2>
            <div class=\"status\" id=\"result\">Loading host state…</div>
            <div class=\"field\" style=\"margin-top: 16px;\">
                <label for=\"status-output\">Service status</label>
                <pre id=\"status-output\"></pre>
            </div>
        </section>
    </main>
    <script>
        async function request(path, options = {}) {
            const response = await fetch(path, {
                headers: {"Content-Type": "application/json"},
                ...options,
            });
            const payload = await response.json();
            if (!response.ok || payload.ok === false) {
                throw new Error(payload.error || "Request failed");
            }
            return payload;
        }

        function setResult(message, isError = false) {
            const node = document.getElementById("result");
            node.textContent = message;
            node.style.color = isError ? "#ff9c9c" : "#9db0c3";
        }

        function applyConfig(config) {
            document.getElementById("near").value = config.DEPTH_NEAR_COLOR || "#0000FF";
            document.getElementById("mid").value = config.DEPTH_MID_COLOR || "#00FF00";
            document.getElementById("far").value = config.DEPTH_FAR_COLOR || "#FF0000";
            document.getElementById("mode").value = config.mode || "all";
            document.getElementById("status-output").textContent = config.status || "";
        }

        async function refresh() {
            try {
                setResult("Refreshing host state…");
                const payload = await request("/api/config");
                applyConfig(payload.config);
                setResult("Host state refreshed.");
            } catch (error) {
                setResult(error.message, true);
            }
        }

        async function applyMode() {
            try {
                setResult("Applying stream mode…");
                await request("/api/mode", {
                    method: "POST",
                    body: JSON.stringify({mode: document.getElementById("mode").value}),
                });
                await refresh();
                setResult("Mode applied. The service was restarted.");
            } catch (error) {
                setResult(error.message, true);
            }
        }

        async function applyPalette() {
            try {
                setResult("Applying live depth palette…");
                await request("/api/depth-colors", {
                    method: "POST",
                    body: JSON.stringify({
                        near: document.getElementById("near").value,
                        mid: document.getElementById("mid").value,
                        far: document.getElementById("far").value,
                    }),
                });
                await refresh();
                setResult("Depth palette applied live.");
            } catch (error) {
                setResult(error.message, true);
            }
        }

        async function restartService() {
            try {
                setResult("Restarting host service…");
                await request("/api/restart", {method: "POST", body: "{}"});
                await refresh();
                setResult("Host service restarted.");
            } catch (error) {
                setResult(error.message, true);
            }
        }

        document.getElementById("refresh").addEventListener("click", refresh);
        document.getElementById("apply-mode").addEventListener("click", applyMode);
        document.getElementById("apply-palette").addEventListener("click", applyPalette);
        document.getElementById("restart").addEventListener("click", restartService);
        refresh();
    </script>
</body>
</html>
"""


def ensure_flatpak_spawn():
    if shutil.which("flatpak-spawn") is not None:
        return
    if shutil.which("distrobox-host-exec") is not None:
        return

    print(
        "This control app requires flatpak-spawn or distrobox-host-exec to reach the host service.",
        file=sys.stderr,
    )
    raise SystemExit(1)


def host_bridge_prefix():
    if shutil.which("flatpak-spawn") is not None:
        return ["flatpak-spawn", "--host"]
    if shutil.which("distrobox-host-exec") is not None:
        return ["distrobox-host-exec"]
    raise RuntimeError("No supported host bridge command found")


def resolve_hostctl():
    prefix = host_bridge_prefix()
    for candidate in HOSTCTL_CANDIDATES:
        if not candidate:
            continue
        check = subprocess.run(
            prefix + ["test", "-x", candidate],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            check=False,
        )
        if check.returncode == 0:
            return candidate
    return HOSTCTL_CANDIDATES[-1]


def host_command(args, privileged=False):
    command = host_bridge_prefix()
    if privileged:
        command.append("pkexec")
    command.append(resolve_hostctl())
    return command + args


def run(args, privileged=False, capture_output=False):
    ensure_flatpak_spawn()
    if capture_output:
        return subprocess.run(host_command(args, privileged=privileged), capture_output=True, text=True, check=False)
    return subprocess.call(host_command(args, privileged=privileged))


def open_url(url):
    if shutil.which("xdg-open") is not None:
        subprocess.Popen(["xdg-open", url], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        return

    try:
        import webbrowser

        webbrowser.open(url, new=1)
    except Exception:
        pass


def parse_config_output(output):
    config = DEFAULT_DEPTH_COLORS.copy()
    for raw_line in output.splitlines():
        if "=" not in raw_line:
            continue
        key, value = raw_line.split("=", 1)
        config[key.strip()] = value.strip()

    flags = (
        config.get("ENABLE_COLOR", "0"),
        config.get("ENABLE_IR", "0"),
        config.get("ENABLE_DEPTH", "0"),
    )
    if flags == ("1", "0", "0"):
        config["mode"] = "color"
    elif flags == ("0", "1", "0"):
        config["mode"] = "ir"
    elif flags == ("0", "0", "1"):
        config["mode"] = "depth"
    else:
        config["mode"] = "all"

    return config


def load_config_payload():
    config_result = run(["config"], capture_output=True)
    if config_result.returncode != 0:
        raise RuntimeError((config_result.stderr or config_result.stdout or "Unable to read host config").strip())

    status_result = run(["status"], capture_output=True)
    status_text = (status_result.stdout or status_result.stderr or "").strip()

    config = parse_config_output(config_result.stdout)
    config["status"] = status_text
    return config


class ControlRequestHandler(BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path == "/":
            content = HTML.encode("utf-8")
            self.send_response(HTTPStatus.OK)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Content-Length", str(len(content)))
            self.end_headers()
            self.wfile.write(content)
            return

        if self.path == "/api/config":
            try:
                self.respond_json({"ok": True, "config": load_config_payload()})
            except RuntimeError as error:
                self.respond_json({"ok": False, "error": str(error)}, status=HTTPStatus.INTERNAL_SERVER_ERROR)
            return

        self.respond_json({"ok": False, "error": "Not found"}, status=HTTPStatus.NOT_FOUND)

    def do_POST(self):
        length = int(self.headers.get("Content-Length", "0"))
        raw_body = self.rfile.read(length) if length else b"{}"
        payload = json.loads(raw_body.decode("utf-8"))

        try:
            if self.path == "/api/mode":
                mode = payload.get("mode", "")
                result = run(["mode", mode], privileged=True, capture_output=True)
            elif self.path == "/api/depth-colors":
                result = run(
                    [
                        "depth-colors",
                        payload.get("near", ""),
                        payload.get("mid", ""),
                        payload.get("far", ""),
                    ],
                    privileged=True,
                    capture_output=True,
                )
            elif self.path == "/api/restart":
                result = run(["restart"], privileged=True, capture_output=True)
            else:
                self.respond_json({"ok": False, "error": "Not found"}, status=HTTPStatus.NOT_FOUND)
                return

            if result.returncode != 0:
                raise RuntimeError((result.stderr or result.stdout or "Host command failed").strip())

            self.respond_json({"ok": True})
        except RuntimeError as error:
            self.respond_json({"ok": False, "error": str(error)}, status=HTTPStatus.INTERNAL_SERVER_ERROR)

    def log_message(self, format, *args):
        return

    def respond_json(self, payload, status=HTTPStatus.OK):
        content = json.dumps(payload).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(content)))
        self.end_headers()
        self.wfile.write(content)


def launch_gui():
    ensure_flatpak_spawn()
    try:
        server = ThreadingHTTPServer(("127.0.0.1", DEFAULT_CONTROL_PORT), ControlRequestHandler)
    except OSError:
        server = ThreadingHTTPServer(("127.0.0.1", 0), ControlRequestHandler)
    url = f"http://127.0.0.1:{server.server_port}/"
    print(f"OpenKinect v2 control panel: {url}")
    threading.Thread(target=lambda: open_url(url), daemon=True).start()
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()


def main():
    if len(sys.argv) == 1:
        launch_gui()
        return 0
    return run(sys.argv[1:])


if __name__ == "__main__":
    raise SystemExit(main())
