#include <pipy/nmi.h>
#include <stdlib.h>

struct pipeline_state {
  pjs_value start;
  pjs_value body;
};

static int class_id_Data;
static int class_id_MessageStart;
static int class_id_MessageEnd;
static int class_id_StreamEnd;

static void pipeline_init(pipy_pipeline ppl, void **user_ptr) {
  *user_ptr = calloc(1, sizeof(struct pipeline_state));
}

static void pipeline_free(pipy_pipeline ppl, void *user_ptr) {
  struct pipeline_state *state = (struct pipeline_state *)user_ptr;
  pjs_free(state->start);
  pjs_free(state->body);
  free(user_ptr);
}

static void pipeline_process(pipy_pipeline ppl, void *user_ptr, pjs_value evt) {
  struct pipeline_state *state = (struct pipeline_state *)user_ptr;
  if (pjs_is_instance_of(evt, class_id_MessageStart)) {
    if (!state->start) {
      state->start = pjs_hold(evt);
      state->body = pjs_hold(pipy_Data_new(0, 0));
    }
  } else if (pjs_is_instance_of(evt, class_id_Data)) {
    if (state->start) {
      pipy_Data_push(state->body, evt);
    }
  } else if (pjs_is_instance_of(evt, class_id_MessageEnd)) {
    if (state->start) {
      pjs_free(state->start);
      pjs_free(state->body);
      state->start = 0;
      state->body = 0;
      pipy_output_event(ppl, pipy_MessageStart_new(0));
      pipy_output_event(ppl, pipy_Data_new("Hi!", 3));
      pipy_output_event(ppl, pipy_MessageEnd_new(0, 0));
    }
  }
}

struct pipy_module_def* pipy_module_init() {
  static struct pipy_pipeline_def pipeline = {
    "",
    pipeline_init,
    pipeline_free,
    pipeline_process,
  };

  static struct pipy_variable_def* variables[] = { 0 };
  static struct pipy_pipeline_def* pipelines[] = { &pipeline, 0 };

  static struct pipy_module_def module = {
    variables,
    pipelines,
  };

  class_id_Data = pjs_class_id("pipy::Data");
  class_id_MessageStart = pjs_class_id("pipy::MessageStart");
  class_id_MessageEnd = pjs_class_id("pipy::MessageEnd");
  class_id_StreamEnd = pjs_class_id("pipy::StreamEnd");

  return &module;
}
