#!/bin/sh
set -eu

LC_ALL=C
TERM=dumb
export LC_ALL TERM

CPU=4
CALIBRATION_TARGET_NS=250000000
MAX_REPEATS=1000000000
WARMUPS=3
PAIRS=15
M_VALUES='128 256 512 1024 2048 4096'
COMMON_COMPILE_FLAGS='-O2 -DNDEBUG -g -fno-omit-frame-pointer -DSQLITE_ENABLE_FTS5 -DSQLITE_THREADSAFE=1 -DSQLITE_DEFAULT_MEMSTATUS=1'

usage() {
  printf '%s\n' "usage: $0 BASELINE_SQLITE3_C OUTPUT_DIR" >&2
  printf '%s\n' "       $0 BASELINE_SQLITE3_C CANDIDATE_SQLITE3_C OUTPUT_DIR" >&2
}

die() {
  printf 'error=%s\n' "$*" >&2
  exit 1
}

require_command() {
  command -v "$1" >/dev/null 2>&1 || die "missing-command-$1"
}

absolute_file() {
  path=$1
  [ -f "$path" ] || die "missing-file-$path"
  dir=$(CDPATH='' cd "$(dirname "$path")" && pwd) || die "unreadable-directory-$path"
  printf '%s/%s\n' "$dir" "$(basename "$path")"
}

field() {
  file=$1
  key=$2
  awk -F= -v key="$key" '
    $1==key { count++; value=$2 }
    END {
      if( count!=1 || value=="" ) exit 1
      print value
    }
  ' "$file"
}

numeric_field() {
  value=$(field "$1" "$2") || die "missing-field-$2-in-$1"
  case $value in
    ''|*[!0-9]*) die "non-numeric-field-$2-in-$1" ;;
  esac
  printf '%s\n' "$value"
}

validate_sample() {
  raw=$1
  variant=$2
  sha=$3
  build=$4
  m=$5
  sample=$6
  repeats=$7
  expected_bytes=$((m * 15))

  [ "$(field "$raw" mode)" = sample ] || die "unexpected-mode-$raw"
  [ "$(field "$raw" sha)" = "$sha" ] || die "unexpected-sha-$raw"
  [ "$(field "$raw" build)" = "$build" ] || die "unexpected-build-$raw"
  [ "$(field "$raw" variant)" = "$variant" ] || die "unexpected-variant-$raw"
  [ "$(field "$raw" m)" = "$m" ] || die "unexpected-m-$raw"
  [ "$(field "$raw" sample)" = "$sample" ] || die "unexpected-sample-$raw"
  [ "$(field "$raw" repeats)" = "$repeats" ] || die "unexpected-repeats-$raw"
  [ "$(field "$raw" calls)" = "$repeats" ] || die "unexpected-calls-$raw"
  [ "$(field "$raw" output_bytes)" = "$expected_bytes" ] || die "unexpected-output-bytes-$raw"
  [ "$(field "$raw" guard)" = ok ] || die "failed-guard-$raw"
  numeric_field "$raw" elapsed_ns >/dev/null
  numeric_field "$raw" heap_highwater >/dev/null
}

run_direct() {
  prefix=$1
  bin=$2
  variant=$3
  sha=$4
  build=$5
  m=$6
  sample=$7
  repeats=$8

  taskset -c "$CPU" "$bin" \
    --mode sample \
    --m "$m" \
    --repeats "$repeats" \
    --sample "$sample" \
    --variant "$variant" \
    --sha "$sha" \
    --build "$build" \
    >"$prefix.raw" 2>"$prefix.stderr"
  validate_sample "$prefix.raw" "$variant" "$sha" "$build" "$m" "$sample" "$repeats"
}

run_dump() {
  prefix=$1
  bin=$2
  variant=$3
  sha=$4
  build=$5
  m=$6

  taskset -c "$CPU" "$bin" \
    --mode dump \
    --m "$m" \
    --repeats 1 \
    --sample dump \
    --variant "$variant" \
    --sha "$sha" \
    --build "$build" \
    >"$prefix.dump" 2>"$prefix.dump.stderr"
}

run_stat() {
  prefix=$1
  bin=$2
  variant=$3
  sha=$4
  build=$5
  m=$6
  sample=$7
  repeats=$8

  taskset -c "$CPU" perf stat -x, -o "$prefix.perf-stat.csv" -- "$bin" \
    --mode sample \
    --m "$m" \
    --repeats "$repeats" \
    --sample "$sample" \
    --variant "$variant" \
    --sha "$sha" \
    --build "$build" \
    >"$prefix.raw" 2>"$prefix.stderr"
  validate_sample "$prefix.raw" "$variant" "$sha" "$build" "$m" "$sample" "$repeats"
  [ -s "$prefix.perf-stat.csv" ] || die "empty-perf-stat-$prefix"
}

