#!/bin/sh
set -eu

LC_ALL=C
export LC_ALL

HARNESS_DIR=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
readonly HARNESS_DIR
readonly HARNESS_C="$HARNESS_DIR/benchmark.c"
readonly BUILD_ID=cc-O2-NDEBUG-g-fno-omit-frame-pointer-fts5-threadsafe1-memstatus1
readonly DATASET_ROWS=1000000
readonly SMOKE_ROWS=10000
readonly WARMUPS=3
readonly PAIRS=15
readonly CPU=4
readonly EVENTS=cycles,instructions,branches,branch-misses,cache-misses,task-clock
readonly TIME_BIN=/usr/bin/time

usage() {
  printf '%s\n' \
    "usage: $0 --baseline-only BASELINE_SQLITE3_C OUTPUT_DIR" \
    "       $0 BASELINE_SQLITE3_C CANDIDATE_SQLITE3_C OUTPUT_DIR"
}

die() {
  printf 'benchmark-runner: %s\n' "$*" >&2
  exit 1
}

require_file() {
  if [ ! -f "$1" ] || [ ! -r "$1" ]; then
    die "unreadable file: $1"
  fi
}

require_command() {
  command -v "$1" >/dev/null 2>&1 || die "required command not found: $1"
}

safe_token() {
  case $1 in
    ''|*[!ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789._-]*)
      return 1
      ;;
    *) return 0 ;;
  esac
}

compile_variant() {
  compile_variant_name=$1
  compile_variant_source=$2
  compile_variant_dir=$(CDPATH='' cd -- "$(dirname -- "$compile_variant_source")" && pwd)
  compile_variant_hash_line=$(sha256sum -- "$compile_variant_source")
  compile_variant_sha=${compile_variant_hash_line%% *}

  case $compile_variant_sha in
    ????????*) ;;
    *) die "could not determine SHA-256 for $compile_variant_source" ;;
  esac
  compile_variant_bin="$build_dir/benchmark-$compile_variant_name"
  cc -O2 -DNDEBUG -g -fno-omit-frame-pointer \
    -DSQLITE_ENABLE_FTS5 -DSQLITE_THREADSAFE=1 -DSQLITE_DEFAULT_MEMSTATUS=1 \
    -I "$compile_variant_dir" \
    "-DBENCH_AMALGAMATION_SHA=\"$compile_variant_sha\"" \
    "-DBENCH_BUILD_ID=\"$BUILD_ID\"" \
    "$HARNESS_C" "$compile_variant_source" -ldl -lpthread -lm \
    -o "$compile_variant_bin"
}

verify_perf_output() {
  verify_perf_event=$1
  awk -F, -v event="$verify_perf_event" '
    $3 == event && $1 !~ /</ { found=1 }
    END { exit(found ? 0 : 1) }
  ' "$perf_file" || die "perf stat did not collect $verify_perf_event"
}

run_sample() {
  sample_variant=$1
  sample_bin=$2
  sample_sha=$3
  sample_n=$4
  sample_label=$5

  safe_token "$sample_variant" || die "unsafe variant token: $sample_variant"
  safe_token "$sample_label" || die "unsafe sample token: $sample_label"
  sample_stem="$sample_variant-sha-$sample_sha-n-$sample_n-sample-$sample_label"
  harness_file="$raw_dir/$sample_stem.harness"
  perf_file="$raw_dir/$sample_stem.perf.csv"
  rss_file="$raw_dir/$sample_stem.max-rss"
  stderr_file="$raw_dir/$sample_stem.stderr"
  [ ! -e "$harness_file" ] || die "refusing to overwrite raw output: $harness_file"

  perf stat -x, -e "$EVENTS" -o "$perf_file" -- \
    "$TIME_BIN" -f 'max_rss_kib=%M' -o "$rss_file" \
    taskset -c "$CPU" "$sample_bin" sample "$dataset_file" \
    "$sample_n" "$sample_label" "$sample_variant" \
    >"$harness_file" 2>"$stderr_file" || die "sample failed: $sample_stem"

  [ -s "$harness_file" ] || die "empty harness output: $sample_stem"
  [ -s "$perf_file" ] || die "empty perf output: $sample_stem"
  [ -s "$rss_file" ] || die "empty Max RSS output: $sample_stem"
  sample_result=$(cat "$harness_file")
  case $sample_result in
    *' guard=ok') ;;
    *) die "harness guard failed: $sample_stem" ;;
  esac
  sample_rss=$(cat "$rss_file")
  case $sample_rss in
    max_rss_kib=*[!0123456789]*|max_rss_kib=) die "invalid Max RSS output: $sample_stem" ;;
  esac
  for perf_event in cycles instructions branches branch-misses cache-misses task-clock; do
    verify_perf_output "$perf_event"
  done
  printf '%s\n' "$sample_result"
}

