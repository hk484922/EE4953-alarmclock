#!/usr/bin/env bash

set -euo pipefail

test_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
project_dir="$(cd -- "${test_dir}/.." && pwd)"
test_binary="$(mktemp /tmp/alarmclock-tests.XXXXXX)"

cleanup() {
    rm -f -- "${test_binary}"
}
trap cleanup EXIT

g++ \
    -std=c++11 \
    -Wall \
    -Wextra \
    -Werror \
    -I"${project_dir}/include" \
    "${test_dir}/clock_logic_test.cpp" \
    -o "${test_binary}"

"${test_binary}"
