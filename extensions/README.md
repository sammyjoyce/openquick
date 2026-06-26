# OpenQuick Pi extensions

This directory contains Pi extension source that ships with the OpenQuick repository.

- `pi-artifact/` registers an `artifact` tool that publishes static web artifacts through `quick deploy --profile cf`, including an OpenQuick SDK-backed `mode: "codemode"` editor/live-preview artifact.

For project-local auto-discovery, tracked shims can live under `.pi/extensions/` and re-export these sources.
