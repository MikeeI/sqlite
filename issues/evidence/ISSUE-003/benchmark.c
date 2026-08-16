#define _POSIX_C_SOURCE 200809L

#include "sqlite3.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define DOCUMENT_UNIT "alpha x "
#define HIGHLIGHTED_UNIT "<b>alpha</b> x "
#define DOCUMENT_UNIT_BYTES (sizeof(DOCUMENT_UNIT) - 1)
#define HIGHLIGHTED_UNIT_BYTES (sizeof(HIGHLIGHTED_UNIT) - 1)
#define MAX_REPEATS 1000000000ULL
#define NANOSECONDS_PER_SECOND 1000000000ULL
#define FNV_OFFSET_BASIS 14695981039346656037ULL
#define FNV_PRIME 1099511628211ULL

typedef enum BenchmarkMode BenchmarkMode;
typedef struct Config Config;
typedef struct Benchmark Benchmark;
typedef struct SampleResult SampleResult;

enum BenchmarkMode {
  MODE_SAMPLE,
  MODE_DUMP
};

struct Config {
  BenchmarkMode eMode;
  int m;
  sqlite3_uint64 repeats;
  const char *zSample;
  const char *zVariant;
  const char *zSha;
  const char *zBuild;
};

struct Benchmark {
  sqlite3 *db;
  sqlite3_stmt *pSelect;
  char *zDocument;
  char *zExpected;
  size_t nDocument;
  size_t nExpected;
};

struct SampleResult {
  sqlite3_uint64 elapsedNs;
  sqlite3_uint64 checksum;
  sqlite3_int64 heapHighwater;
};

static void usage(const char *zProgram){
  fprintf(
      stderr,
      "usage=%s --mode sample|dump --m 128|256|512|1024|2048|4096 "
      "--repeats POSITIVE --sample TOKEN --variant TOKEN --sha TOKEN "
      "--build TOKEN\n",
      zProgram
  );
}

static int isSafeToken(const char *z){
  unsigned char c;

  if( z==0 || z[0]==0 ) return 0;
  while( (c=(unsigned char)*z++)!=0 ){
    if( !(isalnum(c) || c=='-' || c=='_' || c=='.') ) return 0;
  }
  return 1;
}

static int parsePositiveU64(const char *z, sqlite3_uint64 *pValue){
  char *zEnd = 0;
  unsigned long long value;
  const unsigned char *p;

  if( z==0 || z[0]==0 ) return 0;
  for(p=(const unsigned char*)z; *p; p++){
    if( !isdigit(*p) ) return 0;
  }
  errno = 0;
  value = strtoull(z, &zEnd, 10);
  if( errno==ERANGE || zEnd==z || *zEnd!=0 || value==0 ) return 0;
  *pValue = (sqlite3_uint64)value;
  return 1;
}

static int parseM(const char *z, int *pM){
  sqlite3_uint64 value;

  if( !parsePositiveU64(z, &value) ) return 0;
  switch( value ){
    case 128:
    case 256:
    case 512:
    case 1024:
    case 2048:
    case 4096:
      *pM = (int)value;
      return 1;
  }
  return 0;
}

static int parseConfig(int argc, char **argv, Config *pConfig){
  int i;
  int seenMode = 0;
  int seenM = 0;
  int seenRepeats = 0;
  int seenSample = 0;
  int seenVariant = 0;
  int seenSha = 0;
  int seenBuild = 0;

  memset(pConfig, 0, sizeof(*pConfig));
  for(i=1; i<argc; i+=2){
    const char *zOption;
    const char *zValue;

    if( i+1>=argc ) return 0;
    zOption = argv[i];
    zValue = argv[i+1];
    if( strcmp(zOption, "--mode")==0 && !seenMode ){
      if( strcmp(zValue, "sample")==0 ){
        pConfig->eMode = MODE_SAMPLE;
      }else if( strcmp(zValue, "dump")==0 ){
        pConfig->eMode = MODE_DUMP;
      }else{
        return 0;
      }
      seenMode = 1;
    }else if( strcmp(zOption, "--m")==0 && !seenM ){
      if( !parseM(zValue, &pConfig->m) ) return 0;
      seenM = 1;
    }else if( strcmp(zOption, "--repeats")==0 && !seenRepeats ){
      if( !parsePositiveU64(zValue, &pConfig->repeats)
       || pConfig->repeats>MAX_REPEATS
      ){
        return 0;
      }
      seenRepeats = 1;
    }else if( strcmp(zOption, "--sample")==0 && !seenSample ){
      if( !isSafeToken(zValue) ) return 0;
      pConfig->zSample = zValue;
      seenSample = 1;
    }else if( strcmp(zOption, "--variant")==0 && !seenVariant ){
      if( !isSafeToken(zValue) ) return 0;
      pConfig->zVariant = zValue;
      seenVariant = 1;
    }else if( strcmp(zOption, "--sha")==0 && !seenSha ){
      if( !isSafeToken(zValue) ) return 0;
      pConfig->zSha = zValue;
      seenSha = 1;
    }else if( strcmp(zOption, "--build")==0 && !seenBuild ){
      if( !isSafeToken(zValue) ) return 0;
      pConfig->zBuild = zValue;
      seenBuild = 1;
    }else{
      return 0;
    }
  }
  return seenMode && seenM && seenRepeats && seenSample && seenVariant
      && seenSha && seenBuild;
}