prepare_dataset() {
  prepare_rows=$1

  if [ -e "$dataset_file" ]; then
    return
  fi
  prepare_stdout="$run_dir/prepare.harness"
  prepare_stderr="$run_dir/prepare.stderr"
  "$baseline_bin" prepare "$dataset_file" "$prepare_rows" \
    >"$prepare_stdout" 2>"$prepare_stderr" || die "dataset preparation failed"
  prepare_result=$(cat "$prepare_stdout")
  case $prepare_result in
    "mode=prepare rows=$prepare_rows payload_bytes=256 transaction=single guard=ok") ;;
    *) die "unexpected prepare output" ;;
  esac
}

if [ "$#" -eq 3 ] && [ "$1" = "--baseline-only" ]; then
  run_mode=baseline-only
  baseline_source=$2
  candidate_source=
  output_root=$3
elif [ "$#" -eq 3 ]; then
  run_mode=compare
  baseline_source=$1
  candidate_source=$2
  output_root=$3
else
  usage >&2
  exit 1
fi

require_file "$HARNESS_C"
require_file "$baseline_source"
if [ "$run_mode" = compare ]; then
  require_file "$candidate_source"
fi
require_command cc
require_command perf
require_command taskset
require_command sha256sum
require_file "$TIME_BIN"

mkdir -p -- "$output_root"
run_number=1
while [ -e "$output_root/run-$run_number" ]; do
  run_number=$((run_number + 1))
done
run_dir="$output_root/run-$run_number"
raw_dir="$run_dir/raw"
build_dir="$run_dir/build"
mkdir -- "$run_dir"
mkdir -- "$raw_dir"
mkdir -- "$build_dir"

compile_variant baseline "$baseline_source"
baseline_bin=$compile_variant_bin
baseline_sha=$compile_variant_sha
if [ "$run_mode" = compare ]; then
  compile_variant candidate "$candidate_source"
  candidate_bin=$compile_variant_bin
  candidate_sha=$compile_variant_sha
fi

if [ "$run_mode" = baseline-only ]; then
  dataset_file="$output_root/dataset-rows-$SMOKE_ROWS-payload-256.db"
  prepare_dataset "$SMOKE_ROWS"
  run_sample baseline "$baseline_bin" "$baseline_sha" "$SMOKE_ROWS" smoke
  exit 0
fi

dataset_file="$output_root/dataset-rows-$DATASET_ROWS-payload-256.db"
prepare_dataset "$DATASET_ROWS"
printf '%s\n' \
  'repetitions=1' \
  'calibration=not_applicable_fresh_process_connection_memory_highwater' \
  'warmups=3' \
  'paired_samples=15' \
  "baseline_sha=$baseline_sha" \
  "candidate_sha=$candidate_sha" \
  "build=$BUILD_ID" \
  "cpu=$CPU" \
  "events=$EVENTS" \
  >"$run_dir/config.keyval"

for size in 10000 30000 100000 300000 1000000; do
  warmup=1
  while [ "$warmup" -le "$WARMUPS" ]; do
    run_sample baseline "$baseline_bin" "$baseline_sha" "$size" "warmup-$warmup"
    run_sample candidate "$candidate_bin" "$candidate_sha" "$size" "warmup-$warmup"
    warmup=$((warmup + 1))
  done

  pair=1
  while [ "$pair" -le "$PAIRS" ]; do
    if [ $((pair % 2)) -eq 1 ]; then
      run_sample baseline "$baseline_bin" "$baseline_sha" "$size" "pair-$pair-ab-first"
      run_sample candidate "$candidate_bin" "$candidate_sha" "$size" "pair-$pair-ab-second"
    else
      run_sample candidate "$candidate_bin" "$candidate_sha" "$size" "pair-$pair-ba-first"
      run_sample baseline "$baseline_bin" "$baseline_sha" "$size" "pair-$pair-ba-second"
    fi
    pair=$((pair + 1))
  done
done
