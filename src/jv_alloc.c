#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "jv.h"

struct nomem_handler {
    jv_nomem_handler_f handler;
    void *data;
};

#if !defined(HAVE_PTHREAD_KEY_CREATE) || \
    !defined(HAVE_PTHREAD_ONCE) || \
    !defined(HAVE_ATEXIT)

/* Try thread-local storage? */

#ifdef _MSC_VER
/* Visual C++: yes */
static __declspec(thread) struct nomem_handler nomem_handler;
#define USE_TLS
#else
#ifdef HAVE___THREAD
/* GCC and friends: yes */
static __thread struct nomem_handler nomem_handler;
#define USE_TLS
#endif /* HAVE___THREAD */
#endif /* _MSC_VER */

#endif /* !HAVE_PTHREAD_KEY_CREATE */

#ifdef USE_TLS
void jv_nomem_handler(jv_nomem_handler_f handler, void *data) {
  nomem_handler.handler = handler;
}

static void memory_exhausted(void) {
  if (nomem_handler.handler)
    nomem_handler.handler(nomem_handler.data); // Maybe handler() will longjmp() to safety
  // Or not
  fprintf(stderr, "jq: error: cannot allocate memory\n");
  abort();
}
#else /* USE_TLS */

#ifdef HAVE_PTHREAD_KEY_CREATE
#include <pthread.h>

static pthread_key_t nomem_handler_key;
static pthread_once_t mem_once = PTHREAD_ONCE_INIT;

/* tsd_fini is called on application exit */
/* it clears the nomem_handler allocated in the main thread */
static void tsd_fini(void) {
  struct nomem_handler *nomem_handler;
  nomem_handler = pthread_getspecific(nomem_handler_key);
  if (nomem_handler) {
    (void) pthread_setspecific(nomem_handler_key, NULL);
    free(nomem_handler);
  }
}

/* The tsd_fini_thread is a destructor set by calling */
/* pthread_key_create(&nomem_handler_key, tsd_fini_thread) */
/* It is called when thread ends */
static void tsd_fini_thread(void *nomem_handler) {
  free(nomem_handler);
}

static void tsd_init(void) {
  if (pthread_key_create(&nomem_handler_key, tsd_fini_thread) != 0) {
    fprintf(stderr, "jq: error: cannot create thread specific key");
    abort();
  }
  if (atexit(tsd_fini) != 0) {
    fprintf(stderr, "jq: error: cannot set an exit handler");
    abort();
  }
}

static void tsd_init_nomem_handler(void)
{
  if (pthread_getspecific(nomem_handler_key) == NULL) {
    struct nomem_handler *nomem_handler = calloc(1, sizeof(struct nomem_handler));
    if (pthread_setspecific(nomem_handler_key, nomem_handler) != 0) {
      fprintf(stderr, "jq: error: cannot set thread specific data");
      abort();
    }
  }
}

void jv_nomem_handler(jv_nomem_handler_f handler, void *data) {
  pthread_once(&mem_once, tsd_init); // cannot fail
  tsd_init_nomem_handler();

  struct nomem_handler *nomem_handler;

  nomem_handler = pthread_getspecific(nomem_handler_key);
  if (nomem_handler == NULL) {
    handler(data);
    fprintf(stderr, "jq: error: cannot allocate memory\n");
    abort();
  }
  nomem_handler->handler = handler;
  nomem_handler->data = data;
}

static void memory_exhausted(void) {
  struct nomem_handler *nomem_handler;

  pthread_once(&mem_once, tsd_init);
  tsd_init_nomem_handler();

  nomem_handler = pthread_getspecific(nomem_handler_key);
  if (nomem_handler && nomem_handler->handler)
    nomem_handler->handler(nomem_handler->data); // Maybe handler() will longjmp() to safety
  // Or not
  fprintf(stderr, "jq: error: cannot allocate memory\n");
  abort();
}

#else

/* No thread-local storage of any kind that we know how to handle */

static struct nomem_handler nomem_handler;
void jv_nomem_handler(jv_nomem_handler_f handler, void *data) {
  nomem_handler.handler = handler;
  nomem_handler.data = data;
}

static void memory_exhausted(void) {
  fprintf(stderr, "jq: error: cannot allocate memory\n");
  abort();
}

#endif /* HAVE_PTHREAD_KEY_CREATE */
#endif /* USE_TLS */


void* jv_mem_alloc(size_t sz) {
  void* p = malloc(sz);
  if (!p) {
    memory_exhausted();
  }
  return p;
}

void* jv_mem_alloc_unguarded(size_t sz) {
  return malloc(sz);
}

void* jv_mem_calloc(size_t nemb, size_t sz) {
  assert(nemb > 0 && sz > 0);
  void* p = calloc(nemb, sz);
  if (!p) {
    memory_exhausted();
  }
  return p;
}

void* jv_mem_calloc_unguarded(size_t nemb, size_t sz) {
  assert(nemb > 0 && sz > 0);
  return calloc(nemb, sz);
}

char* jv_mem_strdup(const char *s) {
  char *p = strdup(s);
  if (!p) {
    memory_exhausted();
  }
  return p;
}

char* jv_mem_strdup_unguarded(const char *s) {
  return strdup(s);
}

void jv_mem_free(void* p) {
  free(p);
}

void* jv_mem_realloc(void* p, size_t sz) {
  p = realloc(p, sz);
  if (!p) {
    memory_exhausted();
  }
  return p;
}
