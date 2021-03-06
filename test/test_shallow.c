/* Libottery by Nick Mathewson.

   This software has been dedicated to the public domain under the CC0
   public domain dedication.

   To the extent possible under law, the person who associated CC0 with
   libottery has waived all copyright and related or neighboring rights
   to libottery.

   You should have received a copy of the CC0 legalcode along with this
   work in doc/cc0.txt.  If not, see
      <http://creativecommons.org/publicdomain/zero/1.0/>.
 */
/**
 * @file test_shallow.c
 *
 * "Shallow" unit tests that try to make sure of various black-box and
 * glass-box properties of the libottery code without investigating how it
 * calls its backends.
 */
#define OTTERY_INTERNAL
#include "ottery-internal.h"
#include "ottery.h"
#include "ottery_st.h"
#include "ottery_nolock.h"

#include "tinytest.h"
#include "tinytest_macros.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#define STATE() state
#define STATE_NOLOCK() ((struct ottery_state_nolock *)state)
#define USING_STATE() (state != NULL)
#define USING_NOLOCK() (state_nolock != 0)
#include "st_wrappers.h"

char *state_allocation = NULL;
struct ottery_state *state = NULL;
int state_nolock = 0;

#define OT_ENABLE_STATE TT_FIRST_USER_FLAG
#define OT_ENABLE_STATE_NOLOCK ((TT_FIRST_USER_FLAG<<1)|OT_ENABLE_STATE)

void *
setup_state(const struct testcase_t *testcase)
{
  if (state)
    return NULL;
  if (testcase->flags & OT_ENABLE_STATE) {
    if (ottery_get_sizeof_state() != ottery_get_sizeof_state_nolock())
      return NULL;
    state_allocation = malloc(ottery_get_sizeof_state() + 16);
    const int misalign = (int) (((uintptr_t)state_allocation) & 0xf);
    state = (struct ottery_state *)(state_allocation + ((16-misalign)&0xf));
    if (testcase->flags & OT_ENABLE_STATE_NOLOCK)
      ottery_st_init_nolock(STATE_NOLOCK(), NULL);
    else
      ottery_st_init(state, NULL);
    state_nolock = (testcase->flags & OT_ENABLE_STATE_NOLOCK);
  }
  return (void*) 1;
}
int
cleanup_state(const struct testcase_t *testcase, void *ptr)
{
  int retval = 1;
  (void)testcase;
  (void)ptr;
  if (state) {
    if (state->block_counter > 512 ||
        state->pos >= state->prf.output_len ||
        state->pid != getpid()) {
      retval = 0;
    }
  }
  if (state_allocation) {
    free(state_allocation);
  }
  return retval;
}
struct testcase_setup_t setup = {
  setup_state, cleanup_state
};

static void
test_get_sizeof(void *arg)
{
  (void)arg;
  tt_int_op(ottery_get_sizeof_config(), <=, sizeof(struct ottery_config));
  tt_int_op(ottery_get_sizeof_state(), <=, sizeof(struct ottery_state));
 end:
  ;
}

