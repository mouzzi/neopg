/* global.c  -	global control functions
 * Copyright (C) 1998, 1999, 2000, 2001, 2002, 2003
 *               2004, 2005, 2006, 2008, 2011,
 *               2012  Free Software Foundation, Inc.
 * Copyright (C) 2013, 2014, 2017 g10 Code GmbH
 *
 * This file is part of Libgcrypt.
 *
 * Libgcrypt is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser general Public License as
 * published by the Free Software Foundation; either version 2.1 of
 * the License, or (at your option) any later version.
 *
 * Libgcrypt is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include <config.h>

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#ifdef HAVE_SYSLOG
#include <syslog.h>
#endif /*HAVE_SYSLOG*/

#include "cipher.h"
#include "g10lib.h"
#include "gcrypt-testapi.h"
#include "secmem.h" /* our own secmem allocator */
#include "stdmem.h" /* our own memory allocator */

/****************
 * flag bits: 0 : general cipher debug
 *	      1 : general MPI debug
 */
static unsigned int debug_flags;

/* Controlled by global_init().  */
static int any_init_done;

/* Memory management. */

static gcry_handler_no_mem_t outofcore_handler;
static void *outofcore_handler_value;
static int no_secure_memory;

/* This is our handmade constructor.  It gets called by any function
   likely to be called at startup.  */
static void global_init(void) {
  gpg_error_t err = 0;

  if (any_init_done) return;
  any_init_done = 1;

  /* Initialize the modules - this is mainly allocating some memory and
     creating mutexes.  */
  err = _gcry_primegen_init();
  if (err) goto fail;
  err = _gcry_secmem_module_init();
  if (err) goto fail;
  err = _gcry_mpi_init();
  if (err) goto fail;

  return;

fail:
  BUG();
}

/* Version number parsing.  */

/* This function parses the first portion of the version number S and
   stores it in *NUMBER.  On success, this function returns a pointer
   into S starting with the first character, which is not part of the
   initial number portion; on failure, NULL is returned.  */
static const char *parse_version_number(const char *s, int *number) {
  int val = 0;

  if (*s == '0' && isdigit(s[1]))
    return NULL; /* leading zeros are not allowed */
  for (; isdigit(*s); s++) {
    val *= 10;
    val += *s - '0';
  }
  *number = val;
  return val < 0 ? NULL : s;
}

/* This function breaks up the complete string-representation of the
   version number S, which is of the following struture: <major
   number>.<minor number>.<micro number><patch level>.  The major,
   minor and micro number components will be stored in *MAJOR, *MINOR
   and *MICRO.

   On success, the last component, the patch level, will be returned;
   in failure, NULL will be returned.  */

static const char *parse_version_string(const char *s, int *major, int *minor,
                                        int *micro) {
  s = parse_version_number(s, major);
  if (!s || *s != '.') return NULL;
  s++;
  s = parse_version_number(s, minor);
  if (!s || *s != '.') return NULL;
  s++;
  s = parse_version_number(s, micro);
  if (!s) return NULL;
  return s; /* patchlevel */
}

static void print_config(const char *what, gpgrt_stream_t fp) {
  int i;
  const char *s;

  if (!what || !strcmp(what, "version")) {
    gpgrt_fprintf(fp, "version:%s:%x:%s:%x:\n", "", 0, "", 0);
  }
  if (!what || !strcmp(what, "cc")) {
    gpgrt_fprintf(fp, "cc:%d:%s:\n", GPGRT_GCC_VERSION,
#ifdef __clang__
                  "clang:" __VERSION__
#elif __GNUC__
                  "gcc:" __VERSION__
#else
                  ":"
#endif
                  );
  }

  if (!what || !strcmp(what, "ciphers"))
    gpgrt_fprintf(fp, "ciphers:%s:\n", LIBGCRYPT_CIPHERS);
  if (!what || !strcmp(what, "pubkeys"))
    gpgrt_fprintf(fp, "pubkeys:%s:\n", LIBGCRYPT_PUBKEY_CIPHERS);
  if (!what || !strcmp(what, "digests"))
    gpgrt_fprintf(fp, "digests:%s:\n", LIBGCRYPT_DIGESTS);

  if (!what || !strcmp(what, "cpu-arch")) {
    gpgrt_fprintf(fp,
                  "cpu-arch:"
#if defined(HAVE_CPU_ARCH_X86)
                  "x86"
#elif defined(HAVE_CPU_ARCH_ALPHA)
                  "alpha"
#elif defined(HAVE_CPU_ARCH_SPARC)
                  "sparc"
#elif defined(HAVE_CPU_ARCH_MIPS)
                  "mips"
#elif defined(HAVE_CPU_ARCH_M68K)
                  "m68k"
#elif defined(HAVE_CPU_ARCH_PPC)
                  "ppc"
#elif defined(HAVE_CPU_ARCH_ARM)
                  "arm"
#endif
                  ":\n");
  }

  if (!what || !strcmp(what, "mpi-asm"))
    gpgrt_fprintf(fp, "mpi-asm:%s:\n", _gcry_mpi_get_hw_config());
}

