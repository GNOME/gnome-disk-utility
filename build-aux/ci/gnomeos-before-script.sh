#!/bin/bash

export RUSTUP_HOME="${CI_PROJECT_DIR}/.rustup"
export CARGO_HOME="${CI_PROJECT_DIR}/.cargo"
export PATH="${CARGO_HOME}/bin:${PATH}"

if ! command -v rustc >/dev/null 2>&1; then
  curl --proto '=https' --tlsv1.2 --silent --show-error --fail https://sh.rustup.rs |
    sh -s -- -y --profile minimal --default-toolchain stable
fi

rustc --version
cargo --version
build-aux/ci/ci-helper.sh "INFO"
build-aux/ci/ci-helper.sh "GIT_INFO"
