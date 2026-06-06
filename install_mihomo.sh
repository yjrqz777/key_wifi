#!/usr/bin/env bash
set -e

# sudo apt update
# sudo apt install -y curl wget unzip tar python3

cd /tmp

MIHOMO_URL=$(python3 - <<'PY'
import json, urllib.request
api='https://api.github.com/repos/MetaCubeX/mihomo/releases/latest'
with urllib.request.urlopen(api) as r:
    data=json.load(r)
for a in data['assets']:
    name=a['name'].lower()
    if 'linux-amd64' in name and name.endswith('.gz') and 'compatible' not in name:
        print(a['browser_download_url'])
        break
PY
)

if [ -z "$MIHOMO_URL" ]; then
  echo "未找到 mihomo linux-amd64 下载地址"
  exit 1
fi

wget -O mihomo.gz "$MIHOMO_URL"
gunzip -f mihomo.gz
chmod +x mihomo
sudo mv mihomo /usr/local/bin/mihomo

sudo mkdir -p /etc/mihomo/ui

SUB_URL="${SUB_URL:-http://82.41.180.151:16052/91a0c710-ec9d-4ee7-a512-de06fc5b9f26/clmi.yaml}"
echo "正在拉取 mihomo 配置：$SUB_URL"
curl -L --retry 5 --retry-delay 3 --retry-all-errors --connect-timeout 10 --max-time 60 -o config.yaml "$SUB_URL"

if ! grep -q "external-controller:" config.yaml; then
  cat >> config.yaml <<'EOF'

external-controller: 127.0.0.1:9090
external-ui: /etc/mihomo/ui
secret: ""
EOF
else
  python3 - <<'PY'
from pathlib import Path
p = Path('config.yaml')
lines = p.read_text().splitlines()
out = []
seen_ui = seen_secret = False
for line in lines:
    if line.startswith('external-controller:'):
        out.append('external-controller: 127.0.0.1:9090')
    elif line.startswith('external-ui:'):
        out.append('external-ui: /etc/mihomo/ui')
        seen_ui = True
    elif line.startswith('secret:'):
        out.append('secret: ""')
        seen_secret = True
    else:
        out.append(line)
if not seen_ui:
    out.append('external-ui: /etc/mihomo/ui')
if not seen_secret:
    out.append('secret: ""')
p.write_text('\n'.join(out) + '\n')
PY
fi

sudo cp config.yaml /etc/mihomo/config.yaml

curl -L --retry 5 --retry-delay 3 --retry-all-errors -o metacubexd.zip https://github.com/MetaCubeX/metacubexd/archive/refs/heads/gh-pages.zip
rm -rf metacubexd-gh-pages
unzip -o metacubexd.zip
sudo rm -rf /etc/mihomo/ui/*
sudo cp -r metacubexd-gh-pages/* /etc/mihomo/ui/

if [ "$(ps -p 1 -o comm=)" = "systemd" ]; then
  sudo tee /etc/systemd/system/mihomo.service >/dev/null <<'EOF'
[Unit]
Description=mihomo Daemon
After=network-online.target
Wants=network-online.target

[Service]
Type=simple
ExecStart=/usr/local/bin/mihomo -d /etc/mihomo
Restart=on-failure
RestartSec=5
LimitNOFILE=1048576

[Install]
WantedBy=multi-user.target
EOF

  sudo systemctl daemon-reload
  sudo systemctl enable --now mihomo
  systemctl status mihomo --no-pager
else
  echo "当前环境 PID 1 不是 systemd，跳过 systemd 服务安装。"
  echo "可手动启动 mihomo：sudo /usr/local/bin/mihomo -d /etc/mihomo"
fi

mihomo -v