/* Command dispatcher function, acting as general control
   function.  */
gpg_error_t _gcry_vcontrol(enum gcry_ctl_cmds cmd, va_list arg_ptr) {
  static int init_finished = 0;
  gpg_error_t rc = 0;

  switch (cmd) {
    case GCRYCTL_ENABLE_M_GUARD:
      _gcry_private_enable_m_guard();
      break;

    case GCRYCTL_DUMP_MEMORY_STATS:
      /*m_print_stats("[fixme: prefix]");*/
      break;

    case GCRYCTL_DUMP_SECMEM_STATS:
      _gcry_secmem_dump_stats();
      break;

    case GCRYCTL_DROP_PRIVS:
      global_init();
      _gcry_secmem_init(0);
      break;

    case GCRYCTL_DISABLE_SECMEM:
      global_init();
      no_secure_memory = 1;
      break;

    case GCRYCTL_INIT_SECMEM:
      global_init();
      _gcry_secmem_init(va_arg(arg_ptr, unsigned int));
      if ((_gcry_secmem_get_flags() & GCRY_SECMEM_FLAG_NOT_LOCKED))
        rc = GPG_ERR_GENERAL;
      break;

    case GCRYCTL_TERM_SECMEM:
      global_init();
      _gcry_secmem_term();
      break;

    case GCRYCTL_DISABLE_SECMEM_WARN:
      _gcry_secmem_set_flags(
          (_gcry_secmem_get_flags() | GCRY_SECMEM_FLAG_NO_WARNING));
      break;

    case GCRYCTL_SUSPEND_SECMEM_WARN:
      _gcry_secmem_set_flags(
          (_gcry_secmem_get_flags() | GCRY_SECMEM_FLAG_SUSPEND_WARNING));
      break;

    case GCRYCTL_RESUME_SECMEM_WARN:
      _gcry_secmem_set_flags(
          (_gcry_secmem_get_flags() & ~GCRY_SECMEM_FLAG_SUSPEND_WARNING));
      break;

    case GCRYCTL_SET_VERBOSITY:
      _gcry_set_log_verbosity(va_arg(arg_ptr, int));
      break;

    case GCRYCTL_SET_DEBUG_FLAGS:
      debug_flags |= va_arg(arg_ptr, unsigned int);
      break;

    case GCRYCTL_CLEAR_DEBUG_FLAGS:
      debug_flags &= ~va_arg(arg_ptr, unsigned int);
      break;

    case GCRYCTL_DISABLE_INTERNAL_LOCKING:
      /* Not used anymore.  */
      global_init();
      break;

    case GCRYCTL_ANY_INITIALIZATION_P:
      if (any_init_done) rc = GPG_ERR_GENERAL;
      break;

    case GCRYCTL_INITIALIZATION_FINISHED_P:
      if (init_finished) rc = GPG_ERR_GENERAL; /* Yes.  */
      break;

    case GCRYCTL_INITIALIZATION_FINISHED:
      /* This is a hook which should be used by an application after
         all initialization has been done and right before any threads
         are started.  It is not really needed but the only way to be
         really sure that all initialization for thread-safety has
         been done. */
      if (!init_finished) {
        global_init();
        init_finished = 1;
      }
      break;

    case GCRYCTL_SET_THREAD_CBS:
      /* This is now a dummy call.  We used to install our own thread
         library here. */
      global_init();
      break;

    case GCRYCTL_DISABLE_LOCKED_SECMEM:
      _gcry_secmem_set_flags(
          (_gcry_secmem_get_flags() | GCRY_SECMEM_FLAG_NO_MLOCK));
      break;

    case GCRYCTL_DISABLE_PRIV_DROP:
      _gcry_secmem_set_flags(
          (_gcry_secmem_get_flags() | GCRY_SECMEM_FLAG_NO_PRIV_DROP));
      break;

    default:
      rc = GPG_ERR_INV_OP;
  }

  return rc;
}

