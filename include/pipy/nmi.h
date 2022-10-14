/*
 *  Copyright (c) 2019 by flomesh.io
 *
 *  Unless prior written consent has been obtained from the copyright
 *  owner, the following shall not be allowed.
 *
 *  1. The distribution of any source codes, header files, make files,
 *     or libraries of the software.
 *
 *  2. Disclosure of any source codes pertaining to the software to any
 *     additional parties.
 *
 *  3. Alteration or removal of any notices in or on the software or
 *     within the documentation included within the software.
 *
 *  ALL SOURCE CODE AS WELL AS ALL DOCUMENTATION INCLUDED WITH THIS
 *  SOFTWARE IS PROVIDED IN AN “AS IS” CONDITION, WITHOUT WARRANTY OF ANY
 *  KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 *  OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 *  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 *  CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 *  TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 *  SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef __PIPY_NMI_H__
#define __PIPY_NMI_H__

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * PipyJS
 */

typedef int pjs_value;

typedef enum _pjs_type {
  PJS_TYPE_UNDEFINED = 0,
  PJS_TYPE_BOOLEAN   = 1,
  PJS_TYPE_NUMBER    = 2,
  PJS_TYPE_STRING    = 3,
  PJS_TYPE_OBJECT    = 4,
} pjs_type;

extern pjs_value   pjs_undefined();
extern pjs_value   pjs_boolean(int b);
extern pjs_value   pjs_number(double n);
extern pjs_value   pjs_string(const char *s, int len);
extern pjs_value   pjs_object();
extern pjs_value   pjs_array(int len);
extern pjs_value   pjs_copy(pjs_value v, pjs_value src);
extern void        pjs_hold(pjs_value v);
extern void        pjs_free(pjs_value v);
extern pjs_type    pjs_type_of(pjs_value v);
extern int         pjs_class_of(pjs_value v);
extern int         pjs_class_id(const char *name);
extern int         pjs_is_undefined(pjs_value v);
extern int         pjs_is_null(pjs_value v);
extern int         pjs_is_nullish(pjs_value v);
extern int         pjs_is_empty_string(pjs_value v);
extern int         pjs_is_instance_of(pjs_value v, int class_id);
extern int         pjs_is_array(pjs_value v);
extern int         pjs_is_function(pjs_value v);
extern int         pjs_is_equal(pjs_value a, pjs_value b);
extern int         pjs_is_identical(pjs_value a, pjs_value b);
extern int         pjs_to_boolean(pjs_value v);
extern double      pjs_to_number(pjs_value v);
extern pjs_value   pjs_to_string(pjs_value v);
extern int         pjs_string_get_length(pjs_value str);
extern int         pjs_string_get_char_code(pjs_value str, int pos);
extern int         pjs_string_get_utf8_size(pjs_value str);
extern int         pjs_string_get_utf8_data(pjs_value str, char *buf, int len);
extern int         pjs_object_get_property(pjs_value obj, pjs_value k, pjs_value v);
extern int         pjs_object_set_property(pjs_value obj, pjs_value k, pjs_value v);
extern int         pjs_object_delete(pjs_value obj, pjs_value k);
extern void        pjs_object_iterate(pjs_value obj, int (*cb)(pjs_value k, pjs_value v));
extern int         pjs_array_get_length(pjs_value arr);
extern int         pjs_array_set_length(pjs_value arr, int len);
extern int         pjs_array_get_element(pjs_value arr, int i, pjs_value v);
extern int         pjs_array_set_element(pjs_value arr, int i, pjs_value v);
extern int         pjs_array_delete(pjs_value arr, int i);
extern int         pjs_array_push(pjs_value arr, int cnt, pjs_value v, ...);
extern pjs_value   pjs_array_pop(pjs_value arr);
extern pjs_value   pjs_array_shift(pjs_value arr);
extern int         pjs_array_unshift(pjs_value arr, int cnt, pjs_value v, ...);
extern pjs_value   pjs_array_splice(pjs_value arr, int pos, int del_cnt, int ins_cnt, pjs_value v, ...);

/*
 * Pipy API
 */

typedef int pipy_pipeline;

struct pipy_variable_def {
  int id;
  const char *name;
  const char *ns;
  pjs_value value;
};

struct pipy_pipeline_def {
  const char *name;
  void (*pipeline_init   )(pipy_pipeline ppl, void **user_ptr);
  void (*pipeline_free   )(pipy_pipeline ppl, void  *user_ptr);
  void (*pipeline_process)(pipy_pipeline ppl, void  *user_ptr, pjs_value evt);
};

struct pipy_module_def {
  struct pipy_variable_def **variables;
  struct pipy_pipeline_def **pipelines;
};

typedef struct pipy_module_def* (*pipy_module_init_fn)();

extern pjs_value pipy_Data_new(const char *buf, int len);
extern pjs_value pipy_Data_push(pjs_value obj, pjs_value data);
extern pjs_value pipy_Data_pop(pjs_value obj, int len);
extern pjs_value pipy_Data_shift(pjs_value obj, int len);
extern int       pipy_Data_get_size(pjs_value obj);
extern int       pipy_Data_get_data(pjs_value obj, char *buf, int len);
extern pjs_value pipy_MessageStart_new(pjs_value head);
extern pjs_value pipy_MessageStart_get_head(pjs_value obj);
extern pjs_value pipy_MessageEnd_new(pjs_value tail, pjs_value payload);
extern pjs_value pipy_MessageEnd_get_tail(pjs_value obj);
extern pjs_value pipy_MessageEnd_get_payload(pjs_value obj);
extern pjs_value pipy_StreamEnd_new(pjs_value error);
extern pjs_value pipy_StreamEnd_get_error(pjs_value obj);

extern void pipy_output_event(pipy_pipeline ppl, pjs_value evt);
extern void pipy_get_variable(pipy_pipeline ppl, int id, pjs_value value);
extern void pipy_set_variable(pipy_pipeline ppl, int id, pjs_value value);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* __PIPY_NMI_H__ */
