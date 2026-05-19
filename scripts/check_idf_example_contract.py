#!/usr/bin/env python3
from __future__ import annotations

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]

IDF_EXAMPLE_MACRO = "STATUSLED_EXAMPLE_PLATFORM_IDF"
CLI_SOURCE_INCLUDE = '#include "examples/01_status_led_cli/main.cpp"'
REQUIRED_EXAMPLE_COMPONENTS = [
    "StatusLED",
    "esp_timer",
    "freertos",
    "vfs",
]
REQUIRED_FILES = [
    "CMakeLists.txt",
    "idf_component.yml",
    "examples/common/IdfArduinoCompat.h",
    "examples/espidf_basic/CMakeLists.txt",
    "examples/espidf_basic/main/CMakeLists.txt",
    "examples/espidf_basic/main/main.cpp",
]
REQUIRED_COMPAT_TOKENS = [
    "class IdfConsole",
    "esp_timer_get_time",
    "fcntl",
    "STDIN_FILENO",
    "vTaskDelay",
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
        fail(f"CLI source missing command dispatch '{command}'")


def require_help_item(text: str, command: str) -> None:
    if f'print_help_item("{command}' not in text:
        fail(f"CLI source missing help item '{command}'")


def main() -> int:
    for rel in REQUIRED_FILES:
        require_exists(rel)

    idf_main = (ROOT / "examples" / "espidf_basic" / "main" / "main.cpp").read_text(
        encoding="utf-8", errors="replace"
    )
    require_token(idf_main, f"#define {IDF_EXAMPLE_MACRO} 1", "ESP-IDF main")
    require_token(idf_main, '#include "examples/common/IdfArduinoCompat.h"', "ESP-IDF main")
    require_token(idf_main, CLI_SOURCE_INCLUDE, "ESP-IDF main")
    require_token(idf_main, 'extern "C" void app_main(void)', "ESP-IDF main")
    require_token(idf_main, "setup();", "ESP-IDF main")
    require_token(idf_main, "loop();", "ESP-IDF main")

    cmake = (ROOT / "examples" / "espidf_basic" / "main" / "CMakeLists.txt").read_text(
        encoding="utf-8", errors="replace"
    )
    for component in REQUIRED_EXAMPLE_COMPONENTS:
        if re.search(rf"\b{re.escape(component)}\b", cmake) is None:
            fail(f"ESP-IDF example CMake missing required component '{component}'")

    compat = (ROOT / "examples" / "common" / "IdfArduinoCompat.h").read_text(
        encoding="utf-8", errors="replace"
    )
    for token in REQUIRED_COMPAT_TOKENS:
        require_token(compat, token, "IdfArduinoCompat.h")

    cli = (ROOT / "examples" / "01_status_led_cli" / "main.cpp").read_text(
        encoding="utf-8", errors="replace"
    )
    require_token(cli, f"defined({IDF_EXAMPLE_MACRO})", "shared CLI")
    for command in MANDATORY_COMMANDS:
        require_command_dispatch(cli, command)
        require_help_item(cli, command)

    manifest = (ROOT / "idf_component.yml").read_text(encoding="utf-8", errors="replace")
    for token in ("esp32s2", "esp32s3", 'idf: ">=6.0.1"'):
        require_token(manifest, token, "idf_component.yml")

    print("IDF example contract PASSED")
    return 0


if __name__ == "__main__":
    sys.exit(main())
