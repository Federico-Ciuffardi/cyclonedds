/*
 * Copyright(c) 2006 to 2018 ADLINK Technology Limited and others
 *
 * This program and the accompanying materials are made available under the
 * terms of the Eclipse Public License v. 2.0 which is available at
 * http://www.eclipse.org/legal/epl-2.0, or the Eclipse Distribution License
 * v. 1.0 which is available at
 * http://www.eclipse.org/org/documents/edl-v10.php.
 *
 * SPDX-License-Identifier: EPL-2.0 OR BSD-3-Clause
 */
#include <stdlib.h>
#include <ctype.h>
#include <assert.h>
#include <string.h>

#include "dds/ddsrt/heap.h"
#include "dds/ddsrt/log.h"
#include "dds/ddsrt/md5.h"
#include "dds/ddsrt/mh3.h"
#include "dds/ddsi/q_bswap.h"
#include "dds/ddsi/q_config.h"
#include "dds/ddsi/q_freelist.h"
#include "dds/ddsi/ddsi_tkmap.h"
#include "dds/ddsi/ddsi_cdrstream.h"
#include "dds/ddsi/q_radmin.h"
#include "dds/ddsi/ddsi_domaingv.h"
#include "dds/ddsi/ddsi_serdata_default.h"
#ifdef DDS_HAS_SHM
#include "dds/ddsi/q_xmsg.h"
#include "dds/ddsi/shm_sync.h"
#include "iceoryx_binding_c/chunk.h"
#endif

#if DDSRT_ENDIAN == DDSRT_LITTLE_ENDIAN
#define NATIVE_ENCODING CDR_LE
#define NATIVE_ENCODING_PL PL_CDR_LE
#elif DDSRT_ENDIAN == DDSRT_BIG_ENDIAN
#define NATIVE_ENCODING CDR_BE
#define NATIVE_ENCODING_PL PL_CDR_BE
#else
#error "DDSRT_ENDIAN neither LITTLE nor BIG"
#endif

/* 8k entries in the freelist seems to be roughly the amount needed to send
   minimum-size (well, 4 bytes) samples as fast as possible over loopback
   while using large messages -- actually, it stands to reason that this would
   be the same as the WHC node pool size */
#define MAX_POOL_SIZE 8192
#define MAX_SIZE_FOR_POOL 256
#define DEFAULT_NEW_SIZE 128
#define CHUNK_SIZE 128

#ifndef NDEBUG
static int ispowerof2_size (size_t x)
{
  return x > 0 && !(x & (x-1));
}
#endif

static size_t alignup_size (size_t x, size_t a);

struct serdatapool * ddsi_serdatapool_new (void)
{
  struct serdatapool * pool;
  pool = ddsrt_malloc (sizeof (*pool));
  nn_freelist_init (&pool->freelist, MAX_POOL_SIZE, offsetof (struct ddsi_serdata_default, next));
  return pool;
}

static void serdata_free_wrap (void *elem)
{
#ifndef NDEBUG
  struct ddsi_serdata_default *d = elem;
  assert(ddsrt_atomic_ld32(&d->c.refc) == 0);
#endif
  dds_free(elem);
}

void ddsi_serdatapool_free (struct serdatapool * pool)
{
  nn_freelist_fini (&pool->freelist, serdata_free_wrap);
  ddsrt_free (pool);
}

static size_t alignup_size (size_t x, size_t a)
{
  size_t m = a-1;
  assert (ispowerof2_size (a));
  return (x+m) & ~m;
}

static void *serdata_default_append (struct ddsi_serdata_default **d, size_t n)
{
  char *p;
  if ((*d)->pos + n > (*d)->size)
  {
    size_t size1 = alignup_size ((*d)->pos + n, CHUNK_SIZE);
    *d = ddsrt_realloc (*d, offsetof (struct ddsi_serdata_default, data) + size1);
    (*d)->size = (uint32_t)size1;
  }
  assert ((*d)->pos + n <= (*d)->size);
  p = (*d)->data + (*d)->pos;
  (*d)->pos += (uint32_t)n;
  return p;
}

static void serdata_default_append_blob (struct ddsi_serdata_default **d, size_t sz, const void *data)
{
  char *p = serdata_default_append (d, sz);
  memcpy (p, data, sz);
}

