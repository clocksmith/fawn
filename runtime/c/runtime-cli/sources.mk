# SPDX-License-Identifier: MIT
# Canonical host and runtime sources, shared by Make and the Windows SDK builder.
CLI_CORE = src/host_io.c src/json.c src/stream.c src/stream_texture.c src/stream_render.c src/stream_diagnostics.c src/stream_surface.c src/trace.c src/sha256.c src/quirks.c \
  src/operators.c src/numeric_policy.c src/numeric_stability.c src/plan.c
CLI_RUNTIME = src/cli_options.c src/cli_inputs.c src/cli_output.c src/execute.c src/queue.c src/kernel.c src/texture.c src/render.c src/render_public.c src/render_public_resources.c src/map.c src/diagnostics.c src/diagnostics_lifecycle.c src/surface.c
