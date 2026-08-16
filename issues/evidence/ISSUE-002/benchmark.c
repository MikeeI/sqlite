#define _POSIX_C_SOURCE 200809L

#include <sqlite3.h>

#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

enum {
  kDocumentUnitBytes = sizeof("alpha. ") - 1
};

static const char kCreateTableSql[] =
  "CREATE VIRTUAL TABLE ft USING fts5(body,detail=full)";
static const char kInsertSql[] = "INSERT INTO ft(body) VALUES(?)";
static const char kQuerySql[] =
  "SELECT snippet(ft,0,'[',']','...',32) FROM ft WHERE ft MATCH 'alpha'";
static const uint64_t kFnvOffsetBasis = UINT64_C(14695981039346656037);
static const uint64_t kFnvPrime = UINT64_C(1099511628211);
static const uint64_t kNanosecondsPerSecond = UINT64_C(1000000000);

typedef enum BenchmarkMode BenchmarkMode;
enum BenchmarkMode {
  BENCHMARK_MODE_NONE = 0,
  BENCHMARK_MODE_DUMP,
  BENCHMARK_MODE_SAMPLE
};

typedef struct BenchmarkOptions BenchmarkOptions;
struct BenchmarkOptions {
  BenchmarkMode eMode;
  int nItems;
  uint64_t nRepeat;
  const char *zSample;
  const char *zVariant;
  const char *zSha;
  const char *zBuild;
};

typedef struct Harness Harness;
struct Harness {
  sqlite3 *db;
  sqlite3_stmt *pQuery;
  char *zDocument;
  unsigned char *aExpected;
  int nExpected;
};

typedef struct SampleResult SampleResult;
struct SampleResult {
  uint64_t nElapsedNs;
  uint64_t nChecksum;
  uint64_t nTotalBytes;
  sqlite3_int64 nHeapHighwater;
};

static void print_usage(const char *zProgram){
  fprintf(stderr,
      "usage: %s --mode dump --i ITEMS\\n"
      "       %s --mode sample --i ITEMS --repeats COUNT --sample ID "
      "--variant NAME --sha SHA256 --build ID\\n",
      zProgram, zProgram);
}

static int is_supported_item_count(int nItems){
  switch( nItems ){
    case 128:
    case 256:
    case 512:
    case 1024:
    case 2048:
    case 4096:
      return 1;
  }
  return 0;
}

static int is_output_token(const char *z){
  size_t i;
  size_t n = strlen(z);
  if( n==0 || n>128 ) return 0;
  for(i=0; i<n; i++){
    char c = z[i];
    if( !(c>='A' && c<='Z')
     && !(c>='a' && c<='z')
     && !(c>='0' && c<='9')
     && c!='.' && c!='_' && c!='-' ){
      return 0;
    }
  }
  return 1;
}

static int parse_u64(const char *z, uint64_t *pn){
  char *zEnd = 0;
  unsigned long long n;

  if( z[0]=='-' || z[0]=='\0' ) return 1;
  errno = 0;
  n = strtoull(z, &zEnd, 10);
  if( errno==ERANGE || zEnd==z || *zEnd!='\0' || n>UINT64_MAX ) return 1;
  *pn = (uint64_t)n;
  return 0;
}

