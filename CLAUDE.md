# CLAUDE.md

This repository contains the Arduino Nano ESP32 firmware for the EmberSensor sprinkler slider and pan/tilt arm.

## Shared EmberSensor rules

> This section is identical in every EmberSensor repository. Update all four copies together.

### Related repositories
All under `github.com/shauryarchs`:
- `embersensor-site` — website (`docs/`) and Cloudflare Worker API (`worker/`). The hub: every device and client talks only to the Worker. **Issues for all repositories are tracked here.**
- `embersensor-ios` — iOS app; reads `/api/status`, `/api/fires`, `/api/calfire-fires`.
- `sprinkler-slide-pan-tilt` — Arduino Nano ESP32 firmware for the sprinkler slider and pan/tilt arm; polls `/api/motor/command`, pushes `/api/motor/state`.
- `fireguard-rachio` — Arduino UNO R4 WiFi sensor node; POSTs readings to `/api/update`, reads `riskIndex` from `/api/status`, and starts the Rachio sprinkler zone when `riskIndex > 7`.

Changing a Worker endpoint or response field can break the website, the iOS app, and both firmware projects — check all consumers.

### Work workflow (required for all new work)
1. **Create an issue first** in `embersensor-site` describing the task, scope, and acceptance criteria — even if the work is in another repository.
2. **Before making changes, create a branch** in each repository involved, named `shaurya/<issue-number>_<issue-title>` using the `embersensor-site` issue number and a short, lowercase, hyphenated title. Example: `shaurya/42_add-sprinkler-controls`.
3. Complete the work on those branches and run the relevant checks.
4. **Open a pull request in each affected repository**, referencing the issue (e.g. `shauryarchs/embersensor-site#42`) and summarizing the changes and validation.
5. **Do not merge until the repository owner explicitly approves.**
6. After approval, merge each PR into its `main` branch. Close the issue once all work covered by it has been merged.

### No AI attribution
Do not include any reference to "Claude" (or other AI-tool attribution) in code, comments, documentation, filenames, commit messages, branch names, issues, pull requests, or other project artifacts. This includes `Co-Authored-By` trailers and "Generated with …" footers. Commit messages describe the actual change only. Before committing or pushing, check that no such attribution or generated signature has been added.

The one intentional exception is this instructions file (`CLAUDE.md`), which stays tracked so every contributor shares the same instructions.

### Safety
- Keep any **new automatic sprinkler activation or motor movement disabled** until it has been reviewed and validated with the repository owner.
- `flame` is **active-low**: `0` means flame detected. It forces `riskIndex = 10`, which makes FireGuard start the real sprinkler zone. Test or "reset" payloads sent to `/api/update` must use `"flame": 1` unless deliberately simulating a fire.
- Never commit or expose secrets, tokens, Wi-Fi credentials, or access codes.

### Hardware work
- Confirm the exact part model and interface before implementing (ask for a product link or photo if ambiguous).
- Verify compatibility with the actual board — voltage levels, power, memory, and existing pin assignments — using the manufacturer's documentation.
- PRs for hardware changes include: a wiring table (component pin → board pin), power requirements and extra components, mounting guidance, required libraries and configuration, and a step-by-step initial test with expected results.
- Clearly separate checks done in software from tests that require the physical hardware, and document assumptions and limitations.

### Reviews and findings
- Support findings with file paths and function names.
- Distinguish what the code confirms from what is inferred. Documentation and comments alone are not proof that something is implemented.
