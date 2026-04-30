# C Assistant

An AI-powered C programming assistant with two interfaces:
- **Native Windows app** — system tray, global hotkey (Ctrl+Alt+C), zero dependencies
- **Web app** — works in any browser, shareable link

Powered by [Groq API](https://console.groq.com) (free) + `llama-3.3-70b-versatile`.

---

## Features

| Mode | What it does |
|---|---|
| **Explain** | Breaks down your code with analogy + complexity |
| **Hint** | One focused nudge toward the right algorithm |
| **Fix** | Finds every bug and returns corrected code |
| **Solution** | Full optimal solution with approach comparison, dry run, edge cases |

---

## Web App (quickest start)

Just open `web/index.html` in any browser — no install needed.

1. Get a free API key at https://console.groq.com/keys
2. Paste it in the key bar at the top
3. Paste your C code or question → choose a mode

---

## Native Windows App

### Requirements
- Windows 10 or later
- No extra installs — compiler is bundled

### Setup

```bat
# Step 1 — download bundled GCC compiler (~65 MB, one time)
SETUP_COMPILER.bat

# Step 2 — build the exe
BUILD.bat

# Step 3 — add your Groq API key
# Edit dist\.env and replace the placeholder:
GROQ_API_KEY=your_key_here

# Step 4 — launch
START.bat
```

### Usage
- `Ctrl+Alt+C` — open assistant popup
- Select code in any app → `Ctrl+Alt+C` — assistant pre-fills with your selection
- Right-click tray icon → **Quit** to exit

---

## Project Structure

```
c-assistant-native/
├── src/
│   ├── main.c        # WinMain, tray icon, global hotkey
│   ├── popup.c       # UI window, buttons, dark theme
│   ├── groq.c        # HTTPS call to Groq API (WinHTTP)
│   └── config.c      # Reads API key from .env
├── include/
│   └── app.h         # Shared types and constants
├── web/
│   └── index.html    # Browser version (self-contained)
├── BUILD.bat         # Compiles src/ → dist/c-assistant.exe
├── START.bat         # Launches the app (builds first if needed)
├── SETUP_COMPILER.bat# Downloads portable GCC into tools/
└── test_assistant.py # Automated test suite (Groq-as-judge)
```

---

## Automated Testing

```bat
python test_assistant.py              # Run all 10 questions
python test_assistant.py --questions 3   # Run first 3 only
python test_assistant.py --mode fix   # Test fix mode
python test_assistant.py --verbose    # Print full responses
```

Saves a full report to `test_report.txt`.

---

## Tech Stack

| Component | Technology |
|---|---|
| Native UI | Win32 API (pure C, no frameworks) |
| HTTP | WinHTTP (built into Windows) |
| Web UI | Vanilla HTML/CSS/JS + marked.js + highlight.js |
| AI model | llama-3.3-70b-versatile via Groq |
| Compiler | w64devkit (portable GCC for Windows) |

---

## License

MIT
