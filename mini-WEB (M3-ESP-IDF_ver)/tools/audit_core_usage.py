"""Read-only source inventory of M3 task placement; never compile or connect to a board."""
import argparse
import hashlib
import json
import re
from pathlib import Path


def arguments(source, start):
    """Split C call arguments, preserving nested calls and quoted strings."""
    depth, quote, escaped, items, begin = 1, None, False, [], start
    for pos in range(start, len(source)):
        char = source[pos]
        if quote:
            if escaped:
                escaped = False
            elif char == "\\":
                escaped = True
            elif char == quote:
                quote = None
        elif char in ('"', "'"):
            quote = char
        elif char == "(":
            depth += 1
        elif char == ")":
            depth -= 1
            if depth == 0:
                return items + [source[begin:pos].strip()]
        elif char == "," and depth == 1:
            items.append(source[begin:pos].strip())
            begin = pos + 1
    raise ValueError("Unclosed task creation call")


def inventory(project):
    config = (project / "sdkconfig").read_text(encoding="utf-8-sig")
    cores = (project / "main/include/m3_cores.h").read_text(encoding="utf-8-sig")
    constants = dict(re.findall(r"^#define\s+(M3_CORE_\w+)\s+(\d+)", cores, re.M))
    tasks, hashes = [], {}
    for path in sorted((project / "main").rglob("*")):
        if path.suffix not in (".c", ".inc") or "private" in path.name or path.name == "network_settings.inc":
            continue
        source = path.read_text(encoding="utf-8-sig")
        # Replace comments with spaces while retaining offsets and line numbers.
        source = re.sub(
            r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|/\*.*?\*/|//[^\n]*',
            lambda m: m[0] if m[0][0] in ('"', "'") else "".join("\n" if c == "\n" else " " for c in m[0]),
            source, flags=re.S,
        )
        matches = list(re.finditer(r"\b(xTaskCreatePinnedToCore|xTaskCreate)\s*\(", source))
        if not matches:
            continue
        relative = path.relative_to(project).as_posix()
        hashes[relative] = hashlib.sha256(path.read_bytes()).hexdigest()
        for match in matches:
            args = arguments(source, match.end())
            pinned = match[1] == "xTaskCreatePinnedToCore"
            if len(args) != (7 if pinned else 6):
                raise ValueError("Unexpected task signature in " + relative)
            affinity = args[6] if pinned else "tskNO_AFFINITY"
            affinity = constants.get(affinity, affinity)
            tasks.append({
                "name": args[1].strip('"'), "function": args[0],
                "stack_bytes": int(args[2]) if args[2].isdigit() else args[2],
                "priority": int(args[4]) if args[4].isdigit() else args[4],
                "core": int(affinity) if affinity.isdigit() else affinity,
                "source": relative, "line": source.count("\n", 0, match.start()) + 1,
            })
    relevant = re.compile(r"^CONFIG_(?:ESP_DEFAULT_CPU_FREQ_MHZ|FREERTOS_(?:NUMBER_OF_CORES|HZ|GENERATE_RUN_TIME_STATS|RUN_TIME_COUNTER_TYPE_U32|RUN_TIME_COUNTER_TYPE_U64)|LWIP_TCPIP_TASK_(?:PRIO|AFFINITY))=(.+)$", re.M)
    settings = {m[0].split("=", 1)[0]: m[1] for m in relevant.finditer(config)}
    settings["CONFIG_ULP_COPROC_ENABLED"] = "CONFIG_ULP_COPROC_ENABLED=y" in config
    settings["CONFIG_PM_ENABLE"] = "CONFIG_PM_ENABLE=y" in config
    hashes["sdkconfig"] = hashlib.sha256((project / "sdkconfig").read_bytes()).hexdigest()
    hashes["main/include/m3_cores.h"] = hashlib.sha256((project / "main/include/m3_cores.h").read_bytes()).hexdigest()
    return {
        "evidence": "source_configuration_only_not_runtime_or_compilation",
        "settings": settings, "application_task_creation_sites": tasks,
        "notes": [
            "Creation sites are not simultaneously running task counts.",
            "LVGL, HTTP server and managed/system tasks need separate inspection.",
            "No-affinity tasks may migrate; floating-point use can constrain actual placement.",
            "IDF task stack sizes are bytes. No stack reduction is justified by this inventory.",
        ], "source_sha256": hashes,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--project", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    result = inventory(args.project.resolve())
    output = json.dumps(result, ensure_ascii=False, indent=2) + "\n"
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(output, encoding="utf-8")
        print("Wrote task inventory:", args.output)
        print("Application task creation sites:", len(result["application_task_creation_sites"]))
    else:
        print(output, end="")


if __name__ == "__main__":
    main()
