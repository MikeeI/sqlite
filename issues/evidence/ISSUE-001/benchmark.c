#define _POSIX_C_SOURCE 200809L

#include "sqlite3.h"

#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#ifndef BENCH_AMALGAMATION_SHA
# define BENCH_AMALGAMATION_SHA "unknown"
#endif
#ifndef BENCH_BUILD_ID
# define BENCH_BUILD_ID "unknown"
#endif

#define BENCH_MAX_ROWS 1000000
#define PAYLOAD_BYTES 256
#define NS_PER_SECOND 1000000000LL
#define FNV_OFFSET UINT64_C(1469598103934665603)
#define FNV_PRIME UINT64_C(1099511628211)

static const char zSampleSql[] =
  "SELECT first_value(payload) OVER (ORDER BY id) "
  "FROM t WHERE id<=?1 ORDER BY id";

static volatile uint64_t benchChecksum = FNV_OFFSET;

static void usage(const char *zProg){
  fprintf(stderr,
    "usage: %s prepare DATABASE ROWS\n"
    "       %s sample DATABASE N SAMPLE VARIANT\n",
    zProg, zProg
  );
}

static int fail(const char *zMessage){
  fprintf(stderr, "benchmark: %s\n", zMessage);
  return 1;
}

static int fail_sqlite(sqlite3 *db, const char *zOperation, int rc){
  fprintf(
    stderr, "benchmark: %s: rc=%d: %s\n", zOperation, rc,
    db ? sqlite3_errmsg(db) : "no database handle"
  );
  return 1;
}

static int parse_rows(const char *zValue, int *pnRow){
  char *zEnd = 0;
  long n;

  errno = 0;
  n = strtol(zValue, &zEnd, 10);
  if( errno!=0 || zValue[0]=='\0' || zEnd[0]!='\0'
   || n<1 || n>BENCH_MAX_ROWS ){
    return 0;
  }
  *pnRow = (int)n;
  return 1;
}

static int is_safe_token(const char *zValue){
  const unsigned char *z = (const unsigned char *)zValue;

  if( z[0]==0 ) return 0;
  for(; *z; z++){
    if( !( (*z>='a' && *z<='z')
        || (*z>='A' && *z<='Z')
        || (*z>='0' && *z<='9')
        || *z=='-' || *z=='_' || *z=='.' ) ){
      return 0;
    }
  }
  return 1;
}

static int close_database(sqlite3 *db){
  int rc = sqlite3_close(db);
  int rcCloseV2;

  if( rc==SQLITE_OK ) return 0;
  fail_sqlite(db, "sqlite3_close", rc);
  rcCloseV2 = sqlite3_close_v2(db);
  if( rcCloseV2!=SQLITE_OK ){
    fail_sqlite(db, "sqlite3_close_v2 after sqlite3_close", rcCloseV2);
  }
  return 1;
}

static void close_after_error(sqlite3 *db, sqlite3_stmt *pStmt){
  int rc;

  if( pStmt ){
    rc = sqlite3_finalize(pStmt);
    if( rc!=SQLITE_OK ){
      fprintf(stderr, "benchmark: sqlite3_finalize during cleanup: rc=%d\n", rc);
    }
  }
  if( db ){
    rc = sqlite3_close_v2(db);
    if( rc!=SQLITE_OK ){
      fprintf(stderr, "benchmark: sqlite3_close_v2 during cleanup: rc=%d\n", rc);
    }
  }
}

static int exec_sql(sqlite3 *db, const char *zSql, const char *zOperation){
  char *zErr = 0;
  int rc = sqlite3_exec(db, zSql, 0, 0, &zErr);

  if( rc!=SQLITE_OK ){
    fprintf(
      stderr, "benchmark: %s: rc=%d: %s\n", zOperation, rc,
      zErr ? zErr : sqlite3_errmsg(db)
    );
    sqlite3_free(zErr);
    return 1;
  }
  sqlite3_free(zErr);
  return 0;
}

