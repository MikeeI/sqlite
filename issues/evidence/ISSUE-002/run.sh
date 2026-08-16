#!/bin/sh
# POSIX shell runner. Output is for evidence consumers: stable key=value rows.

set -eu

CPU=4
TARGET_NS=250000000
MAX_REPEATS=1073741824
ITEM_COUNTS='128 256 512 1024 2048 4096'
PERF_EVENTS='task-clock,cycles,instructions,branches,branch-misses'
BUILD_ID='cc-O2-DNDEBUG-g-fno-omit-frame-pointer-SQLITE_ENABLE_FTS5-SQLITE_THREADSAFE-1-SQLITE_DEFAULT_MEMSTATUS-1'

usage(){
  printf '%s\n' \
    "usage: $0 --baseline PATH/TO/sqlite3.c --out-dir NEW_OUTPUT_DIRECTORY [--candidate PATH/TO/sqlite3.c]" >&2
}

die(){
  printf 'error=%s\n' "$*" >&2
  exit 1
}

row_field(){
  awk -v key="$2" '
    {
      for(i=1; i<=NF; i++){
        n = index($i, "=")
        if(n>1 && substr($i, 1, n-1)==key){
          print substr($i, n+1)
          found = 1
          exit
        }
      }
    }
    END { if(!found) exit 1 }
  ' "$1"
}

assert_field(){
  file=$1
  key=$2
  expected=$3
  actual=$(row_field "$file" "$key") || die "missing_field file=$file key=$key"
  [ "$actual" = "$expected" ] || die "field_mismatch file=$file key=$key expected=$expected actual=$actual"
}

is_decimal(){
  case $1 in
    ''|*[!0-9]*) return 1 ;;
    *) return 0 ;;
  esac
}

elapsed_at_least_target(){
  is_decimal "$1" || return 1
  if [ "${#1}" -gt 9 ]; then
    return 0
  fi
  if [ "${#1}" -lt 9 ]; then
    return 1
  fi
  [ "$1" -ge "$TARGET_NS" ]
}

require_amalgamation(){
  source=$1
  [ -f "$source" ] || die "missing_amalgamation path=$source"
  source_dir=$(dirname "$source")
  [ -f "$source_dir/sqlite3.h" ] || die "missing_sqlite3_header directory=$source_dir"
}

compile_variant(){
  variant=$1
  source=$2
  source_dir=$(dirname "$source")

  cc -O2 -DNDEBUG -g -fno-omit-frame-pointer \
    -DSQLITE_ENABLE_FTS5 -DSQLITE_THREADSAFE=1 -DSQLITE_DEFAULT_MEMSTATUS=1 \
    -I"$source_dir" "$HARNESS" "$source" -lm -o "$OUT_DIR/bin/$variant"
}

verify_sample_row(){
  file=$1
  variant=$2
  sha=$3
  items=$4
  repeats=$5
  sample=$6

  assert_field "$file" mode sample
  assert_field "$file" variant "$variant"
  assert_field "$file" sha "$sha"
  assert_field "$file" build "$BUILD_ID"
  assert_field "$file" i "$items"
  assert_field "$file" repeats "$repeats"
  assert_field "$file" calls "$repeats"
  assert_field "$file" sample "$sample"
  assert_field "$file" guard ok
}

run_sample(){
  binary=$1
  variant=$2
  sha=$3
  items=$4
  repeats=$5
  sample=$6
  raw=$7

  taskset -c "$CPU" "$binary" \
    --mode sample --i "$items" --repeats "$repeats" --sample "$sample" \
    --variant "$variant" --sha "$sha" --build "$BUILD_ID" > "$raw"
  verify_sample_row "$raw" "$variant" "$sha" "$items" "$repeats" "$sample"
}

run_timed_sample(){
  binary=$1
  variant=$2
  sha=$3
  items=$4
  repeats=$5
  sample=$6
  raw=$7
  perf_output=$8

  taskset -c "$CPU" perf stat -x, -o "$perf_output" -e "$PERF_EVENTS" -- \
    "$binary" --mode sample --i "$items" --repeats "$repeats" --sample "$sample" \
    --variant "$variant" --sha "$sha" --build "$BUILD_ID" > "$raw"
  [ -s "$perf_output" ] || die "missing_perf_output file=$perf_output"
  verify_sample_row "$raw" "$variant" "$sha" "$items" "$repeats" "$sample"
}

