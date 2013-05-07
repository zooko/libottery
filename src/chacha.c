#include "chacha.h"
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#define u64 uint64_t
#define u32 uint32_t
#define u8 uint8_t

#ifndef CHACHA_RNDS
#define CHACHA_RNDS 20    /* 8 (high speed), 20 (conservative), 12 (middle) */
#endif

#if ! defined(CHACHARAND_NO_VECS)      \
       && (defined(__ARM_NEON__) ||    \
           defined(__ALTIVEC__)  ||    \
           defined(__SSE2__))
#include "src/chacha_krovetz.c"
#else
#include "src/chacha_merged.c"
#endif

#if CHACHA_RNDS == 8
/*HACK*/
void
chacha_state_setup(struct chacha_state *state,
                   const uint8_t *key,
                   const uint8_t *nonce,
                   uint64_t counter)
{
  memcpy(state->key, key, 32);
  memcpy(state->nonce, nonce, 8);
  state->block_counter = counter;
}

int
chacharand_os_bytes(void *out, size_t outlen)
{
  int fd;
  fd = open("/dev/urandom", O_RDONLY|O_CLOEXEC);
  if (fd < 0)
    return -1;
  if (read(fd, out, outlen) != outlen)
    return -1;
  close(fd);
  return 0;
}
#endif

#ifdef BPI
#define BUFFER_SIZE (BPI*64)
#else
#define BUFFER_SIZE (64)
#endif
#define BUFFER_MASK ((size_t)(BUFFER_SIZE - 1))

#if BUFFER_SIZE > 256
#error "impossible buffer size"
#endif

struct chacharand_state {
  struct chacha_state chst;
  uint8_t buffer[BUFFER_SIZE];
  uint8_t pos;
  uint8_t initialized;
};

static void
chacharand_memclear(void *mem, size_t len)
{
  memset(mem, 0, len); /* XXXXX WRONG WRONG WRONG ! */
}

#if CHACHA_RNDS == 8
#define chacharand_init chacharand8_init
#define chacharand_flush chacharand8_flush
#define chacharand_stir chacharand8_stir
#define chacharand_bytes chacharand8_bytes
#define crypto_stream crypto_stream_8
#else
#define chacharand_init chacharand20_init
#define chacharand_flush chacharand20_flush
#define chacharand_stir chacharand20_stir
#define chacharand_bytes chacharand20_bytes
#define crypto_stream crypto_stream_20
#endif

int
chacharand_init(struct chacharand_state *st)
{
  uint8_t inp[40];
  memset(st, 0, sizeof(st));
  if (chacharand_os_bytes(inp, sizeof(inp)) < 0)
    return -1;
  chacha_state_setup(&st->chst, inp, inp+32, 0);
  chacharand_memclear(inp, 40);
  crypto_stream(st->buffer, BUFFER_SIZE, &st->chst);
  st->pos=0;
  st->initialized = 1;
  return 0;
}

void
chacharand_flush(struct chacharand_state *st)
{
  crypto_stream(st->buffer, BUFFER_SIZE, &st->chst);
  st->pos = 0;
}

void
chacharand_stir(struct chacharand_state *st)
{
  uint8_t inp[BUFFER_SIZE];
  crypto_stream(inp, BUFFER_SIZE, &st->chst);
  chacha_state_setup(&st->chst, inp, inp+32, 0);
  chacharand_memclear(inp, 40);
  crypto_stream(st->buffer, BUFFER_SIZE, &st->chst);
  st->pos=0;
}

void
chacharand_bytes(struct chacharand_state *st, void *out,
                   size_t n)
{
  if (!st->initialized)
    abort();

  if (n >= BUFFER_SIZE) {
    crypto_stream(out, n & ~BUFFER_MASK, &st->chst);
    out += (n & ~BUFFER_MASK);
    n &= BUFFER_MASK;
  }

  if (n + st->pos < BUFFER_SIZE) {
    memcpy(out, st->buffer+st->pos, n);
    st->pos += n;
  } else {
    memcpy(out, st->buffer+st->pos, BUFFER_SIZE-st->pos);
    n -= (BUFFER_SIZE-st->pos);
    out += (BUFFER_SIZE-st->pos);
    crypto_stream(st->buffer, BUFFER_SIZE, &st->chst);
    memcpy(out, st->buffer, n);
    st->pos = n;
  }

  if (st->chst.block_counter > (1<<20))
    chacharand_stir(st);
}

