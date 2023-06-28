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

#define NMI_EXPORT extern __attribute__((used))

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

NMI_EXPORT pjs_value   pjs_undefined();
NMI_EXPORT pjs_value   pjs_boolean(int b);
NMI_EXPORT pjs_value   pjs_number(double n);
NMI_EXPORT pjs_value   pjs_string(const char *s, int len);
NMI_EXPORT pjs_value   pjs_object();
NMI_EXPORT pjs_value   pjs_array(int len);
NMI_EXPORT pjs_value   pjs_copy(pjs_value v, pjs_value src);
NMI_EXPORT pjs_value   pjs_hold(pjs_value v);
NMI_EXPORT void        pjs_free(pjs_value v);
NMI_EXPORT pjs_type    pjs_type_of(pjs_value v);
NMI_EXPORT int         pjs_class_of(pjs_value v);
NMI_EXPORT int         pjs_class_id(const char *name);
NMI_EXPORT int         pjs_is_undefined(pjs_value v);
NMI_EXPORT int         pjs_is_null(pjs_value v);
NMI_EXPORT int         pjs_is_nullish(pjs_value v);
NMI_EXPORT int         pjs_is_empty_string(pjs_value v);
NMI_EXPORT int         pjs_is_instance_of(pjs_value v, int class_id);
NMI_EXPORT int         pjs_is_array(pjs_value v);
NMI_EXPORT int         pjs_is_function(pjs_value v);
NMI_EXPORT int         pjs_is_equal(pjs_value a, pjs_value b);
NMI_EXPORT int         pjs_is_identical(pjs_value a, pjs_value b);
NMI_EXPORT int         pjs_to_boolean(pjs_value v);
NMI_EXPORT double      pjs_to_number(pjs_value v);
NMI_EXPORT pjs_value   pjs_to_string(pjs_value v);
NMI_EXPORT int         pjs_string_get_length(pjs_value str);
NMI_EXPORT int         pjs_string_get_char_code(pjs_value str, int pos);
NMI_EXPORT int         pjs_string_get_utf8_size(pjs_value str);
NMI_EXPORT int         pjs_string_get_utf8_data(pjs_value str, char *buf, int len);
NMI_EXPORT int         pjs_object_get_property(pjs_value obj, pjs_value k, pjs_value v);
NMI_EXPORT int         pjs_object_set_property(pjs_value obj, pjs_value k, pjs_value v);
NMI_EXPORT int         pjs_object_delete(pjs_value obj, pjs_value k);
NMI_EXPORT void        pjs_object_iterate(pjs_value obj, int (*cb)(pjs_value k, pjs_value v, void *user_ptr), void *user_ptr);
NMI_EXPORT int         pjs_array_get_length(pjs_value arr);
NMI_EXPORT int         pjs_array_set_length(pjs_value arr, int len);
NMI_EXPORT int         pjs_array_get_element(pjs_value arr, int i, pjs_value v);
NMI_EXPORT int         pjs_array_set_element(pjs_value arr, int i, pjs_value v);
NMI_EXPORT int         pjs_array_delete(pjs_value arr, int i);
NMI_EXPORT int         pjs_array_push(pjs_value arr, pjs_value v);
NMI_EXPORT pjs_value   pjs_array_pop(pjs_value arr);
NMI_EXPORT pjs_value   pjs_array_shift(pjs_value arr);
NMI_EXPORT int         pjs_array_unshift(pjs_value arr, pjs_value v);
NMI_EXPORT pjs_value   pjs_array_splice(pjs_value arr, int pos, int del_cnt, int ins_cnt, pjs_value v[]);

/*
 * Pipy API
 */

typedef int pipy_pipeline;

typedef void (*fn_pipy_module_init)();
typedef void (*fn_pipeline_init   )(pipy_pipeline ppl, void **user_ptr);
typedef void (*fn_pipeline_free   )(pipy_pipeline ppl, void  *user_ptr);
typedef void (*fn_pipeline_process)(pipy_pipeline ppl, void  *user_ptr, pjs_value evt);

NMI_EXPORT int       pipy_is_Data(pjs_value obj);
NMI_EXPORT int       pipy_is_MessageStart(pjs_value obj);
NMI_EXPORT int       pipy_is_MessageEnd(pjs_value obj);
NMI_EXPORT int       pipy_is_StreamEnd(pjs_value obj);
NMI_EXPORT pjs_value pipy_Data_new(const char *buf, int len);
NMI_EXPORT pjs_value pipy_Data_push(pjs_value obj, pjs_value data);
NMI_EXPORT pjs_value pipy_Data_pop(pjs_value obj, int len);
NMI_EXPORT pjs_value pipy_Data_shift(pjs_value obj, int len);
NMI_EXPORT int       pipy_Data_get_size(pjs_value obj);
NMI_EXPORT int       pipy_Data_get_data(pjs_value obj, char *buf, int len);
NMI_EXPORT pjs_value pipy_MessageStart_new(pjs_value head);
NMI_EXPORT pjs_value pipy_MessageStart_get_head(pjs_value obj);
NMI_EXPORT pjs_value pipy_MessageEnd_new(pjs_value tail, pjs_value payload);
NMI_EXPORT pjs_value pipy_MessageEnd_get_tail(pjs_value obj);
NMI_EXPORT pjs_value pipy_MessageEnd_get_payload(pjs_value obj);
NMI_EXPORT pjs_value pipy_StreamEnd_new(pjs_value error);
NMI_EXPORT pjs_value pipy_StreamEnd_get_error(pjs_value obj);

NMI_EXPORT void pipy_define_variable(int id, const char *name, const char *ns, pjs_value value);
NMI_EXPORT void pipy_define_pipeline(const char *name, fn_pipeline_init init, fn_pipeline_free free, fn_pipeline_process process);
NMI_EXPORT void pipy_hold(pipy_pipeline ppl);
NMI_EXPORT void pipy_free(pipy_pipeline ppl);
NMI_EXPORT void pipy_output_event(pipy_pipeline ppl, pjs_value evt);
NMI_EXPORT void pipy_get_variable(pipy_pipeline ppl, int id, pjs_value value);
NMI_EXPORT void pipy_set_variable(pipy_pipeline ppl, int id, pjs_value value);
NMI_EXPORT void pipy_schedule(pipy_pipeline ppl, double timeout, void (*fn)(void *), void *user_ptr);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* __PIPY_NMI_H__ */