static void fill_payload(int id, unsigned char aPayload[PAYLOAD_BYTES]){
  uint64_t state = UINT64_C(0x9e3779b97f4a7c15) ^ (uint64_t)id;
  int i;

  for(i=0; i<8; i++){
    aPayload[i] = (unsigned char)(((uint64_t)id >> (8*i)) & 0xff);
  }
  for(i=8; i<PAYLOAD_BYTES; i++){
    state ^= state >> 12;
    state ^= state << 25;
    state ^= state >> 27;
    state *= UINT64_C(2685821657736338717);
    aPayload[i] = (unsigned char)(state >> 56);
  }
}

static void consume_payload(const unsigned char *aPayload, int nPayload){
  uint64_t checksum = benchChecksum;
  int i;

  for(i=0; i<nPayload; i++){
    checksum ^= aPayload[i];
    checksum *= FNV_PRIME;
  }
  benchChecksum = checksum;
}

static int monotonic_ns(sqlite3_int64 *pnTime){
  struct timespec t;

  if( clock_gettime(CLOCK_MONOTONIC, &t)!=0 ){
    fprintf(stderr, "benchmark: clock_gettime: %s\n", strerror(errno));
    return 1;
  }
  if( t.tv_sec<0 || (sqlite3_int64)t.tv_sec
      > (INT64_MAX - (sqlite3_int64)t.tv_nsec) / NS_PER_SECOND ){
    return fail("CLOCK_MONOTONIC value overflows nanoseconds");
  }
  *pnTime = (sqlite3_int64)t.tv_sec * NS_PER_SECOND + (sqlite3_int64)t.tv_nsec;
  return 0;
}

static int prepare_database(const char *zPath, int nRow){
  sqlite3 *db = 0;
  sqlite3_stmt *pInsert = 0;
  unsigned char aPayload[PAYLOAD_BYTES];
  int rc;
  int i;

  if( access(zPath, F_OK)==0 ){
    return fail("prepare refuses to replace an existing database");
  }
  if( errno!=ENOENT ){
    fprintf(stderr, "benchmark: cannot inspect database path: %s\n", strerror(errno));
    return 1;
  }
  rc = sqlite3_open_v2(
    zPath, &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_EXRESCODE,
    0
  );
  if( rc!=SQLITE_OK ){
    fail_sqlite(db, "sqlite3_open_v2", rc);
    close_after_error(db, 0);
    return 1;
  }
  if( exec_sql(db, "BEGIN IMMEDIATE", "begin prepare transaction") ) goto error_out;
  if( exec_sql(
    db, "CREATE TABLE t(id INTEGER PRIMARY KEY,payload BLOB NOT NULL)",
    "create benchmark table"
  ) ) goto rollback_out;
  rc = sqlite3_prepare_v2(
    db, "INSERT INTO t(id,payload) VALUES(?1,?2)", -1, &pInsert, 0
  );
  if( rc!=SQLITE_OK ){
    fail_sqlite(db, "prepare insert", rc);
    goto rollback_out;
  }
  for(i=1; i<=nRow; i++){
    fill_payload(i, aPayload);
    rc = sqlite3_reset(pInsert);
    if( rc!=SQLITE_OK ){
      fail_sqlite(db, "reset insert", rc);
      goto rollback_out;
    }
    rc = sqlite3_clear_bindings(pInsert);
    if( rc!=SQLITE_OK ){
      fail_sqlite(db, "clear insert bindings", rc);
      goto rollback_out;
    }
    rc = sqlite3_bind_int(pInsert, 1, i);
    if( rc!=SQLITE_OK ){
      fail_sqlite(db, "bind insert id", rc);
      goto rollback_out;
    }
    rc = sqlite3_bind_blob(pInsert, 2, aPayload, PAYLOAD_BYTES, SQLITE_TRANSIENT);
    if( rc!=SQLITE_OK ){
      fail_sqlite(db, "bind insert payload", rc);
      goto rollback_out;
    }
    rc = sqlite3_step(pInsert);
    if( rc!=SQLITE_DONE ){
      fail_sqlite(db, "step insert", rc);
      goto rollback_out;
    }
  }
  rc = sqlite3_finalize(pInsert);
  pInsert = 0;
  if( rc!=SQLITE_OK ){
    fail_sqlite(db, "finalize insert", rc);
    goto rollback_out;
  }
  if( exec_sql(db, "COMMIT", "commit prepare transaction") ) goto error_out;
  if( close_database(db) ) return 1;
  printf(
    "mode=prepare rows=%d payload_bytes=%d transaction=single guard=ok\n",
    nRow, PAYLOAD_BYTES
  );
  return 0;

rollback_out:
  if( exec_sql(db, "ROLLBACK", "rollback prepare transaction") ){
    close_after_error(db, pInsert);
    return 1;
  }
error_out:
  close_after_error(db, pInsert);
  return 1;
}

