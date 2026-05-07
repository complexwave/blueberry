#!/usr/bin/env bash
set -euo pipefail

BLUEBERRY="$(cd "$(dirname "$0")/.." && pwd)/blueberry"
BENCHDIR="$(cd "$(dirname "$0")" && pwd)"
RUNS=${RUNS:-3}

# benchmark_name  lua_file  ci_file  arg
benchmarks=(
    "binarytrees    1.lua  1.ci  12"
    "merkletrees    1.lua  1.ci  11"
    #"spectral-norm  1.lua  1.ci  100"   # blueberry: float print is ugly but runs
    #"nbody          1.lua  1.ci  500000" # blueberry: produces wrong results (number boxing issue)
    "nsieve         1.lua  1.ci  7"
    "maps           1.lua  1.ci  1000000"
)

sep="─────────────────────────────────────────────────────────"

time_cmd() {
    # returns seconds with 3 decimal places, best of $RUNS runs
    local best=""
    for ((r=1; r<=RUNS; r++)); do
        local t
        t=$( { time "$@" > /dev/null 2>&1; } 2>&1 )
        # extract real time — handles both "0m1.234s" and "1.234" formats
        # replace comma decimal separator with dot for non-C locales
        t=$(echo "$t" | tr ',' '.')
        local secs
        secs=$(echo "$t" | grep real | sed 's/.*\t//;s/real[[:space:]]*//' | sed 's/\([0-9]*\)m\([0-9.]*\)s/\1 \2/' | awk '{if(NF==2) print $1*60+$2; else print $1}')
        if [ -z "$best" ] || (( $(echo "$secs < $best" | bc -l) )); then
            best=$secs
        fi
    done
    printf "%s" "$best"
}

printf "\n%s\n" "$sep"
printf "  Blueberry Benchmark Suite — best of %d runs\n" "$RUNS"
printf "%s\n\n" "$sep"

for entry in "${benchmarks[@]}"; do
    read -r name luaf cif arg <<< "$entry"
    dir="$BENCHDIR/$name"

    printf "  %-16s (arg=%s)\n" "$name" "$arg"
    printf "  %-16s" ""

    # lua 5.4
    if command -v lua5.4 &>/dev/null; then
        t=$(time_cmd lua5.4 "$dir/$luaf" "$arg")
        printf "lua5.4: %7ss  " "$t"
    else
        printf "lua5.4: [missing]  "
    fi

    # luajit (JIT on)
    if command -v luajit &>/dev/null; then
        t=$(time_cmd luajit "$dir/$luaf" "$arg")
        printf "luajit: %7ss  " "$t"
    else
        printf "luajit: [missing]  "
    fi

    # luajit -joff
    if command -v luajit &>/dev/null; then
        t=$(time_cmd luajit -joff "$dir/$luaf" "$arg")
        printf "luajit-nojit: %7ss  " "$t"
    else
        printf "luajit-nojit: [missing]  "
    fi

    # blueberry
    t=$(time_cmd "$BLUEBERRY" "$dir/$cif" "$arg")
    printf "blueberry: %7ss" "$t"

    printf "\n\n"
done

printf "%s\n" "$sep"
