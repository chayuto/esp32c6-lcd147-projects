# MCP Tool Design Checklist

Use this when designing or reviewing MCP tool schemas for any project — embedded or cloud.
Based on: *Advanced Architectural Paradigms and Optimization Strategies for MCP Implementations* (2026-03)

---

## 1. Tool Budget

- **5–8 tools maximum** per server for optimal LLM attention and 80% task success rate
- Every tool added beyond 8 degrades selection accuracy — consolidate or remove before adding
- Group related operations behind a single higher-level tool rather than exposing granular endpoints

## 2. Six-Component Description Framework

Every tool description MUST include all six. 97.1% of production tools fail at least one.

```
[PURPOSE]      One sentence: what it does and its primary goal.
[GUIDANCE]     When to call it. What to call before it (prerequisites). Sequential order if required.
[LIMITATIONS]  Hard bounds: max array size, max string length, coordinate range, unsupported modes.
[PARAMETERS]   Type, range, default, and format for every input field.
[COMPLETENESS] Any side effects (e.g., "this clears all previous drawing").
[EXEMPLAR]     Brief example parameter values — not full JSON, just values. Omit if description is already clear.
```

**Anti-pattern:** Over-augmented descriptions inflate LLM execution steps by 67%.
Keep descriptions concise — purpose + limits + parameter transparency is the minimum viable set.

## 3. Negative Guidance (Critical)

State explicitly when NOT to use each tool. Missing this is the #1 cause of LLM dead-end paths.

Template:
```
"Do NOT use [tool] for [wrong use case]. Use [correct tool] instead."
"Do NOT pass more than [N] items — excess are silently dropped."
"Do NOT call this if [precondition not met]."
```

## 4. Naming Conventions

| Rule | Good | Bad |
|---|---|---|
| verb_noun format | `draw_rect`, `clear_screen` | `rectangle`, `screenClear` |
| No jargon or acronyms | `search_products` | `prod_lookup_v2` |
| Consistent field names across tools | all use `r,g,b` | one uses `color`, another `rgb` |
| Outputs pipeline into next tool inputs | tool A returns `user_id`, tool B takes `user_id` | tool A returns `userId`, tool B takes `id` |

## 5. Parameter Schema Requirements

In the JSON Schema `inputSchema` object:
- Mark all required fields in the `"required"` array at root level
- Add `"minimum"` / `"maximum"` for all numeric fields with bounds
- Add `"maxLength"` for all string fields
- Add `"maxItems"` for all array fields
- Add an `"examples"` or `"default"` key for optional parameters

```json
"x": {
  "type": "integer",
  "description": "Left edge of rectangle. Range: 0-171.",
  "minimum": 0,
  "maximum": 171
}
```

## 6. Error Response Design

Errors must be **self-correcting** — give the LLM enough info to fix and retry autonomously.

**Bad:** `"error": "invalid params"`
**Good:** `"x out of range: got 200, must be 0-171"`

For tool-level errors (bad args), use `isError: true` in the result, NOT a JSON-RPC error object:
```json
{
  "result": {
    "content": [{"type": "text", "text": "x out of range: got 200, must be 0-171"}],
    "isError": true
  }
}
```

Reserve JSON-RPC `"error"` objects for protocol-level failures (parse error, method not found).

## 7. Tool Ordering in `tools/list`

LLM attention is front-weighted. Order matters:
1. Discovery/info tool first (e.g., `get_screen_info`, `list_datasets`)
2. Reset/init tool second (e.g., `clear_screen`, `create_session`)
3. Most commonly used tools next
4. Specialized or rare tools last

## 8. Visual Feedback — Image Content Type

For any MCP server driving a display or generating visual output, add a snapshot/screenshot tool.
The MCP spec supports `"type":"image"` in tool results — vision-capable LLMs (Claude) will see it:

```json
{
  "result": {
    "content": [
      {
        "type": "image",
        "data": "<base64-encoded JPEG or PNG>",
        "mimeType": "image/jpeg"
      }
    ],
    "isError": false
  }
}
```

**This closes the read→reason→write loop.** Without it the LLM draws blind and cannot self-correct.

Tool description must warn: "Do NOT call in rapid succession — encoding is slow. Allow Nms between calls."

On embedded (ESP32-C6): use `espressif/esp_new_jpeg` v0.6.1 from the component registry (pure C,
supports encoding on all 8 ESP32 targets including C6). Hardware JPEG exists only on ESP32-P4.
Downsample 2× before encoding to keep the RGB888 intermediate buffer within RAM budget.
Static-allocate the RGB888 and JPEG output buffers (BSS) to avoid heap fragmentation.
Protect the pixel buffer with a mutex — rendering task writes, snapshot handler reads, different tasks.

## 10. Health Endpoint

Always expose `GET /ping` alongside `POST /mcp`:
```json
{"status": "healthy", "queue_depth": 2, "queue_max": 8}
```
Lets the LLM (or orchestrator) detect if the server is alive and whether it's keeping up with commands.

## 9. `get_screen_info` / Discovery Tool Pattern

For any server with a bounded resource (screen, database, API quota), expose a no-parameter
discovery tool as tool #1. It should return:
- All dimension/capacity constraints
- Available modes, fonts, or data types
- Field naming conventions used by other tools
- Explicit instruction: "Call this before any other tool"

## 10. Atomic Tool Principle

One tool = one unambiguous action. Never branch behavior based on optional params.

**Bad:** `get_or_create_customer` — agent cannot predict if it will read or write
**Good:** `get_customer_by_id` + `create_customer` — intent is explicit

For embedded projects: never combine a destructive operation (clear, erase) with a constructive one (draw) in the same tool.

---

## Quick Self-Review Checklist

Before shipping any `tools/list` response:

- [ ] Total tool count ≤ 8
- [ ] Every description has: purpose, usage guidance, limitations, parameter transparency
- [ ] Every tool has at least one "Do NOT" negative guidance statement
- [ ] All tools use consistent field names (`r,g,b` not mixed with `color`)
- [ ] All numeric params have `minimum`/`maximum` in schema
- [ ] All string params have `maxLength` in schema
- [ ] All array params have `maxItems` in schema
- [ ] Error messages include the bad value and the valid range
- [ ] Discovery/info tool is listed first
- [ ] Snapshot/screenshot tool exists if server drives a display or generates visual output
- [ ] Snapshot tool description warns about call frequency / encoding latency
- [ ] `GET /ping` health endpoint exists
- [ ] Prerequisites are stated explicitly in description prose (not just inferred)