static int set_pragma_int(
  sqlite3 *db, const char *zSet, const char *zRead, sqlite3_int64 expected,
  const char *zName
){
  sqlite3_stmt *pStmt = 0;
  sqlite3_int64 actual;
  int rc;
  int rcFinal;

  if( exec_sql(db, zSet, zName) ) return 1;
  rc = sqlite3_prepare_v2(db, zRead, -1, &pStmt, 0);
  if( rc!=SQLITE_OK ){
    fail_sqlite(db, zName, rc);
    return 1;
  }
  rc = sqlite3_step(pStmt);
  if( rc!=SQLITE_ROW ){
    fail_sqlite(db, zName, rc);
    rcFinal = sqlite3_finalize(pStmt);
    if( rcFinal!=SQLITE_OK ) fail_sqlite(db, zName, rcFinal);
    return 1;
  }
  if( sqlite3_column_type(pStmt, 0)!=SQLITE_INTEGER ){
    fprintf(stderr, "benchmark: %s did not return an integer\n", zName);
    rcFinal = sqlite3_finalize(pStmt);
    if( rcFinal!=SQLITE_OK ) fail_sqlite(db, zName, rcFinal);
    return 1;
  }
  actual = sqlite3_column_int64(pStmt, 0);
  if( actual!=expected ){
    fprintf(
      stderr, "benchmark: %s returned %" PRId64 ", expected %" PRId64 "\n",
      zName, (int64_t)actual, (int64_t)expected
    );
    rcFinal = sqlite3_finalize(pStmt);
    if( rcFinal!=SQLITE_OK ) fail_sqlite(db, zName, rcFinal);
    return 1;
  }
  rc = sqlite3_step(pStmt);
  if( rc!=SQLITE_DONE ){
    fail_sqlite(db, zName, rc);
    rcFinal = sqlite3_finalize(pStmt);
    if( rcFinal!=SQLITE_OK ) fail_sqlite(db, zName, rcFinal);
    return 1;
  }
  rcFinal = sqlite3_finalize(pStmt);
  if( rcFinal!=SQLITE_OK ){
    fail_sqlite(db, zName, rcFinal);
    return 1;
  }
  return 0;
}

