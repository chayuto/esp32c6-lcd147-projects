#!/usr/bin/env python3
"""
MCP Server Display — integration test script
Usage:
    python3 test_mcp.py                    # auto-detect via esp32-canvas.local
    python3 test_mcp.py 192.168.1.110      # explicit IP
    python3 test_mcp.py --save-snapshots   # save JPEG snapshots to ./snapshots/

Requires: pip install requests
"""

import sys
import os
import time
import json
import base64
import math
import argparse
import requests

# ── Config ────────────────────────────────────────────────────────────────────

DEFAULT_HOST = "esp32-canvas.local"
TIMEOUT      = 8   # seconds per request
SNAP_DELAY   = 0.3 # seconds to wait after draw commands before snapshot

PASS  = "\033[32mPASS\033[0m"
FAIL  = "\033[31mFAIL\033[0m"
SKIP  = "\033[33mSKIP\033[0m"
BOLD  = "\033[1m"
RESET = "\033[0m"

# ── Helpers ───────────────────────────────────────────────────────────────────

_id_counter = 0

def next_id():
    global _id_counter
    _id_counter += 1
    return _id_counter

def mcp_call(base_url, method, params=None):
    payload = {"jsonrpc": "2.0", "id": next_id(), "method": method}
    if params:
        payload["params"] = params
    r = requests.post(f"{base_url}/mcp", json=payload, timeout=TIMEOUT)
    r.raise_for_status()
    return r.json()

def tool_call(base_url, name, arguments=None):
    return mcp_call(base_url, "tools/call", {
        "name": name,
        "arguments": arguments or {}
    })

def assert_result(resp, label):
    if "error" in resp:
        print(f"  {FAIL}  {label}: JSON-RPC error {resp['error']}")
        return False
    if "result" not in resp:
        print(f"  {FAIL}  {label}: no result field")
        return False
    result = resp["result"]
    if result.get("isError"):
        text = result.get("content", [{}])[0].get("text", "")
        print(f"  {FAIL}  {label}: tool error — {text}")
        return False
    print(f"  {PASS}  {label}")
    return True

def assert_error(resp, label):
    """Expect a failure (bad args) — pass if error present, fail if success."""
    if "error" in resp or (resp.get("result", {}).get("isError")):
        print(f"  {PASS}  {label} (correctly rejected)")
        return True
    print(f"  {FAIL}  {label}: expected error, got success")
    return False

def save_snapshot(base_url, filename, save_dir):
    """Download /snapshot.jpg and save to save_dir/filename."""
    r = requests.get(f"{base_url}/snapshot.jpg", timeout=TIMEOUT + 5)
    if r.status_code != 200 or r.headers.get("content-type") != "image/jpeg":
        return False
    os.makedirs(save_dir, exist_ok=True)
    path = os.path.join(save_dir, filename)
    with open(path, "wb") as f:
        f.write(r.content)
    print(f"         snapshot saved → {path} ({len(r.content)} bytes)")
    return True

# ── Test suites ───────────────────────────────────────────────────────────────

def test_health(base_url):
    print(f"\n{BOLD}[ Health ]{RESET}")
    r = requests.get(f"{base_url}/ping", timeout=TIMEOUT)
    r.raise_for_status()
    data = r.json()
    ok = data.get("status") == "healthy"
    print(f"  {PASS if ok else FAIL}  GET /ping — {data}")
    return ok


def test_initialize(base_url):
    print(f"\n{BOLD}[ MCP Handshake ]{RESET}")
    resp = mcp_call(base_url, "initialize", {
        "protocolVersion": "2024-11-05",
        "clientInfo": {"name": "test_mcp.py", "version": "1.0"}
    })
    ok = assert_result(resp, "initialize")
    if ok:
        info = resp["result"].get("serverInfo", {})
        print(f"         server: {info.get('name')} v{info.get('version')}")
        caps = resp["result"].get("capabilities", {})
        print(f"         capabilities: {list(caps.keys())}")
    return ok


