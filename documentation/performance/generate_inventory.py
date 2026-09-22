"""Regenerate static responsiveness leads; matches are NOT confirmed defects."""

import csv
import json
import re
import subprocess
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
RULES = {
    "explicit_delay": r"\b(?:furi_delay_(?:ms|us|tick)|HAL_Delay|vTaskDelay)\s*\(",
    "unbounded_wait": r"\b(?:FuriWaitForever|portMAX_DELAY)\b",
    "thread_join": r"\bfuri_thread_join\s*\(",
    "blocking_notification": r"\bnotification_(?:internal_)?message_block\s*\(",
    "timer_or_ui_cadence": r"\b(?:furi_timer_start|view_dispatcher_set_tick_event_callback|popup_set_timeout)\s*\(",
    "critical_or_scheduler_lock": r"\b(?:FURI_CRITICAL_ENTER|vTaskSuspendAll|taskENTER_CRITICAL)\s*\(",
    "priority_change": r"\bfuri_thread_set_priority\s*\(",
}


def main():
    result = subprocess.run(
        ["rg", "--json", "-n", "-g", "*.c", "-g", "*.h", "-g", "*.cpp",
         "|".join(RULES.values()), "applications", "applications_user", "furi", "targets", "lib"],
        cwd=ROOT, capture_output=True, text=True, encoding="utf-8", errors="replace",
    )
    if result.returncode not in (0, 1):
        raise RuntimeError(result.stderr)
    rows = []
    for line in result.stdout.splitlines():
        record = json.loads(line)
        if record["type"] != "match":
            continue
        data = record["data"]
        source = data["lines"]["text"].strip()
        path = data["path"]["text"].replace("\\", "/")
        for category, pattern in RULES.items():
            if re.search(pattern, source):
                rows.append((category, path, data["line_number"], source))
    rows.sort(key=lambda row: (row[1], row[2], row[0]))
    destination = Path(__file__).with_name("responsiveness_inventory.csv")
    with destination.open("w", newline="", encoding="utf-8") as output:
        writer = csv.writer(output)
        writer.writerow(("category", "path", "line", "source"))
        writer.writerows(rows)
    print(json.dumps({"rows": len(rows), "files": len({r[1] for r in rows}),
                      "categories": dict(Counter(r[0] for r in rows))}, indent=2))


if __name__ == "__main__":
    main()
