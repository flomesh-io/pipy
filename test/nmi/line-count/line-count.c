#include <pipy/nmi.h>
#include <stdlib.h>

enum {
  id_variable_lineCount,
};

static int id_class_Data = 0;

static void pipeline_init(pipy_pipeline ppl, void **user_ptr) {
  *user_ptr = calloc(1, sizeof(int));
}

static void pipeline_free(pipy_pipeline ppl, void *user_ptr) {
  free(user_ptr);
}

static void pipeline_process(pipy_pipeline ppl, void *user_ptr, pjs_value evt) {
  if (pjs_is_instance_of(evt, id_class_Data)) {
    int n = pipy_Data_get_size(evt);
    char buf[n];
    pipy_Data_get_data(evt, buf, n);

    int i, count = 0;
    for (i = 0; i < n; i++) {
      if (buf[i] == '\n') count++;
    }

    pipy_set_variable(
      ppl,
      id_variable_lineCount,
      pjs_number(
        *(int *)user_ptr += count
      )
    );
  }

  pipy_output_event(ppl, evt);
}

struct pipy_module_def* pipy_module_init() {
  static struct pipy_variable_def variable_lineCount = {
    id_variable_lineCount,
    "__lineCount",
    "line-count",
  };

  static struct pipy_pipeline_def pipeline = {
    "",
    pipeline_init,
    pipeline_free,
    pipeline_process,
  };

  static struct pipy_variable_def* variables[] = { &variable_lineCount, 0 };
  static struct pipy_pipeline_def* pipelines[] = { &pipeline, 0 };

  static struct pipy_module_def module = {
    variables,
    pipelines,
  };


  id_class_Data = pjs_class_id("pipy::Data");
  variable_lineCount.value = pjs_number(0);
  return &module;
}