static int parse_options(int argc, char **argv, BenchmarkOptions *p){
  int i;
  int bHaveMode = 0;
  int bHaveItems = 0;
  int bHaveRepeats = 0;
  int bHaveSample = 0;
  int bHaveVariant = 0;
  int bHaveSha = 0;
  int bHaveBuild = 0;
  uint64_t nItems = 0;

  memset(p, 0, sizeof(*p));
  if( argc<5 || (argc&1)==0 ) return 1;
  for(i=1; i<argc; i+=2){
    const char *zOption = argv[i];
    const char *zValue = argv[i+1];
    if( strcmp(zOption, "--mode")==0 && !bHaveMode ){
      bHaveMode = 1;
      if( strcmp(zValue, "dump")==0 ){
        p->eMode = BENCHMARK_MODE_DUMP;
      }else if( strcmp(zValue, "sample")==0 ){
        p->eMode = BENCHMARK_MODE_SAMPLE;
      }else{
        return 1;
      }
    }else if( strcmp(zOption, "--i")==0 && !bHaveItems ){
      bHaveItems = 1;
      if( parse_u64(zValue, &nItems) || nItems>INT_MAX ) return 1;
      p->nItems = (int)nItems;
    }else if( strcmp(zOption, "--repeats")==0 && !bHaveRepeats ){
      bHaveRepeats = 1;
      if( parse_u64(zValue, &p->nRepeat) || p->nRepeat==0 ) return 1;
    }else if( strcmp(zOption, "--sample")==0 && !bHaveSample ){
      bHaveSample = 1;
      p->zSample = zValue;
    }else if( strcmp(zOption, "--variant")==0 && !bHaveVariant ){
      bHaveVariant = 1;
      p->zVariant = zValue;
    }else if( strcmp(zOption, "--sha")==0 && !bHaveSha ){
      bHaveSha = 1;
      p->zSha = zValue;
    }else if( strcmp(zOption, "--build")==0 && !bHaveBuild ){
      bHaveBuild = 1;
      p->zBuild = zValue;
    }else{
      return 1;
    }
  }
  if( !bHaveMode || !bHaveItems || !is_supported_item_count(p->nItems) ){
    return 1;
  }
  if( p->eMode==BENCHMARK_MODE_DUMP ){
    return bHaveRepeats || bHaveSample || bHaveVariant || bHaveSha || bHaveBuild;
  }
  if( !bHaveRepeats || !bHaveSample || !bHaveVariant || !bHaveSha || !bHaveBuild ){
    return 1;
  }
  if( !is_output_token(p->zSample) || !is_output_token(p->zVariant)
   || !is_output_token(p->zSha) || !is_output_token(p->zBuild) ){
    return 1;
  }
  return 0;
}

static void report_sqlite_error(sqlite3 *db, const char *zOperation, int rc){
  const char *zMessage = db ? sqlite3_errmsg(db) : "sqlite3_open_v2 failed";
  fprintf(stderr, "error=%s rc=%d message=%s\n", zOperation, rc, zMessage);
}

static int setup_harness(Harness *p, int nItems){
  int rc;
  size_t nDocument;
  int i;
  sqlite3_stmt *pInsert = 0;
  char *zSqlError = 0;

  memset(p, 0, sizeof(*p));
  rc = sqlite3_open_v2(":memory:", &p->db,
      SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_NOMUTEX, 0);
  if( rc!=SQLITE_OK ){
    report_sqlite_error(p->db, "sqlite3_open_v2", rc);
    return 1;
  }
  rc = sqlite3_exec(p->db, kCreateTableSql, 0, 0, &zSqlError);
  if( rc!=SQLITE_OK ){
    fprintf(stderr, "error=CREATE_VIRTUAL_TABLE rc=%d message=%s\n", rc,
        zSqlError ? zSqlError : sqlite3_errmsg(p->db));
    sqlite3_free(zSqlError);
    return 1;
  }
  if( (size_t)nItems>(SIZE_MAX-1)/kDocumentUnitBytes ){
    fprintf(stderr, "error=document_size_overflow\n");
    return 1;
  }
  nDocument = (size_t)nItems * kDocumentUnitBytes;
  if( nDocument>INT_MAX ){
    fprintf(stderr, "error=document_too_large\n");
    return 1;
  }
  p->zDocument = (char*)malloc(nDocument+1);
  if( p->zDocument==0 ){
    fprintf(stderr, "error=document_allocation_failed\n");
    return 1;
  }
  for(i=0; i<nItems; i++){
    memcpy(&p->zDocument[(size_t)i*kDocumentUnitBytes], "alpha. ",
        kDocumentUnitBytes);
  }
  p->zDocument[nDocument] = '\0';

  rc = sqlite3_prepare_v2(p->db, kInsertSql, -1, &pInsert, 0);
  if( rc!=SQLITE_OK ){
    report_sqlite_error(p->db, "sqlite3_prepare_v2_insert", rc);
    return 1;
  }
  rc = sqlite3_bind_text(pInsert, 1, p->zDocument, (int)nDocument,
      SQLITE_STATIC);
  if( rc==SQLITE_OK ) rc = sqlite3_step(pInsert);
  if( rc!=SQLITE_DONE ){
    int rcFinalize;
    report_sqlite_error(p->db, "insert_document", rc);
    rcFinalize = sqlite3_finalize(pInsert);
    if( rcFinalize!=SQLITE_OK ){
      report_sqlite_error(p->db, "sqlite3_finalize_insert", rcFinalize);
    }
    return 1;
  }
  rc = sqlite3_finalize(pInsert);
  if( rc!=SQLITE_OK ){
    report_sqlite_error(p->db, "sqlite3_finalize_insert", rc);
    return 1;
  }
  rc = sqlite3_prepare_v2(p->db, kQuerySql, -1, &p->pQuery, 0);
  if( rc!=SQLITE_OK ){
    report_sqlite_error(p->db, "sqlite3_prepare_v2_query", rc);
    return 1;
  }
  return 0;
}

