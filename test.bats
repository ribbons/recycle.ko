#!/usr/bin/env bats

# Copyright © 2024 Matt Robinson

@test "module loads successfully" {
    sudo insmod recycle.ko
}

teardown_file() {
    sudo rmmod recycle || true
}
