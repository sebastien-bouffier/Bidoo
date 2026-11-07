
#ifndef GVERBDSP_H
#define GVERBDSP_H

#include "ladspa-util.h"

typedef struct {
  int size;
  int idx;
  float *buf;
} ty_fixeddelay;

typedef struct {
  int size;
  float coeff;
  int idx;
  float *buf;
} ty_diffuser;

typedef struct {
  float damping;
  float delay;
} ty_damper;

ty_diffuser *diffuser_make(int, float);
void diffuser_free(ty_diffuser *);
void diffuser_flush(ty_diffuser *);
//float diffuser_do(ty_diffuser *, float);

ty_damper *damper_make(float);
void damper_free(ty_damper *);
void damper_flush(ty_damper *);
//void damper_set(ty_damper *, float);
//float damper_do(ty_damper *, float);

ty_fixeddelay *fixeddelay_make(int);
void fixeddelay_free(ty_fixeddelay *);
void fixeddelay_flush(ty_fixeddelay *);
//float fixeddelay_read(ty_fixeddelay *, int);
//void fixeddelay_write(ty_fixeddelay *, float);

int isprime(int);
int nearest_prime(int, float);

static inline float diffuser_do(ty_diffuser *p, float x)
{
  float y,w;

  w = x - p->buf[p->idx]*p->coeff;
  w = flush_to_zero(w);
  y = p->buf[p->idx] + w*p->coeff;
  p->buf[p->idx] = w;
  /* compare and wrap: no integer division per sample */
  if (++p->idx >= p->size) p->idx = 0;
  return(y);
}

static inline float fixeddelay_read(ty_fixeddelay *p, int n)
{
  int i;

  i = p->idx - n;
  if (i < 0) i += p->size;
  return(p->buf[i]);
}

/* n samples back, n fractional (linear interpolation) */
static inline float fixeddelay_read_frac(ty_fixeddelay *p, float n)
{
  int i = (int)n;
  float f = n - (float)i;
  int a = p->idx - i;
  if (a < 0) a += p->size;
  int b = a - 1;
  if (b < 0) b += p->size;
  return p->buf[a] + (p->buf[b] - p->buf[a])*f;
}

static inline void fixeddelay_write(ty_fixeddelay *p, float x)
{
  p->buf[p->idx] = x;
  if (++p->idx >= p->size) p->idx = 0;
}

static inline void damper_set(ty_damper *p, float damping)
{
  p->damping = damping;
}

static inline float damper_do(ty_damper *p, float x)
{
  float y;

  y = x*(1.0-p->damping) + p->delay*p->damping;
  p->delay = y;
  return(y);
}

#endif