static void
test_osrandom(void *arg)
{
  (void) arg;
  uint8_t buf[66];
  uint8_t scratch[66];
  struct ottery_osrng_config cfg;
  int i, j;
  uint32_t flags=0;

  memset(buf, 0, sizeof(buf));
  memset(scratch, 0, sizeof(scratch));
  memset(&cfg, 0, sizeof(cfg));
  /* randbytes must succeed. */
  tt_int_op(0, ==, ottery_os_randbytes_(NULL, 0, buf, 64, scratch, &flags));
  tt_assert(flags & OTTERY_ENTROPY_FL_OS);
  tt_assert(flags & OTTERY_ENTROPY_FL_STRONG);

  /* Does it stop after the number of bytes we tell it? */
  tt_int_op(0, ==, buf[64]);
  tt_int_op(0, ==, buf[65]);
  tt_int_op(0, ==, scratch[64]);
  tt_int_op(0, ==, scratch[65]);

  /* Can we get an odd number of bytes? */  memset(buf, 0, sizeof(buf));
  memset(scratch, 0, sizeof(scratch));
  flags = 0;
  tt_int_op(0, ==, ottery_os_randbytes_(NULL, 0, buf, 63, scratch, &flags));
  tt_int_op(0, ==, buf[63]);
  tt_int_op(0, ==, buf[64]);
  tt_int_op(0, ==, scratch[63]);
  tt_int_op(0, ==, scratch[64]);
  tt_assert(flags & OTTERY_ENTROPY_FL_OS);
  tt_assert(flags & OTTERY_ENTROPY_FL_STRONG);

  /* Try it 8 times; make sure every byte is set at least once */
  for (i = 0; i < 7; ++i) {
    uint8_t buf2[64];
    tt_int_op(0, ==, ottery_os_randbytes_(NULL, 0, buf2, 64, scratch, &flags));
    for (j = 0; j < 64; ++j)
      buf[j] |= buf2[j];
  }
  for (j = 0; j < 64; ++j)
    tt_int_op(0, !=, buf[j]);

  cfg.disabled_sources = OTTERY_ENTROPY_SRC_RDRAND;
  cfg.urandom_fname = "/dev/please-dont-create-this-file";
  flags = 0;
  tt_int_op(OTTERY_ERR_INIT_STRONG_RNG, ==,
            ottery_os_randbytes_(&cfg, 0, buf, 12, scratch, &flags));
  tt_int_op(flags, ==, 0);

  cfg.urandom_fname = "/dev/null";
  tt_int_op(OTTERY_ERR_ACCESS_STRONG_RNG, ==,
            ottery_os_randbytes_(&cfg, 0, buf, 12, scratch, &flags));
  tt_int_op(flags, ==, 0);

  /* Make sure at least one OS source works. */
  cfg.disabled_sources = 0;
  flags = 0;
  tt_int_op(0, ==, ottery_os_randbytes_(NULL, OTTERY_ENTROPY_FL_OS, buf, 64, scratch, &flags));
  tt_assert(flags & OTTERY_ENTROPY_FL_OS);
  tt_assert(flags & OTTERY_ENTROPY_FL_STRONG);

  /* Make sure at least one OS source works in another way. */
  cfg.disabled_sources = 0;
  ottery_disable_cpu_capabilities_(OTTERY_CPUCAP_RAND);
  tt_int_op(0, ==, ottery_os_randbytes_(NULL, 0, buf, 64, scratch, &flags));
  tt_assert(flags & OTTERY_ENTROPY_FL_OS);
  tt_assert(flags & OTTERY_ENTROPY_FL_STRONG);

 end:
  ;
}

static void
test_single_buf(size_t n)
{
  unsigned offset, i, j;
  uint8_t *b = malloc(n + 32);
  uint8_t *sum = malloc(n);

  tt_assert(b);
  tt_assert(sum);

  for (offset = 0; offset < 16; ++offset) {
    memset(b, 0, n+32);
    memset(sum, 0, n);
    for (i = 0; i < 100; ++i) {
      OTTERY_RAND_BYTES(b + offset, n);

      /* Make sure that it doesn't write outside of its range. */
      for (j = 0; j < offset; ++j) {
        tt_int_op(b[j], ==, 0);
      }
      for (j = n+offset; j < n+32; ++j) {
        tt_int_op(b[j], ==, 0);
      }

      /* Make sure every in-range byte sometimes gets written to. */
      for (j = 0; j < n; ++j) {
        sum[j] |= b[offset+j];
      }
    }
    for (j = 0; j < n; ++j) {
      tt_int_op(sum[j], ==, 0xff);
    }
  }

 end:
  if (sum)
    free(sum);
  if (b)
    free(b);
}

static void
test_rand_little_buf(void *arg)
{
  (void)arg;
  test_single_buf(1);
  test_single_buf(4);
  test_single_buf(7);
  test_single_buf(30);
  test_single_buf(63);
}

static void
test_rand_big_buf(void *arg)
{
  (void)arg;
  test_single_buf(64);
  test_single_buf(79);
  test_single_buf(80);
  test_single_buf(120);
  test_single_buf(1024);
  test_single_buf(2048);
  test_single_buf(4096);
}