/****************
 * Set an optional handler which is called in case the xmalloc functions
 * ran out of memory.  This handler may do one of these things:
 *   o free some memory and return true, so that the xmalloc function
 *     tries again.
 *   o Do whatever it like and return false, so that the xmalloc functions
 *     use the default fatal error handler.
 *   o Terminate the program and don't return.
 *
 * The handler function is called with 3 arguments:  The opaque value set with
 * this function, the requested memory size, and a flag with these bits
 * currently defined:
 *	bit 0 set = secure memory has been requested.
 */
void _gcry_set_outofcore_handler(int (*f)(void *, size_t, unsigned int),
                                 void *value) {
  global_init();

  outofcore_handler = f;
  outofcore_handler_value = value;
}

/* Return the no_secure_memory flag.  */
static int get_no_secure_memory(void) {
  if (!no_secure_memory) return 0;
  return no_secure_memory;
}

static gpg_error_t do_malloc(size_t n, unsigned int flags, void **mem) {
  gpg_error_t err = 0;
  void *m;

  if ((flags & GCRY_ALLOC_FLAG_SECURE) && !get_no_secure_memory()) {
    m = _gcry_private_malloc_secure(n, !!(flags & GCRY_ALLOC_FLAG_XHINT));
  } else {
    m = _gcry_private_malloc(n);
  }

  if (!m) {
    /* Make sure that ERRNO has been set in case a user supplied
       memory handler didn't it correctly. */
    if (!errno) gpg_err_set_errno(ENOMEM);
    err = gpg_error_from_errno(errno);
  } else
    *mem = m;

  return err;
}

void *_gcry_malloc(size_t n) {
  void *mem = NULL;

  do_malloc(n, 0, &mem);

  return mem;
}

static void *_gcry_malloc_secure_core(size_t n, int xhint) {
  void *mem = NULL;

  do_malloc(n, (GCRY_ALLOC_FLAG_SECURE | (xhint ? GCRY_ALLOC_FLAG_XHINT : 0)),
            &mem);

  return mem;
}

void *_gcry_malloc_secure(size_t n) { return _gcry_malloc_secure_core(n, 0); }

int _gcry_is_secure(const void *a) {
  if (get_no_secure_memory()) return 0;
  return _gcry_private_is_secure(a);
}

static void *_gcry_realloc_core(void *a, size_t n, int xhint) {
  void *p;

  /* To avoid problems with non-standard realloc implementations and
     our own secmem_realloc, we divert to malloc and free here.  */
  if (!a) return _gcry_malloc(n);
  if (!n) {
    xfree(a);
    return NULL;
  }

  p = _gcry_private_realloc(a, n, xhint);
  if (!p && !errno) gpg_err_set_errno(ENOMEM);
  return p;
}

void *_gcry_realloc(void *a, size_t n) { return _gcry_realloc_core(a, n, 0); }

void _gcry_free(void *p) {
  int save_errno;

  if (!p) return;

  /* In case ERRNO is set we better save it so that the free machinery
     may not accidentally change ERRNO.  We restore it only if it was
     already set to comply with the usual C semantic for ERRNO.  */
  save_errno = errno;
  _gcry_private_free(p);

  if (save_errno) gpg_err_set_errno(save_errno);
}

void *_gcry_calloc(size_t n, size_t m) {
  size_t bytes;
  void *p;

  bytes = n * m; /* size_t is unsigned so the behavior on overflow is
                    defined. */
  if (m && bytes / m != n) {
    gpg_err_set_errno(ENOMEM);
    return NULL;
  }

  p = _gcry_malloc(bytes);
  if (p) memset(p, 0, bytes);
  return p;
}

void *_gcry_calloc_secure(size_t n, size_t m) {
  size_t bytes;
  void *p;

  bytes = n * m; /* size_t is unsigned so the behavior on overflow is
                    defined. */
  if (m && bytes / m != n) {
    gpg_err_set_errno(ENOMEM);
    return NULL;
  }

  p = _gcry_malloc_secure(bytes);
  if (p) memset(p, 0, bytes);
  return p;
}

static char *_gcry_strdup_core(const char *string, int xhint) {
  char *string_cp = NULL;
  size_t string_n = 0;

  string_n = strlen(string);

  if (_gcry_is_secure(string))
    string_cp = (char *)_gcry_malloc_secure_core(string_n + 1, xhint);
  else
    string_cp = (char *)_gcry_malloc(string_n + 1);

  if (string_cp) strcpy(string_cp, string);

  return string_cp;
}