def test_tools_list(base_url):
    print(f"\n{BOLD}[ tools/list ]{RESET}")
    resp = mcp_call(base_url, "tools/list")
    if not assert_result(resp, "tools/list"):
        return False

    tools = resp["result"].get("tools", [])
    names = [t["name"] for t in tools]
    expected = [
        "clear_canvas", "draw_rect", "draw_line", "draw_arc",
        "draw_text", "draw_path", "get_canvas_info", "get_canvas_snapshot"
    ]
    all_present = True
    for name in expected:
        present = name in names
        print(f"  {'  ok' if present else FAIL}  tool: {name}")
        if not present:
            all_present = False

    print(f"         total tools: {len(tools)} (budget max: 8)")
    if len(tools) > 8:
        print(f"  {FAIL}  tool count {len(tools)} exceeds 8-tool budget")
        all_present = False

    return all_present


def test_canvas_info(base_url):
    print(f"\n{BOLD}[ get_canvas_info ]{RESET}")
    resp = tool_call(base_url, "get_canvas_info")
    ok = assert_result(resp, "get_canvas_info")
    if ok:
        text = resp["result"]["content"][0]["text"]
        print(f"         {text}")
        # Verify screen dimensions are mentioned
        if "172" not in text or "320" not in text:
            print(f"  {FAIL}  expected 172x320 in info response")
            return False
    return ok


def test_drawing_primitives(base_url, save_dir):
    print(f"\n{BOLD}[ Drawing Primitives ]{RESET}")
    passed = 0
    total  = 0

    def run(label, name, args):
        nonlocal passed, total
        total += 1
        resp = tool_call(base_url, name, args)
        if assert_result(resp, label):
            passed += 1

    # clear_canvas
    run("clear_canvas (black)",
        "clear_canvas", {"r": 0, "g": 0, "b": 0})
    run("clear_canvas (dark blue)",
        "clear_canvas", {"r": 0, "g": 0, "b": 40})

    # draw_rect — filled
    run("draw_rect filled, no radius",
        "draw_rect", {"x": 10, "y": 10, "w": 80, "h": 50, "r": 220, "g": 50, "b": 50, "filled": True, "radius": 0})
    # draw_rect — outline with radius
    run("draw_rect outline, radius=8",
        "draw_rect", {"x": 100, "y": 10, "w": 60, "h": 50, "r": 50, "g": 200, "b": 50, "filled": False, "radius": 8})

    # draw_line
    run("draw_line width=1",
        "draw_line", {"x1": 5, "y1": 80, "x2": 165, "y2": 80, "r": 255, "g": 255, "b": 0, "width": 1})
    run("draw_line width=3",
        "draw_line", {"x1": 5, "y1": 90, "x2": 165, "y2": 90, "r": 255, "g": 165, "b": 0, "width": 3})

    # draw_arc — partial
    run("draw_arc partial (0-90 deg)",
        "draw_arc", {"cx": 86, "cy": 130, "radius": 40, "start_angle": 0, "end_angle": 90,
                     "r": 100, "g": 180, "b": 255, "width": 3})
    # draw_arc — full circle
    run("draw_arc full circle",
        "draw_arc", {"cx": 86, "cy": 200, "radius": 25, "start_angle": 0, "end_angle": 360,
                     "r": 200, "g": 100, "b": 255, "width": 2})

    # draw_text — font 14
    run("draw_text font_size=14",
        "draw_text", {"x": 4, "y": 250, "r": 255, "g": 255, "b": 255, "font_size": 14,
                      "text": "MCP Server Display"})
    # draw_text — font 20
    run("draw_text font_size=20",
        "draw_text", {"x": 4, "y": 270, "r": 0, "g": 220, "b": 180, "font_size": 20,
                      "text": "All systems go"})

    # draw_path — polyline (open)
    run("draw_path polyline (open)",
        "draw_path", {"points": [{"x": 10, "y": 300}, {"x": 50, "y": 290},
                                  {"x": 90, "y": 305}, {"x": 130, "y": 295}],
                      "closed": False, "filled": False, "r": 255, "g": 100, "b": 100, "width": 2})
    # draw_path — polygon (closed + filled)
    run("draw_path polygon (closed, filled)",
        "draw_path", {"points": [{"x": 140, "y": 290}, {"x": 165, "y": 310}, {"x": 140, "y": 318}],
                      "closed": True, "filled": True, "r": 255, "g": 200, "b": 50, "width": 1})

    print(f"         primitives: {passed}/{total} passed")

    # Snapshot of the test scene
    time.sleep(SNAP_DELAY)
    if save_dir:
        save_snapshot(base_url, "primitives_scene.jpg", save_dir)

    return passed == total


