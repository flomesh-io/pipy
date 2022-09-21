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

#ifndef __PIPY_MODULE_H__
#define __PIPY_MODULE_H__

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * PipyJS
 */

typedef int pjs_value;

enum pjs_type {
  PJS_TYPE_UNDEFINED = 0,
  PJS_TYPE_BOOLEAN   = 1,
  PJS_TYPE_NUMBER    = 2,
  PJS_TYPE_STRING    = 3,
  PJS_TYPE_OBJECT    = 4,
};

pjs_value   pjs_undefined();
pjs_value   pjs_boolean(int b);
pjs_value   pjs_number(double n);
pjs_value   pjs_string(const char *s, int len);
pjs_value   pjs_object();
pjs_value   pjs_array(int len);
pjs_value   pjs_copy(pjs_value v, pjs_value src);
void        pjs_hold(pjs_value v);
void        pjs_free(pjs_value v);
pjs_type    pjs_type_of(pjs_value v);
int         pjs_class_of(pjs_value v);
int         pjs_class_id(const char *name);
int         pjs_is_null(pjs_value v);
int         pjs_is_nullish(pjs_value v);
int         pjs_is_instance_of(pjs_value v, int class_id);
int         pjs_is_array(pjs_value v);
int         pjs_is_function(pjs_value v);
int         pjs_is_equal(pjs_value a, pjs_value b);
int         pjs_is_identical(pjs_value a, pjs_value b);
int         pjs_to_boolean(pjs_value v);
double      pjs_to_number(pjs_value v);
pjs_value   pjs_to_string(pjs_value v);
int         pjs_string_get_length(pjs_value str);
int         pjs_string_get_char_code(pjs_value str, int pos);
int         pjs_string_get_utf8_size(pjs_value str);
int         pjs_string_get_utf8_data(pjs_value str, char *buf, int len);
int         pjs_object_get_property(pjs_value obj, pjs_value k, pjs_value v);
int         pjs_object_set_property(pjs_value obj, pjs_value k, pjs_value v);
int         pjs_object_delete(pjs_value obj, pjs_value k);
void        pjs_object_iterate(pjs_value obj, int (*cb)(pjs_value k, pjs_value v));
int         pjs_array_get_length(pjs_value arr);
int         pjs_array_set_length(pjs_value arr, int len);
int         pjs_array_get_element(pjs_value arr, int i, pjs_value v);
int         pjs_array_set_element(pjs_value arr, int i, pjs_value v);
int         pjs_array_delete(pjs_value arr, int i);
int         pjs_array_push(pjs_value arr, int cnt, pjs_value v, ...);
pjs_value   pjs_array_pop(pjs_value arr);
pjs_value   pjs_array_shift(pjs_value arr);
int         pjs_array_unshift(pjs_value arr, int cnt, pjs_value v, ...);
pjs_value   pjs_array_splice(pjs_value arr, int pos, int del_cnt, int ins_cnt, pjs_value v, ...);

/*
 * Pipy API
 */

typedef int pipy_module;
typedef int pipy_context;
typedef int pipy_pipeline;

struct pipy_pipeline_handlers {
  void (*pipeline_init   )(pipy_pipeline ppl, pipy_context ctx, pjs_value init_value);
  void (*pipeline_free   )(pipy_pipeline ppl, pipy_context ctx);
  void (*pipeline_process)(pipy_pipeline ppl, pipy_context ctx, pjs_value evt);
};

typedef int (*pipy_module_init)(pipy_module mod);

int  pipy_export_variable(pipy_module mod, const char *name, const char *ns, pjs_value value);
void pipy_define_pipeline(pipy_module mod, const char *name, struct pipy_pipeline_handlers *handlers);

void pipy_context_set(pipy_context ctx, int export_id, pjs_value value);
void pipy_context_get(pipy_context ctx, int export_id, pjs_value value);
void pipy_pipeline_output(pipy_pipeline ppl, pjs_value evt);

pjs_value pipy_Data_new(const char *buf, int len);
pjs_value pipy_Data_push(pjs_value obj, pjs_value data);
pjs_value pipy_Data_pop(pjs_value obj, int len);
pjs_value pipy_Data_shift(pjs_value obj, int len);
int       pipy_Data_get_size(pjs_value obj);
int       pipy_Data_get_data(pjs_value obj, char *buf, int len);
pjs_value pipy_MessageStart_new(pjs_value head);
pjs_value pipy_MessageStart_get_head(pjs_value obj);
pjs_value pipy_MessageEnd_new(pjs_value tail, pjs_value payload);
pjs_value pipy_MessageEnd_get_tail(pjs_value obj);
pjs_value pipy_MessageEnd_get_payload(pjs_value obj);
pjs_value pipy_StreamEnd_new(pjs_value error);
pjs_value pipy_StreamEnd_get_error(pjs_value obj);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* __PIPY_MODULE_H__ */
