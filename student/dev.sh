#!/usr/bin/env bash
set -euo pipefail

repo_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
image_target=dev
build_only=false
if [[ "${1:-}" == --tools ]]; then
    image_target=tools
    shift
fi

case "${1:-}" in
    -h|--help|help)
        cat <<'HELP'
Usage: ./dev.sh [--tools] [command ...]

  (no command)         Build the environment, then run scaffold checks.
  image                Build the environment only.
  shell                Open a shell in the mounted repository.
  exec COMMAND ...     Run any command inside the environment.
  unit S06.Q02         Run a labs/run.py command with its original arguments.
  list --stage 06      List questions and test registration.

--tools adds clang-format-22 and clang-tidy-22 to the minimal image.
LABS_JOBS sets build parallelism (default: 2, accepted range: 1-16).
FERRUM_BUILD_NETWORK overrides the Docker build network when needed.
HELP
        exit 0
        ;;
esac

if ! command -v docker >/dev/null 2>&1; then
    printf '%s\n' 'Docker is required. Install and start Docker, then retry ./dev.sh.' >&2
    exit 2
fi
if ! docker info >/dev/null 2>&1; then
    printf '%s\n' 'Cannot access the Docker daemon. Check Docker is running and your user can access it.' >&2
    exit 2
fi

case "${1:-scaffold}" in
    image)
        if [[ $# -ne 1 ]]; then
            printf '%s\n' 'image takes no arguments.' >&2
            exit 2
        fi
        build_only=true
        ;;
    shell)
        shift
        container_command=(bash "$@")
        ;;
    exec)
        shift
        if [[ $# -eq 0 ]]; then
            printf '%s\n' 'exec requires a command.' >&2
            exit 2
        fi
        container_command=("$@")
        ;;
    *)
        if [[ $# -eq 0 ]]; then
            set -- scaffold
        fi
        container_command=(python3 labs/run.py "$@")
        ;;
esac

image_name="ferrum-cpp-dev:debian13-gcc14-${image_target}"
build_options=(--target "$image_target" --tag "$image_name")
for proxy_key in http_proxy https_proxy no_proxy HTTP_PROXY HTTPS_PROXY NO_PROXY; do
    if [[ -n "${!proxy_key:-}" ]]; then
        build_options+=(--build-arg "$proxy_key")
    fi
done
build_network="${FERRUM_BUILD_NETWORK:-}"
if [[ -z "$build_network" && "$(uname -s)" == Linux ]]; then
    for proxy_endpoint in "${http_proxy:-${HTTP_PROXY:-}}" "${https_proxy:-${HTTPS_PROXY:-}}"; do
        if [[ "$proxy_endpoint" =~ ^https?://([^/@]+@)?(localhost|127[.]0[.]0[.]1|\[::1\])(:|/) ]]; then
            build_network=host
        fi
    done
fi
if [[ -n "$build_network" ]]; then
    build_options+=(--network "$build_network")
fi
docker build "${build_options[@]}" "$repo_dir/docker"
if [[ "$build_only" == true ]]; then
    exit 0
fi

run_options=(
    --rm --init --interactive
    --user "$(id -u):$(id -g)"
    --mount "type=bind,source=${repo_dir},target=/workspace"
    --workdir /workspace
    --env "LABS_JOBS=${LABS_JOBS:-2}"
    --env "LABS_BUILD_DIR=/workspace/build/docker-gcc14-${image_target}"
)
if [[ -t 0 && -t 1 ]]; then
    run_options+=(--tty)
fi

exec docker run "${run_options[@]}" "$image_name" "${container_command[@]}"
