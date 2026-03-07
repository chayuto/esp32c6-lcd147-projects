#!/usr/bin/env python3
"""
MCP GPIO Server — integration test script
Usage:
    python3 test_mcp.py                   # auto-detect via esp32-gpio.local
    python3 test_mcp.py 192.168.1.110     # explicit IP

Tests every tool in the 6-tool set:
  get_gpio_capabilities, configure_pins, write_digital_pins,
  read_pins, set_pwm_duty, i2c_scan

No external hardware needed — tests use pull-ups, floating ADC reads,
and PWM duty checks against the state table.

Requires: pip install requests
"""

import sys
import json
import time
import argparse
import requests

# ── Config ─────────────────────────────────────────────────────────────────────

DEFAULT_HOST = "esp32-gpio.local"
TIMEOUT      = 10   # seconds per request

PASS  = "\033[32mPASS\033[0m"
FAIL  = "\033[31mFAIL\033[0m"
BOLD  = "\033[1m"
RESET = "\033[0m"
DIM   = "\033[2m"

# Pins verified safe on Waveshare ESP32-C6-LCD-1.47
SAFE_PINS     = [0, 1, 2, 3, 18, 19, 20, 23]
ADC_PINS      = [0, 1, 2, 3]
DIGITAL_PINS  = [18, 19, 20, 23]   # no ADC
EXPECTED_TOOLS = [
    "get_gpio_capabilities",
    "configure_pins",
    "write_digital_pins",
    "read_pins",
    "set_pwm_duty",
    "i2c_scan",
]

# ── MCP helpers ────────────────────────────────────────────────────────────────

_id = 0

def next_id():
    global _id
    _id += 1
    return _id

def mcp_call(url, method, params=None):
    payload = {"jsonrpc": "2.0", "id": next_id(), "method": method}
    if params:
        payload["params"] = params
    r = requests.post(f"{url}/mcp", json=payload, timeout=TIMEOUT)
    r.raise_for_status()
    return r.json()

def tool_call(url, name, arguments=None):
    return mcp_call(url, "tools/call", {"name": name, "arguments": arguments or {}})

def get_text(resp):
    """Extract the text string from an MCP tool result content array."""
    try:
        return resp["result"]["content"][0]["text"]
    except (KeyError, IndexError, TypeError):
        return ""

def is_tool_error(resp):
    return (resp.get("result", {}).get("isError") is True or
            "error" in resp)

def assert_ok(resp, label):
    if is_tool_error(resp):
        text = get_text(resp) or str(resp.get("error", ""))
        print(f"  {FAIL}  {label}")
        print(f"         {DIM}{text[:120]}{RESET}")
        return False
    print(f"  {PASS}  {label}")
    return True

def assert_err(resp, label):
    """Expect the call to be rejected — pass if it is."""
    if is_tool_error(resp):
        print(f"  {PASS}  {label} {DIM}(correctly rejected){RESET}")
        return True
    print(f"  {FAIL}  {label} — expected rejection, got success")
    return False

# ── Test suites ────────────────────────────────────────────────────────────────

def test_health(url):
    print(f"\n{BOLD}[ Health ]{RESET}")
    r = requests.get(f"{url}/ping", timeout=TIMEOUT)
    r.raise_for_status()
    data = r.json()
    ok = data.get("status") == "healthy"
    print(f"  {PASS if ok else FAIL}  GET /ping — {data}")
    return ok


def test_initialize(url):
    print(f"\n{BOLD}[ MCP Handshake ]{RESET}")
    resp = mcp_call(url, "initialize", {
        "protocolVersion": "2024-11-05",
        "clientInfo": {"name": "test_mcp.py", "version": "1.0"}
    })
    if "error" in resp:
        print(f"  {FAIL}  initialize: {resp['error']}")
        return False
    info = resp.get("result", {}).get("serverInfo", {})
    print(f"  {PASS}  initialize — server: {info.get('name')} v{info.get('version')}")
    return True


