#!/bin/bash
# Services bootstrapper inside chroot Ubuntu

export HOME=/root
export TERM=xterm-256color
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin

# 1. 启动 OpenSSH Server (给 Muse 和内网访问，端口 22)
mkdir -p /run/sshd
# 只杀 chroot 自己的 sshd（/usr/sbin/sshd），别碰 Termux 的 8022（2026-10-17 改）
pkill -9 -f "/usr/sbin/sshd" 2>/dev/null || true
sleep 1
/usr/sbin/sshd 2>/dev/null || true

# 2. 启动 Tailscale (直连模式，不走任何代理，使用内核 TUN，设备名 xperia)
unset ALL_PROXY HTTP_PROXY HTTPS_PROXY all_proxy http_proxy https_proxy
mkdir -p /var/lib/tailscale /run/tailscale
pkill -9 tailscaled 2>/dev/null || true
sleep 1
tailscaled --state=/var/lib/tailscale/tailscaled.state --socket=/run/tailscale/tailscaled.sock > /var/log/tailscaled.log 2>&1 &
sleep 3
tailscale --socket=/run/tailscale/tailscaled.sock up --hostname=xperia --netfilter-mode=off --ssh=false --reset 2>/dev/null || true
sleep 2
ip addr add 100.123.179.27/32 dev tailscale0 2>/dev/null || true
ip route replace 100.64.0.0/10 dev tailscale0 2>/dev/null || true
ip route replace 100.64.0.0/10 dev tailscale0 table 52 2>/dev/null || true

# 3. 设置外部代理变量（经 Tailscale 连接电脑 YJW 的代理端口 7897）
export HTTP_PROXY="http://100.83.157.82:7897"
export HTTPS_PROXY="http://100.83.157.82:7897"
export ALL_PROXY="http://100.83.157.82:7897"
export http_proxy="http://100.83.157.82:7897"
export https_proxy="http://100.83.157.82:7897"
export all_proxy="http://100.83.157.82:7897"
export NO_PROXY="localhost,127.0.0.1,100.123.179.27"
export no_proxy="localhost,127.0.0.1,100.123.179.27"

# 4. 配置并启动 OpenClaw Gateway (常驻后台)
openclaw config set proxy.enabled true 2>/dev/null || true
openclaw config set proxy.proxyUrl http://100.83.157.82:7897 2>/dev/null || true
pkill -9 -f "openclaw" 2>/dev/null || true
sleep 1
nohup openclaw gateway run --force </dev/null > /var/log/openclaw.log 2>&1 &

# 5. 启动 Antigravity 远程服务 (独立常驻后台进程，使用 serve 命令)
pkill -9 -f "agy remote-control serve" 2>/dev/null || true
pkill -9 -f "/usr/local/bin/agy" 2>/dev/null || true
sleep 1
nohup agy remote-control serve </dev/null > /var/log/agy-remote.log 2>&1 &

echo "All services (sshd, tailscale direct, agy remote serve via YJW proxy, openclaw via YJW proxy) initiated."

# 6. 启动 DeskPet HTTP & UDP 服务 (端口 8088 & 8888)
pkill -9 -f "desk-pet-server.py" 2>/dev/null || true
nohup python3 /usr/local/bin/desk-pet-server.py </dev/null >/var/log/desk-pet-server.log 2>&1 &