static void
test_rand_uint(void *arg)
{
  (void)arg;
  int i;
  unsigned acc = 0, dec = (unsigned)-1;
  uint32_t acc32 = 0, dec32 = (uint32_t)-1;
  uint64_t acc64 = 0, dec64 = (uint64_t)-1;
  uint8_t buf;
  /* Not much we can do to black-box test rand_unsinged and rand_uint64 other
     than to make sure that they don't crash and that the values are
     random-ish. For real results, we'll need the deep tests, plus statistical
     examining of dump_bytes.c output.
   */

  for (i = 0; i < 100; ++i) {
    unsigned u = OTTERY_RAND_UNSIGNED();
    uint32_t u32 = OTTERY_RAND_UINT32();
    uint64_t u64 = OTTERY_RAND_UINT64();
    acc |= u;
    acc32 |= u32;
    acc64 |= u64;
    dec &= u;
    dec32 &= u32;
    dec64 &= u64;
  }

  tt_int_op(acc, ==, (unsigned)-1);
  tt_assert(acc32 == (uint32_t)-1);
  tt_assert(acc64 == (uint64_t)-1);
  tt_int_op(dec, ==, 0);
  tt_assert(dec32 == 0);
  tt_assert(dec64 == 0);

  acc = 0;
  acc32 = 0;
  acc64 = 0;
  dec = (unsigned)-1;
  dec32 = (unsigned)-1;
  dec64 = (uint64_t)-1;
  for (i = 0; i < 100; ++i) {
    unsigned u = OTTERY_RAND_UNSIGNED();
    unsigned u32 = OTTERY_RAND_UINT32();
    uint64_t u64 = OTTERY_RAND_UINT64();
    OTTERY_RAND_BYTES(&buf, 1); /* make sure unaligned works. */
    acc |= u;
    acc32 |= u32;
    acc64 |= u64;
    dec &= u;
    dec32 &= u32;
    dec64 &= u64;
  }
  tt_int_op(acc, ==, (unsigned)-1);
  tt_assert(acc32 == (uint32_t)-1);
  tt_assert(acc64 == (uint64_t)-1);
  tt_int_op(dec, ==, 0);
  tt_assert(dec32 == 0);
  tt_assert(dec64 == 0);

 end:
  ;
}

static void
test_range(void *arg)
{
  int i;
  int count[6];
  const uint64_t quite_big = ((uint64_t)1)<<60;
  int got_a_big_small_one = 0;
  int got_a_big_one = 0;
  (void)arg;

  memset(count, 0, sizeof(count));

  for (i = 0; i < 1000; ++i) {
    tt_int_op(OTTERY_RAND_RANGE(5), <=, 5);
    count[OTTERY_RAND_RANGE(5)] += 1;
    tt_int_op(OTTERY_RAND_RANGE(0), ==, 0);
    tt_int_op(OTTERY_RAND_RANGE64(0), ==, 0);
    tt_int_op(OTTERY_RAND_RANGE(10), <=, 10);
    tt_int_op(OTTERY_RAND_RANGE(100), <=, 100);
    tt_int_op(OTTERY_RAND_RANGE64(100), <=, 100);
    tt_int_op(OTTERY_RAND_RANGE64(10000), <=, 10000);
    tt_assert(OTTERY_RAND_RANGE64(quite_big) <= quite_big);
    if (OTTERY_RAND_RANGE64(quite_big) > (((uint64_t)1)<<40))
      ++got_a_big_one;
    if (OTTERY_RAND_RANGE(3000000000U) > 2000000000U)
      ++got_a_big_small_one;
  }
  for (i = 0; i <= 5; ++i) {
    tt_int_op(0, !=, count[i]);
  }
  tt_int_op(got_a_big_one, !=, 0);
  tt_int_op(got_a_big_small_one, !=, 0);

 end:
  ;
}

void
test_fork(void *arg)
{
  (void) arg;
#if defined(_WIN32) || defined(OTTERY_NO_PID_CHECK)
  tt_skip();
 end:
  ;
#else
  uint8_t buf[256];
  uint8_t buf2[256];
  int fd[2] = { -1, -1 };
  pid_t p;

  if (pipe(fd) < 0)
    tt_abort_perror("pipe");

  if (!state)
    ottery_init(NULL);

  if ((p = fork()) == 0) {
    /* child */
    OTTERY_RAND_BYTES(buf, sizeof(buf));
    if (write(fd[1], buf, sizeof(buf)) < 0) {
      perror("write");
    }
    exit(0);
  } else if (p == -1) {
    tt_abort_perror("fork");
  }
  /* parent. */
  OTTERY_RAND_BYTES(buf, sizeof(buf));
  tt_int_op(sizeof(buf2), ==, read(fd[0], buf2, sizeof(buf2)));
  tt_assert(memcmp(buf, buf2, sizeof(buf)))
  tt_assert(memcmp(buf, buf2, 16))

 end:
  if (fd[0] >= 0)
    close(fd[0]);
  if (fd[1] >= 0)
    close(fd[1]);
#endif
}