static struct ddsi_serdata *fix_serdata_default(struct ddsi_serdata_default *d, uint32_t basehash)
{
  if (d->keyhash.m_iskey)
    d->c.hash = ddsrt_mh3 (d->keyhash.m_hash, 16, 0) ^ basehash;
  else
    d->c.hash = *((uint32_t *)d->keyhash.m_hash) ^ basehash;
  return &d->c;
}

static struct ddsi_serdata *fix_serdata_default_nokey(struct ddsi_serdata_default *d, uint32_t basehash)
{
  d->c.hash = basehash;
  return &d->c;
}

static uint32_t serdata_default_get_size(const struct ddsi_serdata *dcmn)
{
  const struct ddsi_serdata_default *d = (const struct ddsi_serdata_default *) dcmn;
  return d->pos + (uint32_t)sizeof (struct CDRHeader);
}

static bool serdata_default_eqkey(const struct ddsi_serdata *acmn, const struct ddsi_serdata *bcmn)
{
  const struct ddsi_serdata_default *a = (const struct ddsi_serdata_default *)acmn;
  const struct ddsi_serdata_default *b = (const struct ddsi_serdata_default *)bcmn;
  assert (a->keyhash.m_set);
#if 0
  char astr[50], bstr[50];
  for (int i = 0; i < 16; i++) {
    sprintf (astr + 3*i, ":%02x", (unsigned char)a->keyhash.m_hash[i]);
  }
  for (int i = 0; i < 16; i++) {
    sprintf (bstr + 3*i, ":%02x", (unsigned char)b->keyhash.m_hash[i]);
  }
  printf("serdata_default_eqkey: %s %s\n", astr+1, bstr+1);
#endif
  return memcmp (a->keyhash.m_hash, b->keyhash.m_hash, 16) == 0;
}

static bool serdata_default_eqkey_nokey (const struct ddsi_serdata *acmn, const struct ddsi_serdata *bcmn)
{
  (void)acmn; (void)bcmn;
  return true;
}

static void serdata_default_free(struct ddsi_serdata *dcmn)
{
  struct ddsi_serdata_default *d = (struct ddsi_serdata_default *)dcmn;
  assert(ddsrt_atomic_ld32(&d->c.refc) == 0);

#ifdef DDS_HAS_SHM
  //ICEORYX_TODO the chunk is released concurrently to iox_sub_take_chunk here,
  //                 we will need mutex protection for this call
  //                 when is the free called exactly?
  if (d->c.iox_chunk)
  {
    iox_sub_t *sub = d->c.iox_subscriber;
    shm_lock_iox_sub(*sub);
    iox_sub_release_chunk(*sub, d->c.iox_chunk);
    d->c.iox_chunk = NULL;
    shm_unlock_iox_sub(*sub);
  }
#endif

  if (d->size > MAX_SIZE_FOR_POOL || !nn_freelist_push (&d->serpool->freelist, d))
    dds_free (d);
}

static void serdata_default_init(struct ddsi_serdata_default *d, const struct ddsi_sertype_default *tp, enum ddsi_serdata_kind kind)
{
  ddsi_serdata_init (&d->c, &tp->c, kind);
  d->pos = 0;
#ifndef NDEBUG
  d->fixed = false;
#endif
  d->hdr.identifier = tp->native_encoding_identifier;
  d->hdr.options = 0;
  memset (d->keyhash.m_hash, 0, sizeof (d->keyhash.m_hash));
  d->keyhash.m_set = 0;
  d->keyhash.m_iskey = 0;
  d->keyhash.m_keysize = 0;
}

static struct ddsi_serdata_default *serdata_default_allocnew (struct serdatapool *serpool, uint32_t init_size)
{
  struct ddsi_serdata_default *d = ddsrt_malloc (offsetof (struct ddsi_serdata_default, data) + init_size);
  d->size = init_size;
  d->serpool = serpool;
  return d;
}

static struct ddsi_serdata_default *serdata_default_new_size (const struct ddsi_sertype_default *tp, enum ddsi_serdata_kind kind, uint32_t size)
{
  struct ddsi_serdata_default *d;
  if (size <= MAX_SIZE_FOR_POOL && (d = nn_freelist_pop (&tp->serpool->freelist)) != NULL)
    ddsrt_atomic_st32 (&d->c.refc, 1);
  else if ((d = serdata_default_allocnew (tp->serpool, size)) == NULL)
    return NULL;
  serdata_default_init (d, tp, kind);
  return d;
}

