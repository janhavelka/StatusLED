#!/usr/bin/env python3
from __future__ import annotations

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]

REQUIRED_EXAMPLE_COMPONENTS = [
    "StatusLED",
    "esp_timer",
    "freertos",
    "vfs",
]
REQUIRED_FILES = [
    "CMakeLists.txt",
    "idf_component.yml",
    "examples/espidf_basic/CMakeLists.txt",
    "examples/espidf_basic/main/CMakeLists.txt",
    "examples/espidf_basic/main/main.cpp",
]
REQUIRED_NATIVE_TOKENS = [
    'extern "C" void app_main(void)',
    "esp_timer_get_time",
    "fcntl",
    "STDIN_FILENO",
    "::read",
    "vTaskDelay",
]
FORBIDDEN_IDF_TOKENS = [
    "Arduino.h",
    "IdfArduinoCompat",
    "Serial",
    "millis()",
    "delay(",
    "#include \"examples/01_status_led_cli/main.cpp\"",
]
MANDATORY_COMMANDS = [
    "help",
    "version",
    "begin",
    "end",
    "stress",
    "info",
    "status",
    "config",
    "last",
    "list_modes",
    "list_presets",
    "mode",
    "modep",
    "color",
    "alt",
    "preset",
    "default",
    "temp",
    "bright",
    "gbright",
    "clear",
    "cleartemp",
    "allpreset",
    "allmode",
    "allcolor",
    "refresh",
]


def fail(msg: str) -> None:
    print(f"IDF example contract FAILED: {msg}")
    raise SystemExit(1)


def require_exists(rel: str) -> None:
    if not (ROOT / rel).exists():
        fail(f"missing {rel}")


def require_token(text: str, token: str, label: str) -> None:
    if token not in text:
        fail(f"{label} missing token '{token}'")


def require_command_dispatch(text: str, command: str) -> None:
    pattern = rf'strcmp\(\s*argv\[0\]\s*,\s*"{re.escape(command)}"\s*\)\s*==\s*0'
    if re.search(pattern, text) is None:
        fail(f"ESP-IDF main missing command dispatch '{command}'")


def require_help_item(text: str, command: str) -> None:
    if f'printHelpItem("{command}' not in text:
        fail(f"ESP-IDF main missing help item '{command}'")


def main() -> int:
    for rel in REQUIRED_FILES:
        require_exists(rel)

    idf_main = (ROOT / "examples" / "espidf_basic" / "main" / "main.cpp").read_text(
        encoding="utf-8", errors="replace"
    )
    for token in REQUIRED_NATIVE_TOKENS:
        require_token(idf_main, token, "ESP-IDF main")
    for token in FORBIDDEN_IDF_TOKENS:
        if token in idf_main:
            fail(f"ESP-IDF main must not use Arduino compatibility token '{token}'")

    cmake = (ROOT / "examples" / "espidf_basic" / "main" / "CMakeLists.txt").read_text(
        encoding="utf-8", errors="replace"
    )
    for component in REQUIRED_EXAMPLE_COMPONENTS:
        if re.search(rf"\b{re.escape(component)}\b", cmake) is None:
            fail(f"ESP-IDF example CMake missing required component '{component}'")

    for command in MANDATORY_COMMANDS:
        require_command_dispatch(idf_main, command)
        require_help_item(idf_main, command)

    manifest = (ROOT / "idf_component.yml").read_text(encoding="utf-8", errors="replace")
    for token in ("esp32s2", "esp32s3", 'idf: ">=6.0.1"'):
        require_token(manifest, token, "idf_component.yml")

    print("IDF example contract PASSED")
    return 0


if __name__ == "__main__":
    sys.exit(main())
