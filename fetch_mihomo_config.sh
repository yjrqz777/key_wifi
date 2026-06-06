#!/usr/bin/env bash
set -euo pipefail

SUB_URL="${SUB_URL:-http://82.41.180.151:16052/91a0c710-ec9d-4ee7-a512-de06fc5b9f26/clmi.yaml}"
CONFIG_PATH="${CONFIG_PATH:-/etc/mihomo/config.yaml}"
UI_PATH="${UI_PATH:-/etc/mihomo/ui}"
TMP_CONFIG="$(mktemp)"

cleanup() {
  rm -f "$TMP_CONFIG"
}
trap cleanup EXIT

echo "正在拉取 mihomo 配置：$SUB_URL"
curl -L --retry 5 --retry-delay 3 --retry-all-errors --connect-timeout 10 --max-time 60 -o "$TMP_CONFIG" "$SUB_URL"

if [ ! -s "$TMP_CONFIG" ]; then
  echo "配置下载失败：文件为空"
  exit 1
fi

python3 - "$TMP_CONFIG" "$UI_PATH" <<'PY'
from pathlib import Path
import sys

path = Path(sys.argv[1])
ui_path = sys.argv[2]
lines = path.read_text(errors='replace').splitlines()
out = []
seen_controller = seen_ui = seen_secret = False

for line in lines:
    if line.startswith('external-controller:'):
        out.append('external-controller: 127.0.0.1:9090')
        seen_controller = True
    elif line.startswith('external-ui:'):
        out.append(f'external-ui: {ui_path}')
        seen_ui = True
    elif line.startswith('secret:'):
        out.append('secret: ""')
        seen_secret = True
    else:
        out.append(line)

if not seen_controller:
    out.append('external-controller: 127.0.0.1:9090')
if not seen_ui:
    out.append(f'external-ui: {ui_path}')
if not seen_secret:
    out.append('secret: ""')

path.write_text('\n'.join(out) + '\n')
PY

sudo mkdir -p "$(dirname "$CONFIG_PATH")"
sudo cp "$TMP_CONFIG" "$CONFIG_PATH"

echo "配置已写入：$CONFIG_PATH"
echo "如果 mihomo 正在运行，请重启或重新启动："
echo "  sudo /usr/local/bin/mihomo -d /etc/mihomo"
