# Codex Agent Setup

This repository keeps AI-agent guidance in layers so Codex loads only the
context needed for the current task.

## Layers

- `AGENTS.md`: always-on map, core invariants, command entrypoints, and skill
  routing. Keep this short.
- `.agents/skills/*/SKILL.md`: Codex repo skills. Each skill has frontmatter
  with `name` and `description`, then a focused workflow for one task family.
- `agent-docs/*.md`: deeper project references. Skills and `AGENTS.md` point
  here, but these files should be read only when relevant.
- `CLAUDE.md`: compatibility entrypoint for Claude Code. Keep it as a pointer,
  not a duplicate manual.

## Why Skills Instead Of One Big Manual

Codex discovers skills from their metadata and only loads the full `SKILL.md`
when the task matches. Smaller skills reduce context noise and make task
selection clearer than one large project guide.

## MCP Policy

No project MCP server is currently required for normal CPR-vCodex work.

Add MCP only when it provides a stable external capability that the repository,
shell, browser, or GitHub plugin cannot provide well, such as live authoritative
documentation, a design system, telemetry, or issue tracker operations.

Rules:

- Keep secrets and tokens in user-level Codex config, never in the repo.
- Prefer existing Codex plugins for GitHub and browser work when available.
- Prefer official OpenAI documentation or the OpenAI Docs MCP for OpenAI/Codex
  questions.
- Do not add broad MCP servers just because they are available; each server
  should have a clear maintenance reason.

## Maintenance

When adding a new recurring workflow, create a focused skill under
`.agents/skills/<name>/` and link it from `AGENTS.md`. Put long reference
material in `agent-docs/` rather than expanding `AGENTS.md`.