static struct ddsi_serdata_default *serdata_default_new (const struct ddsi_sertype_default *tp, enum ddsi_serdata_kind kind)
{
  return serdata_default_new_size (tp, kind, DEFAULT_NEW_SIZE);
}

/* Construct a serdata from a fragchain received over the network */
static struct ddsi_serdata_default *serdata_default_from_ser_common (const struct ddsi_sertype *tpcmn, enum ddsi_serdata_kind kind, const struct nn_rdata *fragchain, size_t size)
{
  const struct ddsi_sertype_default *tp = (const struct ddsi_sertype_default *)tpcmn;

  /* FIXME: check whether this really is the correct maximum: offsets are relative
     to the CDR header, but there are also some places that use a serdata as-if it
     were a stream, and those use offsets (m_index) relative to the start of the
     serdata */
  if (size > UINT32_MAX - offsetof (struct ddsi_serdata_default, hdr))
    return NULL;
  struct ddsi_serdata_default *d = serdata_default_new_size (tp, kind, (uint32_t) size);
  if (d == NULL)
    return NULL;

  uint32_t off = 4; /* must skip the CDR header */

  assert (fragchain->min == 0);
  assert (fragchain->maxp1 >= off); /* CDR header must be in first fragment */

  memcpy (&d->hdr, NN_RMSG_PAYLOADOFF (fragchain->rmsg, NN_RDATA_PAYLOAD_OFF (fragchain)), sizeof (d->hdr));
  assert (d->hdr.identifier == CDR_LE || d->hdr.identifier == CDR_BE);

  while (fragchain)
  {
    assert (fragchain->min <= off);
    assert (fragchain->maxp1 <= size);
    if (fragchain->maxp1 > off)
    {
      /* only copy if this fragment adds data */
      const unsigned char *payload = NN_RMSG_PAYLOADOFF (fragchain->rmsg, NN_RDATA_PAYLOAD_OFF (fragchain));
      serdata_default_append_blob (&d, fragchain->maxp1 - off, payload + off - fragchain->min);
      off = fragchain->maxp1;
    }
    fragchain = fragchain->nextfrag;
  }

  const bool needs_bswap = (d->hdr.identifier != NATIVE_ENCODING);
  d->hdr.identifier = NATIVE_ENCODING;
  const uint32_t pad = ddsrt_fromBE2u (d->hdr.options) & 2;
  if (d->pos < pad)
  {
    ddsi_serdata_unref (&d->c);
    return NULL;
  }
  else if (!dds_stream_normalize (d->data, d->pos - pad, needs_bswap, tp, kind == SDK_KEY))
  {
    ddsi_serdata_unref (&d->c);
    return NULL;
  }
  else
  {
    dds_istream_t is;
    dds_istream_from_serdata_default (&is, d);
    dds_stream_extract_keyhash (&is, &d->keyhash, tp, kind == SDK_KEY);
    return d;
  }
}

static struct ddsi_serdata_default *serdata_default_from_ser_iov_common (const struct ddsi_sertype *tpcmn, enum ddsi_serdata_kind kind, ddsrt_msg_iovlen_t niov, const ddsrt_iovec_t *iov, size_t size)
{
  const struct ddsi_sertype_default *tp = (const struct ddsi_sertype_default *)tpcmn;

  /* FIXME: check whether this really is the correct maximum: offsets are relative
     to the CDR header, but there are also some places that use a serdata as-if it
     were a stream, and those use offsets (m_index) relative to the start of the
     serdata */
  if (size > UINT32_MAX - offsetof (struct ddsi_serdata_default, hdr))
    return NULL;
  assert (niov >= 1);
  if (iov[0].iov_len < 4) /* CDR header */
    return NULL;
  struct ddsi_serdata_default *d = serdata_default_new_size (tp, kind, (uint32_t) size);
  if (d == NULL)
    return NULL;

