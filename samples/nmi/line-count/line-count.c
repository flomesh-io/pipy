#include <pipy/nmi.h>
#include <stdlib.h>

enum {
  id_variable_lineCount,
};

static void pipeline_init(pipy_pipeline ppl, void **user_ptr) {
  *user_ptr = calloc(1, sizeof(int));
}

static void pipeline_free(pipy_pipeline ppl, void *user_ptr) {
  free(user_ptr);
}

static void pipeline_process(pipy_pipeline ppl, void *user_ptr, pjs_value evt) {
  if (pipy_is_Data(evt)) {
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

void pipy_module_init() {
  pipy_define_variable(id_variable_lineCount, "__lineCount", "line-count", pjs_number(0));
  pipy_define_pipeline("", pipeline_init, pipeline_free, pipeline_process);
}
