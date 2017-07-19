extern "C" {
#include <stdbool.h>

#include "collectd.h"
#include "common.h"
#include "plugin.h"

#include "daemon/utils_cache.h"
}

#include <memory>
#include <vector>

#include <google/protobuf/repeated_field.h>
#include <google/protobuf/util/time_util.h>

#include <grpc++/grpc++.h>
#include <grpc++/channel.h>
#include <grpc++/client_context.h>
#include <grpc++/create_channel.h>
#include <grpc++/security/credentials.h>

#include "google/api/servicecontrol/v1/service_controller.pb.h"

using google::api::servicecontrol::v1::MetricValue;
using google::api::servicecontrol::v1::MetricValueSet;

using google::protobuf::Map;
using google::protobuf::RepeatedPtrField;
using google::protobuf::string;
using google::protobuf::Timestamp;
using google::protobuf::util::TimeUtil;

static const char this_plugin_name[] = "write_test_plugin";

static Map<string, string> metadata_to_lables(meta_data_t *meta) {
  // Items to clean up on exit
  char **toc = nullptr;
  int toc_size = 0;
  int result = -1;

  if (meta != nullptr) {
    toc_size = meta_data_toc(meta, &toc);
    if (toc_size < 0) {
    }
  }

  leave:
    ERROR("%s: meta_data_to_labels: error reading metadata table of contents.");
}

static void raw_values_to_metric_value_sets(
    const data_set_t *cd_datasource,
    const value_list_t *cd_values,
    RepeatedPtrField<MetricValueSet> *value_sets_out) { /* {{{ */

  int64_t start_ns = CDTIME_T_TO_NS(cd_values->time);
  int64_t duration_ns = CDTIME_T_TO_NS(cd_values->interval);
  Timestamp start_time = TimeUtil::NanosecondsToTimestamp(start_ns);
  Timestamp end_time = TimeUtil::NanosecondsToTimestamp(start_ns + duration_ns);
  if (start_ns + duration_ns < start_ns) {
      ERROR(": overflow of start_ns + duration_ns");
  }


  unsigned int i;
  for (i = 0; i < cd_datasource->ds_num; i++) {
    data_source_t *src = &cd_datasource->ds[i];
    value_t value = cd_values->values[i];

    MetricValueSet *new_value_set = value_sets_out->Add();
    new_value_set->set_metric_name(&src->name[0]);

    RepeatedPtrField<MetricValue> *new_values = new_value_set->mutable_metric_values();
    MetricValue *new_value = new_values->Add();
    Map<string, string> *labels = new_value->mutable_labels();
    *new_value->mutable_start_time() = start_time;
    *new_value->mutable_end_time() = end_time;

    // TODO write value as bool based on inception config
    switch (src->type) {
      case DS_TYPE_COUNTER:
        new_value->set_int64_value(value.counter);
        break;
      case DS_TYPE_GAUGE:
        new_value->set_double_value(value.gauge);
        break;
      case DS_TYPE_DERIVE:
        new_value->set_int64_value(value.derive);
        break;
      case DS_TYPE_ABSOLUTE:
        new_value->set_int64_value(value.absolute);
        break;
      default:
        // Clean up the resources from the metric we tried to add
        value_sets_out->RemoveLast();
        new_values->RemoveLast();
        ERROR("raw_values_to_metric_value_sets: unknown value type");
        continue;
    }
  }
} /* }}} raw_values_to_metric_value_sets */


//
// -----  wtest_context impl -----
//
typedef struct {
} wtest_context_t;

static void wtest_context_destroy(wtest_context_t *context);

static wtest_context_t *wtest_context_create() { /* {{{ */
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
} /* }}} wtest_context_create */

static void wtest_context_destroy(wtest_context_t *context) { /* {{{ */
  if (context == NULL) {
    return;
  }
  sfree(context);
} /* }}} wtest_context_destroy */

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
  assert(ds->ds_num > 0);
  wtest_context_t *ctx = (wtest_context_t *)user_data->data;

  auto channel = grpc::CreateChannel(
                    "localhost:50051",
                    grpc::InsecureChannelCredentials());

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
