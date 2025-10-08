# Repository Guidelines

## Project Structure & Module Organization
- `BLE-sample2.ino` hosts both setup logic for the M5Stack CoreS3 and the BLE server callbacks; keep peripheral helpers in the same sketch unless they merit a dedicated library.
- External dependencies (`M5CoreS3`, `BLEDevice`, `BLEUtils`, `BLEServer`, `BLE2902`) are pulled from the Arduino ESP32 core—document any added libraries in comments at the top of the sketch.
- Store board-specific assets (schematics, reference docs) in `docs/` if you introduce them so they do not clutter the sketch directory.

## Build, Flash, and Monitor Commands
- Detect the board and port: `arduino-cli board list`.
- Compile for CoreS3: `arduino-cli compile --fqbn m5stack:esp32:m5stack_coreS3 BLE-sample2.ino`.
- Upload firmware: `arduino-cli upload -p /dev/ttyACM0 --fqbn m5stack:esp32:m5stack_coreS3 BLE-sample2.ino` (replace device path as needed).
- Inspect runtime logs: `arduino-cli monitor -p /dev/ttyACM0 --config 115200` to watch BLE status messages printed on the LCD/Serial.

## Coding Style & Naming Conventions
- Follow the existing two-space indentation and brace-on-same-line style; prefer early returns for guard clauses (see `loop()`).
- Keep constants in all caps with underscores, e.g., `SERVICE_UUID`; use CamelCase for classes and lowerCamelCase for functions/variables (`setupBle`, `deviceConnected`).
- When adding handlers, group related sections under descriptive comment banners (`/* ================ BLE ================ */`) to mirror the current layout.

## Testing Guidelines
- Functional coverage relies on hardware testing: confirm LCD messages for connect/disconnect, read/write, and notify flows before merging.
- When adding features, include a short checklist in the PR describing manual verification steps (button presses, BLE client app used, characteristic values observed).
- If you add automated checks (e.g., unit tests via `arduino-ci`), document the command under this section and ensure it runs headless.

## Commit & Pull Request Guidelines
- Write concise, imperative commit subjects (e.g., `Add notify handler for button press`); include hardware context in the body when relevant (firmware version, BLE client).
- Reference related issues or discussions with `Fixes #ID` where applicable.
- PRs should summarize behavior changes, list manual test results, call out new dependencies, and include screenshots or logs when they clarify UI/device feedback.
