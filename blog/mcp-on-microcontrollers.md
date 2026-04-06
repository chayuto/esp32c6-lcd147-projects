# I Put an MCP Server on a $4 Microcontroller. Here's What I Learned.

*April 2026*

---

There's a moment in every side project where you stop and think, "Wait, is this actually useful — or am I just having fun?" With this one, the answer was both.

I put a Model Context Protocol (MCP) server on an ESP32-C6 — a $4 RISC-V microcontroller with Wi-Fi and 8MB of flash. Not a Raspberry Pi. Not a beefy edge device. A chip you'd find inside a smart plug. And it works remarkably well.

This post is about what MCP is, why I think it matters for hardware, and what I learned building two real MCP servers on embedded devices.

## What Is MCP, and Why Should You Care?

MCP — the Model Context Protocol — is an open standard that lets AI models call tools over a structured JSON-RPC interface. Think of it as a USB-C port for AI: one standard way for any model to discover and use any tool, regardless of who built either side.

Before MCP, connecting an AI to a tool meant writing custom integration code for each model-tool pair. MCP replaces that with a simple contract:

1. The AI asks: *"What tools do you have?"* (`tools/list`)
2. The server responds with tool names, descriptions, and JSON schemas
3. The AI calls tools by name with structured arguments (`tools/call`)
4. The server returns results — text, data, or even images

That's it. No SDKs, no auth tokens, no vendor lock-in. Just HTTP and JSON.

The implication for hardware is profound: **any device that can serve HTTP can become an AI-controllable tool.** Your microcontroller doesn't need to know anything about LLMs. It just needs to answer "what can you do?" and "do this thing."

## The Projects

I built two MCP servers on the same hardware — an ESP32-C6 board with a 1.47" LCD:

### Project 1: MCP Canvas Display

An AI-controllable drawing surface. The LCD becomes a 172x320 pixel canvas that any MCP client can draw on.

**8 tools:**
- `get_canvas_info` — resolution, color format, available fonts
- `clear_canvas` — fill with a solid color
- `draw_rect`, `draw_line`, `draw_arc`, `draw_text`, `draw_path` — drawing primitives
- `get_canvas_snapshot` — returns a JPEG screenshot (base64-encoded)

That last tool is the interesting one. The AI can *see what it drew*. It calls `get_canvas_snapshot`, gets back a JPEG, evaluates the result, and self-corrects. I've watched Claude draw a HUD-style radar display — concentric rings, sweep arcs, status bars — iterating on layout by taking snapshots between draw calls.

A visual feedback loop on a microcontroller. That still feels like science fiction to me.

### Project 2: MCP GPIO Controller

Full hardware I/O control — digital read/write, ADC, PWM, I2C scanning, and an onboard RGB LED — all exposed as MCP tools.

**7 tools:**
- `get_gpio_capabilities` — which pins are safe, which are reserved, what modes are available
- `configure_pins` — set pin modes (input, output, ADC, PWM)
- `write_digital_pins` / `read_pins` — digital and analog I/O
- `set_pwm_duty` — hardware PWM control
- `i2c_scan` — probe for I2C devices on any two pins
- `set_rgb_led` — control the onboard WS2812

The display doubles as a live dashboard — a color-coded table showing every pin's current state, refreshing twice per second.

## What I Learned About Designing MCP Tools for LLMs

Building MCP servers is easy. Building MCP servers that LLMs can *actually use reliably* is a different challenge entirely. Here's what I picked up:

### 1. Discovery Tools Are Non-Negotiable

The first tool in every server should be a "tell me about yourself" tool. For the canvas server, it's `get_canvas_info` (returns resolution, color depth, available fonts). For GPIO, it's `get_gpio_capabilities` (returns safe pins, reserved pins, supported modes).

Without this, the AI guesses. And it guesses wrong. With discovery tools, the AI grounds itself in reality before acting.

### 2. Keep It Under 8 Tools

Research and my own testing both point to a sweet spot: **5-8 tools per server**. Beyond that, LLM tool selection accuracy drops noticeably. If you need 15 operations, you probably need two servers — not one server with 15 tools.

### 3. Tell the AI What *Not* to Do

This was the biggest revelation. Tool descriptions should include explicit negative guidance:

> *"Pin values are 0 or 1 only. Do NOT pass voltage values like 3.3."*

> *"Colors are 0-255 per channel. Do NOT use hex strings like '#FF0000'."*

Without these, models routinely pass hex color strings, voltage floats, or other "reasonable but wrong" values. A single "Do NOT" line in the tool description fixes it almost every time.