static int guard_query_plan(sqlite3 *db, int nRow){
  char *zEqpSql;
  sqlite3_stmt *pStmt = 0;
  int rc;
  int rcFinal;
  int nPlanRow = 0;

  zEqpSql = sqlite3_mprintf("EXPLAIN QUERY PLAN %s", zSampleSql);
  if( !zEqpSql ) return fail("allocate EXPLAIN QUERY PLAN SQL");
  rc = sqlite3_prepare_v2(db, zEqpSql, -1, &pStmt, 0);
  sqlite3_free(zEqpSql);
  if( rc!=SQLITE_OK ){
    fail_sqlite(db, "prepare EXPLAIN QUERY PLAN", rc);
    return 1;
  }
  rc = sqlite3_bind_int(pStmt, 1, nRow);
  if( rc!=SQLITE_OK ){
    fail_sqlite(db, "bind EXPLAIN QUERY PLAN N", rc);
    rcFinal = sqlite3_finalize(pStmt);
    if( rcFinal!=SQLITE_OK ) fail_sqlite(db, "finalize EXPLAIN QUERY PLAN", rcFinal);
    return 1;
  }
  while( (rc = sqlite3_step(pStmt))==SQLITE_ROW ){
    const unsigned char *zDetail;
    int nDetail;

    nPlanRow++;
    zDetail = sqlite3_column_text(pStmt, 3);
    nDetail = sqlite3_column_bytes(pStmt, 3);
    if( !zDetail || nDetail<=0 ){
      fprintf(stderr, "benchmark: EQP guard returned an empty detail row\n");
      rcFinal = sqlite3_finalize(pStmt);
      if( rcFinal!=SQLITE_OK ) fail_sqlite(db, "finalize EXPLAIN QUERY PLAN", rcFinal);
      return 1;
    }
    if( strstr((const char *)zDetail, "USE TEMP B-TREE")!=0 ){
      fprintf(stderr, "benchmark: guard failure: EQP uses a temporary B-tree\n");
      rcFinal = sqlite3_finalize(pStmt);
      if( rcFinal!=SQLITE_OK ) fail_sqlite(db, "finalize EXPLAIN QUERY PLAN", rcFinal);
      return 1;
    }
  }
  if( rc!=SQLITE_DONE ){
    fail_sqlite(db, "step EXPLAIN QUERY PLAN", rc);
    rcFinal = sqlite3_finalize(pStmt);
    if( rcFinal!=SQLITE_OK ) fail_sqlite(db, "finalize EXPLAIN QUERY PLAN", rcFinal);
    return 1;
  }
  if( nPlanRow==0 ){
    fprintf(stderr, "benchmark: EQP guard returned no plan rows\n");
    rcFinal = sqlite3_finalize(pStmt);
    if( rcFinal!=SQLITE_OK ) fail_sqlite(db, "finalize EXPLAIN QUERY PLAN", rcFinal);
    return 1;
  }
  rcFinal = sqlite3_finalize(pStmt);
  if( rcFinal!=SQLITE_OK ){
    fail_sqlite(db, "finalize EXPLAIN QUERY PLAN", rcFinal);
    return 1;
  }
  return 0;
}

