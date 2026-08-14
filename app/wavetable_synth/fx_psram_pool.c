/****************************************************************************
 * 1 MiB FX bump allocator backed by linker-reserved PSRAM section.
 ****************************************************************************/

#include "fx_psram_pool.h"

#include <stdint.h>
#include <string.h>
#include <syslog.h>

/* Provided by armca_psram.lds.S (.fx_psram_pool in PSRAM, not NuttX heap). */
extern uint8_t __fx_pool_start[];
extern uint8_t __fx_alloc_start[];
extern uint8_t __fx_pool_end[];

static uint8_t *g_fx_cur;
static size_t g_fx_size;

static size_t align_up(size_t v, size_t a)
{
  return (v + (a - 1u)) & ~(a - 1u);
}

void fx_pool_init(void)
{
  uintptr_t start = (uintptr_t)__fx_alloc_start;
  uintptr_t end = (uintptr_t)__fx_pool_end;

  if (end <= start)
    {
      g_fx_cur = NULL;
      g_fx_size = 0;
      syslog(LOG_ERR, "fx_pool: empty linker region start=%p end=%p\n",
             (void *)start, (void *)end);
      return;
    }

  g_fx_size = (size_t)(end - start);
  g_fx_cur = __fx_alloc_start;
  /* Custom NOLOAD section is not covered by the normal NuttX BSS clear. */
  memset(__fx_pool_start, 0,
         (size_t)((uintptr_t)__fx_pool_end -
                  (uintptr_t)__fx_pool_start));
  syslog(LOG_NOTICE,
         "fx_pool: PSRAM alloc=%lu KiB @ %p..%p, state=%lu KiB\n",
         (unsigned long)(g_fx_size / 1024u),
         (void *)start, (void *)end,
         (unsigned long)(((uintptr_t)__fx_alloc_start -
                          (uintptr_t)__fx_pool_start) / 1024u));
}

void fx_pool_reset(void)
{
  if (g_fx_size == 0)
    {
      return;
    }
  g_fx_cur = __fx_alloc_start;
  memset(__fx_alloc_start, 0, g_fx_size);
}

void *fx_pool_alloc(size_t bytes)
{
  uintptr_t cur;
  uintptr_t next;
  size_t need;

  if (bytes == 0 || g_fx_size == 0 || !g_fx_cur)
    {
      return NULL;
    }

  cur = align_up((uintptr_t)g_fx_cur, 64u);
  need = align_up(bytes, 64u);
  next = cur + need;
  if (next < cur || next > (uintptr_t)__fx_alloc_start + g_fx_size)
    {
      syslog(LOG_ERR, "fx_pool: OOM need=%lu used=%lu/%lu\n",
             (unsigned long)bytes,
             (unsigned long)fx_pool_used(),
             (unsigned long)g_fx_size);
      return NULL;
    }

  g_fx_cur = (uint8_t *)next;
  return (void *)cur;
}

void *fx_pool_calloc(size_t count, size_t size)
{
  size_t bytes;
  void *p;

  if (count == 0 || size == 0)
    {
      return NULL;
    }
  if (count > (SIZE_MAX / size))
    {
      return NULL;
    }
  bytes = count * size;
  p = fx_pool_alloc(bytes);
  /* Pool is zeroed on init/reset; fresh bump spans are still clean. */
  if (p)
    {
      memset(p, 0, bytes);
    }
  return p;
}

size_t fx_pool_used(void)
{
  if (!g_fx_size || !g_fx_cur)
    {
      return 0;
    }
  return (size_t)(g_fx_cur - __fx_alloc_start);
}

size_t fx_pool_free(void)
{
  if (g_fx_size < fx_pool_used())
    {
      return 0;
    }
  return g_fx_size - fx_pool_used();
}

uintptr_t fx_pool_base(void)
{
  return (uintptr_t)__fx_alloc_start;
}
