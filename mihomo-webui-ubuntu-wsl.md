---
title: Ubuntu/WSL 安装 mihomo + MetaCubeXD Web UI 并解决订阅 CORS 问题
date: 2026-05-27 23:00:00
tags:
  - Ubuntu
  - WSL
  - mihomo
  - Clash
  - MetaCubeXD
categories:
  - Linux
---

本文记录在 Ubuntu/WSL 环境中安装 mihomo、配置 MetaCubeXD Web UI，以及解决 Web UI 拉取远程订阅时遇到 CORS 报错的过程。

## 背景

之前使用的是 Clash Verge，卸载后希望改成更轻量的组合：

- mihomo 作为 Clash.Meta 内核
- MetaCubeXD 作为 Web UI
- systemd 管理后台服务

如果环境是 WSL 或容器，可能不能使用 systemd，需要手动启动 mihomo。

## 卸载 Clash Verge

先查看安装包名：

```bash
dpkg -l | grep -i clash
```

如果看到 `clash-verge`，卸载：

```bash
sudo apt purge clash-verge
sudo apt autoremove
```

如需清理用户配置：

```bash
rm -rf ~/.config/clash-verge
rm -rf ~/.local/share/clash-verge
```

## 安装 mihomo 和 Web UI

可以创建安装脚本：

```bash
nano install_mihomo.sh
```

写入：

```bash
#!/usr/bin/env bash
set -e

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

curl -L --retry 5 --retry-delay 3 --retry-all-errors \
  -o metacubexd.zip \
  https://github.com/MetaCubeX/metacubexd/archive/refs/heads/gh-pages.zip

rm -rf metacubexd-gh-pages
unzip -o metacubexd.zip
sudo rm -rf /etc/mihomo/ui/*
sudo cp -r metacubexd-gh-pages/* /etc/mihomo/ui/

mihomo -v
```

添加执行权限并运行：

```bash
chmod +x install_mihomo.sh
./install_mihomo.sh
```

如果下载 GitHub 文件时出现 SSL 中断，可以多试几次，或者先设置代理：

```bash
export https_proxy=http://127.0.0.1:7890
export http_proxy=http://127.0.0.1:7890
./install_mihomo.sh
```

## 单独拉取订阅配置

Web UI 直接访问远程订阅地址时，可能会报：

```text
Access to fetch at 'http://example.com/config.yaml' from origin 'http://127.0.0.1:9090' has been blocked by CORS policy
```

这是浏览器的 CORS 限制。解决办法是不要让 Web UI 直接拉订阅，而是在本机用脚本拉取配置，再写入 `/etc/mihomo/config.yaml`。

创建脚本：

```bash
nano fetch_mihomo_config.sh
```

写入：

```bash
#!/usr/bin/env bash
set -euo pipefail

SUB_URL="${SUB_URL:-你的订阅地址}"
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
```

添加执行权限：

```bash
chmod +x fetch_mihomo_config.sh
```

使用默认订阅地址：

```bash
./fetch_mihomo_config.sh
```

使用新的订阅地址：

```bash
SUB_URL="http://example.com/config.yaml" ./fetch_mihomo_config.sh
```

## 启动 mihomo

如果系统支持 systemd，可以创建服务：

```bash
sudo nano /etc/systemd/system/mihomo.service
```

写入：

```ini
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
```

启动并设置自启：

```bash
sudo systemctl daemon-reload
sudo systemctl enable --now mihomo
```

如果是在 WSL 或容器里，可能会遇到：

```text
System has not been booted with systemd as init system (PID 1). Can't operate.
```

这说明当前环境不能使用 systemd，可以手动启动：

```bash
sudo /usr/local/bin/mihomo -d /etc/mihomo
```

## 打开 Web UI

启动 mihomo 后访问：

```text
http://127.0.0.1:9090/ui
```

如果能打开页面，说明 Web UI 已经正常加载。配置已经由本机脚本写入 `/etc/mihomo/config.yaml`，因此不需要在 Web UI 里再拉远程订阅。

## 常见问题

### 1. Web UI 拉订阅提示 CORS

这是正常的浏览器安全限制。不要在 Web UI 中直接填远程订阅地址，改用脚本拉取：

```bash
./fetch_mihomo_config.sh
```

### 2. GEOIP 数据库下载失败

如果日志里出现：

```text
can't download MMDB: context deadline exceeded
```

可以手动下载：

```bash
cd /etc/mihomo
sudo curl -L --retry 5 -o Country.mmdb https://github.com/MetaCubeX/meta-rules-dat/releases/download/latest/country.mmdb
```

或者临时删除配置里的 GEOIP 规则：

```yaml
- GEOIP,LAN,DIRECT
- GEOIP,CN,DIRECT
```

### 3. systemctl 不能使用

检查 PID 1：

```bash
ps -p 1 -o comm=
```

如果不是 `systemd`，就不能用 `systemctl`，请手动启动 mihomo。
