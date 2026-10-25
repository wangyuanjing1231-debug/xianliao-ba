#!/bin/bash
# record-login-link.sh — agy 想弹浏览器登录页时，quota-service 不真打开浏览器，
# 改把链接记到桌面一个固定文件夹里的单个文件（每次追加一行，永远只有一个文件）。
# 用法（由系统的“打开浏览器”机制调用）：record-login-link.sh <链接>
DIR="$HOME/桌面/agy掉登录记录"
FILE="$DIR/记录.txt"
mkdir -p "$DIR"
if [ ! -f "$FILE" ]; then
  printf '%s\n' \
    'agy 掉登录记录' \
    '==============' \
    '下面每一行是 agy 一次想让你重新登录的时间和链接（最新在最下面）。' \
    '多数是网络抖动造成的假掉登录，过一会儿它自己就恢复了，不用管。' \
    '如果额度屏上的 Antigravity 数字长时间不更新，才需要处理：' \
    '把最下面一行的链接复制到浏览器打开、登录一次，然后把这个文件删掉即可。' \
    '' > "$FILE"
fi
printf '%s  %s\n' "$(date '+%Y-%m-%d %H:%M:%S')" "$*" >> "$FILE"
exit 0
