- Prefer splitting work into multiple subtasks and delegating them to subagents to reduce per-session context usage

## graphify

- Before answering architecture or codebase questions, read graphify-out/GRAPH_REPORT.md for god nodes and community structure
- After modifying code files in this session, run `python3 -c "from graphify.watch import _rebuild_code; from pathlib import Path; _rebuild_code(Path('.'))"` to keep the graph current; do not build code
