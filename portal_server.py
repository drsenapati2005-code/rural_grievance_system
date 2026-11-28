"""Local folder synchronization server for the Rural Grievance System.

Serves index.html strictly on localhost (127.0.0.1) and persists every record
change directly into the project folder:
  - Strictly appends immutable chronological entries to panchayat_records.txt
  - Syncs citizens.txt, officials.txt, grievances.txt, history.txt in the exact
    pipe-delimited schema expected by rural_grievance.c.
"""

from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from datetime import datetime
import json
import os
import threading

BASE_DIR = os.path.dirname(os.path.abspath(__file__))
HOST = "127.0.0.1"
PORT = 8765

FILE_CITIZENS = os.path.join(BASE_DIR, "citizens.txt")
FILE_OFFICIALS = os.path.join(BASE_DIR, "officials.txt")
FILE_GRIEVANCES = os.path.join(BASE_DIR, "grievances.txt")
FILE_HISTORY = os.path.join(BASE_DIR, "history.txt")
FILE_MASTER_LOG = os.path.join(BASE_DIR, "panchayat_records.txt")
FILE_INDEX = os.path.join(BASE_DIR, "index.html")

IO_LOCK = threading.Lock()

MASTER_HEADER = (
    "# ============================================================================\n"
    "# RURAL PUBLIC SERVICE REQUEST & GRIEVANCE MANAGEMENT SYSTEM\n"
    "# Master Append-Only Record Ledger (panchayat_records.txt)\n"
    "# Format: TIMESTAMP | SOURCE | RECORD_TYPE | ID | ACTOR | DETAILS\n"
    "# Note  : New actions and records are strictly appended; prior lines persist.\n"
    "# ============================================================================\n"
)


def sanitize_pipes(text):
    """Replace pipe separators and newlines so record lines stay single-line."""
    if text is None:
        return ""
    return str(text).replace("|", "-").replace("\r", " ").replace("\n", " ").strip()


def now_ts():
    """Return formatted local timestamp matching C get_timestamp()."""
    return datetime.now().strftime("%Y-%m-%d %H:%M:%S")


def ensure_master_log():
    """Create panchayat_records.txt with header only if missing or empty."""
    if not os.path.exists(FILE_MASTER_LOG) or os.path.getsize(FILE_MASTER_LOG) == 0:
        with open(FILE_MASTER_LOG, "a", encoding="utf-8") as fh:
            fh.write(MASTER_HEADER)


def append_entries_to_master(entries):
    """Append one or more chronological record lines to panchayat_records.txt."""
    if not entries:
        return 0
    ensure_master_log()
    written = 0
    with open(FILE_MASTER_LOG, "a", encoding="utf-8") as fh:
        for item in entries:
            ts = sanitize_pipes(item.get("timestamp") or now_ts())
            src = sanitize_pipes(item.get("source") or "WEB")
            rtype = sanitize_pipes(item.get("recordType") or "RECORD")
            raw_id = item.get("refId", 0)
            actor = sanitize_pipes(item.get("actor") or "System")
            details = sanitize_pipes(item.get("details") or "")
            fh.write(
                f"{ts} | {src:<8} | {rtype:<18} | #{str(raw_id):<6} | {actor:<18} | {details}\n"
            )
            written += 1
    return written


