#!/bin/bash
TIME=$(date "+%Y-%m-%d %H:%M:%S.%3N")
INPUT=$(cat)
printf "[%s] [STATUSLINE] %s\n" "$TIME" "$INPUT" >> /root/agy-status-test.log
printf "[AGY] active\n"
