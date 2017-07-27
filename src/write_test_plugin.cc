extern "C" {
#include <stdbool.h>

#include "collectd.h"
#include "common.h"
#include "plugin.h"
#include "string.h"

#include "daemon/utils_cache.h"
}

#include <memory>
#include <time.h>
#include <uuid/uuid.h> // TODO: add uuid-dev to deps in configure
#include <vector>

#include <google/protobuf/repeated_field.h>
#include <google/protobuf/util/time_util.h>

#include <grpc++/grpc++.h>
#include <grpc++/channel.h>
#include <grpc++/client_context.h>
#include <grpc++/create_channel.h>
#include <grpc++/security/credentials.h>

#include "google/api/servicecontrol/v1/service_controller.grpc.pb.h"

using google::api::servicecontrol::v1::MetricValue;
using google::api::servicecontrol::v1::MetricValueSet;
using google::api::servicecontrol::v1::Operation;
using google::api::servicecontrol::v1::ReportRequest;
using google::api::servicecontrol::v1::ReportResponse;
using google::api::servicecontrol::v1::ServiceController;

using google::protobuf::Map;
using google::protobuf::RepeatedPtrField;
using google::protobuf::string;
using google::protobuf::Timestamp;
using google::protobuf::util::TimeUtil;

static const char this_plugin_name[] = "write_test_plugin";

class ServiceControllerClient {
  public:
    ServiceControllerClient(
      std::shared_ptr<grpc::Channel> channel,
      std::shared_ptr<grpc::CallCredentials> call_creds
      ) : stub_(ServiceController::NewStub(channel)),
          call_creds_(call_creds) {}

  int Report(ReportRequest request) {
    ReportResponse response;
    grpc::ClientContext context;
    context.set_credentials(call_creds_);
    grpc::Status status = stub_->Report(&context, request, &response);

    if (status.ok()) {
      return 0;
    } else {
      ERROR("%s: ServiceControllerClient::Report failed with error code %d: %s",
            this_plugin_name, status.error_code(), status.error_message().c_str());
      return -1;
    }
  }

  private:
    std::unique_ptr<ServiceController::Stub> stub_;
    std::shared_ptr<grpc::CallCredentials> call_creds_;
};

bool is_prefix_of(const char* str1, const char* str2) {
    return (strncmp(str1, str2, strlen(str1)) == 0);
}

static int metadata_value_as_string(meta_data_t *meta, const char* key, string *str_out) {
  switch(meta_data_type(meta, key)) {
  case MD_TYPE_STRING:
    char *md_string;
    if (meta_data_get_string(meta, key, &md_string) != 0 || md_string == nullptr) {
      return -1;
    }
    *str_out = string(md_string);
    free(md_string);
    return 0;
  case MD_TYPE_SIGNED_INT:
    int64_t md_int;
    if (meta_data_get_signed_int(meta, key, &md_int) != 0) {
      return -1;
    }
    *str_out = std::to_string(md_int);
    return 0;
  case MD_TYPE_UNSIGNED_INT:
    uint64_t md_uint;
    if (meta_data_get_unsigned_int(meta, key, &md_uint) != 0) {
      return -1;
    }
    *str_out = std::to_string(md_uint);
    return 0;
  case MD_TYPE_DOUBLE:
    double md_double;
    if (meta_data_get_double(meta, key, &md_double) != 0) {
      return -1;
    }
    *str_out = std::to_string(md_double);
  case MD_TYPE_BOOLEAN:
    bool md_bool;
    if (meta_data_get_boolean(meta, key, &md_bool) != 0) {
      return -1;
    }
    *str_out = std::to_string(md_double);
  default:
    return -1;
  }
}

