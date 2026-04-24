## Collaboration

- Prefer splitting substantial work into multiple subtasks and delegating suitable parts to subagents.
- The assistant may decide when to use explorer or worker subagents without asking every time.
- Use explorer subagents for read-only investigation, such as API discovery, codebase mapping, test discovery, and risk analysis.
- Use worker subagents only for narrow, low-conflict implementation tasks with explicit file ownership.
- Do not run multiple worker subagents that edit the same file or same code region concurrently.
- Keep shared integration work, especially main control flow changes, in the main session unless the task is clearly isolated.

## graphify

- Before answering architecture or codebase questions, read graphify-out/GRAPH_REPORT.md for god nodes and community structure
- After modifying code files in this session, run `python3 -c "from graphify.watch import _rebuild_code; from pathlib import Path; _rebuild_code(Path('.'))"` to keep the graph current