static int cleanup_harness(Harness *p, int bPriorError){
  int rc;
  int bError = bPriorError;

  if( p->pQuery ){
    rc = sqlite3_finalize(p->pQuery);
    p->pQuery = 0;
    if( rc!=SQLITE_OK ){
      report_sqlite_error(p->db, "sqlite3_finalize_query", rc);
      bError = 1;
    }
  }
  free(p->aExpected);
  p->aExpected = 0;
  free(p->zDocument);
  p->zDocument = 0;
  if( p->db ){
    rc = sqlite3_close(p->db);
    if( rc!=SQLITE_OK ){
      report_sqlite_error(p->db, "sqlite3_close", rc);
      bError = 1;
    }else{
      p->db = 0;
    }
  }
  return bError;
}

static int reset_query(Harness *p){
  int rc = sqlite3_reset(p->pQuery);
  if( rc!=SQLITE_OK ){
    report_sqlite_error(p->db, "sqlite3_reset", rc);
    return 1;
  }
  return 0;
}

static int step_query(Harness *p, const unsigned char **pz, int *pn){
  int rc = sqlite3_step(p->pQuery);
  const unsigned char *z;
  int n;

  if( rc!=SQLITE_ROW ){
    report_sqlite_error(p->db, "sqlite3_step_row", rc);
    return 1;
  }
  z = sqlite3_column_text(p->pQuery, 0);
  n = sqlite3_column_bytes(p->pQuery, 0);
  if( z==0 || n<0 ){
    report_sqlite_error(p->db, "sqlite3_column_text", sqlite3_errcode(p->db));
    return 1;
  }
  *pz = z;
  *pn = n;
  return 0;
}

static int finish_query(Harness *p){
  int rc = sqlite3_step(p->pQuery);
  if( rc!=SQLITE_DONE ){
    report_sqlite_error(p->db, "sqlite3_step_done", rc);
    return 1;
  }
  return 0;
}

static int capture_expected_output(Harness *p){
  const unsigned char *z;
  int n;

  if( step_query(p, &z, &n) ) return 1;
  p->aExpected = (unsigned char*)malloc((size_t)n ? (size_t)n : 1);
  if( p->aExpected==0 ){
    fprintf(stderr, "error=expected_output_allocation_failed\n");
    return 1;
  }
  if( n>0 ) memcpy(p->aExpected, z, (size_t)n);
  p->nExpected = n;
  if( finish_query(p) ) return 1;
  return reset_query(p);
}

static uint64_t consume_bytes(uint64_t nChecksum, const unsigned char *z, int n){
  int i;
  for(i=0; i<n; i++){
    nChecksum ^= z[i];
    nChecksum *= kFnvPrime;
  }
  return nChecksum;
}

static int calculate_elapsed_ns(const struct timespec *pStart,
    const struct timespec *pEnd, uint64_t *pnElapsed){
  uint64_t nSeconds;
  uint64_t nNanoseconds;

  if( pEnd->tv_sec<pStart->tv_sec
   || (pEnd->tv_sec==pStart->tv_sec && pEnd->tv_nsec<pStart->tv_nsec) ){
    fprintf(stderr, "error=monotonic_clock_reversed\n");
    return 1;
  }
  nSeconds = (uint64_t)(pEnd->tv_sec-pStart->tv_sec);
  nNanoseconds = (uint64_t)(pEnd->tv_nsec-pStart->tv_nsec);
  if( pEnd->tv_nsec<pStart->tv_nsec ){
    nSeconds--;
    nNanoseconds += kNanosecondsPerSecond;
  }
  if( nSeconds>(UINT64_MAX-nNanoseconds)/kNanosecondsPerSecond ){
    fprintf(stderr, "error=elapsed_time_overflow\n");
    return 1;
  }
  *pnElapsed = nSeconds*kNanosecondsPerSecond+nNanoseconds;
  return 0;
}