static int buildInputs(Benchmark *p, int m){
  size_t i;
  size_t nDocument;
  size_t nExpected;

  if( (sqlite3_uint64)m>(sqlite3_uint64)(SIZE_MAX-1)/DOCUMENT_UNIT_BYTES
   || (sqlite3_uint64)m>(sqlite3_uint64)(SIZE_MAX-1)/HIGHLIGHTED_UNIT_BYTES
  ){
    return SQLITE_TOOBIG;
  }
  nDocument = (size_t)m * DOCUMENT_UNIT_BYTES;
  nExpected = (size_t)m * HIGHLIGHTED_UNIT_BYTES;
  if( nDocument>(size_t)INT_MAX || nExpected>(size_t)INT_MAX ){
    return SQLITE_TOOBIG;
  }

  p->zDocument = sqlite3_malloc64((sqlite3_uint64)nDocument + 1);
  if( p->zDocument==0 ) return SQLITE_NOMEM;
  p->zExpected = sqlite3_malloc64((sqlite3_uint64)nExpected + 1);
  if( p->zExpected==0 ) return SQLITE_NOMEM;

  for(i=0; i<(size_t)m; i++){
    memcpy(&p->zDocument[i*DOCUMENT_UNIT_BYTES], DOCUMENT_UNIT,
           DOCUMENT_UNIT_BYTES);
    memcpy(&p->zExpected[i*HIGHLIGHTED_UNIT_BYTES], HIGHLIGHTED_UNIT,
           HIGHLIGHTED_UNIT_BYTES);
  }
  p->zDocument[nDocument] = 0;
  p->zExpected[nExpected] = 0;
  p->nDocument = nDocument;
  p->nExpected = nExpected;
  return SQLITE_OK;
}

static int setupBenchmark(Benchmark *p, int m){
  char *zError = 0;
  sqlite3_stmt *pInsert = 0;
  int rc;

  memset(p, 0, sizeof(*p));
  rc = buildInputs(p, m);
  if( rc!=SQLITE_OK ) return rc;
  rc = sqlite3_open_v2(
      ":memory:", &p->db,
      SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_MEMORY,
      0
  );
  if( rc!=SQLITE_OK ) return rc;
  rc = sqlite3_exec(
      p->db,
      "CREATE VIRTUAL TABLE ft USING fts5(body,detail=full)",
      0, 0, &zError
  );
  sqlite3_free(zError);
  if( rc!=SQLITE_OK ) return rc;
  rc = sqlite3_prepare_v2(
      p->db, "INSERT INTO ft(rowid,body) VALUES(1,?1)", -1, &pInsert, 0
  );
  if( rc!=SQLITE_OK ) return rc;
  rc = sqlite3_bind_text(
      pInsert, 1, p->zDocument, (int)p->nDocument, SQLITE_STATIC
  );
  if( rc==SQLITE_OK ){
    rc = sqlite3_step(pInsert);
    if( rc==SQLITE_DONE ) rc = SQLITE_OK;
  }
  {
    int rcFinalize = sqlite3_finalize(pInsert);
    pInsert = 0;
    if( rc==SQLITE_OK ) rc = rcFinalize;
  }
  if( rc!=SQLITE_OK ) return rc;
  return sqlite3_prepare_v2(
      p->db,
      "SELECT highlight(ft,0,'<b>','</b>') FROM ft WHERE ft MATCH 'alpha'",
      -1, &p->pSelect, 0
  );
}

static int cleanupBenchmark(Benchmark *p){
  int rc = SQLITE_OK;
  int rcCleanup;

  if( p->pSelect ){
    rcCleanup = sqlite3_finalize(p->pSelect);
    p->pSelect = 0;
    if( rc==SQLITE_OK ) rc = rcCleanup;
  }
  if( p->db ){
    rcCleanup = sqlite3_close(p->db);
    p->db = 0;
    if( rc==SQLITE_OK ) rc = rcCleanup;
  }
  sqlite3_free(p->zExpected);
  sqlite3_free(p->zDocument);
  p->zExpected = 0;
  p->zDocument = 0;
  return rc;
}

