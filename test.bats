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
    local -a recycledirs
    rootdir=$(mktemp -dp "$XDG_RUNTIME_DIR")

    for name in "${@:-recycled}"; do
        local recycledir=$rootdir/$name
        mkdir -p "$recycledir"
        recycledirs+=("$recycledir")
    done

    sudo insmod recycle.ko paths="$(IFS=,; echo "${recycledirs[*]}")"
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

@test "file mtime is updated when moved to recycle dir" {
    load_with_paths

    touch -t 200001010000 "$rootdir/inroot"

    rmtime=$EPOCHSECONDS
    rm "$rootdir/inroot"

    [[ $(stat -c '%X' "$rootdir/recycled/inroot") -ge rmtime ]]
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

@test "file from nested dirs moves to partly existing equivalent recycle dir" {
    load_with_paths

    mkdir -p "$rootdir/subdir1/subdir2" "$rootdir/recycled/subdir1"
    touch "$rootdir/subdir1/subdir2/file"
    inode=$(stat -c '%i' "$rootdir/subdir1/subdir2/file")

    rm "$rootdir/subdir1/subdir2/file"
    [[ ! -f $rootdir/subdir1/subdir2/file ]]

    [[ -f $rootdir/recycled/subdir1/subdir2/file ]]
    [[ $inode -eq $(stat -c '%i' "$rootdir/recycled/subdir1/subdir2/file") ]]
}

@test "file from nested dirs moves to created equivalent under recycle dir" {
    load_with_paths

    mkdir -p "$rootdir/subdir1/subdir2"
    touch "$rootdir/subdir1/subdir2/file"
    inode=$(stat -c '%i' "$rootdir/subdir1/subdir2/file")

    rm "$rootdir/subdir1/subdir2/file"
    [[ ! -f $rootdir/subdir1/subdir2/file ]]

    [[ -f $rootdir/recycled/subdir1/subdir2/file ]]
    [[ $inode -eq $(stat -c '%i' "$rootdir/recycled/subdir1/subdir2/file") ]]
}

@test "files under different roots are moved to correct recycle directories" {
    load_with_paths root1/recycled root2/recycled

    touch "$rootdir/root1/inroot1" "$rootdir/root2/inroot2"
    inode1=$(stat -c '%i' "$rootdir/root1/inroot1")
    inode2=$(stat -c '%i' "$rootdir/root2/inroot2")

    rm "$rootdir/root1/inroot1" "$rootdir/root2/inroot2"
    [[ ! -f $rootdir/root1/inroot1 && ! -f $rootdir/root2/inroot2 ]]

    [[ -f $rootdir/root1/recycled/inroot1 ]]
    [[ -f $rootdir/root2/recycled/inroot2 ]]

    [[ $inode1 -eq $(stat -c '%i' "$rootdir/root1/recycled/inroot1") ]]
    [[ $inode2 -eq $(stat -c '%i' "$rootdir/root2/recycled/inroot2") ]]
}

@test "error when creating subdirectory is reflected as unlink failure" {
    load_with_paths

    mkdir -p "$rootdir/sub"
    touch "$rootdir/sub/file" "$rootdir/recycled/sub"

    run rm "$rootdir/sub/file"
    [[ $status -ne 0 ]]
    [[ ${lines[0]} == *"Not a directory" ]]

    [[ -f "$rootdir/sub/file" ]]
}

@test "resulting path longer than PATH_MAX is reflected as unlink failure" {
    load_with_paths

    longname=$(printf '%-255s' "" | tr ' ' 'c')
    overlength=$rootdir$(printf "/$longname%.0s" {1..16})
    pathmax=${overlength:0:4086}

    mkdir -p "$(dirname "$pathmax")"
    touch "$pathmax"

    run rm "$pathmax"
    [[ $status -ne 0 ]]
    [[ ${lines[0]} == *"File name too long" ]]

    [[ -f "$pathmax" ]]
}

teardown() {
    sudo rmmod recycle
    [[ $rootdir ]] && rm -r "$rootdir"
    true
}
