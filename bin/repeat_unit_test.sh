#!/usr/bin/env bash

# Copyright (c) YugaByte, Inc.

show_usage() {
  cat <<-EOT
Usage: ${0##*/} <test_executable_name> <test_filter> [<options>]
Runs the given test in a loop locally, collects statistics about successes/failures, and saves
logs.

Options:
  -h, --help
    Show usage.
  -p, --parallelism
    Run this many instances of the test in parallel.
  -n, --num-iter
    Run this many iterations of the test.
  -v <verbosity>
    Verbosity option passed to the test.
  -k, --keep-all-logs
    Keep all logs, not just failing tests' logs.
EOT
}

set -euo pipefail

. "${0%/*}"/../build-support/common-build-env.sh

script_name=${0##*/}
script_name_no_ext=${script_name%.sh}


if [[ $# -eq 0 ]]; then
  show_usage >&2
  exit 1
fi
positional_args=()
more_test_args=""
log_dir=""
declare -i parallelism=4
declare -i iteration=0
declare -i num_iter=1000
keep_all_logs=false
original_args=( "$@" )

while [[ $# -gt 0 ]]; do
  case "$1" in
    -h|--help)
      show_usage >&2
      exit 1
    ;;
    -v)
      more_test_args+=" $1=$2"
      shift
    ;;
    -p|--parallelism)
      parallelism=$2
      shift
    ;;
    --log_dir)
      # Used internally for parallel execution
      log_dir="$2"
      if [[ ! -d $log_dir ]]; then
        echo "Specified log directory '$log_dir' does not exist" >&2
        exit 1
      fi
      shift
    ;;
    -n|--num-iter)
      num_iter=$2
      shift
    ;;
    --iteration)
      # Used internally for parallel execution
      iteration=$2
      shift
    ;;
    -k|--keep-all-logs)
      keep_all_logs=true
    ;;
    *)
      positional_args+=( "$1" )
    ;;
  esac
  shift
done

declare -i -r num_pos_args=${#positional_args[@]}
if [[ $num_pos_args -ne 2 ]]; then
  show_usage >&2
  fatal "Expected exactly two positional arguments: <test_executable_name> <test_filter>" >&2
fi

test_executable_name=${positional_args[0]}
test_filter=${positional_args[1]}

test_executable=$YB_SRC_ROOT/build/latest/bin/$test_executable_name
if [[ -z $log_dir ]]; then
  log_dir=$HOME/logs/$script_name_no_ext/$test_executable_name/$test_filter/$(
    get_timestamp_for_filenames
  )
  mkdir -p "$log_dir"
fi

if [[ $iteration -gt 0 ]]; then
  log_path=$log_dir/$iteration.log
  # One iteration
  set +e
  "$test_executable" --gtest_filter="$test_filter" $more_test_args >"$log_path" 2>&1
  exit_code=$?
  set -e
  if [[ $exit_code -ne 0 ]]; then
    gzip "$log_path"
    echo "FAILED: iteration $iteration (log: $log_path.gz)"
  elif "$keep_all_logs"; then
    gzip "$log_path"
    echo "PASSED: iteration $iteration (log: $log_path.gz)"
  else
    echo "PASSED: iteration $iteration"
    rm -f "$log_path"
  fi
else
  # Parallel execution of many iterations
  seq 1 $num_iter | \
    xargs -P $parallelism -n 1 "$0" "${original_args[@]}" --log_dir "$log_dir" --iteration
fi