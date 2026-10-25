#!/usr/bin/env bash
export DISPLAY="${DISPLAY:-:0}"
if [ -z "$XAUTHORITY" ]; then
  if [ -f "/run/user/$(id -u)/gdm/Xauthority" ]; then
    export XAUTHORITY="/run/user/$(id -u)/gdm/Xauthority"
  elif [ -f "$HOME/.Xauthority" ]; then
    export XAUTHORITY="$HOME/.Xauthority"
  fi
fi

exec /usr/bin/playwright-mcp \
  --port 8931 \
  --host 127.0.0.1 \
  --allowed-hosts "127.0.0.1:8931,localhost:8931" \
  --browser chrome \
  --user-data-dir "$HOME/playwright-chrome-profile" \
  --shared-browser-context
