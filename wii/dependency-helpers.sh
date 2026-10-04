#!/bin/sh
# Shared dependency checks; never reset a developer's checkout.
clone_at() {
	name=$1
	revision=$2
	directory="$WORKSPACE/$name"
	if [ ! -d "$directory/.git" ]; then
		git clone "https://github.com/NetSurf-browser/$name.git" "$directory"
		git -C "$directory" checkout --detach "$revision"
	fi
	verify_revision "$directory" "$revision"
}

verify_revision() {
	actual=$(git -C "$1" rev-parse HEAD)
	expected=$(git -C "$1" rev-parse "$2^{commit}")
	if [ "$actual" != "$expected" ]; then
		echo "Dependency revision mismatch: $1 (expected $expected, found $actual)" >&2
		exit 1
	fi
}

apply_patch_once() {
	if git -C "$1" apply --check "$2" 2>/dev/null; then
		git -C "$1" apply "$2"
	elif ! git -C "$1" apply --reverse --check "$2" 2>/dev/null; then
		echo "Dependency patch neither applies nor is already applied: $2" >&2
		exit 1
	fi
}