run_dump(){
  binary=$1
  items=$2
  output=$3

  "$binary" --mode dump --i "$items" > "$output"
}

calibrate_baseline(){
  items=$1
  repeats=1

  while :; do
    raw="$OUT_DIR/raw/baseline-i${items}-calibration-r${repeats}.row"
    run_sample "$BASELINE_BIN" baseline "$BASELINE_SHA" "$items" "$repeats" \
      calibration "$raw"
    elapsed=$(row_field "$raw" elapsed_ns) || die "missing_elapsed_ns file=$raw"
    is_decimal "$elapsed" || die "invalid_elapsed_ns file=$raw value=$elapsed"
    if elapsed_at_least_target "$elapsed"; then
      printf '%s\n' "$repeats"
      return 0
    fi
    [ "$repeats" -le $((MAX_REPEATS / 2)) ] || die "calibration_repeat_overflow items=$items"
    repeats=$((repeats * 2))
  done
}

run_warmups(){
  items=$1
  repeats=$2
  warmup=1

  while [ "$warmup" -le 3 ]; do
    if [ $((warmup % 2)) -eq 1 ]; then
      run_sample "$BASELINE_BIN" baseline "$BASELINE_SHA" "$items" "$repeats" \
        "warmup-$warmup" "$OUT_DIR/raw/baseline-i${items}-warmup${warmup}.row"
      run_sample "$CANDIDATE_BIN" candidate "$CANDIDATE_SHA" "$items" "$repeats" \
        "warmup-$warmup" "$OUT_DIR/raw/candidate-i${items}-warmup${warmup}.row"
    else
      run_sample "$CANDIDATE_BIN" candidate "$CANDIDATE_SHA" "$items" "$repeats" \
        "warmup-$warmup" "$OUT_DIR/raw/candidate-i${items}-warmup${warmup}.row"
      run_sample "$BASELINE_BIN" baseline "$BASELINE_SHA" "$items" "$repeats" \
        "warmup-$warmup" "$OUT_DIR/raw/baseline-i${items}-warmup${warmup}.row"
    fi
    warmup=$((warmup + 1))
  done
}

run_paired_samples(){
  items=$1
  repeats=$2
  sample=1

  while [ "$sample" -le 15 ]; do
    if [ $((sample % 2)) -eq 1 ]; then
      run_timed_sample "$BASELINE_BIN" baseline "$BASELINE_SHA" "$items" "$repeats" \
        "$sample" "$OUT_DIR/raw/baseline-i${items}-sample${sample}.row" \
        "$OUT_DIR/perf/baseline-i${items}-sample${sample}.csv"
      run_timed_sample "$CANDIDATE_BIN" candidate "$CANDIDATE_SHA" "$items" "$repeats" \
        "$sample" "$OUT_DIR/raw/candidate-i${items}-sample${sample}.row" \
        "$OUT_DIR/perf/candidate-i${items}-sample${sample}.csv"
    else
      run_timed_sample "$CANDIDATE_BIN" candidate "$CANDIDATE_SHA" "$items" "$repeats" \
        "$sample" "$OUT_DIR/raw/candidate-i${items}-sample${sample}.row" \
        "$OUT_DIR/perf/candidate-i${items}-sample${sample}.csv"
      run_timed_sample "$BASELINE_BIN" baseline "$BASELINE_SHA" "$items" "$repeats" \
        "$sample" "$OUT_DIR/raw/baseline-i${items}-sample${sample}.row" \
        "$OUT_DIR/perf/baseline-i${items}-sample${sample}.csv"
    fi
    sample=$((sample + 1))
  done
}

BASELINE=''
CANDIDATE=''
OUT_DIR=''

