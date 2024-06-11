#!/usr/bin/env bats

# Copyright © 2024 Matt Robinson

@test "module fails to load without arguments" {
    run sudo insmod recycle.ko
    [[ $status -ne 0 ]]
    [[ ${lines[0]} == *"Invalid parameters" ]]
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