  memcpy (&d->hdr, iov[0].iov_base, sizeof (d->hdr));
  assert (d->hdr.identifier == CDR_LE || d->hdr.identifier == CDR_BE);
  serdata_default_append_blob (&d, iov[0].iov_len - 4, (const char *) iov[0].iov_base + 4);
  for (ddsrt_msg_iovlen_t i = 1; i < niov; i++)
    serdata_default_append_blob (&d, iov[i].iov_len, iov[i].iov_base);

  const bool needs_bswap = (d->hdr.identifier != NATIVE_ENCODING);
  d->hdr.identifier = NATIVE_ENCODING;
  const uint32_t pad = ddsrt_fromBE2u (d->hdr.options) & 2;
  if (d->pos < pad)
  {
    ddsi_serdata_unref (&d->c);
    return NULL;
  }
  else if (!dds_stream_normalize (d->data, d->pos - pad, needs_bswap, tp, kind == SDK_KEY))
  {
    ddsi_serdata_unref (&d->c);
    return NULL;
  }
  else
  {
    dds_istream_t is;
    dds_istream_from_serdata_default (&is, d);
    dds_stream_extract_keyhash (&is, &d->keyhash, tp, kind == SDK_KEY);
    return d;
  }
}

static struct ddsi_serdata *serdata_default_from_ser (const struct ddsi_sertype *tpcmn, enum ddsi_serdata_kind kind, const struct nn_rdata *fragchain, size_t size)
{
  struct ddsi_serdata_default *d;
  if ((d = serdata_default_from_ser_common (tpcmn, kind, fragchain, size)) == NULL)
    return NULL;
  return fix_serdata_default (d, tpcmn->serdata_basehash);
}

static struct ddsi_serdata *serdata_default_from_ser_iov (const struct ddsi_sertype *tpcmn, enum ddsi_serdata_kind kind, ddsrt_msg_iovlen_t niov, const ddsrt_iovec_t *iov, size_t size)
{
  struct ddsi_serdata_default *d;
  if ((d = serdata_default_from_ser_iov_common (tpcmn, kind, niov, iov, size)) == NULL)
    return NULL;
  return fix_serdata_default (d, tpcmn->serdata_basehash);
}

static struct ddsi_serdata *serdata_default_from_ser_nokey (const struct ddsi_sertype *tpcmn, enum ddsi_serdata_kind kind, const struct nn_rdata *fragchain, size_t size)
{
  struct ddsi_serdata_default *d;
  if ((d = serdata_default_from_ser_common (tpcmn, kind, fragchain, size)) == NULL)
    return NULL;
  return fix_serdata_default_nokey (d, tpcmn->serdata_basehash);
}

static struct ddsi_serdata *serdata_default_from_ser_iov_nokey (const struct ddsi_sertype *tpcmn, enum ddsi_serdata_kind kind, ddsrt_msg_iovlen_t niov, const ddsrt_iovec_t *iov, size_t size)
{
  struct ddsi_serdata_default *d;
  if ((d = serdata_default_from_ser_iov_common (tpcmn, kind, niov, iov, size)) == NULL)
    return NULL;
  return fix_serdata_default_nokey (d, tpcmn->serdata_basehash);
}

static struct ddsi_serdata *ddsi_serdata_from_keyhash_cdr (const struct ddsi_sertype *tpcmn, const ddsi_keyhash_t *keyhash)
{
  /* FIXME: not quite sure this is correct, though a check against a specially hacked OpenSplice suggests it is */
  const struct ddsi_sertype_default *tp = (const struct ddsi_sertype_default *)tpcmn;
  if (!(tp->type.flagset & DDS_TOPIC_FIXED_KEY))
  {
    /* keyhash is MD5 of a key value, so impossible to turn into a key value */
    return NULL;
  }
  else
  {
    struct ddsi_serdata_default *d = serdata_default_new(tp, SDK_KEY);
    if (d == NULL)
      return NULL;
    serdata_default_append_blob (&d, sizeof (keyhash->value), keyhash->value);
    DDSRT_WARNING_MSVC_OFF(6326)
    if (!dds_stream_normalize (d->data, d->pos, (NATIVE_ENCODING != CDR_BE), tp, true))
    {
      ddsi_serdata_unref (&d->c);
      return NULL;
    }
    DDSRT_WARNING_MSVC_ON(6326)
    memcpy (d->keyhash.m_hash, keyhash->value, sizeof (d->keyhash.m_hash));
    d->keyhash.m_set = 1;
    d->keyhash.m_iskey = 1;
    d->keyhash.m_keysize = sizeof (d->keyhash.m_hash);
    return fix_serdata_default(d, tp->c.serdata_basehash);
  }
}

