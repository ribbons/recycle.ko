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
    local recycledir
    rootdir=$(mktemp -dp "$XDG_RUNTIME_DIR")
    recycledir=$rootdir/${1:-recycled}
    mkdir -p "$recycledir"
    sudo insmod recycle.ko paths="$recycledir"
}

@test "module loads successfully with directory path" {
    load_with_paths
}

@test "file from different mount is ignored" {
    load_with_paths

    tempfile=$(mktemp)
    rm "$tempfile"
    [[ ! -f $tempfile ]]
}

@test "file from below recycle root is ignored" {
    load_with_paths nested/recycled

    touch "$rootdir/inroot"
    rm "$rootdir/inroot"
    [[ ! -f $rootdir/inroot ]]
}

@test "file within recycle directory is ignored" {
    load_with_paths

    touch "$rootdir/recycled/inrecycled"
    rm "$rootdir/recycled/inrecycled"
    [[ ! -f $rootdir/recycled/inrecycled ]]
}

@test "file from recycle root moves to recycle dir root" {
    load_with_paths

    touch "$rootdir/inroot"
    inode=$(stat -c '%i' "$rootdir/inroot")

    rm "$rootdir/inroot"
    [[ ! -f $rootdir/inroot ]]

    [[ -f $rootdir/recycled/inroot ]]
    [[ $inode -eq $(stat -c '%i' "$rootdir/recycled/inroot") ]]
}

@test "file from nested dirs moves to existing equivalent under recycle dir" {
    load_with_paths

    mkdir -p "$rootdir/subdir1/subdir2" "$rootdir/recycled/subdir1/subdir2"
    touch "$rootdir/subdir1/subdir2/file"
    inode=$(stat -c '%i' "$rootdir/subdir1/subdir2/file")

    rm "$rootdir/subdir1/subdir2/file"
    [[ ! -f $rootdir/subdir1/subdir2/file ]]

    [[ -f $rootdir/recycled/subdir1/subdir2/file ]]
    [[ $inode -eq $(stat -c '%i' "$rootdir/recycled/subdir1/subdir2/file") ]]
}

teardown() {
    sudo rmmod recycle
    [[ $rootdir ]] && rm -r "$rootdir"
    true
}