def test_error_handling(base_url):
    print(f"\n{BOLD}[ Error Handling ]{RESET}")
    passed = 0
    total  = 0

    def run_err(label, name, args):
        nonlocal passed, total
        total += 1
        resp = tool_call(base_url, name, args)
        if assert_error(resp, label):
            passed += 1

    run_err("draw_rect: x out of range (x=200)",
            "draw_rect", {"x": 200, "y": 10, "w": 20, "h": 20})
    run_err("draw_text: invalid font_size=16",
            "draw_text", {"x": 0, "y": 0, "font_size": 16, "text": "hi"})
    run_err("draw_path: too many points (9)",
            "draw_path", {"points": [{"x": i, "y": i} for i in range(9)],
                          "closed": False, "filled": False})
    run_err("draw_path: missing points",
            "draw_path", {"closed": False, "filled": False})
    run_err("tools/call: unknown tool",
            "nonexistent_tool", {})

    # Unknown method
    total += 1
    resp = mcp_call(base_url, "unknown/method")
    if "error" in resp:
        print(f"  {PASS}  unknown method returns JSON-RPC error")
        passed += 1
    else:
        print(f"  {FAIL}  unknown method: expected error")

    print(f"         error handling: {passed}/{total} passed")
    return passed == total


def test_snapshot(base_url, save_dir):
    print(f"\n{BOLD}[ Snapshot ]{RESET}")

    # Draw a clean reference scene first
    tool_call(base_url, "clear_canvas", {"r": 10, "g": 10, "b": 30})
    tool_call(base_url, "draw_rect",   {"x": 20, "y": 60, "w": 130, "h": 60,
                                        "r": 0, "g": 180, "b": 100, "filled": True, "radius": 6})
    tool_call(base_url, "draw_text",   {"x": 26, "y": 82, "r": 255, "g": 255, "b": 255,
                                        "font_size": 20, "text": "Snapshot OK"})
    time.sleep(SNAP_DELAY)

    passed = 0

    # Test via MCP get_canvas_snapshot (returns base64 image in content)
    resp = tool_call(base_url, "get_canvas_snapshot")
    ok = assert_result(resp, "get_canvas_snapshot (MCP)")
    if ok:
        content = resp["result"].get("content", [{}])
        img = next((c for c in content if c.get("type") == "image"), None)
        if img and img.get("mimeType") == "image/jpeg" and img.get("data"):
            raw = base64.b64decode(img["data"])
            print(f"         MCP image: {len(raw)} bytes JPEG, mimeType={img['mimeType']}")
            if save_dir:
                os.makedirs(save_dir, exist_ok=True)
                path = os.path.join(save_dir, "mcp_snapshot.jpg")
                with open(path, "wb") as f:
                    f.write(raw)
                print(f"         saved → {path}")
            passed += 1
        else:
            print(f"  {FAIL}  get_canvas_snapshot: image content item missing or malformed")

    # Test via GET /snapshot.jpg (binary JPEG)
    r = requests.get(f"{base_url}/snapshot.jpg", timeout=TIMEOUT + 5)
    jpeg_ok = (r.status_code == 200
               and r.headers.get("content-type") == "image/jpeg"
               and len(r.content) > 500)
    if jpeg_ok:
        print(f"  {PASS}  GET /snapshot.jpg ({len(r.content)} bytes)")
        if save_dir:
            path = os.path.join(save_dir, "http_snapshot.jpg")
            os.makedirs(save_dir, exist_ok=True)
            with open(path, "wb") as f:
                f.write(r.content)
            print(f"         saved → {path}")
        passed += 1
    else:
        print(f"  {FAIL}  GET /snapshot.jpg: status={r.status_code} size={len(r.content)}")

    return passed == 2


# ── Showcase scene ────────────────────────────────────────────────────────────

