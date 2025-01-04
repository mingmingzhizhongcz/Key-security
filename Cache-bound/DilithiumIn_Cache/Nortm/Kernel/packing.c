#include <linux/memory.h>
#include "params.h"
#include "packing.h"
#include "polyvec.h"
#include "poly.h"
#include "tsx.h"
#include "aes.h"
#include "rtm.h"

void printinfohex(char *info, unsigned char * output, int len);

/*************************************************
* Name:        pack_pk
*
* Description: Bit-pack public key pk = (rho, t1).
*
* Arguments:   - uint8_t pk[]: output byte array
*              - const uint8_t rho[]: byte array containing rho
*              - const polyveck *t1: pointer to vector t1
**************************************************/
void pack_pk(uint8_t pk[CRYPTO_PUBLICKEYBYTES],
             const uint8_t rho[SEEDBYTES],
             const polyveck *t1)
{
  unsigned int i;

  for(i = 0; i < SEEDBYTES; ++i)
    pk[i] = rho[i];
  pk += SEEDBYTES;

  for(i = 0; i < K; ++i)
    polyt1_pack(pk + i*POLYT1_PACKEDBYTES, &t1->vec[i]);
}

/*************************************************
* Name:        unpack_pk
*
* Description: Unpack public key pk = (rho, t1).
*
* Arguments:   - const uint8_t rho[]: output byte array for rho
*              - const polyveck *t1: pointer to output vector t1
*              - uint8_t pk[]: byte array containing bit-packed pk
**************************************************/
void unpack_pk(uint8_t rho[SEEDBYTES],
               polyveck *t1,
               const uint8_t pk[CRYPTO_PUBLICKEYBYTES])
{
  unsigned int i;

  for(i = 0; i < SEEDBYTES; ++i)
    rho[i] = pk[i];
  pk += SEEDBYTES;

  for(i = 0; i < K; ++i)
    polyt1_unpack(&t1->vec[i], pk + i*POLYT1_PACKEDBYTES);
}

/*************************************************
* Name:        pack_sk
*
* Description: Bit-pack secret key sk = (rho, tr, key, t0, s1, s2).
*
* Arguments:   - uint8_t sk[]: output byte array
*              - const uint8_t rho[]: byte array containing rho
*              - const uint8_t tr[]: byte array containing tr
*              - const uint8_t key[]: byte array containing key
*              - const polyveck *t0: pointer to vector t0
*              - const polyvecl *s1: pointer to vector s1
*              - const polyveck *s2: pointer to vector s2
**************************************************/
void pack_sk(uint8_t sk[CRYPTO_SECRETKEYBYTES],
             const uint8_t rho[SEEDBYTES],
             const uint8_t tr[CRHBYTES],
             const uint8_t key[SEEDBYTES],
             const polyveck *t0,
             const polyvecl *s1,
             const polyveck *s2)
{
  unsigned int i;

  for(i = 0; i < SEEDBYTES; ++i)
    sk[i] = rho[i];
  sk += SEEDBYTES;

  for(i = 0; i < SEEDBYTES; ++i)
    sk[i] = key[i];
  sk += SEEDBYTES;

  for(i = 0; i < CRHBYTES; ++i)
    sk[i] = tr[i];
  sk += CRHBYTES;

  for(i = 0; i < L; ++i)
  {
    // printinfohex("s1", (unsigned char *)&s1->vec[i], sizeof(poly));
    polyeta_pack(sk + i*POLYETA_PACKEDBYTES, &s1->vec[i]);
  }
  sk += L*POLYETA_PACKEDBYTES;

  for(i = 0; i < K; ++i)
    polyeta_pack(sk + i*POLYETA_PACKEDBYTES, &s2->vec[i]);
  sk += K*POLYETA_PACKEDBYTES;

  for(i = 0; i < K; ++i)
    polyt0_pack(sk + i*POLYT0_PACKEDBYTES, &t0->vec[i]);
}

/*************************************************
* Name:        unpack_sk
*
* Description: Unpack secret key sk = (rho, tr, key, t0, s1, s2).
*
* Arguments:   - const uint8_t rho[]: output byte array for rho
*              - const uint8_t tr[]: output byte array for tr
*              - const uint8_t key[]: output byte array for key
*              - const polyveck *t0: pointer to output vector t0
*              - const polyvecl *s1: pointer to output vector s1
*              - const polyveck *s2: pointer to output vector s2
*              - uint8_t sk[]: byte array containing bit-packed sk
**************************************************/
void unpack_sk(uint8_t rho[SEEDBYTES],
               uint8_t tr[CRHBYTES],
               uint8_t key[SEEDBYTES],
               polyveck *t0,
               polyvecl *s1,
               polyveck *s2,
               const uint8_t sk[CRYPTO_SECRETKEYBYTES])
{
  unsigned int i;

  memcpy(rho, sk, SEEDBYTES);

  sk += SEEDBYTES;
  memcpy(key, sk, SEEDBYTES);

  sk += SEEDBYTES;
  memcpy(tr, sk, CRHBYTES);

  sk += CRHBYTES;
  for(i=0; i < L; ++i)
    polyeta_unpack(&s1->vec[i], sk + i*POLYETA_PACKEDBYTES);

  sk += L*POLYETA_PACKEDBYTES;
  for(i=0; i < K; ++i)
    polyeta_unpack(&s2->vec[i], sk + i*POLYETA_PACKEDBYTES);

  sk += K*POLYETA_PACKEDBYTES;
  for(i=0; i < K; ++i)
    polyt0_unpack(&t0->vec[i], sk + i*POLYT0_PACKEDBYTES);
}