### 4. Safety in Layers, Not Gates

For the GPIO server, I couldn't just trust the AI to pick safe pins. The board has pins that control flash, the LCD, and the crystal oscillator — writing to them could brick the device.

I ended up with four layers of protection:

1. **JSON schema `enum`** — only safe pin numbers are valid values
2. **Discovery tool** — tells the AI which pins are reserved and why
3. **Tool descriptions** — negative guidance ("Do NOT configure pins not listed in capabilities")
4. **Firmware check** — `board_is_safe_pin()` rejects bad pins even if all else fails

Any single layer would be insufficient. Together, I haven't had a single unsafe pin access across hundreds of test calls.

### 5. Give the AI Eyes

The canvas snapshot feature changed everything. Before it, the AI was drawing blind — it would send commands and hope for the best. After adding JPEG snapshots, accuracy jumped dramatically. The AI would draw, snapshot, notice a label was clipped, and adjust.

If your MCP server controls anything visual or spatial, **return images**. It's the highest-leverage feature you can add.

## The Architecture That Works on 8MB

Both servers follow the same pattern, and it's surprisingly clean:

```
Wi-Fi → HTTP Server → JSON-RPC Router → Tool Handlers
                                              ↓
                                         FreeRTOS Queue
                                              ↓
                                    LVGL Timer (renders UI)
```

The key insight: **never let the HTTP handler touch the display directly.** LVGL (the graphics library) isn't thread-safe. So MCP tool calls push commands onto a FreeRTOS queue, and a periodic timer drains the queue under a task lock. This pattern — shared state via queues, UI updates via timers — is the embedded equivalent of "don't update the DOM from a web worker."

Total binary size: ~1.3MB per project, fitting comfortably in a 2MB app partition. The entire MCP server — HTTP routing, JSON parsing, tool dispatch, drawing engine, JPEG encoding — adds maybe 200KB on top of a baseline LVGL project.

## Use Cases Beyond My Desk

I built these as learning projects, but the pattern generalizes:

- **Smart displays** — conference room dashboards, factory status boards, retail signage, all controllable by AI agents without custom firmware updates
- **Sensor hubs** — expose temperature, humidity, motion, and light sensors as MCP tools; let AI agents query physical-world data as naturally as they query APIs
- **Lab automation** — I2C scan + GPIO control means an AI can probe and interact with breadboard circuits, adjusting PWM frequencies or reading ADC values conversationally
- **Accessibility interfaces** — an AI that can read sensors and draw to a display creates a natural language bridge to physical devices for users who can't interact with them directly
- **Education** — students describe what they want ("make the LED pulse slowly, read the temperature sensor every 5 seconds, show it on the display") and the AI translates intent to hardware actions in real time

The common thread: **MCP turns hardware into a tool the AI already knows how to use.** No driver code, no SDK integration, no recompilation. Deploy the firmware once, and the AI can figure out the rest from the tool descriptions alone.

## What Surprised Me

**How little code it takes.** The MCP server itself is maybe 500 lines of C across the HTTP router and JSON-RPC dispatcher. Most of the complexity is in the tool handlers — the actual hardware logic you'd write anyway.

**How good the tool descriptions need to be.** I spent more time writing and refining tool descriptions than writing the server code. A well-described tool with clear boundaries, examples, and negative guidance works on the first call. A vaguely described tool fails in creative and unpredictable ways.

**How natural the interaction feels.** Once the server is running, you can say "draw a bar chart showing these values" or "scan for I2C devices and tell me what you find" and it just... works. The MCP layer disappears. You're talking to the hardware through the AI, and the protocol is invisible.

## Try It Yourself

Both projects are open source:

- **[esp32c6-lcd147-projects](https://github.com/chayuto/esp32c6-lcd147-projects)** — full source, integration tests, and build instructions

You'll need an ESP32-C6 board with an ST7789 LCD (the specific 1.47" module I used is linked in the repo). Flash the firmware, connect to Wi-Fi, point your MCP client at `http://esp32-canvas.local/mcp`, and start drawing.

Or adapt the pattern to your own hardware. The architecture is simple enough to port in an afternoon. The MCP spec doesn't care what's behind the HTTP endpoint — it could be an ESP32, an Arduino with a Wi-Fi shield, or a Raspberry Pi Pico W. If it serves JSON, it's an MCP server.

---

*The best interfaces disappear. MCP makes AI-to-hardware communication feel like that — not a protocol you wrestle with, but a bridge you walk across without noticing. The hard part isn't the protocol. It's describing your tools well enough that the AI never has to ask for directions.*