def draw_showcase(base_url, save_dir):
    """
    Draw a sci-fi HUD / radar display that exercises every primitive.
    Designed to look impressive in the 86x160 snapshot.

    Layout (172x320 canvas):
      Y  0- 28  Header bar + title
      Y 28- 56  Subtitle + accent lines
      Y 56-210  Radar HUD (centre 86,140): rings, crosshairs, sweep arc, blips
      Y210-290  Three status bars (SIG / PWR / UPL)
      Y290-320  Footer bar
    """
    print(f"\n{BOLD}[ Showcase — MCP HUD Display ]{RESET}")

    def D(name, args):
        return tool_call(base_url, name, args)

    # ── Background ──────────────────────────────────────────────────────────
    D("clear_canvas", {"r": 4, "g": 6, "b": 18})

    # ── Header bar ──────────────────────────────────────────────────────────
    D("draw_rect",  {"x": 0, "y": 0, "w": 172, "h": 28,
                     "r": 0, "g": 90, "b": 160, "filled": True, "radius": 0})
    D("draw_text",  {"x": 6, "y": 4,
                     "r": 255, "g": 255, "b": 255, "font_size": 20, "text": "ESP32-C6"})
    # Cyan accent lines under header
    D("draw_line",  {"x1": 0,  "y1": 28, "x2": 172, "y2": 28,
                     "r": 0, "g": 210, "b": 255, "width": 2})
    D("draw_line",  {"x1": 0,  "y1": 31, "x2": 172, "y2": 31,
                     "r": 0, "g": 60,  "b": 100, "width": 1})

    # ── Subtitle ─────────────────────────────────────────────────────────────
    D("draw_text",  {"x": 6, "y": 36,
                     "r": 0, "g": 210, "b": 255, "font_size": 14,
                     "text": "MCP CANVAS SERVER"})

    # ── Radar HUD ─────────────────────────────────────────────────────────────
    cx, cy = 86, 140

    # Dim crosshair grid
    D("draw_line", {"x1": cx-65, "y1": cy, "x2": cx+65, "y2": cy,
                    "r": 18, "g": 28, "b": 48, "width": 1})
    D("draw_line", {"x1": cx, "y1": cy-65, "x2": cx, "y2": cy+65,
                    "r": 18, "g": 28, "b": 48, "width": 1})
    # Diagonal grid lines via draw_path polyline
    D("draw_path", {"points": [{"x": cx-46, "y": cy-46}, {"x": cx+46, "y": cy+46}],
                    "closed": False, "filled": False, "r": 18, "g": 28, "b": 48, "width": 1})
    D("draw_path", {"points": [{"x": cx+46, "y": cy-46}, {"x": cx-46, "y": cy+46}],
                    "closed": False, "filled": False, "r": 18, "g": 28, "b": 48, "width": 1})

    # Concentric range rings (dim blue-grey)
    for r in [60, 45, 28, 12]:
        D("draw_arc", {"cx": cx, "cy": cy, "radius": r,
                       "start_angle": 0, "end_angle": 360,
                       "r": 20, "g": 48, "b": 78, "width": 1})

    # Sweep arc — bright green, 0-115 degrees clockwise from 3-o'clock
    D("draw_arc", {"cx": cx, "cy": cy, "radius": 60,
                   "start_angle": 0, "end_angle": 115,
                   "r": 0, "g": 230, "b": 80, "width": 3})
    D("draw_arc", {"cx": cx, "cy": cy, "radius": 45,
                   "start_angle": 0, "end_angle": 115,
                   "r": 0, "g": 170, "b": 60, "width": 2})

    # Sweep leading-edge radial line
    a = math.radians(115)
    x2 = cx + int(60 * math.cos(a))
    y2 = cy + int(60 * math.sin(a))
    D("draw_line", {"x1": cx, "y1": cy, "x2": x2, "y2": y2,
                    "r": 0, "g": 230, "b": 80, "width": 2})

    # Radar blips — filled 4×4 and 3×3 squares
    for bx, by, br, bg, bb in [
        (cx+14, cy-22, 0, 255,  80),   # contact 1 — bright green
        (cx-28, cy+8,  0, 255,  80),   # contact 2 — bright green
        (cx+32, cy+18, 0, 160,  50),   # contact 3 — dim green (far)
        (cx-6,  cy-40, 0, 200, 255),   # contact 4 — cyan (different type)
    ]:
        D("draw_rect", {"x": bx-2, "y": by-2, "w": 4, "h": 4,
                        "r": br, "g": bg, "b": bb, "filled": True, "radius": 0})

    # Centre pip
    D("draw_rect", {"x": cx-2, "y": cy-2, "w": 5, "h": 5,
                    "r": 0, "g": 230, "b": 80, "filled": True, "radius": 0})

    # ── Status bars ───────────────────────────────────────────────────────────
    D("draw_line", {"x1": 4, "y1": 213, "x2": 168, "y2": 213,
                    "r": 0, "g": 60, "b": 100, "width": 1})

    bars = [
        ("SIG", 80,  0, 210,  80),   # signal  — green
        ("PWR", 65, 230, 130,   0),   # power   — orange
        ("UPL", 42,  0, 180, 230),   # uplink  — cyan
    ]
    for i, (label, pct, br, bg, bb) in enumerate(bars):
        y = 218 + i * 22
        D("draw_text", {"x": 4, "y": y, "r": 90, "g": 100, "b": 120,
                        "font_size": 14, "text": label})
        bar_w = 118
        filled_w = int(bar_w * pct / 100)
        D("draw_rect", {"x": 38, "y": y + 2, "w": bar_w, "h": 9,
                        "r": 14, "g": 18, "b": 32, "filled": True, "radius": 0})
        D("draw_rect", {"x": 38, "y": y + 2, "w": filled_w, "h": 9,
                        "r": br, "g": bg, "b": bb, "filled": True, "radius": 0})

    # ── Footer ────────────────────────────────────────────────────────────────
    D("draw_line",  {"x1": 0, "y1": 286, "x2": 172, "y2": 286,
                     "r": 0, "g": 60, "b": 100, "width": 1})
    D("draw_rect",  {"x": 0, "y": 288, "w": 172, "h": 32,
                     "r": 0, "g": 55, "b": 100, "filled": True, "radius": 0})
    D("draw_text",  {"x": 12, "y": 293,
                     "r": 0, "g": 210, "b": 255, "font_size": 14,
                     "text": "DRAWN VIA MCP"})

    time.sleep(SNAP_DELAY)

    if save_dir:
        ok = save_snapshot(base_url, "showcase.jpg", save_dir)
        print(f"  {PASS if ok else FAIL}  showcase snapshot")
        return ok
    print(f"  {PASS}  showcase drawn (run with --save-snapshots to capture)")
    return True