def test_tools_list(url):
    print(f"\n{BOLD}[ tools/list ]{RESET}")
    resp = mcp_call(url, "tools/list")
    if "error" in resp:
        print(f"  {FAIL}  tools/list: {resp['error']}")
        return False

    tools = resp.get("result", {}).get("tools", [])
    names = {t["name"] for t in tools}
    all_ok = True
    for name in EXPECTED_TOOLS:
        ok = name in names
        print(f"  {'  ok' if ok else FAIL}  {name}")
        if not ok:
            all_ok = False

    extra = names - set(EXPECTED_TOOLS)
    if extra:
        print(f"         extra tools: {sorted(extra)}")

    print(f"         {len(tools)} tools total (budget max: 8)")
    if len(tools) > 8:
        print(f"  {FAIL}  tool budget exceeded ({len(tools)} > 8)")
        all_ok = False

    # Spot-check that configure_pins schema contains gpio enum with our safe pins
    cfg_tool = next((t for t in tools if t["name"] == "configure_pins"), None)
    if cfg_tool:
        schema_str = json.dumps(cfg_tool.get("inputSchema", {}))
        for p in SAFE_PINS:
            if str(p) not in schema_str:
                print(f"  {FAIL}  configure_pins schema missing GPIO {p} in enum")
                all_ok = False
                break
        else:
            print(f"         configure_pins gpio enum: all {len(SAFE_PINS)} safe pins present")

    return all_ok


def test_capabilities(url):
    print(f"\n{BOLD}[ get_gpio_capabilities ]{RESET}")
    resp = tool_call(url, "get_gpio_capabilities")
    if not assert_ok(resp, "get_gpio_capabilities"):
        return False

    text = get_text(resp)
    try:
        caps = json.loads(text)
    except json.JSONDecodeError:
        print(f"  {FAIL}  response is not valid JSON")
        return False

    ok = True

    # Board name present
    board = caps.get("board", "")
    print(f"         board: {board}")
    if not board:
        print(f"  {FAIL}  missing board name")
        ok = False

    # safe_pins list
    safe_pins = caps.get("safe_pins", [])
    safe_gpios = {p["gpio"] for p in safe_pins}
    for gpio in SAFE_PINS:
        if gpio not in safe_gpios:
            print(f"  {FAIL}  GPIO {gpio} missing from safe_pins")
            ok = False
    print(f"         safe_pins: {sorted(safe_gpios)}")

    # ADC capability flags
    adc_in_caps = {p["gpio"] for p in safe_pins if p.get("adc_capable")}
    for gpio in ADC_PINS:
        if gpio not in adc_in_caps:
            print(f"  {FAIL}  GPIO {gpio} should be adc_capable=true")
            ok = False
    for gpio in DIGITAL_PINS:
        if gpio in adc_in_caps:
            print(f"  {FAIL}  GPIO {gpio} should be adc_capable=false")
            ok = False
    print(f"         adc_capable pins: {sorted(adc_in_caps)}")

    # Reserved pins mention known system peripherals
    reserved = caps.get("reserved_pins", [])
    reserved_gpios = {r["gpio"] for r in reserved}
    for must_be_reserved in [6, 7, 8, 14, 15, 21, 22]:
        if must_be_reserved not in reserved_gpios:
            print(f"  {FAIL}  GPIO {must_be_reserved} should be in reserved_pins")
            ok = False
    print(f"         reserved pins: {sorted(reserved_gpios)}")

    if ok:
        print(f"  {PASS}  capabilities structure valid")
    return ok