static struct ddsi_serdata *ddsi_serdata_from_keyhash_cdr_nokey (const struct ddsi_sertype *tpcmn, const ddsi_keyhash_t *keyhash)
{
  const struct ddsi_sertype_default *tp = (const struct ddsi_sertype_default *)tpcmn;
  struct ddsi_serdata_default *d = serdata_default_new(tp, SDK_KEY);
  if (d == NULL)
    return NULL;
  (void)keyhash;
  d->keyhash.m_set = 1;
  d->keyhash.m_iskey = 1;
  d->keyhash.m_keysize = 0;
  return fix_serdata_default_nokey(d, tp->c.serdata_basehash);
}

static void gen_keyhash_from_sample (const struct ddsi_sertype_default *type, dds_keyhash_t *kh, const char *sample)
{
  const struct ddsi_sertype_default_desc *desc = &type->type;
  kh->m_set = 1;
  if (desc->keys.nkeys == 0)
  {
    kh->m_iskey = 1;
    kh->m_keysize = 0;
  }
  else if (desc->flagset & DDS_TOPIC_FIXED_KEY)
  {
    dds_ostreamBE_t os;
    kh->m_iskey = 1;
    kh->m_keysize = sizeof(kh->m_hash);
    dds_ostreamBE_init (&os, 0);
    os.x.m_buffer = kh->m_hash;
    os.x.m_size = 16;
    dds_stream_write_keyBE (&os, sample, type);
  }
  else
  {
    dds_ostreamBE_t os;
    ddsrt_md5_state_t md5st;
    kh->m_iskey = 0;
    kh->m_keysize = sizeof(kh->m_hash);
    dds_ostreamBE_init (&os, 64);
    dds_stream_write_keyBE (&os, sample, type);
    ddsrt_md5_init (&md5st);
    ddsrt_md5_append (&md5st, os.x.m_buffer, os.x.m_index);
    ddsrt_md5_finish (&md5st, kh->m_hash);
    dds_ostreamBE_fini (&os);
  }
}

#ifdef DDS_HAS_SHM
static struct ddsi_serdata* serdata_default_from_received_iox_buffer(const struct ddsi_sertype* tpcmn, enum ddsi_serdata_kind kind, void* sub, void* iox_buffer)
{   
  iox_chunk_header_t* chunk_header = iox_chunk_header_from_user_payload(iox_buffer);
  const iceoryx_header_t* ice_hdr = iox_chunk_header_to_user_header(chunk_header);

  const struct ddsi_sertype_default* tp = (const struct ddsi_sertype_default*)tpcmn;

  struct ddsi_serdata_default* d = serdata_default_new_size(tp, kind, ice_hdr->data_size);

  //ICEORYX_TODO: we do no copy here but store the pointer to the chunk
  //              the pointer was gotten in some concurrent thread of the shm_monitor
  //              problems may arise due to concurrent iox_release_chunk
  d->c.iox_chunk = iox_buffer;
  d->c.iox_subscriber = sub;
  memcpy(d->keyhash.m_hash, ice_hdr->keyhash.value, 16);
  d->keyhash.m_set = 1;
  d->keyhash.m_iskey = 0;  //if set to 1, will cause issues @ serdata_default_to_untyped at the endianness swap
  d->keyhash.m_keysize = 16;
  fix_serdata_default(d, tpcmn->serdata_basehash);

  return (struct ddsi_serdata*)d;
}

// Creates a serdata for the case where only iceoryx is required (i.e. no network).
// This skips expensive serialization and just takes ownership of the iceoryx buffer.
// Computing the keyhash is currently still required.
static struct ddsi_serdata *ddsi_serdata_default_from_loaned_sample (const struct ddsi_sertype *type, enum ddsi_serdata_kind kind, const char *sample) {
  const struct ddsi_sertype_default *t = (const struct ddsi_sertype_default *)type;
  struct ddsi_serdata_default *d = serdata_default_new(t, kind);

