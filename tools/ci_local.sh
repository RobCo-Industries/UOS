#!/bin/sh
# Run the CI build job in the robco-ci container, as GitHub Actions does:
#   make CI=1 all test-host
# The tree is copied to build/ci-local/src, so the container's outputs
# never mix with the host's. The container runs as the current user.
#
# Usage: tools/ci_local.sh [make arguments]
#   With no arguments it runs the build job's targets. Pass other
#   targets to run them instead, for example
#   "-k all test-host" or "test-target BOARD=raspi0".
# CI_IMAGE overrides the image. By default it is
# ghcr.io/robco-industries/robco-ci:<tag>, or robco-ci:<tag> when that
# local tag exists, with <tag> from external/robco-core/ci/IMAGE_TAG.
# To build the image locally:
#   docker build -f external/robco-core/ci/Dockerfile \
#     -t robco-ci:<tag> external/robco-core/ci
set -eu

root=$(cd "$(dirname "$0")/.." && pwd)
tag=$(cat "$root/external/robco-core/ci/IMAGE_TAG")
if [ -z "${CI_IMAGE:-}" ]; then
    if docker image inspect "robco-ci:$tag" >/dev/null 2>&1; then
        CI_IMAGE="robco-ci:$tag"
    else
        CI_IMAGE="ghcr.io/robco-industries/robco-ci:$tag"
    fi
fi

work="$root/build/ci-local"
mkdir -p "$work"
rsync -a --delete --exclude /build --exclude /.venv --exclude .git \
    "$root/" "$work/src/"
# check-doc-links reads the submodule's HEAD. Give each checkout a
# minimal .git directory with its commit, as actions/checkout leaves.
for d in . external/robco-core; do
    rm -rf "$work/src/$d/.git"
    mkdir -p "$work/src/$d/.git"
    git -C "$root/$d" rev-parse HEAD > "$work/src/$d/.git/HEAD"
done

[ $# -gt 0 ] || set -- all test-host
echo "ci-local: $CI_IMAGE: make CI=1 $*"
exec docker run --rm --user "$(id -u):$(id -g)" -e HOME=/tmp \
    -v "$work/src:/work" -w /work "$CI_IMAGE" make CI=1 "$@"