def test_configure_and_read(url):
    print(f"\n{BOLD}[ configure_pins + read_pins ]{RESET}")
    passed = 0
    total  = 0

    def run(label, tool, args, expect_error=False):
        nonlocal passed, total
        total += 1
        resp = tool_call(url, tool, args)
        ok = assert_err(resp, label) if expect_error else assert_ok(resp, label)
        if ok:
            passed += 1
        return resp if not expect_error else None

    # Configure a mix of modes
    run("configure GPIO 0 → OUTPUT",
        "configure_pins", {"pins": [{"gpio": 0, "mode": "OUTPUT"}]})
    run("configure GPIO 1 → INPUT",
        "configure_pins", {"pins": [{"gpio": 1, "mode": "INPUT"}]})
    run("configure GPIO 2 → ADC",
        "configure_pins", {"pins": [{"gpio": 2, "mode": "ADC"}]})
    run("configure GPIO 3 → ADC",
        "configure_pins", {"pins": [{"gpio": 3, "mode": "ADC"}]})

    # Batch configure
    run("configure GPIO 19,20 → OUTPUT (batch)",
        "configure_pins", {"pins": [
            {"gpio": 19, "mode": "OUTPUT"},
            {"gpio": 20, "mode": "OUTPUT"},
        ]})

    # Error: ADC on non-ADC pin
    run("configure GPIO 18 → ADC (not capable — expect reject)",
        "configure_pins", {"pins": [{"gpio": 18, "mode": "ADC"}]}, expect_error=True)

    # Error: reserved system pin
    run("configure GPIO 6 (SPI MOSI — reserved, expect reject)",
        "configure_pins", {"pins": [{"gpio": 6, "mode": "OUTPUT"}]}, expect_error=True)

    # Error: mode typo
    run("configure GPIO 0 → ANALOG (invalid mode — expect reject)",
        "configure_pins", {"pins": [{"gpio": 0, "mode": "ANALOG"}]}, expect_error=True)

    # Read back configured pins
    resp = tool_call(url, "read_pins", {"gpios": [0, 1, 2, 3, 19, 20]})
    if assert_ok(resp, "read_pins [0,1,2,3,19,20]"):
        passed += 1
        data = json.loads(get_text(resp))
        readings = {r["gpio"]: r for r in data.get("readings", [])}
        # GPIO 1 (INPUT with pull-up) should read HIGH (1)
        if readings.get(1, {}).get("value") == 1:
            print(f"         GPIO 1 (INPUT pull-up): HIGH {DIM}✓{RESET}")
        else:
            print(f"         GPIO 1 (INPUT): value={readings.get(1, {}).get('value')} {DIM}(floating — ok){RESET}")
        # ADC reads
        for gpio in [2, 3]:
            mv = readings.get(gpio, {}).get("value")
            print(f"         GPIO {gpio} (ADC): {mv} mV")
    total += 1

    # Error: read unconfigured pin
    run("read_pins [23] (not configured — expect reject)",
        "read_pins", {"gpios": [23]}, expect_error=True)

    print(f"         {passed}/{total} passed")
    return passed == total


def test_write_digital(url):
    print(f"\n{BOLD}[ write_digital_pins ]{RESET}")
    passed = 0
    total  = 0

    def run(label, args, expect_error=False):
        nonlocal passed, total
        total += 1
        resp = tool_call(url, "write_digital_pins", args)
        ok = assert_err(resp, label) if expect_error else assert_ok(resp, label)
        if ok:
            passed += 1

    # GPIO 0 and 19,20 are OUTPUT from previous test
    run("write GPIO 0 → HIGH",
        {"pins": [{"gpio": 0, "level": 1}]})
    run("write GPIO 19 → LOW, GPIO 20 → HIGH",
        {"pins": [{"gpio": 19, "level": 0}, {"gpio": 20, "level": 1}]})

    # Verify state via read_pins
    resp = tool_call(url, "read_pins", {"gpios": [0, 19, 20]})
    total += 1
    if assert_ok(resp, "read_pins after write"):
        data = json.loads(get_text(resp))
        readings = {r["gpio"]: r for r in data.get("readings", [])}
        checks = [(0, 1), (19, 0), (20, 1)]
        all_match = True
        for gpio, expected in checks:
            actual = readings.get(gpio, {}).get("value")
            match = actual == expected
            icon = "✓" if match else "✗"
            print(f"         GPIO {gpio}: expected {expected}, got {actual} {DIM}{icon}{RESET}")
            if not match:
                all_match = False
        if all_match:
            passed += 1
            print(f"  {PASS}  write levels verified via read_pins")
        else:
            print(f"  {FAIL}  some levels did not match")

    # Error: write to INPUT pin (GPIO 1 is INPUT)
    run("write GPIO 1 (INPUT — expect reject)",
        {"pins": [{"gpio": 1, "level": 1}]}, expect_error=True)

    # Error: write to ADC pin
    run("write GPIO 2 (ADC — expect reject)",
        {"pins": [{"gpio": 2, "level": 0}]}, expect_error=True)

    print(f"         {passed}/{total} passed")
    return passed == total