  if(d == NULL) 
    return NULL;

  // Currently needed even in the shared memory case (since it is potentially used at the reader side).
  // This may still incur computational costs linear in the sample size (?).
  // TODO: Can we avoid this with specific handling on the reader side which does not require the keyhash?
  gen_keyhash_from_sample (t, &d->keyhash, sample);

  struct ddsi_serdata *serdata = &d->c;
  serdata->iox_chunk = (void*) sample;
  return serdata; 
}

static struct ddsi_serdata* serdata_default_from_iox(const struct ddsi_sertype* tpcmn, enum ddsi_serdata_kind kind, void* sub, void* buffer)
{
  if (sub == NULL)
    return ddsi_serdata_default_from_loaned_sample(tpcmn, kind, buffer);
  else
    return serdata_default_from_received_iox_buffer(tpcmn, kind, sub, buffer);
}
#endif

static struct ddsi_serdata_default *serdata_default_from_sample_cdr_common (const struct ddsi_sertype *tpcmn, enum ddsi_serdata_kind kind, const void *sample)
{
  const struct ddsi_sertype_default *tp = (const struct ddsi_sertype_default *)tpcmn;
  struct ddsi_serdata_default *d = serdata_default_new(tp, kind);
  if (d == NULL)
    return NULL;
  dds_ostream_t os;
  gen_keyhash_from_sample (tp, &d->keyhash, sample);
  dds_ostream_from_serdata_default (&os, d);
  switch (kind)
  {
    case SDK_EMPTY:
      break;
    case SDK_KEY:
      dds_stream_write_key (&os, sample, tp);
      break;
    case SDK_DATA:
      dds_stream_write_sample (&os, sample, tp);
      break;
  }
  dds_ostream_add_to_serdata_default (&os, &d);
  return d;
}

static struct ddsi_serdata *serdata_default_from_sample_cdr (const struct ddsi_sertype *tpcmn, enum ddsi_serdata_kind kind, const void *sample)
{
  struct ddsi_serdata_default *d;
  if ((d = serdata_default_from_sample_cdr_common (tpcmn, kind, sample)) == NULL)
    return NULL;
  return fix_serdata_default (d, tpcmn->serdata_basehash);
}

static struct ddsi_serdata *serdata_default_from_sample_cdr_nokey (const struct ddsi_sertype *tpcmn, enum ddsi_serdata_kind kind, const void *sample)
{
  struct ddsi_serdata_default *d;
  if ((d = serdata_default_from_sample_cdr_common (tpcmn, kind, sample)) == NULL)
    return NULL;
  return fix_serdata_default_nokey (d, tpcmn->serdata_basehash);
}

static struct ddsi_serdata *serdata_default_to_untyped (const struct ddsi_serdata *serdata_common)
{
  const struct ddsi_serdata_default *d = (const struct ddsi_serdata_default *)serdata_common;
  const struct ddsi_sertype_default *tp = (const struct ddsi_sertype_default *)d->c.type;
  assert (d->hdr.identifier == NATIVE_ENCODING || d->hdr.identifier == NATIVE_ENCODING_PL);
  struct ddsi_serdata_default *d_tl = serdata_default_new(tp, SDK_KEY);
  if (d_tl == NULL)
    return NULL;
  d_tl->c.type = NULL;
  d_tl->c.hash = d->c.hash;
  d_tl->c.timestamp.v = INT64_MIN;
  d_tl->keyhash = d->keyhash;
  /* These things are used for the key-to-instance map and only subject to eq, free and conversion to an invalid
     sample of some type for topics that can end up in a RHC, so, of the four kinds we have, only for CDR-with-key
     the payload is of interest. */
  if (d->c.ops == &ddsi_serdata_ops_cdr)
  {
    assert (d->hdr.identifier == NATIVE_ENCODING);
    if (d->c.kind == SDK_KEY)
      serdata_default_append_blob (&d_tl, d->pos, d->data);
    else if (d->keyhash.m_iskey)
    {
      serdata_default_append_blob (&d_tl, sizeof (d->keyhash.m_hash), d->keyhash.m_hash);
#if NATIVE_ENCODING != CDR_BE
      bool ok = dds_stream_normalize (d_tl->data, d_tl->pos, true, tp, true);
      assert (ok);
      (void) ok;
#endif
    }
    else
    {
      dds_istream_t is;
      dds_ostream_t os;
      dds_istream_from_serdata_default (&is, d);
      dds_ostream_from_serdata_default (&os, d_tl);
      dds_stream_extract_key_from_data (&is, &os, tp);
      if (os.m_index < os.m_size)
      {
        os.m_buffer = dds_realloc (os.m_buffer, os.m_index);
        os.m_size = os.m_index;
      }
      dds_ostream_add_to_serdata_default (&os, &d_tl);
    }
  }
  return (struct ddsi_serdata *)d_tl;
}