static int elapsedNanoseconds(
  const struct timespec *pStart,
  const struct timespec *pEnd,
  sqlite3_uint64 *pElapsed
){
  sqlite3_uint64 seconds;
  sqlite3_uint64 nanoseconds;
  sqlite3_uint64 maxValue = ~(sqlite3_uint64)0;

  if( pEnd->tv_sec<pStart->tv_sec
   || (pEnd->tv_sec==pStart->tv_sec && pEnd->tv_nsec<pStart->tv_nsec)
  ){
    return SQLITE_ERROR;
  }
  seconds = (sqlite3_uint64)(pEnd->tv_sec - pStart->tv_sec);
  if( pEnd->tv_nsec>=pStart->tv_nsec ){
    nanoseconds = (sqlite3_uint64)(pEnd->tv_nsec - pStart->tv_nsec);
  }else{
    if( seconds==0 ) return SQLITE_ERROR;
    seconds--;
    nanoseconds = NANOSECONDS_PER_SECOND
                + (sqlite3_uint64)pEnd->tv_nsec
                - (sqlite3_uint64)pStart->tv_nsec;
  }
  if( seconds>(maxValue-nanoseconds)/NANOSECONDS_PER_SECOND ){
    return SQLITE_TOOBIG;
  }
  *pElapsed = seconds*NANOSECONDS_PER_SECOND + nanoseconds;
  return SQLITE_OK;
}

static sqlite3_uint64 checksumBytes(
  sqlite3_uint64 checksum,
  const unsigned char *z,
  size_t n
){
  size_t i;

  for(i=0; i<n; i++){
    checksum ^= (sqlite3_uint64)z[i];
    checksum *= FNV_PRIME;
  }
  return checksum;
}

static int consumeAndVerify(Benchmark *p, sqlite3_uint64 *pChecksum){
  const unsigned char *zText;
  int nText;

  if( sqlite3_column_type(p->pSelect, 0)!=SQLITE_TEXT ) return SQLITE_MISMATCH;
  zText = sqlite3_column_text(p->pSelect, 0);
  nText = sqlite3_column_bytes(p->pSelect, 0);
  if( zText==0 || nText<0 || (size_t)nText!=p->nExpected ){
    return SQLITE_MISMATCH;
  }
  if( memcmp(zText, p->zExpected, p->nExpected)!=0 ) return SQLITE_MISMATCH;
  *pChecksum = checksumBytes(*pChecksum, zText, p->nExpected);
  return SQLITE_OK;
}

static int finishSingleRow(Benchmark *p){
  int rc = sqlite3_step(p->pSelect);

  if( rc==SQLITE_DONE ) return SQLITE_OK;
  if( rc==SQLITE_ROW ) return SQLITE_MISMATCH;
  return rc;
}

static int runSample(
  Benchmark *p,
  const Config *pConfig,
  SampleResult *pResult,
  const char **pzStage
){
  sqlite3_uint64 i;
  sqlite3_uint64 elapsed;
  sqlite3_uint64 checksum = FNV_OFFSET_BASIS;
  struct timespec start;
  struct timespec end;
  int rc;

  memset(pResult, 0, sizeof(*pResult));
  sqlite3_memory_highwater(1);
  for(i=0; i<pConfig->repeats; i++){
    rc = sqlite3_reset(p->pSelect);
    if( rc!=SQLITE_OK ){
      *pzStage = "reset";
      return rc;
    }
    if( clock_gettime(CLOCK_MONOTONIC, &start)!=0 ){
      *pzStage = "clock-start";
      return SQLITE_IOERR;
    }
    /* sqlite3_step() evaluates the complete FTS5 highlight() auxiliary call. */
    rc = sqlite3_step(p->pSelect);
    if( clock_gettime(CLOCK_MONOTONIC, &end)!=0 ){
      *pzStage = "clock-end";
      return SQLITE_IOERR;
    }
    if( rc!=SQLITE_ROW ){
      *pzStage = "step-row";
      return rc;
    }
    rc = elapsedNanoseconds(&start, &end, &elapsed);
    if( rc!=SQLITE_OK ){
      *pzStage = "elapsed";
      return rc;
    }
    if( pResult->elapsedNs>(~(sqlite3_uint64)0)-elapsed ){
      *pzStage = "elapsed-overflow";
      return SQLITE_TOOBIG;
    }
    pResult->elapsedNs += elapsed;
    rc = consumeAndVerify(p, &checksum);
    if( rc!=SQLITE_OK ){
      *pzStage = "output-guard";
      return rc;
    }
    rc = finishSingleRow(p);
    if( rc!=SQLITE_OK ){
      *pzStage = "single-row";
      return rc;
    }
  }
  pResult->checksum = checksum;
  pResult->heapHighwater = sqlite3_memory_highwater(0);
  return SQLITE_OK;
}