/* Create and return a copy of the null-terminated string STRING.  If
 * it is contained in secure memory, the copy will be contained in
 * secure memory as well.  In an out-of-memory condition, NULL is
 * returned.  */
char *_gcry_strdup(const char *string) { return _gcry_strdup_core(string, 0); }

void *_gcry_xmalloc(size_t n) {
  void *p;

  while (!(p = _gcry_malloc(n))) {
    if (!outofcore_handler ||
        !outofcore_handler(outofcore_handler_value, n, 0)) {
      _gcry_fatal_error(gpg_error_from_errno(errno), NULL);
    }
  }
  return p;
}

void *_gcry_xrealloc(void *a, size_t n) {
  void *p;

  while (!(p = _gcry_realloc_core(a, n, 1))) {
    if (!outofcore_handler ||
        !outofcore_handler(outofcore_handler_value, n,
                           _gcry_is_secure(a) ? 3 : 2)) {
      _gcry_fatal_error(gpg_error_from_errno(errno), NULL);
    }
  }
  return p;
}

void *_gcry_xmalloc_secure(size_t n) {
  void *p;

  while (!(p = _gcry_malloc_secure_core(n, 1))) {
    if (!outofcore_handler ||
        !outofcore_handler(outofcore_handler_value, n, 1)) {
      _gcry_fatal_error(gpg_error_from_errno(errno),
                        "out of core in secure memory");
    }
  }
  return p;
}

void *_gcry_xcalloc(size_t n, size_t m) {
  size_t nbytes;
  void *p;

  nbytes = n * m;
  if (m && nbytes / m != n) {
    gpg_err_set_errno(ENOMEM);
    _gcry_fatal_error(gpg_error_from_errno(errno), NULL);
  }

  p = _gcry_xmalloc(nbytes);
  memset(p, 0, nbytes);
  return p;
}

void *_gcry_xcalloc_secure(size_t n, size_t m) {
  size_t nbytes;
  void *p;

  nbytes = n * m;
  if (m && nbytes / m != n) {
    gpg_err_set_errno(ENOMEM);
    _gcry_fatal_error(gpg_error_from_errno(errno), NULL);
  }

  p = _gcry_xmalloc_secure(nbytes);
  memset(p, 0, nbytes);
  return p;
}

char *_gcry_xstrdup(const char *string) {
  char *p;

  while (!(p = _gcry_strdup_core(string, 1))) {
    size_t n = strlen(string);
    int is_sec = !!_gcry_is_secure(string);

    if (!outofcore_handler ||
        !outofcore_handler(outofcore_handler_value, n, is_sec)) {
      _gcry_fatal_error(gpg_error_from_errno(errno),
                        is_sec ? "out of core in secure memory" : NULL);
    }
  }

  return p;
}

int _gcry_get_debug_flag(unsigned int mask) { return (debug_flags & mask); }

/* It is often useful to get some feedback of long running operations.
   This function may be used to register a handler for this.
   The callback function CB is used as:

   void cb (void *opaque, const char *what, int printchar,
           int current, int total);

   Where WHAT is a string identifying the the type of the progress
   output, PRINTCHAR the character usually printed, CURRENT the amount
   of progress currently done and TOTAL the expected amount of
   progress.  A value of 0 for TOTAL indicates that there is no
   estimation available.

   Defined values for WHAT:

   "need_entropy"  X    0  number-of-bytes-required
            When running low on entropy
   "primegen"      '\n'  0 0
           Prime generated
                   '!'
           Need to refresh the prime pool
                   '<','>'
           Number of bits adjusted
                   '^'
           Looking for a generator
                   '.'
           Fermat tests on 10 candidates failed
                  ':'
           Restart with a new random value
                  '+'
           Rabin Miller test passed
   "pk_elg"        '+','-','.','\n'   0  0
            Only used in debugging mode.
   "pk_dsa"
            Only used in debugging mode.
*/
void _gcry_set_progress_handler(void (*cb)(void *, const char *, int, int, int),
                                void *cb_data) {
#if USE_DSA
  _gcry_register_pk_dsa_progress(cb, cb_data);
#endif
#if USE_ELGAMAL
  _gcry_register_pk_elg_progress(cb, cb_data);
#endif
  _gcry_register_primegen_progress(cb, cb_data);
}
