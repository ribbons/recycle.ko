#!/usr/bin/env bats

# Copyright © 2024 Matt Robinson

@test "module fails to load without arguments" {
    run sudo insmod recycle.ko
    [[ $status -ne 0 ]]
    [[ ${lines[0]} == *"Invalid parameters" ]]
}

@test "module fails to load with first path to missing directory" {
    run sudo insmod recycle.ko paths="$(mktemp -up "$XDG_RUNTIME_DIR")"
    [[ $status -ne 0 ]]
    [[ ${lines[0]} == *"Invalid parameters" ]]
}

@test "module fails to load with first path to file" {
    tmpfile=$(mktemp -p "$XDG_RUNTIME_DIR")
    run sudo insmod recycle.ko paths="$tmpfile"
    rm "$tmpfile"
    [[ $status -ne 0 ]]
    [[ ${lines[0]} == *"Not a directory" ]]
}

load_with_paths() {
    rootdir=$(mktemp -dp "$XDG_RUNTIME_DIR")
    sudo insmod recycle.ko paths="$rootdir"
}

@test "module loads successfully with directory path" {
    load_with_paths
}

teardown() {
    sudo rmmod recycle
    [[ $rootdir ]] && rm -r "$rootdir"
    true
}