static int measure_sample(Harness *p, const BenchmarkOptions *pOptions,
    SampleResult *pResult){
  struct timespec sStart;
  struct timespec sEnd;
  uint64_t i;
  uint64_t nChecksum = kFnvOffsetBasis;
  const unsigned char *z;
  int n;

  if( p->nExpected>0 && pOptions->nRepeat>UINT64_MAX/(uint64_t)p->nExpected ){
    fprintf(stderr, "error=output_byte_count_overflow\n");
    return 1;
  }
  sqlite3_memory_highwater(1);
  if( clock_gettime(CLOCK_MONOTONIC, &sStart)!=0 ){
    fprintf(stderr, "error=clock_gettime_start errno=%d\n", errno);
    return 1;
  }
  for(i=0; i<pOptions->nRepeat; i++){
    if( step_query(p, &z, &n) ) return 1;
    if( n!=p->nExpected || memcmp(z, p->aExpected, (size_t)n)!=0 ){
      fprintf(stderr, "error=output_changed repetition=%" PRIu64 "\n", i);
      return 1;
    }
    nChecksum = consume_bytes(nChecksum, z, n);
    if( finish_query(p) || reset_query(p) ) return 1;
  }
  if( clock_gettime(CLOCK_MONOTONIC, &sEnd)!=0 ){
    fprintf(stderr, "error=clock_gettime_end errno=%d\n", errno);
    return 1;
  }
  if( calculate_elapsed_ns(&sStart, &sEnd, &pResult->nElapsedNs) ) return 1;
  pResult->nChecksum = nChecksum;
  pResult->nTotalBytes = (uint64_t)p->nExpected*pOptions->nRepeat;
  pResult->nHeapHighwater = sqlite3_memory_highwater(0);
  return 0;
}

static int write_dump(Harness *p){
  if( p->nExpected>0
   && fwrite(p->aExpected, 1, (size_t)p->nExpected, stdout)
        !=(size_t)p->nExpected ){
    fprintf(stderr, "error=stdout_write_failed\n");
    return 1;
  }
  if( fflush(stdout)==EOF ){
    fprintf(stderr, "error=stdout_flush_failed\n");
    return 1;
  }
  return 0;
}

static int print_sample_result(const BenchmarkOptions *pOptions,
    const Harness *p, const SampleResult *pResult){
  uint64_t nNsPerCall = pResult->nElapsedNs/pOptions->nRepeat;
  int rc;

  rc = printf(
      "mode=sample variant=%s sha=%s build=%s i=%d sample=%s repeats=%" PRIu64
      " calls=%" PRIu64 " output_bytes=%d total_output_bytes=%" PRIu64
      " checksum=%016" PRIx64 " elapsed_ns=%" PRIu64
      " ns_per_call=%" PRIu64 " heap_highwater=%lld guard=ok\n",
      pOptions->zVariant, pOptions->zSha, pOptions->zBuild, pOptions->nItems,
      pOptions->zSample, pOptions->nRepeat, pOptions->nRepeat, p->nExpected,
      pResult->nTotalBytes, pResult->nChecksum, pResult->nElapsedNs, nNsPerCall,
      (long long)pResult->nHeapHighwater);
  if( rc<0 || fflush(stdout)==EOF ){
    fprintf(stderr, "error=stdout_write_failed\n");
    return 1;
  }
  return 0;
}

int main(int argc, char **argv){
  BenchmarkOptions sOptions;
  Harness sHarness;
  SampleResult sResult;
  int bError;

  if( parse_options(argc, argv, &sOptions) ){
    print_usage(argv[0]);
    return 2;
  }
  bError = setup_harness(&sHarness, sOptions.nItems);
  if( !bError ) bError = capture_expected_output(&sHarness);
  if( !bError && sOptions.eMode==BENCHMARK_MODE_DUMP ){
    bError = write_dump(&sHarness);
  }else if( !bError ){
    bError = measure_sample(&sHarness, &sOptions, &sResult);
  }
  bError = cleanup_harness(&sHarness, bError);
  if( bError ) return 1;
  if( sOptions.eMode==BENCHMARK_MODE_SAMPLE ){
    return print_sample_result(&sOptions, &sHarness, &sResult);
  }
  return 0;
}
