# Scheduling only (not a validity rule): block until the summed ps %cpu of all
# processes stays under 160 (20% of 8 cores) for 10 consecutive 2-second
# samples, or give up after $1 seconds (default 3600) with exit 1.
limit=${1:-3600}; start=$(date +%s); quiet=0
while [ $quiet -lt 10 ]; do
  [ $(( $(date +%s) - start )) -gt $limit ] && exit 1
  total=$(ps -Ao %cpu= | awk '{s+=$1} END {printf "%d", s}')
  if [ "$total" -lt 160 ]; then quiet=$((quiet+1)); else quiet=0; fi
  sleep 2
done
