#!/usr/bin/env bats

# Copyright © 2024 Matt Robinson
#
# SPDX-License-Identifier: GPL-2.0-or-later

setup_file() {
    sudo dmesg -n 3
}

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

@test "epoch time in ms is appended after filename collision in recycle dir" {
    load_with_paths

    touch "$rootdir/inroot" "$rootdir/recycled/inroot"
    inode=$(stat -c '%i' "$rootdir/inroot")

    rmtime=$(date +%s%3N)
    rm "$rootdir/inroot"
    newfile=("$rootdir"/recycled/inroot.*)

    [[ -f ${newfile[0]} ]]
    [[ $inode -eq $(stat -c '%i' "${newfile[0]}") ]]

    suffix=${newfile[0]##*.}
    [[ $suffix -ge $rmtime ]]
}

@test "appended epoch time can disambiguate more than one unlink a second" {
    load_with_paths

    touch "$rootdir/inroot" "$rootdir/recycled/inroot"
    rm "$rootdir/inroot"
    touch "$rootdir/inroot"
    rm "$rootdir/inroot"
    touch "$rootdir/inroot"
    rm "$rootdir/inroot"
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
    local -r MAX_ROOTS=10
    local -a roots files inodes

    for((i = 0; i < MAX_ROOTS; i++)); do
        roots+=("root$i/recycled")
    done

    load_with_paths "${roots[@]}"

    for((i = 0; i < MAX_ROOTS; i++)); do
        files+=("$rootdir/root$i/inroot$i")
    done

    touch "${files[@]}"
    mapfile -t inodes < <(stat -c '%i' "${files[@]}")
    rm "${files[@]}"

    for((i = 0; i < MAX_ROOTS; i++)); do
        [[ ! -f "$rootdir/root$i/inroot$i" ]]
        [[ -f $rootdir/root$i/recycled/inroot$i ]]
        [[ ${inodes[$i]} -eq $(stat -c '%i' "$rootdir/root$i/recycled/inroot$i") ]]
    done
}

@test "file in recycle dir nested within outer recycle root is ignored" {
    load_with_paths innerroot/recycled recycled

    touch "$rootdir/innerroot/recycled/inrecycled"
    rm "$rootdir/innerroot/recycled/inrecycled"
    [[ ! -f $rootdir/recycled/innerroot/recycled/inrecycled ]]
}

@test "recycle succeeds even if recycle dir path not visible in namespace" {
    load_with_paths

    mkdir "$rootdir/subdir"
    touch "$rootdir/subdir/file"
    inode=$(stat -c '%i' "$rootdir/subdir/file")

    binddir=$(mktemp -d)
    sudo mount --bind "$rootdir/subdir" "$binddir"

    run sudo unshare -m bash <<EOS
        umount "$XDG_RUNTIME_DIR"
        rm "$binddir/file"
EOS

    sudo umount "$binddir"
    rmdir "$binddir"
    [[ $status -eq 0 ]]

    [[ ! -f "$rootdir/subdir/file" ]]
    [[ -f "$rootdir/recycled/subdir/file" ]]

    [[ $inode -eq $(stat -c '%i' "$rootdir/recycled/subdir/file") ]]
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
    overlength=$(printf "/$longname%.0s" {1..16})
    pathmax=${overlength:1:4096}

    cd "$rootdir"
    mkdir -p "$(dirname "$pathmax")"
    touch "$pathmax"

    run rm "$pathmax"
    [[ $status -ne 0 ]]
    [[ ${lines[0]} == *"File name too long" ]]

    [[ -f "$pathmax" ]]
}

teardown() {
    sudo rmmod recycle
    [[ $rootdir ]] && sudo rm -r "$rootdir"
    true
}
