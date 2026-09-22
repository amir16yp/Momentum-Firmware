"""Generate lexical CPU-audit leads, not verified hot paths or defects."""

import csv
import json
import re
import subprocess
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
RULES = {
    "allocation": r"\b(?:malloc|calloc|realloc|free)\s*\(",
    "string_scan": r"\b(?:strlen|strcmp|strncmp|strcasecmp|strncasecmp|strstr|strchr)\s*\(",
    "format_or_log": r"\b(?:printf|fprintf|sprintf|snprintf|vsprintf|vsnprintf|furi_string_printf|furi_string_cat_printf|FURI_LOG_[A-Z]+)\s*\(",
    "copy_or_clear": r"\b(?:memcpy|memmove|memset|strcpy|strncpy)\s*\(",
    "file_io": r"\b(?:storage_file_read|storage_file_write|fread|fwrite|read|write)\s*\(",
    "pixel": r"\b(?:canvas_draw_dot|canvas_draw_pixel|draw_pixel|set_pixel)\s*\(",
    "math": r"\b(?:sqrtf?|powf?|sinf?|cosf?)\s*\(",
    "lock_queue_wait": r"\b(?:furi_mutex_acquire|furi_mutex_release|furi_message_queue_put|furi_message_queue_get|furi_event_flag_wait|furi_thread_flags_wait)\s*\(",
    "loop": r"\b(?:for|while)\s*\(",
    "indirect_call": r"->\w+\s*\(",
    "mapping_or_codec": r"\b(?:crc\w*|checksum\w*|parity\w*|reverse\w*|convert\w*|decode\w*|encode\w*)\s*\(",
    "layout": r"\b(?:struct|packed)\b",
    "peripheral": r"\b\w*(?:dma|spi|uart|usart|i2c|timer|pwm|adc|aes)\w*\s*\(",
}


def main():
    result = subprocess.run(
        ["rg", "--json", "-n", "-g", "*.c", "-g", "*.h",
         "|".join(RULES.values()), "applications", "lib"],
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
    with Path(__file__).with_name("cpu_inventory.csv").open(
        "w", newline="", encoding="utf-8"
    ) as output:
        writer = csv.writer(output)
        writer.writerow(("category", "path", "line", "source"))
        writer.writerows(rows)
    print(json.dumps({"rows": len(rows), "files": len({r[1] for r in rows}),
                      "categories": dict(Counter(r[0] for r in rows))}, indent=2))


if __name__ == "__main__":
    main()
