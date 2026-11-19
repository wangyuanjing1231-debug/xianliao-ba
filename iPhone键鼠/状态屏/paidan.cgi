#!/bin/sh
# 微雪板状态屏的中转：板子问路由器，路由器经 Tailscale 去 vpn-us 账本取清单或语音
TMP=/tmp/paidan.$$
trap 'rm -f "$TMP"' EXIT
case "$QUERY_STRING" in
    tts=*)
        RAW="${QUERY_STRING#tts=}"
        RAW="${RAW%%&*}"
        ID=$(printf '%s' "$RAW" | tr -cd '0-9')
        if [ -z "$ID" ]; then
            echo "Status: 400 Bad Request"
            echo "Content-Type: text/plain; charset=utf-8"
            echo ""
            echo "bad id"
            exit 0
        fi
        if uclient-fetch -q -T 10 -O "$TMP" "http://100.81.27.34:8893/tts?id=$ID" && [ -s "$TMP" ]; then
            LEN=$(wc -c < "$TMP")
            echo "Content-Type: application/octet-stream"
            echo "Content-Length: $LEN"
            echo "Connection: close"
            echo ""
            cat "$TMP"
        else
            echo "Status: 502 Bad Gateway"
            echo "Content-Type: text/plain; charset=utf-8"
            echo ""
            echo "tts fetch failed"
        fi
        ;;
    *)
        if uclient-fetch -q -T 5 -O "$TMP" http://100.81.27.34:8893/screen && [ -s "$TMP" ]; then
            LEN=$(wc -c < "$TMP")
            echo "Content-Type: text/plain; charset=utf-8"
            echo "Content-Length: $LEN"
            echo "Connection: close"
            echo ""
            cat "$TMP"
        else
            echo "Status: 502 Bad Gateway"
            echo "Content-Type: text/plain; charset=utf-8"
            echo ""
            echo "fetch failed"
        fi
        ;;
esac
