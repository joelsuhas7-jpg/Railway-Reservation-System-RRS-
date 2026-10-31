#!/usr/bin/env python3
"""Check the project's public-header include graph against its reviewed baseline.

The SRS section 9 graph is a DAG. This check keeps the concrete C++ header graph
in sync with the reviewed edge set and rejects cycles (back edges).
"""

from __future__ import annotations

import json
import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "src"
BASELINE = ROOT / "docs" / "include-graph.json"
INCLUDE = re.compile(r'^\s*#\s*include\s*["<]([^">]+)[">]')


def read_graph() -> dict[str, set[str]]:
    headers = {p.name: p for p in SRC.glob("*.hpp")}
    graph = {name: set() for name in headers}
    for name, path in headers.items():
        for line in path.read_text(encoding="utf-8").splitlines():
            match = INCLUDE.match(line)
            if match and match.group(1) in headers:
                graph[name].add(match.group(1))
    return graph


def find_cycle(graph: dict[str, set[str]]) -> list[str] | None:
    state: dict[str, int] = {}
    stack: list[str] = []

    def visit(node: str) -> list[str] | None:
        state[node] = 1
        stack.append(node)
        for target in sorted(graph[node]):
            if state.get(target, 0) == 1:
                start = stack.index(target)
                return stack[start:] + [target]
            if state.get(target, 0) == 0:
                cycle = visit(target)
                if cycle:
                    return cycle
        stack.pop()
        state[node] = 2
        return None

    for node in sorted(graph):
        if state.get(node, 0) == 0:
            cycle = visit(node)
            if cycle:
                return cycle
    return None


def main() -> int:
    actual = read_graph()
    if not BASELINE.exists():
        print(f"Missing reviewed include graph: {BASELINE.relative_to(ROOT)}", file=sys.stderr)
        return 1
    expected_raw = json.loads(BASELINE.read_text(encoding="utf-8"))
    expected = {name: set(targets) for name, targets in expected_raw.items()}
    errors: list[str] = []

    if set(actual) != set(expected):
        errors.append("Header set differs from docs/include-graph.json")
        errors.append(f"  missing from baseline: {sorted(set(actual) - set(expected))}")
        errors.append(f"  removed from source: {sorted(set(expected) - set(actual))}")
    for source in sorted(set(actual) & set(expected)):
        added = actual[source] - expected[source]
        removed = expected[source] - actual[source]
        if added:
            errors.append(f"Unexpected include edges from {source}: {', '.join(sorted(added))}")
        if removed:
            errors.append(f"Missing include edges from {source}: {', '.join(sorted(removed))}")

    cycle = find_cycle(actual)
    if cycle:
        errors.append("Include graph contains a back-edge cycle: " + " -> ".join(cycle))

    if errors:
        print("Include graph check failed:", file=sys.stderr)
        print("\n".join(f"- {error}" for error in errors), file=sys.stderr)
        print("Update docs/include-graph.json only after reviewing the SRS section 9 edge set.", file=sys.stderr)
        return 1
    edge_count = sum(map(len, actual.values()))
    print(f"Include graph matches the reviewed SRS edge set ({len(actual)} headers, {edge_count} edges); no back edges.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