static int sample_database(
  const char *zPath, int nRow, const char *zSample, const char *zVariant
){
  sqlite3 *db = 0;
  sqlite3_stmt *pStmt = 0;
  unsigned char aExpected[PAYLOAD_BYTES];
  sqlite3_int64 nMemoryBefore;
  sqlite3_int64 nMemoryHighwater;
  sqlite3_int64 nElapsedStart;
  sqlite3_int64 nElapsedEnd;
  sqlite3_int64 nPeakDelta;
  sqlite3_int64 nMemoryIgnored;
  int nRowOut = 0;
  int rc;
  int rcFinal;

  rc = sqlite3_open_v2(zPath, &db, SQLITE_OPEN_READONLY | SQLITE_OPEN_EXRESCODE, 0);
  if( rc!=SQLITE_OK ){
    fail_sqlite(db, "sqlite3_open_v2", rc);
    close_after_error(db, 0);
    return 1;
  }
  if( set_pragma_int(
    db, "PRAGMA temp_store=MEMORY", "PRAGMA temp_store", 2,
    "PRAGMA temp_store=MEMORY"
  ) ){
    goto error_out;
  }
  if( set_pragma_int(
    db, "PRAGMA cache_size=-2048", "PRAGMA cache_size", -2048,
    "PRAGMA cache_size=-2048"
  ) ){
    goto error_out;
  }
  if( set_pragma_int(
    db, "PRAGMA mmap_size=0", "PRAGMA mmap_size", 0,
    "PRAGMA mmap_size=0"
  ) ){
    goto error_out;
  }
  if( guard_query_plan(db, nRow) ) goto error_out;
  rc = sqlite3_prepare_v2(db, zSampleSql, -1, &pStmt, 0);
  if( rc!=SQLITE_OK ){
    fail_sqlite(db, "prepare sample query", rc);
    goto error_out;
  }
  rc = sqlite3_bind_int(pStmt, 1, nRow);
  if( rc!=SQLITE_OK ){
    fail_sqlite(db, "bind sample N", rc);
    goto error_out;
  }
  fill_payload(1, aExpected);
  rc = sqlite3_status64(
    SQLITE_STATUS_MEMORY_USED, &nMemoryBefore, &nMemoryHighwater, 1
  );
  if( rc!=SQLITE_OK ){
    fail_sqlite(db, "reset SQLITE_STATUS_MEMORY_USED high-water", rc);
    goto error_out;
  }
  if( nMemoryBefore<0 ){
    fprintf(stderr, "benchmark: invalid negative SQLite memory status\n");
    goto error_out;
  }
  if( monotonic_ns(&nElapsedStart) ) goto error_out;
  while( (rc = sqlite3_step(pStmt))==SQLITE_ROW ){
    const unsigned char *aPayload;
    int nPayload;

    if( nRowOut==nRow ){
      fprintf(stderr, "benchmark: guard failure: sample returned too many rows\n");
      goto error_out;
    }
    aPayload = sqlite3_column_blob(pStmt, 0);
    nPayload = sqlite3_column_bytes(pStmt, 0);
    if( !aPayload || nPayload!=PAYLOAD_BYTES ){
      fprintf(stderr, "benchmark: guard failure: sample payload has invalid size\n");
      goto error_out;
    }
    if( memcmp(aPayload, aExpected, PAYLOAD_BYTES)!=0 ){
      fprintf(stderr, "benchmark: guard failure: sample payload differs from id=1\n");
      goto error_out;
    }
    consume_payload(aPayload, nPayload);
    nRowOut++;
  }
  if( monotonic_ns(&nElapsedEnd) ) goto error_out;
  if( rc!=SQLITE_DONE ){
    fail_sqlite(db, "step sample query", rc);
    goto error_out;
  }
  if( nRowOut!=nRow ){
    fprintf(
      stderr, "benchmark: guard failure: sample returned %d rows, expected %d\n",
      nRowOut, nRow
    );
    goto error_out;
  }
  rc = sqlite3_status64(
    SQLITE_STATUS_MEMORY_USED, &nMemoryIgnored, &nMemoryHighwater, 0
  );
  if( rc!=SQLITE_OK ){
    fail_sqlite(db, "read SQLITE_STATUS_MEMORY_USED high-water", rc);
    goto error_out;
  }
  if( nMemoryHighwater<nMemoryBefore ){
    fprintf(stderr, "benchmark: invalid SQLite memory high-water ordering\n");
    goto error_out;
  }
  if( nElapsedEnd<nElapsedStart ){
    fprintf(stderr, "benchmark: CLOCK_MONOTONIC moved backwards\n");
    goto error_out;
  }
  nPeakDelta = nMemoryHighwater - nMemoryBefore;
  rcFinal = sqlite3_finalize(pStmt);
  pStmt = 0;
  if( rcFinal!=SQLITE_OK ){
    fail_sqlite(db, "finalize sample query", rcFinal);
    goto error_out;
  }
  if( close_database(db) ) return 1;
  printf(
    "mode=sample sha=%s build=%s variant=%s n=%d size=%d sample=%s "
    "query=first_value_payload_over_order_by_id rows=%d elapsed_ns=%" PRId64 " "
    "peak_delta_bytes=%" PRId64 " memory_before_bytes=%" PRId64 " "
    "memory_peak_bytes=%" PRId64 " payload_bytes=%d temp_store=MEMORY "
    "cache_size=-2048 mmap_size=0 checksum=%" PRIu64 " guard=ok\n",
    BENCH_AMALGAMATION_SHA, BENCH_BUILD_ID, zVariant, nRow, nRow, zSample,
    nRowOut, (int64_t)(nElapsedEnd - nElapsedStart), (int64_t)nPeakDelta,
    (int64_t)nMemoryBefore, (int64_t)nMemoryHighwater, PAYLOAD_BYTES,
    (uint64_t)benchChecksum
  );
  return 0;

error_out:
  close_after_error(db, pStmt);
  return 1;
}

int main(int argc, char **argv){
  int nRow;

  if( argc==4 && strcmp(argv[1], "prepare")==0 ){
    if( !parse_rows(argv[3], &nRow) ){
      return fail("prepare ROWS must be an integer in 1..1000000");
    }
    return prepare_database(argv[2], nRow);
  }
  if( argc==6 && strcmp(argv[1], "sample")==0 ){
    if( !parse_rows(argv[3], &nRow) ){
      return fail("sample N must be an integer in 1..1000000");
    }
    if( !is_safe_token(argv[4]) || !is_safe_token(argv[5]) ){
      return fail("sample and variant must contain only letters, digits, '.', '_' or '-'");
    }
    return sample_database(argv[2], nRow, argv[4], argv[5]);
  }
  usage(argv[0]);
  return 1;
}