void
test_bad_init(void *arg)
{
  (void)arg;

  struct ottery_config cfg;
  struct ottery_prf bad_prf;

  tt_int_op(0, ==, ottery_config_init(&cfg));
  tt_int_op(OTTERY_ERR_INVALID_ARGUMENT, ==,
            ottery_config_force_implementation(&cfg, "rc4"));
  tt_int_op(0, ==,
            ottery_config_force_implementation(&cfg, "CHACHA20"));

  memcpy(&bad_prf, &ottery_prf_chacha20_merged_, sizeof(struct ottery_prf));
  bad_prf.state_len = 1024;
  ottery_config_set_manual_prf_(&cfg, &bad_prf);
  tt_int_op(OTTERY_ERR_INTERNAL, ==, OTTERY_INIT(&cfg));

  memcpy(&bad_prf, &ottery_prf_chacha20_merged_, sizeof(struct ottery_prf));
  bad_prf.state_bytes = 2000;
  ottery_config_set_manual_prf_(&cfg, &bad_prf);
  tt_int_op(OTTERY_ERR_INTERNAL, ==, OTTERY_INIT(&cfg));

  memcpy(&bad_prf, &ottery_prf_chacha20_merged_, sizeof(struct ottery_prf));
  bad_prf.output_len = 8;
  ottery_config_set_manual_prf_(&cfg, &bad_prf);
  tt_int_op(OTTERY_ERR_INTERNAL, ==, OTTERY_INIT(&cfg));

  ottery_config_init(&cfg);
  ottery_config_force_implementation(&cfg, "CHACHA20");
  tt_int_op(0, ==, OTTERY_INIT(&cfg));

  ottery_config_set_urandom_device_(&cfg,"/dev/please-dont-add-this-device");
  ottery_config_disable_entropy_sources_(&cfg, OTTERY_ENTROPY_SRC_RDRAND);
  tt_int_op(OTTERY_ERR_INIT_STRONG_RNG, ==, OTTERY_INIT(&cfg));

  ottery_config_set_urandom_device_(&cfg,"/dev/null");
  ottery_config_disable_entropy_sources_(&cfg, OTTERY_ENTROPY_SRC_RDRAND);
  tt_int_op(OTTERY_ERR_ACCESS_STRONG_RNG, ==, OTTERY_INIT(&cfg));

  ottery_config_set_urandom_device_(&cfg, NULL);
  tt_int_op(0, ==, OTTERY_INIT(&cfg));

 end:
  ;
}

void
test_reseed_stir(void *arg)
{
  (void)arg;
  uint8_t buf[256], buf2[256];
  struct ottery_state st_orig;
  int op;
  int r;

  OTTERY_RAND_UNSIGNED();
  if (state) {
    memcpy(&st_orig, state, sizeof(st_orig));
  }
  OTTERY_RAND_BYTES(buf, sizeof(buf));
  for (op = 0; op < 6; ++op) {
    if (state)
      memcpy(state, &st_orig, sizeof(st_orig));
    r = 0;
    if (op == 0) {
      ;
    } else if (op == 1) {
      OTTERY_STIR();
    } else if (op == 2) {
      r = OTTERY_ADD_SEED((const uint8_t*)"chosen by fair dice roll. "
                          "guaranteed to be random.",50);
    } else if (op == 3) {
      r = OTTERY_ADD_SEED(NULL, 6060842);
    } else if (op == 4) {
      r = OTTERY_ADD_SEED((const uint8_t*)"", 0);
    } else if (op == 5) {
      OTTERY_WIPE();
      if (state)
        OTTERY_INIT(NULL);
    }
    OTTERY_RAND_BYTES(buf2, sizeof(buf2));
    tt_int_op(r, ==, 0);
    if ((op == 0 || op == 1) && state) {
      tt_assert(0 == memcmp(buf, buf2, sizeof(buf)));
    } else {
      tt_assert(memcmp(buf, buf2, sizeof(buf)));
      tt_assert(memcmp(buf, buf2, 16));
    }
  }

  /* Now check failing ADD_SEED. */
  if (state) {
    state->osrng_config.disabled_sources = ~0;
    tt_int_op(OTTERY_ERR_INIT_STRONG_RNG, ==, OTTERY_ADD_SEED(NULL, 0));
  }

 end:
  ;
}