def load_folder_state():
    """Parse C-compatible pipe-delimited .txt files from the project folder."""
    citizens = []
    next_citizen_id = 1
    if os.path.exists(FILE_CITIZENS):
        with open(FILE_CITIZENS, "r", encoding="utf-8", errors="replace") as fh:
            for line in fh:
                s = line.rstrip("\r\n")
                if not s or s.startswith("#"):
                    if s.startswith("# NEXT_CITIZEN_ID:"):
                        try:
                            next_citizen_id = int(s.split(":", 1)[1].strip())
                        except ValueError:
                            pass
                    continue
                parts = s.split("|")
                if len(parts) >= 5:
                    citizens.append({
                        "id": int(parts[0]),
                        "aadhaar": parts[1],
                        "phone": parts[2],
                        "active": int(parts[3]) != 0,
                        "name": parts[4],
                    })
                    next_citizen_id = max(next_citizen_id, int(parts[0]) + 1)

    officials = []
    next_official_id = 101
    if os.path.exists(FILE_OFFICIALS):
        with open(FILE_OFFICIALS, "r", encoding="utf-8", errors="replace") as fh:
            for line in fh:
                s = line.rstrip("\r\n")
                if not s or s.startswith("#"):
                    if s.startswith("# NEXT_OFFICIAL_ID:"):
                        try:
                            next_official_id = int(s.split(":", 1)[1].strip())
                        except ValueError:
                            pass
                    continue
                parts = s.split("|")
                if len(parts) >= 6:
                    officials.append({
                        "id": int(parts[0]),
                        "username": parts[1],
                        "password": parts[2],
                        "dept": int(parts[3]),
                        "active": int(parts[4]) != 0,
                        "name": parts[5],
                    })
                    next_official_id = max(next_official_id, int(parts[0]) + 1)

    grievances = []
    next_ticket_id = 1001
    if os.path.exists(FILE_GRIEVANCES):
        with open(FILE_GRIEVANCES, "r", encoding="utf-8", errors="replace") as fh:
            for line in fh:
                s = line.rstrip("\r\n")
                if not s or s.startswith("#"):
                    if s.startswith("# NEXT_TICKET_ID:"):
                        try:
                            next_ticket_id = int(s.split(":", 1)[1].strip())
                        except ValueError:
                            pass
                    continue
                parts = s.split("|")
                if len(parts) >= 13:
                    tid = int(parts[0])
                    grievances.append({
                        "ticket_id": tid,
                        "citizen_id": int(parts[1]),
                        "village_id": int(parts[2]),
                        "dept_id": int(parts[3]),
                        "is_hazard": int(parts[4]) != 0,
                        "priority": int(parts[5]),
                        "days_pending": int(parts[6]),
                        "assigned_official_id": int(parts[7]),
                        "status": parts[8],
                        "submitted_at": parts[9],
                        "resolved_at": parts[10],
                        "rating": int(parts[11]),
                        "description": parts[12],
                        "remarks": parts[13] if len(parts) > 13 else "",
                        "feedback": parts[14] if len(parts) > 14 else "",
                    })
                    next_ticket_id = max(next_ticket_id, tid + 1)

    history = []
    if os.path.exists(FILE_HISTORY):
        with open(FILE_HISTORY, "r", encoding="utf-8", errors="replace") as fh:
            for line in fh:
                s = line.rstrip("\r\n")
                if not s or s.startswith("#"):
                    continue
                parts = s.split("|")
                if len(parts) >= 4:
                    history.append({
                        "ticket_id": int(parts[0]),
                        "action": parts[1],
                        "actor": parts[2],
                        "timestamp": parts[3],
                    })

    ensure_master_log()
    master_lines = 0
    if os.path.exists(FILE_MASTER_LOG):
        with open(FILE_MASTER_LOG, "r", encoding="utf-8", errors="replace") as fh:
            master_lines = sum(1 for ln in fh if ln.strip() and not ln.startswith("#"))

    return {
        "connected": True,
        "hasData": len(grievances) > 0 or len(citizens) > 0,
        "citizens": citizens,
        "officials": officials,
        "grievances": grievances,
        "history": history,
        "nextTicketId": next_ticket_id,
        "nextCitizenId": next_citizen_id,
        "nextOfficialId": next_official_id,
        "masterEntriesCount": master_lines,
    }


def write_folder_snapshot(snapshot):
    """Sync C-compatible pipe-delimited .txt files without touching master log."""
    citizens = snapshot.get("citizens", [])
    officials = snapshot.get("officials", [])
    grievances = snapshot.get("grievances", [])
    history = snapshot.get("history", [])
    next_cit = int(snapshot.get("nextCitizenId", len(citizens) + 1))
    next_off = int(snapshot.get("nextOfficialId", 101 + len(officials)))
    next_tkt = int(snapshot.get("nextTicketId", 1001 + len(grievances)))

    with open(FILE_CITIZENS, "w", encoding="utf-8") as fh:
        fh.write("# CITIZEN REGISTRY\n")
        fh.write(f"# NEXT_CITIZEN_ID: {next_cit}\n")
        fh.write("# citizen_id|aadhaar|phone|active|name\n")
        for c in citizens:
            act = 1 if c.get("active", True) is not False else 0
            fh.write(
                f"{int(c['id'])}|{sanitize_pipes(c['aadhaar'])}|"
                f"{sanitize_pipes(c['phone'])}|{act}|{sanitize_pipes(c['name'])}\n"
            )

    with open(FILE_OFFICIALS, "w", encoding="utf-8") as fh:
        fh.write("# OFFICIAL ACCOUNTS REGISTRY\n")
        fh.write(f"# NEXT_OFFICIAL_ID: {next_off}\n")
        fh.write("# official_id|username|password|dept_assigned|active|name\n")
        for o in officials:
            dept = -1 if o.get("dept") is None else int(o["dept"])
            act = 1 if o.get("active", True) else 0
            fh.write(
                f"{int(o['id'])}|{sanitize_pipes(o['username'])}|"
                f"{sanitize_pipes(o['password'])}|{dept}|{act}|"
                f"{sanitize_pipes(o['name'])}\n"
            )

    with open(FILE_GRIEVANCES, "w", encoding="utf-8") as fh:
        fh.write("# GRIEVANCES DATABASE\n")
        fh.write(f"# NEXT_TICKET_ID: {next_tkt}\n")
        fh.write(
            "# ticket_id|citizen_id|village_id|dept_id|is_hazard|priority|"
            "days_pending|assigned_official_id|status|submitted_at|resolved_at|"
            "rating|description|remarks|feedback\n"
        )
        for g in grievances:
            haz = 1 if g.get("is_hazard") else 0
            aoid = g.get("assigned_official_id", -1)
            aoid = -1 if aoid is None else int(aoid)
            fh.write(
                f"{int(g['ticket_id'])}|{int(g['citizen_id'])}|{int(g['village_id'])}|"
                f"{int(g['dept_id'])}|{haz}|{int(g['priority'])}|{int(g['days_pending'])}|"
                f"{aoid}|{sanitize_pipes(g['status'])}|"
                f"{sanitize_pipes(g.get('submitted_at', ''))}|"
                f"{sanitize_pipes(g.get('resolved_at', ''))}|{int(g.get('rating', 0))}|"
                f"{sanitize_pipes(g.get('description', ''))}|"
                f"{sanitize_pipes(g.get('remarks', ''))}|"
                f"{sanitize_pipes(g.get('feedback', ''))}\n"
            )

    with open(FILE_HISTORY, "w", encoding="utf-8") as fh:
        fh.write("# ACTION / AUDIT HISTORY LOG (Most recent on top)\n")
        fh.write("# ticket_id|action|actor|timestamp\n")
        for h in history:
            fh.write(
                f"{int(h['ticket_id'])}|{sanitize_pipes(h['action'])}|"
                f"{sanitize_pipes(h['actor'])}|{sanitize_pipes(h.get('timestamp', ''))}\n"
            )


