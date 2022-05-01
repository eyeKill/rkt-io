#!/bin/bash
set -e

headerfile=$(echo "#include <x86intrin.h>" | \
             ${CPP:-cpp} - | \
             awk '/x86intrin.h/ {gsub(/"/, "", $3); print $3; exit 0 }')

if [[ -z "$headerfile" ]]; then
    echo "x86intrin.h not found" >&2
    exit 1
fi

dirname "$headerfile"
