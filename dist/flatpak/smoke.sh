#!/bin/sh
# Adapter for the shared PTY smoke suite. Permissions apply to its temporary
# fixture directory only; they do not alter the installed application's grants.
set -eu
export DBUS_SESSION_BUS_ADDRESS="$PAPO_FLATPAK_TEST_BUS"
exec flatpak run --env=DBUS_SESSION_BUS_ADDRESS=unix:path=/nonexistent/papo-test-bus \
    --filesystem="$XDG_CONFIG_HOME" --env=XDG_CONFIG_HOME="$XDG_CONFIG_HOME" \
    --env=XDG_STATE_HOME="$XDG_STATE_HOME" io.github.prodesert22.PapoTUI "$@"
