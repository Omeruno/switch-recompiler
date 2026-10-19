#!/bin/sh
# Сверка интерпретатора x86 с настоящим процессором хоста.
# Нужен Linux x86/x86-64 с 32-битным gcc (пакет gcc-multilib).
#
#   sh tools/run_cpu_oracle.sh [число тестов на сид] [группа: int|fpu|sse|all]
set -eu
root="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
out="$root/build-host"
count="${1:-100000}"
group="${2:-all}"
mkdir -p "$out"
gcc -m32 -O1 -no-pie -I"$root/tests/cpu_oracle" "$root/tests/cpu_oracle/oracle.c" \
    "$root/tests/cpu_oracle/oracle_entry.S" -o "$out/oracle"
g++ -std=c++17 -O2 -Wall -I"$root" -I"$root/tests/cpu_oracle" "$root/tests/cpu_oracle/cpu_fuzz.cpp" \
    "$root"/source/cpu/*.cpp -o "$out/cpu_fuzz"
failed=0
for seed in 1 2 3; do
    if ! "$out/cpu_fuzz" "$out/oracle" "$count" "$seed" "$group" > "$out/cpu_fuzz_$seed.log"; then
        failed=1
        head -n 40 "$out/cpu_fuzz_$seed.log"
    fi
    grep "расхождений" "$out/cpu_fuzz_$seed.log"
done
exit "$failed"
