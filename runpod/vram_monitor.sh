#!/bin/bash
# VRAM monitor — run alongside training to track GPU memory usage
# Usage: ./vram_monitor.sh [logfile] [interval_seconds]
#   ./vram_monitor.sh                        # defaults: vram.log, every 30s
#   ./vram_monitor.sh /workspace/output/brady-gut/vram.log 10

LOGFILE="${1:-vram.log}"
INTERVAL="${2:-30}"
PEAK_MB=0

echo "VRAM monitor started: logging to $LOGFILE every ${INTERVAL}s"
echo "timestamp,used_mb,total_mb,peak_mb,utilization_pct" > "$LOGFILE"

while true; do
    # Query GPU memory in MiB
    read USED TOTAL <<< $(nvidia-smi --query-gpu=memory.used,memory.total --format=csv,noheader,nounits | tr ',' ' ')

    if [ -n "$USED" ] && [ -n "$TOTAL" ]; then
        if [ "$USED" -gt "$PEAK_MB" ]; then
            PEAK_MB=$USED
        fi
        PCT=$(awk "BEGIN {printf \"%.1f\", $USED/$TOTAL*100}")
        TIMESTAMP=$(date '+%H:%M:%S')
        echo "$TIMESTAMP,$USED,$TOTAL,$PEAK_MB,$PCT" >> "$LOGFILE"
    fi

    sleep "$INTERVAL"
done