while [ "$#" -gt 0 ]; do
  [ "$#" -ge 2 ] || {
    usage
    exit 2
  }
  case $1 in
    --baseline)
      [ -z "$BASELINE" ] || die "duplicate_option option=--baseline"
      BASELINE=$2
      ;;
    --candidate)
      [ -z "$CANDIDATE" ] || die "duplicate_option option=--candidate"
      CANDIDATE=$2
      ;;
    --out-dir)
      [ -z "$OUT_DIR" ] || die "duplicate_option option=--out-dir"
      OUT_DIR=$2
      ;;
    *)
      usage
      exit 2
      ;;
  esac
  shift 2
done

[ -n "$BASELINE" ] || {
  usage
  exit 2
}
[ -n "$OUT_DIR" ] || {
  usage
  exit 2
}
require_amalgamation "$BASELINE"
if [ -n "$CANDIDATE" ]; then
  require_amalgamation "$CANDIDATE"
fi
if [ -e "$OUT_DIR" ]; then
  die "output_directory_exists path=$OUT_DIR"
fi

LC_ALL=C
export LC_ALL
umask 077
mkdir "$OUT_DIR"
mkdir "$OUT_DIR/bin" "$OUT_DIR/dump" "$OUT_DIR/perf" "$OUT_DIR/raw"
HARNESS=$(
  CDPATH='' cd "$(dirname "$0")" && pwd
)/benchmark.c
[ -f "$HARNESS" ] || die "missing_harness path=$HARNESS"

BASELINE_SUM=$(sha256sum "$BASELINE") || die "baseline_sha_failed"
BASELINE_SHA=${BASELINE_SUM%% *}
compile_variant baseline "$BASELINE"
BASELINE_BIN="$OUT_DIR/bin/baseline"

if [ -n "$CANDIDATE" ]; then
  CANDIDATE_SUM=$(sha256sum "$CANDIDATE") || die "candidate_sha_failed"
  CANDIDATE_SHA=${CANDIDATE_SUM%% *}
  compile_variant candidate "$CANDIDATE"
  CANDIDATE_BIN="$OUT_DIR/bin/candidate"
else
  CANDIDATE_SHA=''
  CANDIDATE_BIN=''
fi

{
  printf 'baseline_path=%s\n' "$BASELINE"
  printf 'baseline_sha=%s\n' "$BASELINE_SHA"
  printf 'candidate_path=%s\n' "${CANDIDATE:-none}"
  printf 'candidate_sha=%s\n' "${CANDIDATE_SHA:-none}"
  printf 'build=%s\n' "$BUILD_ID"
  printf 'cpu=%s\n' "$CPU"
  printf 'target_ns=%s\n' "$TARGET_NS"
  printf 'warmups_per_variant=3\n'
  printf 'paired_samples=15\n'
  printf 'paired_order=alternating-AB-BA\n'
  printf 'perf_events=%s\n' "$PERF_EVENTS"
} > "$OUT_DIR/config.txt"

if [ -z "$CANDIDATE" ]; then
  run_sample "$BASELINE_BIN" baseline "$BASELINE_SHA" 128 1 smoke-calibration \
    "$OUT_DIR/raw/baseline-i128-smoke-calibration.row"
  printf 'status=ok mode=baseline-only output_dir=%s\n' "$OUT_DIR"
  exit 0
fi

for items in $ITEM_COUNTS; do
  run_dump "$BASELINE_BIN" "$items" "$OUT_DIR/dump/baseline-i${items}.bin"
  run_dump "$CANDIDATE_BIN" "$items" "$OUT_DIR/dump/candidate-i${items}.bin"
  cmp -s "$OUT_DIR/dump/baseline-i${items}.bin" \
    "$OUT_DIR/dump/candidate-i${items}.bin" || die "dump_mismatch items=$items"

  repeats=$(calibrate_baseline "$items")
  printf 'items=%s repeats=%s calibration=baseline\n' "$items" "$repeats" \
    >> "$OUT_DIR/config.txt"
  run_warmups "$items" "$repeats"
  run_paired_samples "$items" "$repeats"
done

printf 'status=ok mode=comparison output_dir=%s\n' "$OUT_DIR"