static int runDump(Benchmark *p, const char **pzStage){
  sqlite3_uint64 checksum = FNV_OFFSET_BASIS;
  const unsigned char *zText;
  int rc;

  rc = sqlite3_reset(p->pSelect);
  if( rc!=SQLITE_OK ){
    *pzStage = "reset";
    return rc;
  }
  rc = sqlite3_step(p->pSelect);
  if( rc!=SQLITE_ROW ){
    *pzStage = "step-row";
    return rc;
  }
  rc = consumeAndVerify(p, &checksum);
  if( rc!=SQLITE_OK ){
    *pzStage = "output-guard";
    return rc;
  }
  zText = sqlite3_column_text(p->pSelect, 0);
  if( zText==0 || fwrite(zText, 1, p->nExpected, stdout)!=p->nExpected ){
    *pzStage = "dump-write";
    return SQLITE_IOERR;
  }
  rc = finishSingleRow(p);
  if( rc!=SQLITE_OK ){
    *pzStage = "single-row";
    return rc;
  }
  if( fflush(stdout)==EOF ){
    *pzStage = "dump-write";
    return SQLITE_IOERR;
  }
  return SQLITE_OK;
}

static int emitConfig(FILE *out, const Config *pConfig){
  const char *zMode = pConfig->eMode==MODE_SAMPLE ? "sample" : "dump";

  return fprintf(out,
      "mode=%s\n"
      "sha=%s\n"
      "build=%s\n"
      "variant=%s\n"
      "m=%d\n"
      "sample=%s\n"
      "repeats=%llu\n",
      zMode,
      pConfig->zSha,
      pConfig->zBuild,
      pConfig->zVariant,
      pConfig->m,
      pConfig->zSample,
      (unsigned long long)pConfig->repeats
  )<0 ? SQLITE_IOERR : SQLITE_OK;
}

static int emitSample(const Config *pConfig, const SampleResult *pResult){
  sqlite3_uint64 quotient = pResult->elapsedNs / pConfig->repeats;
  sqlite3_uint64 remainder = pResult->elapsedNs % pConfig->repeats;
  sqlite3_uint64 fractional = (remainder * 1000) / pConfig->repeats;
  int rc;

  rc = emitConfig(stdout, pConfig);
  if( rc!=SQLITE_OK ) return rc;
  if( printf(
      "calls=%llu\n"
      "elapsed_ns=%llu\n"
      "ns_per_call=%llu.%03llu\n"
      "output_bytes=%llu\n"
      "checksum=%016llx\n"
      "heap_highwater=%lld\n"
      "guard=ok\n",
      (unsigned long long)pConfig->repeats,
      (unsigned long long)pResult->elapsedNs,
      (unsigned long long)quotient,
      (unsigned long long)fractional,
      (unsigned long long)((size_t)pConfig->m * HIGHLIGHTED_UNIT_BYTES),
      (unsigned long long)pResult->checksum,
      (long long)pResult->heapHighwater
  )<0 ){
    return SQLITE_IOERR;
  }
  return fflush(stdout)==EOF ? SQLITE_IOERR : SQLITE_OK;
}

static void reportFailure(const char *zStage, int rc){
  fprintf(stderr, "guard=error\nstage=%s\nsqlite_rc=%d\n", zStage, rc);
}

int main(int argc, char **argv){
  Benchmark benchmark;
  Config config;
  SampleResult result;
  const char *zStage = "configuration";
  int rc;
  int rcCleanup;

  if( !parseConfig(argc, argv, &config) ){
    usage(argv[0]);
    return EXIT_FAILURE;
  }
  rc = setupBenchmark(&benchmark, config.m);
  if( rc!=SQLITE_OK ){
    reportFailure("setup", rc);
    cleanupBenchmark(&benchmark);
    return EXIT_FAILURE;
  }

  if( config.eMode==MODE_DUMP ){
    if( emitConfig(stderr, &config)!=SQLITE_OK ){
      cleanupBenchmark(&benchmark);
      return EXIT_FAILURE;
    }
    rc = runDump(&benchmark, &zStage);
    rcCleanup = cleanupBenchmark(&benchmark);
    if( rc!=SQLITE_OK ){
      reportFailure(zStage, rc);
      return EXIT_FAILURE;
    }
    if( rcCleanup!=SQLITE_OK ){
      reportFailure("cleanup", rcCleanup);
      return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
  }

  rc = runSample(&benchmark, &config, &result, &zStage);
  rcCleanup = cleanupBenchmark(&benchmark);
  if( rc!=SQLITE_OK ){
    reportFailure(zStage, rc);
    return EXIT_FAILURE;
  }
  if( rcCleanup!=SQLITE_OK ){
    reportFailure("cleanup", rcCleanup);
    return EXIT_FAILURE;
  }
  if( emitSample(&config, &result)!=SQLITE_OK ){
    reportFailure("output", SQLITE_IOERR);
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