// Returns 0 on success, -1 on failure, -2 on non-Chemist metrics
static int name_and_labels_from_metadata(
  meta_data_t *meta,
  string *name_out,
  Map<string, string> *labels_out
) { /* {{{ */
  if (meta == nullptr) {
    return -2;
  }

  char **meta_data_keys = nullptr;
  int meta_data_keys_len = meta_data_toc(meta, &meta_data_keys);
  if (meta_data_keys_len < 0) {
    ERROR("%s: names_and_labels_from_metadata: "
          "error reading metadata table of contents.", this_plugin_name);
    return -1;
  }

  bool found_name = false;
  for (int i = 0; i < meta_data_keys_len; i++) {
    char *key = meta_data_keys[i];

    if (strcmp("stackdriver_metric_type", key) == 0) {
      found_name = true;

      if (meta_data_type(meta, key) != MD_TYPE_STRING) {
        strarray_free(meta_data_keys, meta_data_keys_len);
        ERROR("%s: names_and_labels_from_metadata: "
              "metric name of non-string type", this_plugin_name);
        return -1;
      }

      char *metric_name;
      if (meta_data_get_string(meta, key, &metric_name) != 0 || metric_name == nullptr) {
        strarray_free(meta_data_keys, meta_data_keys_len);
        ERROR("%s: names_and_labels_from_metadata: "
              "error getting name (string) from metadata", this_plugin_name);
        return -1;
      }
      *name_out = string(metric_name);
      free(metric_name);
    } else if (is_prefix_of("label:", key)) {
      char *label_name = key + strlen("label:");
      string label_value;
      if (metadata_value_as_string(meta, key, &label_value) < 0) {
        strarray_free(meta_data_keys, meta_data_keys_len);
        ERROR("%s: names_and_labels_from_metadata: "
              "error getting metadata with key \"%s\" as string",
              this_plugin_name, key);
        return -1;
      }
      (*labels_out)[string(label_name)] = label_value;
    }
  }

  strarray_free(meta_data_keys, meta_data_keys_len);

  if (found_name) {
    return 0;
  } else {
    return -2;
  }
} /* }}} name_and_labels_from_metadata */

static int raw_values_to_metric_value_sets(
    const data_set_t *cd_datasource,
    const value_list_t *cd_values,
    RepeatedPtrField<MetricValueSet> *value_sets_out) { /* {{{ */

  string metric_name;
  Map<string, string> labels_map;
  if (name_and_labels_from_metadata(cd_values->meta, &metric_name, &labels_map) != 0) {
    return -1;
  }

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
    new_value_set->set_metric_name(metric_name);

    RepeatedPtrField<MetricValue> *new_values = new_value_set->mutable_metric_values();
    MetricValue *new_value = new_values->Add();
    *new_value->mutable_start_time() = start_time;
    *new_value->mutable_end_time() = end_time;

    *new_value->mutable_labels() = Map<string, string>(labels_map);

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

  return 0;
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

  // TODO: use the context to store a reusable client channel/context
  wtest_context_t *ctx = (wtest_context_t *)user_data->data;
  (void)ctx;

  Operation operation;
  if (raw_values_to_metric_value_sets(
        ds, vl, operation.mutable_metric_value_sets()) != 0) {
    // Either the current value wasn't flagged for reporting to
    // ServiceController, or we failed to process it correctly.
    //
    // If we failed to process it correctly, an error has already
    // been reported, so we ignore the result.
    return 0;
  }

  // TODO: set these operation values properly

  uuid_t uuid;
  uuid_generate(uuid);
  char uuid_chars [37]; // UUIDs are 36 characters + trailing '\0'
  uuid_unparse(uuid, &uuid_chars[0]);
  string uuid_string(uuid_chars);
  operation.set_operation_id(uuid_string);

  operation.set_operation_name("redis_metrics_operation");

  operation.set_consumer_id("project:google.com:henryf-test");

  auto labels = operation.mutable_labels();
  (*labels)[string("cloud.googleapis.com/project")] = string("test_project");
  (*labels)[string("cloud.googleapis.com/location")] = string("test_location");
  (*labels)[string("redis.googleapis.com/instance_id")] = string("test_instance_id");
  (*labels)[string("redis.googleapis.com/node_id")] = string("test_node_id");
  (*labels)[string("cloud.googleapis.com/uid")] = string("test_my_super_fancy_uid");

  time_t cur_time = time(NULL);
  operation.mutable_start_time()->set_seconds(cur_time);
  operation.mutable_start_time()->set_nanos(0);
  operation.mutable_end_time()->set_seconds(cur_time);
  operation.mutable_end_time()->set_nanos(0);

  ReportRequest request;
  request.set_service_name("redis.googleapis.com");
  *request.add_operations() = operation;

  auto ssl_creds = grpc::SslCredentials(grpc::SslCredentialsOptions{});
  auto channel = grpc::CreateChannel("servicecontrol.googleapis.com:443", ssl_creds);

  auto call_creds = grpc::GoogleComputeEngineCredentials();
  
  ServiceControllerClient client(channel, call_creds);
  client.Report(request);

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