static void
test_misaligned_init(void *arg)
{
  (void)arg;
  char *ptr;
  struct ottery_state *st;

  ptr = malloc(sizeof(struct ottery_state) + 1);
  tt_assert(ptr);

  if ((((uintptr_t)ptr) & 0xf) == 0) {
    st = (void*)(ptr+1);
  } else {
    st = (void*)ptr;
  }
  /* Now st is misaligned. */

  tt_int_op(OTTERY_ERR_STATE_ALIGNMENT, ==, ottery_st_init(st, NULL));

 end:
  if (ptr)
    free(ptr);
}

static int got_fatal_err = 0;
static void
fatal_handler(int err)
{
  if (got_fatal_err != 0 || err == 0)
    got_fatal_err = -1;
  else
    got_fatal_err = err;
}

static void
test_select_prf(void *arg)
{
  struct ottery_config cfg;
  (void)arg;

  tt_int_op(0, ==, ottery_config_init(&cfg));

  /* Select by name. */
  tt_int_op(0, ==, ottery_config_force_implementation(&cfg, "CHACHA8"));
  tt_ptr_op(cfg.impl, !=, NULL);
  tt_str_op(cfg.impl->name, ==, "CHACHA8");

  /* Select by implementation. */
  tt_int_op(0, ==, ottery_config_force_implementation(&cfg, "CHACHA12-NOSIMD"));
  tt_ptr_op(cfg.impl, !=, NULL);
  tt_ptr_op(cfg.impl, ==, &ottery_prf_chacha12_merged_);

  /* Select by flavor. */
  tt_int_op(0, ==, ottery_config_force_implementation(&cfg,
                                                   "CHACHA20-NOSIMD-DEFAULT"));
  tt_ptr_op(cfg.impl, !=, NULL);
  tt_ptr_op(cfg.impl, ==, &ottery_prf_chacha20_merged_);

  /* Select the default. */
  tt_int_op(0, ==, ottery_config_init(&cfg));
  tt_int_op(0, ==, ottery_config_force_implementation(&cfg, NULL));
  tt_ptr_op(cfg.impl, !=, NULL);
  TT_BLATHER(("The default PRF is %s", cfg.impl->flav));

  /* Now turn off our SIMD support and verify that we get a non-SIMD
   * implementation. */
  ottery_disable_cpu_capabilities_(OTTERY_CPUCAP_SIMD);
  tt_int_op(0, ==, ottery_config_force_implementation(&cfg, NULL));
  tt_ptr_op(cfg.impl, !=, NULL);
  tt_ptr_op(cfg.impl, ==, &ottery_prf_chacha20_merged_);

 end:
  ;
}