def test_pwm(url):
    print(f"\n{BOLD}[ configure_pins (PWM) + set_pwm_duty ]{RESET}")
    passed = 0
    total  = 0

    def run(label, tool, args, expect_error=False):
        nonlocal passed, total
        total += 1
        resp = tool_call(url, tool, args)
        ok = assert_err(resp, label) if expect_error else assert_ok(resp, label)
        if ok:
            passed += 1
        return resp

    # Configure GPIO 18 and 23 as PWM
    run("configure GPIO 18 → PWM",
        "configure_pins", {"pins": [{"gpio": 18, "mode": "PWM"}]})
    run("configure GPIO 23 → PWM",
        "configure_pins", {"pins": [{"gpio": 23, "mode": "PWM"}]})

    # Set duty cycles
    run("set GPIO 18 → 0%  (fully off)",
        "set_pwm_duty", {"pins": [{"gpio": 18, "duty": 0}]})
    run("set GPIO 18 → 50% (half)",
        "set_pwm_duty", {"pins": [{"gpio": 18, "duty": 50}]})
    run("set GPIO 18 → 100% (fully on)",
        "set_pwm_duty", {"pins": [{"gpio": 18, "duty": 100}]})
    run("set GPIO 23 → 75%",
        "set_pwm_duty", {"pins": [{"gpio": 23, "duty": 75}]})

    # Batch update
    run("set GPIO 18 → 25%, GPIO 23 → 80% (batch)",
        "set_pwm_duty", {"pins": [
            {"gpio": 18, "duty": 25},
            {"gpio": 23, "duty": 80},
        ]})

    # Verify via read_pins
    resp = tool_call(url, "read_pins", {"gpios": [18, 23]})
    total += 1
    if assert_ok(resp, "read_pins [18,23] after PWM set"):
        data = json.loads(get_text(resp))
        readings = {r["gpio"]: r for r in data.get("readings", [])}
        ok = True
        for gpio, expected_duty in [(18, 25), (23, 80)]:
            r = readings.get(gpio, {})
            actual = r.get("value")
            mode   = r.get("mode")
            print(f"         GPIO {gpio}: mode={mode}, duty={actual}%")
            if mode != "PWM" or actual != expected_duty:
                print(f"  {FAIL}  GPIO {gpio}: expected PWM/{expected_duty}%, got {mode}/{actual}%")
                ok = False
        if ok:
            passed += 1
            print(f"  {PASS}  PWM duty verified via read_pins")

    # Error: set_pwm_duty on OUTPUT pin (GPIO 0 is OUTPUT)
    run("set_pwm_duty GPIO 0 (OUTPUT — expect reject)",
        "set_pwm_duty", {"pins": [{"gpio": 0, "duty": 50}]}, expect_error=True)

    # Error: set_pwm_duty on unconfigured pin
    run("set_pwm_duty GPIO 3 (ADC mode — expect reject)",
        "set_pwm_duty", {"pins": [{"gpio": 3, "duty": 50}]}, expect_error=True)

    # Reconfigure: PWM → OUTPUT (verifies channel is freed)
    run("reconfigure GPIO 18: PWM → OUTPUT (channel reclaim)",
        "configure_pins", {"pins": [{"gpio": 18, "mode": "OUTPUT"}]})
    run("write GPIO 18 → HIGH (now OUTPUT)",
        "write_digital_pins", {"pins": [{"gpio": 18, "level": 1}]})

    print(f"         {passed}/{total} passed")
    return passed == total