/*************************************************
* Name:        safe_unpack_sk
*
* Description: Unpack secret key sk = (rho, tr, key, t0, s1, s2).
*              secret key sk is input as cipher
*
* Arguments:   - const uint8_t rho[]: output byte array for rho
*              - const uint8_t tr[]: output byte array for tr
*              - const uint8_t key[]: output byte array for key
*              - const polyveck *t0: pointer to output vector t0
*              - const polyvecl *s1: pointer to output vector s1
*              - const polyveck *s2: pointer to output vector s2
*              - uint8_t sk[]: byte array containing bit-packed sk
**************************************************/

int safe_unpack_sk(uint8_t rho[SEEDBYTES],
               uint8_t tr[CRHBYTES],
               uint8_t key[SEEDBYTES],
               polyveck *t0,
               polyvecl *s1,
               polyveck *s2,
               const uint8_t sk[CRYPTO_SECRETKEYBYTES])
{
  unsigned int i;
  unsigned char buff[K*POLYT0_PACKEDBYTES];
#ifdef TSX_ENABLE
	unsigned long flags;
	unsigned int status,tsxflag = 0;
	int try;
#endif

  memcpy(rho, sk, SEEDBYTES);

  sk += SEEDBYTES;
  memcpy(key, sk, SEEDBYTES);

  sk += SEEDBYTES;
  memcpy(tr, sk, CRHBYTES);

  sk += CRHBYTES;
  memcpy(buff, sk, L*POLYETA_PACKEDBYTES);
#ifdef TSX_ENABLE
  tsxflag = 0;
  try = 0;
  while(!tsxflag) {
    get_cpu();
    local_irq_save(flags);
    preempt_disable();
    while(1) {
      if(++try == TSX_MAX_TIMES) {
        local_irq_restore(flags);
        put_cpu();
        preempt_enable();
        if(_xtest()){
          _xend();
        }
        printk("sk_unpack %d\n", try);
        return -2;
      } 
      status = _xbegin();
      if (status == _XBEGIN_STARTED)
        break;
    }
#endif

  // printinfohex("unpack s1 before decrypt", buff, L*POLYETA_PACKEDBYTES);
  AESDecryptWithMode(buff, L*POLYETA_PACKEDBYTES, buff, 0, NULL, ECB, NULL);
  // printinfohex("unpack s1 after decrypt", buff, L*POLYETA_PACKEDBYTES);
  for(i=0; i < L; ++i)
  {
    polyeta_unpack(&s1->vec[i], buff + i*POLYETA_PACKEDBYTES);
    AESEncryptWithMode((uint8_t *)&s1->vec[i], sizeof(poly), (uint8_t *)&s1->vec[i], 0, NULL, ECB, NULL);
  }
  memset(buff, 0, L*POLYETA_PACKEDBYTES);

#ifdef TSX_ENABLE
    tsxflag = 1;
    if(_xtest()){
      _xend();
    }
    local_irq_restore(flags);
    put_cpu();
    preempt_enable();
  }
#endif

  sk += L*POLYETA_PACKEDBYTES;
  memcpy(buff, sk, K*POLYETA_PACKEDBYTES);
#ifdef TSX_ENABLE
  tsxflag = 0;
  try = 0;
  while(!tsxflag) {
    get_cpu();
    local_irq_save(flags);
    preempt_disable();
    while(1) {
      if(++try == TSX_MAX_TIMES) {
        local_irq_restore(flags);
        put_cpu();
        preempt_enable();
        if(_xtest()){
          _xend();
        }
        printk("sk_unpack %d\n", try);
        return -2;
      } 
      status = _xbegin();
      if (status == _XBEGIN_STARTED)
        break;
    }
#endif

  // printinfohex("unpack s2 before decrypt", buff, K*POLYETA_PACKEDBYTES);
  AESDecryptWithMode(buff, K*POLYETA_PACKEDBYTES, buff, 0, NULL, ECB, NULL);
  // printinfohex("unpack s2 after decrypt", buff, K*POLYETA_PACKEDBYTES);
  for(i=0; i < K; ++i)
  {
    polyeta_unpack(&s2->vec[i], buff + i*POLYETA_PACKEDBYTES);
    AESEncryptWithMode((uint8_t *)&s2->vec[i], sizeof(poly), (uint8_t *)&s2->vec[i], 0, NULL, ECB, NULL);
  }
  memset(buff, 0, K*POLYETA_PACKEDBYTES);
  
#ifdef TSX_ENABLE
    tsxflag = 1;
    if(_xtest()){
      _xend();
    }
    local_irq_restore(flags);
    put_cpu();
    preempt_enable();
  }
#endif

  sk += K*POLYETA_PACKEDBYTES;
  memcpy(buff, sk, K*POLYT0_PACKEDBYTES);
#ifdef TSX_ENABLE
  tsxflag = 0;
  try = 0;
  while(!tsxflag) {
    get_cpu();
    local_irq_save(flags);
    preempt_disable();
    while(1) {
      if(++try == TSX_MAX_TIMES) {
        local_irq_restore(flags);
        put_cpu();
        preempt_enable();
        if(_xtest()){
          _xend();
        }
        printk("sk_unpack %d\n", try);
        return -2;
      } 
      status = _xbegin();
      if (status == _XBEGIN_STARTED)
        break;
    }
#endif

  // printinfohex("unpack t0 before decrypt", buff, K*POLYT0_PACKEDBYTES);
  AESDecryptWithMode(buff, K*POLYT0_PACKEDBYTES, buff, 0, NULL, ECB, NULL);
  // printinfohex("unpack t0 after decrypt", buff, K*POLYT0_PACKEDBYTES);
  for(i=0; i < K; ++i)
  {
    polyt0_unpack(&t0->vec[i], buff + i*POLYT0_PACKEDBYTES);
    AESEncryptWithMode((uint8_t *)&t0->vec[i], sizeof(poly), (uint8_t *)&t0->vec[i], 0, NULL, ECB, NULL);
  }
  memset(buff, 0, K*POLYT0_PACKEDBYTES);
  
#ifdef TSX_ENABLE
    tsxflag = 1;
    if(_xtest()){
      _xend();
    }
    local_irq_restore(flags);
    put_cpu();
    preempt_enable();
  }
#endif
  return 0;
}