class PortalHandler(BaseHTTPRequestHandler):
    """HTTP request handler serving index.html and folder append/sync APIs."""

    server_version = "RuralPortalLocal/1.0"

    def _send_cors_headers(self):
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "Content-Type")
        self.send_header("Access-Control-Allow-Private-Network", "true")
        self.send_header("Cache-Control", "no-store")

    def _send_json(self, code, payload):
        body = json.dumps(payload).encode("utf-8")
        self.send_response(code)
        self._send_cors_headers()
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_OPTIONS(self):
        self.send_response(204)
        self._send_cors_headers()
        self.end_headers()

    def do_GET(self):
        path = self.path.split("?", 1)[0]
        static_routes = {
            "/": ("index.html", "text/html; charset=utf-8"),
            "/index.html": ("index.html", "text/html; charset=utf-8"),
            "/styles.css": ("styles.css", "text/css; charset=utf-8"),
            "/app.js": ("app.js", "application/javascript; charset=utf-8"),
        }
        if path in static_routes:
            fname, mime = static_routes[path]
            fpath = os.path.join(BASE_DIR, fname)
            if not os.path.exists(fpath):
                self._send_json(404, {"error": f"{fname} not found"})
                return
            with open(fpath, "rb") as fh:
                data = fh.read()
            self.send_response(200)
            self._send_cors_headers()
            self.send_header("Content-Type", mime)
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            self.wfile.write(data)
            return

        if path == "/api/state":
            with IO_LOCK:
                state = load_folder_state()
            self._send_json(200, state)
            return

        self._send_json(404, {"error": "Not found"})

    def do_POST(self):
        path = self.path.split("?", 1)[0]
        if path != "/api/append":
            self._send_json(404, {"error": "Not found"})
            return

        try:
            length = int(self.headers.get("Content-Length", "0"))
            raw = self.rfile.read(length)
            payload = json.loads(raw.decode("utf-8"))
        except (ValueError, UnicodeDecodeError, json.JSONDecodeError) as exc:
            self._send_json(400, {"error": str(exc)})
            return

        entries = payload.get("appendEntries") or []
        snapshot = payload.get("snapshot")

        with IO_LOCK:
            appended = append_entries_to_master(entries)
            if snapshot and not payload.get("appendOnly"):
                write_folder_snapshot(snapshot)
            master_lines = 0
            if os.path.exists(FILE_MASTER_LOG):
                with open(FILE_MASTER_LOG, "r", encoding="utf-8", errors="replace") as fh:
                    master_lines = sum(
                        1 for ln in fh if ln.strip() and not ln.startswith("#")
                    )

        self._send_json(
            200,
            {"ok": True, "appended": appended, "masterEntriesCount": master_lines},
        )

    def log_message(self, format, *args):
        """Suppress noisy per-request stderr logging."""
        return


class LocalServer(ThreadingHTTPServer):
    allow_reuse_address = True


def main():
    with IO_LOCK:
        ensure_master_log()
    try:
        httpd = LocalServer((HOST, PORT), PortalHandler)
    except OSError:
        print(f"Rural Portal local bridge already running at http://{HOST}:{PORT}/")
        return

    print(f"Rural Portal Local Folder Sync running at http://{HOST}:{PORT}/")
    print(f"Records directory: {BASE_DIR}")
    print(f"Appendable ledger: {os.path.basename(FILE_MASTER_LOG)}")
    print("Press Ctrl+C to stop.")
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        print("\nShutting down local folder bridge cleanly.")
    finally:
        httpd.server_close()


if __name__ == "__main__":
    main()