def test_i2c_scan(url):
    print(f"\n{BOLD}[ i2c_scan ]{RESET}")
    passed = 0
    total  = 0

    # Use GPIO 20 (SDA) and GPIO 23 (SCL) — both free, not configured above.
    # GPIO 23 is currently PWM; i2c_scan will take it over and reset it.
    sda, scl = 20, 23

    total += 1
    t0 = time.time()
    resp = tool_call(url, "i2c_scan", {"sda": sda, "scl": scl})
    elapsed = time.time() - t0

    if assert_ok(resp, f"i2c_scan (sda={sda}, scl={scl})"):
        passed += 1
        data = json.loads(get_text(resp))
        found = data.get("found", [])
        scanned = data.get("scanned", 0)
        print(f"         scanned: {scanned} addresses in {elapsed:.1f}s")
        if found:
            print(f"         found {len(found)} device(s):")
            for dev in found:
                print(f"           {dev['address_hex']} (dec {dev['address_dec']})")
        else:
            print(f"         no devices found (expected without hardware)")
            hint = data.get("hint", "")
            if hint:
                print(f"         {DIM}{hint}{RESET}")

        # Structural checks
        if scanned != 126:
            print(f"  {FAIL}  expected scanned=126, got {scanned}")
            passed -= 1
            total += 1

    # After scan, sda and scl pins should be UNCONFIGURED
    total += 1
    resp2 = tool_call(url, "get_gpio_capabilities")
    if assert_ok(resp2, "capabilities after i2c_scan (verify pins reset)"):
        caps = json.loads(get_text(resp2))
        pin_modes = {p["gpio"]: p["mode"] for p in caps.get("safe_pins", [])}
        if pin_modes.get(sda) == "---" and pin_modes.get(scl) == "---":
            print(f"         GPIO {sda} and {scl} correctly reset to UNCONFIGURED")
            passed += 1
        else:
            print(f"  {FAIL}  expected both pins UNCONFIGURED after scan, got "
                  f"GPIO {sda}={pin_modes.get(sda)}, GPIO {scl}={pin_modes.get(scl)}")

    # Error: sda == scl
    total += 1
    resp3 = tool_call(url, "i2c_scan", {"sda": 0, "scl": 0})
    if assert_err(resp3, "i2c_scan sda==scl (expect reject)"):
        passed += 1

    # Error: reserved pin
    total += 1
    resp4 = tool_call(url, "i2c_scan", {"sda": 6, "scl": 0})
    if assert_err(resp4, "i2c_scan reserved pin GPIO 6 (expect reject)"):
        passed += 1

    print(f"         {passed}/{total} passed")
    return passed == total


def test_error_handling(url):
    print(f"\n{BOLD}[ Protocol Error Handling ]{RESET}")
    passed = 0
    total  = 0

    def run_err(label, method, params=None):
        nonlocal passed, total
        total += 1
        resp = mcp_call(url, method, params)
        if "error" in resp:
            print(f"  {PASS}  {label} {DIM}(JSON-RPC error){RESET}")
            passed += 1
        elif is_tool_error(resp):
            print(f"  {PASS}  {label} {DIM}(tool error){RESET}")
            passed += 1
        else:
            print(f"  {FAIL}  {label} — expected error, got success")

    # Unknown method
    run_err("unknown method: gpio/blink",
            "gpio/blink")
    # tools/call with no name
    run_err("tools/call missing name",
            "tools/call", {"name": None, "arguments": {}})
    # tools/call unknown tool
    run_err("tools/call unknown tool",
            "tools/call", {"name": "gpio_self_destruct", "arguments": {}})
    # read_pins empty array
    run_err("read_pins empty gpios array",
            "tools/call", {"name": "read_pins", "arguments": {"gpios": []}})
    # write_digital_pins empty array
    run_err("write_digital_pins empty pins array",
            "tools/call", {"name": "write_digital_pins", "arguments": {"pins": []}})

    print(f"         {passed}/{total} passed")
    return passed == total