/* Fill buffer with 'size' bytes of serialised data, starting from 'off'; 0 <= off < off+sz <= alignup4(size(d)) */
static void serdata_default_to_ser (const struct ddsi_serdata *serdata_common, size_t off, size_t sz, void *buf)
{
  const struct ddsi_serdata_default *d = (const struct ddsi_serdata_default *)serdata_common;
  assert (off < d->pos + sizeof(struct CDRHeader));
  assert (sz <= alignup_size (d->pos + sizeof(struct CDRHeader), 4) - off);
  memcpy (buf, (char *)&d->hdr + off, sz);
}

static struct ddsi_serdata *serdata_default_to_ser_ref (const struct ddsi_serdata *serdata_common, size_t off, size_t sz, ddsrt_iovec_t *ref)
{
  const struct ddsi_serdata_default *d = (const struct ddsi_serdata_default *)serdata_common;
  assert (off < d->pos + sizeof(struct CDRHeader));
  assert (sz <= alignup_size (d->pos + sizeof(struct CDRHeader), 4) - off);
  ref->iov_base = (char *)&d->hdr + off;
  ref->iov_len = (ddsrt_iov_len_t)sz;
  return ddsi_serdata_ref(serdata_common);
}

static void serdata_default_to_ser_unref (struct ddsi_serdata *serdata_common, const ddsrt_iovec_t *ref)
{
  (void)ref;
  ddsi_serdata_unref(serdata_common);
}

static bool serdata_default_to_sample_cdr (const struct ddsi_serdata *serdata_common, void *sample, void **bufptr, void *buflim)
{
  const struct ddsi_serdata_default *d = (const struct ddsi_serdata_default *)serdata_common;
  const struct ddsi_sertype_default *tp = (const struct ddsi_sertype_default *) d->c.type;
#ifdef DDS_HAS_SHM
  if (d->c.iox_chunk)
  {
    void* user_payload = d->c.iox_chunk;
    iox_chunk_header_t* chunk_header = iox_chunk_header_from_user_payload(user_payload);
    iceoryx_header_t* hdr = iox_chunk_header_to_user_header(chunk_header);
    memcpy(sample, user_payload, hdr->data_size);
    return true;
  }
#endif
  dds_istream_t is;
  if (bufptr) abort(); else { (void)buflim; } /* FIXME: haven't implemented that bit yet! */
  assert (d->hdr.identifier == NATIVE_ENCODING);
  dds_istream_from_serdata_default(&is, d);
  if (d->c.kind == SDK_KEY)
    dds_stream_read_key (&is, sample, tp);
  else
    dds_stream_read_sample (&is, sample, tp);
  return true; /* FIXME: can't conversion to sample fail? */
}

static bool serdata_default_untyped_to_sample_cdr (const struct ddsi_sertype *sertype_common, const struct ddsi_serdata *serdata_common, void *sample, void **bufptr, void *buflim)
{
  const struct ddsi_serdata_default *d = (const struct ddsi_serdata_default *)serdata_common;
  const struct ddsi_sertype_default *tp = (const struct ddsi_sertype_default *) sertype_common;
  dds_istream_t is;
  assert (d->c.type == NULL);
  assert (d->c.kind == SDK_KEY);
  assert (d->c.ops == sertype_common->serdata_ops);
  assert (d->hdr.identifier == NATIVE_ENCODING);
  if (bufptr) abort(); else { (void)buflim; } /* FIXME: haven't implemented that bit yet! */
  dds_istream_from_serdata_default(&is, d);
  dds_stream_read_key (&is, sample, tp);
  return true; /* FIXME: can't conversion to sample fail? */
}