run_record() {
  prefix=$1
  bin=$2
  variant=$3
  sha=$4
  build=$5
  m=$6
  repeats=$7

  taskset -c "$CPU" perf record -o "$prefix.perf.data" --call-graph dwarf -- "$bin" \
    --mode sample \
    --m "$m" \
    --repeats "$repeats" \
    --sample profile \
    --variant "$variant" \
    --sha "$sha" \
    --build "$build" \
    >"$prefix.raw" 2>"$prefix.stderr"
  validate_sample "$prefix.raw" "$variant" "$sha" "$build" "$m" profile "$repeats"
  [ -s "$prefix.perf.data" ] || die "empty-perf-record-$prefix"
  perf report --stdio --no-children -i "$prefix.perf.data" \
    >"$prefix.perf-report.txt" 2>"$prefix.perf-report.stderr"
}

calibrate_repeats() {
  directory=$1
  bin=$2
  variant=$3
  sha=$4
  build=$5
  m=$6
  repeats=1
  iteration=1

  mkdir -p "$directory"
  while :; do
    iteration_id=$(printf '%02d' "$iteration")
    prefix="$directory/calibration-$iteration_id"
    sample="calibration-$iteration_id"
    run_direct "$prefix" "$bin" "$variant" "$sha" "$build" "$m" "$sample" "$repeats"
    elapsed=$(numeric_field "$prefix.raw" elapsed_ns)
    if awk -v elapsed="$elapsed" -v target="$CALIBRATION_TARGET_NS" \
      'BEGIN { exit !(elapsed>=target) }'
    then
      printf '%s\n' "$repeats"
      return 0
    fi
    [ "$iteration" -lt 64 ] || die "calibration-did-not-reach-target-m$m"
    next_repeats=$(awk \
      -v repeats="$repeats" \
      -v elapsed="$elapsed" \
      -v target="$CALIBRATION_TARGET_NS" \
      -v maximum="$MAX_REPEATS" \
      'BEGIN {
        if( elapsed==0 ){
          next_repeats = repeats * 10
        }else{
          next_repeats = int((repeats * target + elapsed - 1) / elapsed)
        }
        if( next_repeats<=repeats ) next_repeats = repeats + 1
        if( next_repeats>maximum ) exit 1
        printf "%.0f\n", next_repeats
      }'
    ) || die "calibration-repeat-overflow-m$m"
    case $next_repeats in
      ''|*[!0-9]*) die "invalid-calibration-repeats-m$m" ;;
    esac
    repeats=$next_repeats
    iteration=$((iteration + 1))
  done
}

compile_variant() {
  variant=$1
  source=$2
  source_dir=$(dirname "$source")

  current_bin="$run_dir/$variant"
  current_source_sha=$(sha256sum "$source" | awk '{print $1}')
  cc -O2 -DNDEBUG -g -fno-omit-frame-pointer -DSQLITE_ENABLE_FTS5 \
    -DSQLITE_THREADSAFE=1 -DSQLITE_DEFAULT_MEMSTATUS=1 \
    -I"$source_dir" "$source" "$benchmark_source" -lm -o "$current_bin"
  current_build_sha=$(sha256sum "$current_bin" | awk '{print $1}')
  [ -n "$current_source_sha" ] || die "missing-source-sha-$variant"
  [ -n "$current_build_sha" ] || die "missing-build-sha-$variant"
  printf 'variant=%s\nsource=%s\nsha=%s\nbuild=%s\n' \
    "$variant" "$source" "$current_source_sha" "$current_build_sha" \
    >"$run_dir/$variant.build"
}

if [ "${1-}" = --help ]; then
  usage
  exit 0
fi

case $# in
  2)
    baseline_source=$(absolute_file "$1")
    candidate_source=
    output_root=$2
    ;;
  3)
    baseline_source=$(absolute_file "$1")
    candidate_source=$(absolute_file "$2")
    output_root=$3
    ;;
  *)
    usage
    exit 1
    ;;
esac

require_command awk
require_command cc
require_command dirname
require_command mkdir
require_command sha256sum
require_command taskset
if [ -n "$candidate_source" ]; then
  require_command cmp
  require_command perf
fi
[ -d "/sys/devices/system/cpu/cpu$CPU" ] || die "missing-cpu-$CPU"

script_dir=$(CDPATH='' cd "$(dirname "$0")" && pwd) || die "unreadable-script-directory"
benchmark_source="$script_dir/benchmark.c"
[ -f "$benchmark_source" ] || die "missing-benchmark-source-$benchmark_source"

mkdir -p "$output_root"
output_root=$(CDPATH='' cd "$output_root" && pwd) || die "unreadable-output-directory-$output_root"
run_number=1
while :; do
  run_id=$(printf '%03d' "$run_number")
  run_dir="$output_root/run-$run_id"
  if [ ! -e "$run_dir" ]; then
    mkdir "$run_dir" || die "cannot-create-run-directory-$run_dir"
    break
  fi
  run_number=$((run_number + 1))
  [ "$run_number" -le 999 ] || die "too-many-existing-runs-$output_root"
done