def test_demo_workflow(url):
    """
    Simulates the sequence an LLM agent would actually use:
    1. Discover capabilities
    2. Set up an LED (OUTPUT) and a light sensor (ADC)
    3. Blink the LED via write_digital_pins
    4. Add a PWM-dimmed LED
    5. Read everything back to confirm
    """
    print(f"\n{BOLD}[ Demo Workflow (LLM simulation) ]{RESET}")

    # Step 1: discover
    resp = tool_call(url, "get_gpio_capabilities")
    if not assert_ok(resp, "1. get_gpio_capabilities"):
        return False
    caps = json.loads(get_text(resp))
    adc_gpio = next(p["gpio"] for p in caps["safe_pins"] if p["adc_capable"])
    out_gpio = next(p["gpio"] for p in caps["safe_pins"] if not p["adc_capable"])
    pwm_gpio = next(p["gpio"] for p in caps["safe_pins"]
                    if not p["adc_capable"] and p["gpio"] != out_gpio)
    print(f"         chose OUTPUT={out_gpio}, ADC={adc_gpio}, PWM={pwm_gpio}")

    # Step 2: configure
    resp = tool_call(url, "configure_pins", {"pins": [
        {"gpio": out_gpio, "mode": "OUTPUT"},
        {"gpio": adc_gpio, "mode": "ADC"},
        {"gpio": pwm_gpio, "mode": "PWM"},
    ]})
    if not assert_ok(resp, "2. configure OUTPUT + ADC + PWM"):
        return False

    # Step 3: blink (HIGH → LOW)
    for level, label in [(1, "HIGH"), (0, "LOW"), (1, "HIGH")]:
        resp = tool_call(url, "write_digital_pins",
                         {"pins": [{"gpio": out_gpio, "level": level}]})
        assert_ok(resp, f"3. write GPIO {out_gpio} → {label}")
        time.sleep(0.1)

    # Step 4: fade PWM 0 → 100 → 0 in steps
    print(f"  {DIM}     fading GPIO {pwm_gpio} (PWM 0→50→100→0){RESET}")
    for duty in [0, 25, 50, 75, 100, 75, 50, 25, 0]:
        resp = tool_call(url, "set_pwm_duty",
                         {"pins": [{"gpio": pwm_gpio, "duty": duty}]})
        if not resp.get("result", {}).get("isError") and "error" not in resp:
            pass  # silent on each step
        time.sleep(0.05)
    assert_ok(tool_call(url, "set_pwm_duty",
                        {"pins": [{"gpio": pwm_gpio, "duty": 0}]}),
              f"4. PWM fade complete, GPIO {pwm_gpio} → 0%")

    # Step 5: read everything back
    resp = tool_call(url, "read_pins",
                     {"gpios": [out_gpio, adc_gpio, pwm_gpio]})
    if not assert_ok(resp, "5. read_pins — final state"):
        return False
    data = json.loads(get_text(resp))
    for r in data.get("readings", []):
        unit = r.get("unit", "")
        val  = r.get("value")
        mode = r.get("mode")
        display = f"{val}%" if unit == "percent" or mode == "PWM" else \
                  f"{val}mV" if unit == "mV" else \
                  r.get("level", str(val))
        print(f"         GPIO {r['gpio']:2d} [{mode:6s}] = {display}")

    print(f"  {PASS}  demo workflow complete")
    return True


# ── Entry point ────────────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(description="MCP GPIO Server integration tests")
    parser.add_argument("host", nargs="?", default=DEFAULT_HOST,
                        help=f"IP or mDNS hostname (default: {DEFAULT_HOST})")
    args = parser.parse_args()

    url = f"http://{args.host}"
    print(f"\n{BOLD}MCP GPIO Server — Integration Tests{RESET}")
    print(f"Target: {url}")

    try:
        requests.get(f"{url}/ping", timeout=4)
    except Exception as e:
        print(f"\n{FAIL}  Cannot reach {url}: {e}")
        print("  Is the board powered and connected to Wi-Fi?")
        sys.exit(1)

    results = {}
    try:
        results["health"]          = test_health(url)
        results["initialize"]      = test_initialize(url)
        results["tools/list"]      = test_tools_list(url)
        results["capabilities"]    = test_capabilities(url)
        results["configure+read"]  = test_configure_and_read(url)
        results["write_digital"]   = test_write_digital(url)
        results["pwm"]             = test_pwm(url)
        results["i2c_scan"]        = test_i2c_scan(url)
        results["error_handling"]  = test_error_handling(url)
        results["demo_workflow"]   = test_demo_workflow(url)
    except requests.exceptions.ConnectionError as e:
        print(f"\n{FAIL}  Connection lost mid-test: {e}")
        sys.exit(1)

    passed = sum(1 for v in results.values() if v)
    total  = len(results)
    print(f"\n{BOLD}{'─' * 44}{RESET}")
    for name, ok in results.items():
        print(f"  {PASS if ok else FAIL}  {name}")
    print(f"{BOLD}{'─' * 44}{RESET}")
    status = PASS if passed == total else FAIL
    print(f"  {status}  {passed}/{total} suites passed\n")
    sys.exit(0 if passed == total else 1)


if __name__ == "__main__":
    main()