/*************************************************
* Name:        pack_sig
*
* Description: Bit-pack signature sig = (c, z, h).
*
* Arguments:   - uint8_t sig[]: output byte array
*              - const uint8_t *c: pointer to challenge hash length SEEDBYTES
*              - const polyvecl *z: pointer to vector z
*              - const polyveck *h: pointer to hint vector h
**************************************************/
void pack_sig(uint8_t sig[CRYPTO_BYTES],
              const uint8_t c[SEEDBYTES],
              const polyvecl *z,
              const polyveck *h)
{
  unsigned int i, j, k;

  for(i=0; i < SEEDBYTES; ++i)
    sig[i] = c[i];
  sig += SEEDBYTES;

  for(i = 0; i < L; ++i)
    polyz_pack(sig + i*POLYZ_PACKEDBYTES, &z->vec[i]);
  sig += L*POLYZ_PACKEDBYTES;

  /* Encode h */
  for(i = 0; i < OMEGA + K; ++i)
    sig[i] = 0;

  k = 0;
  for(i = 0; i < K; ++i) {
    for(j = 0; j < N; ++j)
      if(h->vec[i].coeffs[j] != 0)
        sig[k++] = j;

    sig[OMEGA + i] = k;
  }
}

/*************************************************
* Name:        unpack_sig
*
* Description: Unpack signature sig = (c, z, h).
*
* Arguments:   - uint8_t *c: pointer to output challenge hash
*              - polyvecl *z: pointer to output vector z
*              - polyveck *h: pointer to output hint vector h
*              - const uint8_t sig[]: byte array containing
*                bit-packed signature
*
* Returns 1 in case of malformed signature; otherwise 0.
**************************************************/
int unpack_sig(uint8_t c[SEEDBYTES],
               polyvecl *z,
               polyveck *h,
               const uint8_t sig[CRYPTO_BYTES])
{
  unsigned int i, j, k;

  for(i = 0; i < SEEDBYTES; ++i)
    c[i] = sig[i];
  sig += SEEDBYTES;

  for(i = 0; i < L; ++i)
    polyz_unpack(&z->vec[i], sig + i*POLYZ_PACKEDBYTES);
  sig += L*POLYZ_PACKEDBYTES;

  /* Decode h */
  k = 0;
  for(i = 0; i < K; ++i) {
    for(j = 0; j < N; ++j)
      h->vec[i].coeffs[j] = 0;

    if(sig[OMEGA + i] < k || sig[OMEGA + i] > OMEGA)
      return 1;

    for(j = k; j < sig[OMEGA + i]; ++j) {
      /* Coefficients are ordered for strong unforgeability */
      if(j > k && sig[j] <= sig[j-1]) return 1;
      h->vec[i].coeffs[sig[j]] = 1;
    }

    k = sig[OMEGA + i];
  }

  /* Extra indices are zero for strong unforgeability */
  for(j = k; j < OMEGA; ++j)
    if(sig[j])
      return 1;

  return 0;
}