cp "$benchmark_source" "$run_dir/benchmark.c"
cp "$0" "$run_dir/run.sh"
chmod +x "$run_dir/run.sh"
printf '%s\n' "$COMMON_COMPILE_FLAGS" >"$run_dir/compile-flags.txt"
cc --version >"$run_dir/cc-version.txt" 2>&1
if [ -n "$candidate_source" ]; then
  perf --version >"$run_dir/perf-version.txt" 2>&1
else
  printf '%s\n' unavailable >"$run_dir/perf-version.txt"
fi
printf \
  'cpu=%s\ncalibration_target_ns=%s\nmax_repeats=%s\nwarmups=%s\npaired_ab_ba_pairs=%s\nattribution_stop_gate_percent=10\nprofile_scope=one-frozen-repetition-callgraph-per-variant-and-m\n' \
  "$CPU" "$CALIBRATION_TARGET_NS" "$MAX_REPEATS" "$WARMUPS" "$PAIRS" \
  >"$run_dir/runner.config"

compile_variant baseline "$baseline_source"
baseline_bin=$current_bin
baseline_sha=$current_source_sha
baseline_build=$current_build_sha

if [ -z "$candidate_source" ]; then
  calibration_dir="$run_dir/m-128/calibration"
  baseline_repeats=$(calibrate_repeats \
    "$calibration_dir" "$baseline_bin" baseline "$baseline_sha" "$baseline_build" 128)
  printf 'mode=baseline-only\nrun_dir=%s\nm=128\nrepeats=%s\n' \
    "$run_dir" "$baseline_repeats"
  exit 0
fi

compile_variant candidate "$candidate_source"
candidate_bin=$current_bin
candidate_sha=$current_source_sha
candidate_build=$current_build_sha

for m in $M_VALUES; do
  m_dir="$run_dir/m-$m"
  mkdir "$m_dir"
  run_dump "$m_dir/baseline" "$baseline_bin" baseline "$baseline_sha" "$baseline_build" "$m"
  run_dump "$m_dir/candidate" "$candidate_bin" candidate "$candidate_sha" "$candidate_build" "$m"
  if cmp -s "$m_dir/baseline.dump" "$m_dir/candidate.dump"; then
    printf 'm=%s\ndump_cmp=identical\n' "$m" >"$m_dir/dump-cmp"
  else
    printf 'm=%s\ndump_cmp=different\n' "$m" >"$m_dir/dump-cmp"
    die "dump-mismatch-m$m"
  fi

done

for m in $M_VALUES; do
  m_dir="$run_dir/m-$m"
  calibration_dir="$m_dir/calibration"
  repeats=$(calibrate_repeats \
    "$calibration_dir" "$baseline_bin" baseline "$baseline_sha" "$baseline_build" "$m")
  printf 'm=%s\nrepeats=%s\ncalibration_target_ns=%s\n' \
    "$m" "$repeats" "$CALIBRATION_TARGET_NS" >"$m_dir/calibration.config"

  warmup=1
  while [ "$warmup" -le "$WARMUPS" ]; do
    warmup_id=$(printf '%02d' "$warmup")
    run_direct "$m_dir/warmup-$warmup_id-baseline" \
      "$baseline_bin" baseline "$baseline_sha" "$baseline_build" "$m" \
      "warmup-$warmup_id" "$repeats"
    run_direct "$m_dir/warmup-$warmup_id-candidate" \
      "$candidate_bin" candidate "$candidate_sha" "$candidate_build" "$m" \
      "warmup-$warmup_id" "$repeats"
    warmup=$((warmup + 1))
  done

  pair=1
  while [ "$pair" -le "$PAIRS" ]; do
    pair_id=$(printf '%02d' "$pair")
    run_stat "$m_dir/pair-$pair_id-ab-baseline" \
      "$baseline_bin" baseline "$baseline_sha" "$baseline_build" "$m" \
      "pair-$pair_id-ab" "$repeats"
    run_stat "$m_dir/pair-$pair_id-ab-candidate" \
      "$candidate_bin" candidate "$candidate_sha" "$candidate_build" "$m" \
      "pair-$pair_id-ab" "$repeats"
    run_stat "$m_dir/pair-$pair_id-ba-candidate" \
      "$candidate_bin" candidate "$candidate_sha" "$candidate_build" "$m" \
      "pair-$pair_id-ba" "$repeats"
    run_stat "$m_dir/pair-$pair_id-ba-baseline" \
      "$baseline_bin" baseline "$baseline_sha" "$baseline_build" "$m" \
      "pair-$pair_id-ba" "$repeats"
    pair=$((pair + 1))
  done

  run_record "$m_dir/profile-baseline" \
    "$baseline_bin" baseline "$baseline_sha" "$baseline_build" "$m" "$repeats"
  run_record "$m_dir/profile-candidate" \
    "$candidate_bin" candidate "$candidate_sha" "$candidate_build" "$m" "$repeats"
done

printf 'mode=baseline-candidate\nrun_dir=%s\n' "$run_dir"
