extern "C" {
#include <stdbool.h>

#include "collectd.h"
#include "common.h"
#include "plugin.h"
}

static const char this_plugin_name[] = "write_test_plugin";

//
// -----  wtest_context impl -----
//
typedef struct {
} wtest_context_t;

static void wtest_context_destroy(wtest_context_t *context);

static wtest_context_t *wtest_context_create() {
  wtest_context_t *build = NULL;
  wtest_context_t *result = NULL;

  build = (wtest_context_t *) calloc(1, sizeof(*build));
  if (build == NULL) {
    ERROR("wtest_context_create: calloc failed.");
    goto leave;
  }

  // Success!
  result = build;
  build = NULL;

  leave:
    wtest_context_destroy(build);
    return result;
}

static void wtest_context_destroy(wtest_context_t *context) {
  if (context == NULL) {
    return;
  }
  sfree(context);
}

//
// ----- lifecycle functions -----
//
// wtest_config
// wtest_init
// wtest_flush
// wtest_write
// wtest_shutdown
//
extern "C" { /* {{{ */
static int wtest_flush(cdtime_t timeout,
                       const char *identifier __attribute__((unused)),
                       user_data_t *user_data) { /* {{{ */
  DEBUG("Logging shtuff from wtest_flush");
  return 0;
} /* }}} wtest_flush */

static int wtest_write(const data_set_t *ds,
                       const value_list_t *vl,
                       user_data_t *user_data) { /* {{{ */
  DEBUG("Logging shtuff from wtest_write");
  return 0;
} /* }}} wtest_write */

static int wtest_config(oconfig_item_t *ci) /* {{{ */
{
  return 0;
} /* }}} wtest_config */

static int wtest_init(void) { /* {{{ */
  // Items to cleanup on exit.
  wtest_context_t *ctx = NULL;
  int result = -1;

  user_data_t user_data = {
    .data = NULL,
    .free_func = NULL
  };

  ctx = wtest_context_create();
  if (ctx == NULL) {
    ERROR("%s: wtest_init: wtest_context_create failed.", this_plugin_name);
    goto leave;
  }

  user_data.data = ctx;

  if (plugin_register_flush(this_plugin_name, wtest_flush, &user_data) != 0) {
    ERROR("%s: wtest_init: plugin_register_flush failed.", this_plugin_name);
    goto leave;
  }

  user_data.free_func = (void(*)(void*))&wtest_context_destroy;

  if (plugin_register_write(this_plugin_name, wtest_write, &user_data) != 0) {
    ERROR("%s: wtest_init: plugin_register_write failed.", this_plugin_name);
    goto leave;
  }

  ctx = NULL;
  result = 0;

  leave:
    wtest_context_destroy(ctx);
    return result;
} /* }}} wtest_init */

static int wtest_shutdown(void) { /* {{{ */
    return 0;
} /* }}} wtest_shutdown */


//
// ----- Plugin registration -----
//
// Registers:
//  wtest_config
//  wtest_init, which registers:
//    wtest_flush
//    wtest_write
//  wtest_shutdown
//
void module_register(void) /* {{{ */
{
  INFO("%s: inside module_register for %s", this_plugin_name, COLLECTD_USERAGENT);
  plugin_register_complex_config(this_plugin_name, wtest_config);
  plugin_register_init(this_plugin_name, wtest_init);
  plugin_register_shutdown(this_plugin_name, wtest_shutdown);
} /* }}} module_register */
} /* }}} extern "C" */