static void
test_fatal(void *arg)
{
  (void)arg;
  __attribute__((aligned(16))) struct ottery_state st;
  __attribute__((aligned(16))) struct ottery_state_nolock st_nl;
  int tested = 0;

  ottery_set_fatal_handler(fatal_handler);
  memset(&st, 0xff, sizeof(st));
  memset(&st_nl, 0xff, sizeof(st_nl));

#ifndef OTTERY_NO_INIT_CHECK
  uint8_t buf[8];
  ++tested;
  /* Fatal errors from uninitialized states */
  got_fatal_err = 0;
  ottery_st_add_seed(&st, (const uint8_t*)"xyz", 3);
  tt_int_op(got_fatal_err, ==, OTTERY_ERR_STATE_INIT);

  got_fatal_err = 0;
  ottery_st_add_seed_nolock(&st_nl, (const uint8_t*)"xyz", 3);
  tt_int_op(got_fatal_err, ==, OTTERY_ERR_STATE_INIT);

  got_fatal_err = 0;
  ottery_st_rand_unsigned(&st);
  tt_int_op(got_fatal_err, ==, OTTERY_ERR_STATE_INIT);

  got_fatal_err = 0;
  ottery_st_rand_unsigned_nolock(&st_nl);
  tt_int_op(got_fatal_err, ==, OTTERY_ERR_STATE_INIT);

  got_fatal_err = 0;
  ottery_st_rand_bytes(&st, buf, 8);
  tt_int_op(got_fatal_err, ==, OTTERY_ERR_STATE_INIT);

  got_fatal_err = 0;
  ottery_st_rand_bytes_nolock(&st_nl, buf, 8);
  tt_int_op(got_fatal_err, ==, OTTERY_ERR_STATE_INIT);

  got_fatal_err = 0;
  (void) ottery_st_rand_range(&st, 12);
  tt_int_op(got_fatal_err, ==, OTTERY_ERR_STATE_INIT);

  got_fatal_err = 0;
  (void) ottery_st_rand_range(&st_nl, 12);
  tt_int_op(got_fatal_err, ==, OTTERY_ERR_STATE_INIT);

  got_fatal_err = 0;
  (void) ottery_st_rand_range64(&st, 12);
  tt_int_op(got_fatal_err, ==, OTTERY_ERR_STATE_INIT);

  got_fatal_err = 0;
  (void) ottery_st_rand_range64_nolock(&st_nl, 12);
  tt_int_op(got_fatal_err, ==, OTTERY_ERR_STATE_INIT);
#endif

#ifndef OTTERY_NO_PID_CHECK
  ++tested;
  /* Okay, now we're going to fake out the initialization-failure-postfork
   * code. */
  got_fatal_err = 0;
  tt_int_op(0, ==, ottery_st_init(&st, NULL));
  ottery_st_rand_unsigned(&st);
  st.pid = getpid() + 100; /* force a postfork reseed. */
  st.osrng_config.urandom_fname = "/dev/null"; /* make reseed impossible */
  st.osrng_config.disabled_sources = OTTERY_ENTROPY_SRC_RDRAND;
  tt_int_op(got_fatal_err, ==, 0);
  ottery_st_rand_unsigned(&st);
  tt_int_op(got_fatal_err, ==,
            OTTERY_ERR_ACCESS_STRONG_RNG|OTTERY_ERR_FLAG_POSTFORK_RESEED);

  got_fatal_err = 0;
  tt_int_op(0, ==, ottery_st_init_nolock(&st_nl, NULL));
  ottery_st_rand_unsigned_nolock(&st_nl);
  st_nl.pid = getpid() + 100; /* force a postfork reseed. */
  st_nl.osrng_config.urandom_fname = "/dev/null"; /* make reseed impossible */
  st_nl.osrng_config.disabled_sources = OTTERY_ENTROPY_SRC_RDRAND;
  tt_int_op(got_fatal_err, ==, 0);
  ottery_st_rand_unsigned_nolock(&st_nl);
  tt_int_op(got_fatal_err, ==,
            OTTERY_ERR_ACCESS_STRONG_RNG|OTTERY_ERR_FLAG_POSTFORK_RESEED);
#endif

  if (!tested)
    tt_skip();

 end:
  ;
}

struct testcase_t misc_tests[] = {
  { "osrandom", test_osrandom, TT_FORK, NULL, NULL },
  { "get_sizeof", test_get_sizeof, 0, NULL, NULL },
  { "select_prf", test_select_prf, TT_FORK, 0, NULL },
  { "fatal", test_fatal, TT_FORK, NULL, NULL },
  END_OF_TESTCASES
};

#define COMMON_TESTS(flags)                                            \
  { "range", test_range, TT_FORK|flags, &setup, NULL },                \
  { "unsigned", test_rand_uint, TT_FORK|flags, &setup, NULL },         \
  { "little_buf", test_rand_little_buf, TT_FORK|flags, &setup, NULL }, \
  { "big_buf", test_rand_big_buf, TT_FORK|flags, &setup, NULL },       \
  { "fork", test_fork, TT_FORK|flags, &setup, NULL },                  \
  { "bad_init", test_bad_init, TT_FORK|flags, &setup, NULL, },         \
  { "reseed_and_stir", test_reseed_stir, TT_FORK|flags, &setup, NULL, }

struct testcase_t stateful_tests[] = {
  COMMON_TESTS(OT_ENABLE_STATE),
  { "misaligned_init", test_misaligned_init, 0, NULL, NULL },
  END_OF_TESTCASES,
};

struct testcase_t nolock_tests[] = {
  COMMON_TESTS(OT_ENABLE_STATE_NOLOCK),
  END_OF_TESTCASES,
};

struct testcase_t global_tests[] = {
  COMMON_TESTS(0),
  END_OF_TESTCASES,
};

struct testgroup_t groups[] = {
  { "misc/", misc_tests },
  { "state/", stateful_tests },
  { "nolock/", nolock_tests },
  { "global/", global_tests },
  END_OF_GROUPS
};

int
main(int argc, const char **argv)
{
  setbuf(stdout, NULL);
  return tinytest_main(argc, argv, groups);
}