static bool serdata_default_untyped_to_sample_cdr_nokey (const struct ddsi_sertype *sertype_common, const struct ddsi_serdata *serdata_common, void *sample, void **bufptr, void *buflim)
{
  (void)sertype_common; (void)sample; (void)bufptr; (void)buflim; (void)serdata_common;
  assert (serdata_common->type == NULL);
  assert (serdata_common->kind == SDK_KEY);
  return true;
}

static size_t serdata_default_print_cdr (const struct ddsi_sertype *sertype_common, const struct ddsi_serdata *serdata_common, char *buf, size_t size)
{
  const struct ddsi_serdata_default *d = (const struct ddsi_serdata_default *)serdata_common;
  const struct ddsi_sertype_default *tp = (const struct ddsi_sertype_default *)sertype_common;
  dds_istream_t is;
  dds_istream_from_serdata_default (&is, d);
  if (d->c.kind == SDK_KEY)
    return dds_stream_print_key (&is, tp, buf, size);
  else
    return dds_stream_print_sample (&is, tp, buf, size);
}

static void serdata_default_get_keyhash (const struct ddsi_serdata *serdata_common, struct ddsi_keyhash *buf, bool force_md5)
{
  const struct ddsi_serdata_default *d = (const struct ddsi_serdata_default *)serdata_common;
  assert(buf);
  assert(d->keyhash.m_set);
  if (force_md5 && d->keyhash.m_iskey /* m_iskey == !md5 */)
  {
    ddsrt_md5_state_t md5st;
    ddsrt_md5_init  (&md5st);
    ddsrt_md5_append(&md5st, (ddsrt_md5_byte_t*)(d->keyhash.m_hash), d->keyhash.m_keysize);
    ddsrt_md5_finish(&md5st, (ddsrt_md5_byte_t*)(buf->value));
  }
  else
  {
    memcpy (buf->value, d->keyhash.m_hash, 16);
  }
}

const struct ddsi_serdata_ops ddsi_serdata_ops_cdr = {
  .get_size = serdata_default_get_size,
  .eqkey = serdata_default_eqkey,
  .free = serdata_default_free,
  .from_ser = serdata_default_from_ser,
  .from_ser_iov = serdata_default_from_ser_iov,
  .from_keyhash = ddsi_serdata_from_keyhash_cdr,
  .from_sample = serdata_default_from_sample_cdr,
  .to_ser = serdata_default_to_ser,
  .to_sample = serdata_default_to_sample_cdr,
  .to_ser_ref = serdata_default_to_ser_ref,
  .to_ser_unref = serdata_default_to_ser_unref,
  .to_untyped = serdata_default_to_untyped,
  .untyped_to_sample = serdata_default_untyped_to_sample_cdr,
  .print = serdata_default_print_cdr,
  .get_keyhash = serdata_default_get_keyhash
#ifdef DDS_HAS_SHM
  , .get_sample_size = ddsi_serdata_iox_size
  , .from_iox_buffer = serdata_default_from_iox
#endif
};

const struct ddsi_serdata_ops ddsi_serdata_ops_cdr_nokey = {
  .get_size = serdata_default_get_size,
  .eqkey = serdata_default_eqkey_nokey,
  .free = serdata_default_free,
  .from_ser = serdata_default_from_ser_nokey,
  .from_ser_iov = serdata_default_from_ser_iov_nokey,
  .from_keyhash = ddsi_serdata_from_keyhash_cdr_nokey,
  .from_sample = serdata_default_from_sample_cdr_nokey,
  .to_ser = serdata_default_to_ser,
  .to_sample = serdata_default_to_sample_cdr,
  .to_ser_ref = serdata_default_to_ser_ref,
  .to_ser_unref = serdata_default_to_ser_unref,
  .to_untyped = serdata_default_to_untyped,
  .untyped_to_sample = serdata_default_untyped_to_sample_cdr_nokey,
  .print = serdata_default_print_cdr,
  .get_keyhash = serdata_default_get_keyhash
#ifdef DDS_HAS_SHM
  , .get_sample_size = ddsi_serdata_iox_size
  , .from_iox_buffer = serdata_default_from_iox
#endif
};