# ── Entry point ───────────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(description="MCP Server Display integration tests")
    parser.add_argument("host", nargs="?", default=DEFAULT_HOST,
                        help=f"IP or hostname (default: {DEFAULT_HOST})")
    parser.add_argument("--save-snapshots", action="store_true",
                        help="Save JPEG snapshots to ./snapshots/")
    args = parser.parse_args()

    base_url  = f"http://{args.host}"
    save_dir  = "snapshots" if args.save_snapshots else None

    print(f"\n{BOLD}MCP Server Display — Integration Tests{RESET}")
    print(f"Target: {base_url}")
    if save_dir:
        print(f"Snapshots: ./{save_dir}/")

    # Quick reachability check
    try:
        requests.get(f"{base_url}/ping", timeout=3)
    except Exception as e:
        print(f"\n{FAIL}  Cannot reach {base_url}: {e}")
        print("  Is the board powered and connected to Wi-Fi?")
        sys.exit(1)

    results = {}
    try:
        results["health"]      = test_health(base_url)
        results["initialize"]  = test_initialize(base_url)
        results["tools/list"]  = test_tools_list(base_url)
        results["canvas_info"] = test_canvas_info(base_url)
        results["primitives"]  = test_drawing_primitives(base_url, save_dir)
        results["errors"]      = test_error_handling(base_url)
        results["snapshot"]    = test_snapshot(base_url, save_dir)
        results["showcase"]    = draw_showcase(base_url, save_dir)
    except requests.exceptions.ConnectionError as e:
        print(f"\n{FAIL}  Connection lost mid-test: {e}")
        sys.exit(1)

    # Summary
    passed = sum(1 for v in results.values() if v)
    total  = len(results)
    print(f"\n{BOLD}{'─' * 40}{RESET}")
    for name, ok in results.items():
        icon = PASS if ok else FAIL
        print(f"  {icon}  {name}")
    print(f"{BOLD}{'─' * 40}{RESET}")
    status = PASS if passed == total else FAIL
    print(f"  {status}  {passed}/{total} suites passed\n")

    sys.exit(0 if passed == total else 1)


if __name__ == "__main__":
    main()
