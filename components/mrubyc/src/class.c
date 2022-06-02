/*! @file
  @brief
  mruby/c Object, Proc, Nil, False and True class and class specific functions.

  <pre>
  Copyright (C) 2015-2020 Kyushu Institute of Technology.
  Copyright (C) 2015-2020 Shimane IT Open-Innovation Center.

  This file is distributed under BSD 3-Clause License.


  </pre>
*/

#include "vm_config.h"
#include <string.h>
#include <assert.h>

#include "value.h"
#include "alloc.h"
#include "class.h"
#include "vm.h"
#include "keyvalue.h"
#include "static.h"
#include "symbol.h"
#include "global.h"
#include "console.h"
#include "opcode.h"
#include "load.h"
#include "error.h"

#include "c_array.h"
#include "c_hash.h"
#include "c_numeric.h"
#include "c_math.h"
#include "c_string.h"
#include "c_range.h"



//================================================================
/*! Check the class is the class of object.

  @param  obj	target object
  @param  cls	class
  @return	result
*/
int mrbc_obj_is_kind_of( const mrbc_value *obj, const mrb_class *cls )
{
  const mrbc_class *c = find_class_by_object( 0, obj );
  while( c != NULL ) {
    if( c == cls ) return 1;
    c = c->super;
  }

  return 0;
}


//================================================================
/*! mrbc_instance constructor

  @param  vm    Pointer to VM.
  @param  cls	Pointer to Class (mrbc_class).
  @param  size	size of additional data.
  @return       mrbc_instance object.
*/
mrbc_value mrbc_instance_new(struct VM *vm, mrbc_class *cls, int size)
{
  mrbc_value v = {.tt = MRBC_TT_OBJECT};
  v.instance = (mrbc_instance *)mrbc_alloc(vm, sizeof(mrbc_instance) + size);
  if( v.instance == NULL ) return v;	// ENOMEM

  if( mrbc_kv_init_handle(vm, &v.instance->ivar, 0) != 0 ) {
    mrbc_raw_free(v.instance);
    v.instance = NULL;
    return v;
  }

  v.instance->ref_count = 1;
  v.instance->tt = MRBC_TT_OBJECT;	// for debug only.
  v.instance->cls = cls;

  return v;
}



//================================================================
/*! mrbc_instance destructor

  @param  v	pointer to target value
*/
void mrbc_instance_delete(mrbc_value *v)
{
  mrbc_kv_delete_data( &v->instance->ivar );
  mrbc_raw_free( v->instance );
}


//================================================================
/*! instance variable setter

  @param  obj		pointer to target.
  @param  sym_id	key symbol ID.
  @param  v		pointer to value.
*/
void mrbc_instance_setiv(mrbc_object *obj, mrbc_sym sym_id, mrbc_value *v)
{
  mrbc_dup(v);
  mrbc_kv_set( &obj->instance->ivar, sym_id, v );
}


//================================================================
/*! instance variable getter

  @param  obj		pointer to target.
  @param  sym_id	key symbol ID.
  @return		value.
*/
mrbc_value mrbc_instance_getiv(mrbc_object *obj, mrbc_sym sym_id)
{
  mrbc_value *v = mrbc_kv_get( &obj->instance->ivar, sym_id );
  if( !v ) return mrbc_nil_value();

  mrbc_dup(v);
  return *v;
}



//================================================================
/*! find class by object

  @param  vm	pointer to vm
  @param  obj	pointer to object
  @return	pointer to mrbc_class
*/
mrbc_class *find_class_by_object(struct VM *vm, const mrbc_object *obj)
{
  mrbc_class *cls;

  assert( obj->tt != MRBC_TT_EMPTY );

  switch( obj->tt ) {
  case MRBC_TT_TRUE:	cls = mrbc_class_true;		break;
  case MRBC_TT_FALSE:	cls = mrbc_class_false; 	break;
  case MRBC_TT_NIL:	cls = mrbc_class_nil;		break;
  case MRBC_TT_FIXNUM:	cls = mrbc_class_fixnum;	break;
  case MRBC_TT_FLOAT:	cls = mrbc_class_float; 	break;
  case MRBC_TT_SYMBOL:	cls = mrbc_class_symbol;	break;

  case MRBC_TT_OBJECT:	cls = obj->instance->cls;       break;
  case MRBC_TT_CLASS:   cls = obj->cls;                 break;
  case MRBC_TT_PROC:	cls = mrbc_class_proc;		break;
  case MRBC_TT_ARRAY:	cls = mrbc_class_array; 	break;
  case MRBC_TT_STRING:	cls = mrbc_class_string;	break;
  case MRBC_TT_RANGE:	cls = mrbc_class_range; 	break;
  case MRBC_TT_HASH:	cls = mrbc_class_hash;		break;

  default:		cls = mrbc_class_object;	break;
  }

  return cls;
}



//================================================================
/*! find method from class

  @param  r_cls		found class return pointer or NULL
  @param  cls		target class
  @param  sym_id	sym_id of method
  @return		pointer to mrbc_proc or NULL
*/
mrbc_proc *find_method_by_class( mrbc_class **r_cls, mrbc_class *cls, mrbc_sym sym_id )
{
  while( cls != 0 ) {
    mrbc_proc *proc = cls->procs;
    while( proc != 0 ) {
      if( proc->sym_id == sym_id ) {
	if( r_cls ) *r_cls = cls;
        return proc;
      }
      proc = proc->next;
    }
    cls = cls->super;
  }

  return 0;
}


//================================================================
/*! find method from object

  @param  vm		pointer to vm
  @param  recv		pointer to receiver object.
  @param  sym_id	symbol id.
  @return		pointer to proc or NULL.
*/
mrbc_proc *find_method(struct VM *vm, const mrbc_object *recv, mrbc_sym sym_id)
{
  mrbc_class *cls = find_class_by_object(vm, recv);

  return find_method_by_class(NULL, cls, sym_id);
}



//================================================================
/*! define class

  @param  vm		pointer to vm.
  @param  name		class name.
  @param  super		super class.
  @return		pointer to defined class.
*/
mrbc_class * mrbc_define_class(struct VM *vm, const char *name, mrbc_class *super)
{
  mrbc_sym sym_id = str_to_symid(name);
  mrbc_object *obj = mrbc_get_const( sym_id );

  // create a new class?
  if( obj == NULL ) {
    mrbc_class *cls = mrbc_raw_alloc_no_free( sizeof(mrbc_class) );
    if( !cls ) return cls;	// ENOMEM

    cls->sym_id = sym_id;
#ifdef MRBC_DEBUG
    cls->names = name;	// for debug; delete soon.
#endif
    cls->super = (super == NULL) ? mrbc_class_object : super;
    cls->procs = 0;

    // register to global constant.
    mrbc_set_const( sym_id, &(mrb_value){.tt = MRBC_TT_CLASS, .cls = cls} );
    return cls;
  }

  // already?
  if( obj->tt == MRBC_TT_CLASS ) {
    return obj->cls;
  }

  // error.
  // raise TypeError.
  assert( !"TypeError" );
}



//================================================================
/*! get class by name

  @param  name		class name.
  @return		pointer to class object.
*/
mrbc_class * mrbc_get_class_by_name( const char *name )
{
  mrbc_sym sym_id = str_to_symid(name);
  mrbc_object *obj = mrbc_get_const( sym_id );

  if( obj == NULL ) return NULL;
  return (obj->tt == MRBC_TT_CLASS) ? obj->cls : NULL;
}


//================================================================
/*! define class method or instance method.

  @param  vm		pointer to vm.
  @param  cls		pointer to class.
  @param  name		method name.
  @param  cfunc		pointer to function.
*/
void mrbc_define_method(struct VM *vm, mrbc_class *cls, const char *name, mrbc_func_t cfunc)
{
  if( cls == NULL ) cls = mrbc_class_object;	// set default to Object.

  mrbc_proc *proc = (mrbc_proc *)mrbc_alloc(vm, sizeof(mrbc_proc));
  if( !proc ) return;	// ENOMEM

  proc->ref_count = 1;
  proc->c_func = 1;
  proc->sym_id = str_to_symid(name);
  proc->next = cls->procs;
  proc->callinfo = 0;
  proc->func = cfunc;

  cls->procs = proc;
}


// Call a method
// v[0]: receiver
// v[1..]: params
//================================================================
/*! call a method with params

  @param  vm		pointer to vm
  @param  name		method name
  @param  v		receiver and params
  @param  argc		num of params
*/
void mrbc_funcall(struct VM *vm, const char *name, mrbc_value *v, int argc)
{
  mrbc_sym sym_id = str_to_symid(name);
  mrbc_proc *m = find_method(vm, &v[0], sym_id);

  if( m==0 ) return;   // no method

  mrbc_callinfo *callinfo = mrbc_alloc(vm, sizeof(mrbc_callinfo));
  callinfo->current_regs = vm->current_regs;
  callinfo->pc_irep = vm->pc_irep;
  callinfo->n_args = 0;
  callinfo->target_class = vm->target_class;
  callinfo->prev = vm->callinfo_tail;
  vm->callinfo_tail = callinfo;

  // target irep
  vm->pc_irep = m->irep;

  // new regs
  vm->current_regs += 2;   // recv and symbol

}



//================================================================
/*! (BETA) Call any method of the object, but written by C.

  @param  vm		pointer to vm.
  @param  v		see bellow example.
  @param  reg_ofs	see bellow example.
  @param  recv		pointer to receiver.
  @param  method	method name.
  @param  argc		num of params.

  @example
  // (Fixnum).to_s(16)
  static void c_fixnum_to_s(struct VM *vm, mrbc_value v[], int argc)
  {
    mrbc_value *recv = &v[1];
    mrbc_value arg1 = mrbc_fixnum_value(16);
    mrbc_value ret = mrbc_send( vm, v, argc, recv, "to_s", 1, &arg1 );
    SET_RETURN(ret);
  }
 */
mrbc_value mrbc_send( struct VM *vm, mrbc_value *v, int reg_ofs,
		     mrbc_value *recv, const char *method, int argc, ... )
{
  mrbc_sym sym_id = str_to_symid(method);
  mrbc_proc *m = find_method(vm, recv, sym_id);

  if( m == 0 ) {
    console_printf("No method. vtype=%d method='%s'\n", recv->tt, method );
    goto ERROR;
  }
  if( !m->c_func ) {
    console_printf("Method %s is not C function\n", method );
    goto ERROR;
  }

  // create call stack.
  mrbc_value *regs = v + reg_ofs + 2;
  mrbc_release( &regs[0] );
  regs[0] = *recv;
  mrbc_dup(recv);

  va_list ap;
  va_start(ap, argc);
  int i;
  for( i = 1; i <= argc; i++ ) {
    mrbc_release( &regs[i] );
    regs[i] = *va_arg(ap, mrbc_value *);
  }
  mrbc_release( &regs[i] );
  regs[i] = mrbc_nil_value();
  va_end(ap);

  // call method.
  m->func(vm, regs, argc);
  mrbc_value ret = regs[0];

  for(; i >= 0; i-- ) {
    regs[i].tt = MRBC_TT_EMPTY;
  }

  return ret;

 ERROR:
  return mrbc_nil_value();
}



//================================================================
/*! p - sub function
 */
int mrbc_p_sub(const mrbc_value *v)
{
  switch( v->tt ){
  case MRBC_TT_NIL:
    console_print("nil");
    break;

  case MRBC_TT_SYMBOL:{
    const char *s = mrbc_symbol_cstr( v );
    char *fmt = strchr(s, ':') ? "\":%s\"" : ":%s";
    console_printf(fmt, s);
  } break;

#if MRBC_USE_STRING
  case MRBC_TT_STRING:{
    console_putchar('"');
    const unsigned char *s = (const unsigned char *)mrbc_string_cstr(v);
    int i;
    for( i = 0; i < mrbc_string_size(v); i++ ) {
      if( s[i] < ' ' || 0x7f <= s[i] ) {	// tiny isprint()
	console_printf("\\x%02X", s[i]);
      } else {
	console_putchar(s[i]);
      }
    }
    console_putchar('"');
  } break;
#endif

  case MRBC_TT_RANGE:{
    mrbc_value v1 = mrbc_range_first(v);
    mrbc_p_sub(&v1);
    console_print( mrbc_range_exclude_end(v) ? "..." : ".." );
    v1 = mrbc_range_last(v);
    mrbc_p_sub(&v1);
  } break;

  default:
    mrbc_print_sub(v);
    break;
  }

#if 0
  // display reference counter
  if( v->tt >= MRBC_TT_OBJECT ) {
    console_printf("(%d)", v->instance->ref_count);
  }
#endif

  return 0;
}


//================================================================
/*! print - sub function
  @param  v	pointer to target value.
  @retval 0	normal return.
  @retval 1	already output LF.
*/
int mrbc_print_sub(const mrbc_value *v)
{
  int ret = 0;

  switch( v->tt ){
  case MRBC_TT_EMPTY:	console_print("(empty)");	break;
  case MRBC_TT_NIL:					break;
  case MRBC_TT_FALSE:	console_print("false");		break;
  case MRBC_TT_TRUE:	console_print("true");		break;
  case MRBC_TT_FIXNUM:	console_printf("%D", v->i);	break;
#if MRBC_USE_FLOAT
  case MRBC_TT_FLOAT:	console_printf("%g", v->d);	break;
#endif
  case MRBC_TT_SYMBOL:
    console_print(mrbc_symbol_cstr(v));
    break;

  case MRBC_TT_CLASS:
    console_print(symid_to_str(v->cls->sym_id));
    break;

  case MRBC_TT_OBJECT:
    console_printf( "#<%s:%08x>",
	symid_to_str( find_class_by_object(0,v)->sym_id ), v->instance );
    break;

  case MRBC_TT_PROC:
    console_printf( "#<Proc:%08x>", v->proc );
    break;

  case MRBC_TT_ARRAY:{
    console_putchar('[');
    int i;
    for( i = 0; i < mrbc_array_size(v); i++ ) {
      if( i != 0 ) console_print(", ");
      mrbc_value v1 = mrbc_array_get(v, i);
      mrbc_p_sub(&v1);
    }
    console_putchar(']');
  } break;

#if MRBC_USE_STRING
  case MRBC_TT_STRING:
    console_nprint( mrbc_string_cstr(v), mrbc_string_size(v) );
    if( mrbc_string_size(v) != 0 &&
	mrbc_string_cstr(v)[ mrbc_string_size(v) - 1 ] == '\n' ) ret = 1;
    break;
#endif

  case MRBC_TT_RANGE:{
    mrbc_value v1 = mrbc_range_first(v);
    mrbc_print_sub(&v1);
    console_print( mrbc_range_exclude_end(v) ? "..." : ".." );
    v1 = mrbc_range_last(v);
    mrbc_print_sub(&v1);
  } break;

  case MRBC_TT_HASH:{
    console_putchar('{');
    mrbc_hash_iterator ite = mrbc_hash_iterator_new(v);
    while( mrbc_hash_i_has_next(&ite) ) {
      mrbc_value *vk = mrbc_hash_i_next(&ite);
      mrbc_p_sub(vk);
      console_print("=>");
      mrbc_p_sub(vk+1);
      if( mrbc_hash_i_has_next(&ite) ) console_print(", ");
    }
    console_putchar('}');
  } break;

  default:
    console_printf("Not support MRBC_TT_XX(%d)", v->tt);
    break;
  }

  return ret;
}


//================================================================
/*! puts - sub function

  @param  v	pointer to target value.
  @retval 0	normal return.
  @retval 1	already output LF.
*/
int mrbc_puts_sub(const mrbc_value *v)
{
  if( v->tt == MRBC_TT_ARRAY ) {
    int i;
    for( i = 0; i < mrbc_array_size(v); i++ ) {
      if( i != 0 ) console_putchar('\n');
      mrbc_value v1 = mrbc_array_get(v, i);
      mrbc_puts_sub(&v1);
    }
    return 0;
  }

  return mrbc_print_sub(v);
}



//----------------------------------------------------------------
// Object class
//----------------------------------------------------------------

//================================================================
/*! (method) p
 */
static void c_object_p(struct VM *vm, mrbc_value v[], int argc)
{
  int i;
  for( i = 1; i <= argc; i++ ) {
    mrbc_p_sub( &v[i] );
    console_putchar('\n');
  }
}


//================================================================
/*! (method) print
 */
static void c_object_print(struct VM *vm, mrbc_value v[], int argc)
{
  int i;
  for( i = 1; i <= argc; i++ ) {
    mrbc_print_sub( &v[i] );
  }
}


//================================================================
/*! (method) puts
 */
static void c_object_puts(struct VM *vm, mrbc_value v[], int argc)
{
  int i;
  if( argc ){
    for( i = 1; i <= argc; i++ ) {
      if( mrbc_puts_sub( &v[i] ) == 0 ) console_putchar('\n');
    }
  } else {
    console_putchar('\n');
  }
  SET_NIL_RETURN();
}


//================================================================
/*! (operator) !
 */
static void c_object_not(struct VM *vm, mrbc_value v[], int argc)
{
  SET_BOOL_RETURN( v[0].tt == MRBC_TT_NIL || v[0].tt == MRBC_TT_FALSE );
}


//================================================================
/*! (operator) !=
 */
static void c_object_neq(struct VM *vm, mrbc_value v[], int argc)
{
  int result = mrbc_compare( &v[0], &v[1] );
  SET_BOOL_RETURN( result != 0 );
}


//================================================================
/*! (operator) <=>
 */
static void c_object_compare(struct VM *vm, mrbc_value v[], int argc)
{
  int result = mrbc_compare( &v[0], &v[1] );
  SET_INT_RETURN( result );
}


//================================================================
/*! (operator) ===
 */
static void c_object_equal3(struct VM *vm, mrbc_value v[], int argc)
{
  int result;

  if( v[0].tt == MRBC_TT_CLASS ) {
    result = mrbc_obj_is_kind_of( &v[1], v[0].cls );
  } else {
    result = (mrbc_compare( &v[0], &v[1] ) == 0);
  }

  SET_BOOL_RETURN( result );
}


//================================================================
/*! (method) class
 */
static void c_object_class(struct VM *vm, mrbc_value v[], int argc)
{
  mrbc_value value = {.tt = MRBC_TT_CLASS};
  value.cls = find_class_by_object( vm, v );
  SET_RETURN( value );
}


//================================================================
/*! (method) new
 */
static void c_object_new(struct VM *vm, mrbc_value v[], int argc)
{
  mrbc_value new_obj = mrbc_instance_new(vm, v->cls, 0);

  char syms[]="______initialize";
  mrbc_sym sym_id = str_to_symid(&syms[6]);
  mrbc_proc *m = find_method(vm, &v[0], sym_id);
  if( m==0 ){
    SET_RETURN(new_obj);
    return;
  }
  uint32_to_bin( 1,(uint8_t*)&syms[0]);
  uint16_to_bin(10,(uint8_t*)&syms[4]);

  uint8_t code[] = {
    OP_SEND, 0, 0, argc,
    OP_ABORT,
  };
  mrbc_irep irep = {
    0,     // nlocals
    0,     // nregs
    0,     // rlen
    sizeof(code)/sizeof(uint8_t),     // ilen
    0,     // plen
    (uint8_t *)code,   // code
    NULL,  // pools
    (uint8_t *)syms,  // ptr_to_sym
    NULL,  // reps
  };

  mrbc_class *cls = v->cls;

  mrbc_release(&v[0]);
  v[0] = new_obj;
  mrbc_dup(&new_obj);

  mrbc_irep *org_pc_irep = vm->pc_irep;
  mrbc_value* org_regs = vm->current_regs;
  uint8_t *org_inst = vm->inst;

  vm->pc_irep = &irep;
  vm->current_regs = v;
  vm->inst = irep.code;

  while( mrbc_vm_run(vm) == 0 )
    ;

  vm->pc_irep = org_pc_irep;
  vm->inst = org_inst;
  vm->current_regs = org_regs;

  new_obj.instance->cls = cls;

  SET_RETURN( new_obj );

  return;
}


//================================================================
/*! (method) dup
 */
static void c_object_dup(struct VM *vm, mrbc_value v[], int argc)
{
  if( v->tt == MRBC_TT_OBJECT ) {
    mrbc_value new_obj = mrbc_instance_new(vm, v->instance->cls, 0);
    mrbc_kv_dup( &v->instance->ivar, &new_obj.instance->ivar );

    mrbc_release( v );
    *v = new_obj;
    return;
  }


  // TODO: need support TT_PROC and TT_RANGE. but really need?
  return;
}


//================================================================
/*! (method) instance variable getter
 */
static void c_object_getiv(struct VM *vm, mrbc_value v[], int argc)
{
  const char *name = mrbc_get_callee_name(vm);
  mrbc_sym sym_id = str_to_symid( name );
  mrbc_value ret = mrbc_instance_getiv(&v[0], sym_id);

  SET_RETURN(ret);
}


//================================================================
/*! (method) instance variable setter
 */
static void c_object_setiv(struct VM *vm, mrbc_value v[], int argc)
{
  const char *name = mrbc_get_callee_name(vm);

  char *namebuf = mrbc_alloc(vm, strlen(name));
  if( !namebuf ) return;
  strcpy(namebuf, name);
  namebuf[strlen(name)-1] = '\0';	// delete '='
  mrbc_sym sym_id = str_to_symid(namebuf);

  mrbc_instance_setiv(&v[0], sym_id, &v[1]);
  mrbc_raw_free(namebuf);
}


//================================================================
/*! (class method) access method 'attr_reader'
 */
static void c_object_attr_reader(struct VM *vm, mrbc_value v[], int argc)
{
  int i;
  for( i = 1; i <= argc; i++ ) {
    if( v[i].tt != MRBC_TT_SYMBOL ) continue;	// TypeError raise?

    // define reader method
    const char *name = mrbc_symbol_cstr(&v[i]);
    mrbc_define_method(vm, v[0].cls, name, c_object_getiv);
  }
}


//================================================================
/*! (class method) access method 'attr_accessor'
 */
static void c_object_attr_accessor(struct VM *vm, mrbc_value v[], int argc)
{
  int i;
  for( i = 1; i <= argc; i++ ) {
    if( v[i].tt != MRBC_TT_SYMBOL ) continue;	// TypeError raise?

    // define reader method
    const char *name = mrbc_symbol_cstr(&v[i]);
    mrbc_define_method(vm, v[0].cls, name, c_object_getiv);

    // make string "....=" and define writer method.
    char *namebuf = mrbc_alloc(vm, strlen(name)+2);
    if( !namebuf ) return;
    strcpy(namebuf, name);
    strcat(namebuf, "=");
    mrbc_symbol_new(vm, namebuf);
    mrbc_define_method(vm, v[0].cls, namebuf, c_object_setiv);
    mrbc_raw_free(namebuf);
  }
}


//================================================================
/*! (method) is_a, kind_of
 */
static void c_object_kind_of(struct VM *vm, mrbc_value v[], int argc)
{
  int result = 0;
  if( v[1].tt != MRBC_TT_CLASS ) goto DONE;

  result = mrbc_obj_is_kind_of( &v[0], v[1].cls );

 DONE:
  SET_BOOL_RETURN( result );
}


//================================================================
/*! (method) nil?
 */
static void c_object_nil(struct VM *vm, mrbc_value v[], int argc)
{
  SET_BOOL_RETURN( v[0].tt == MRBC_TT_NIL );
}



//================================================================
/*! (method) block_given?
 */
static void c_object_block_given(struct VM *vm, mrbc_value v[], int argc)
{
  mrbc_callinfo *callinfo = vm->callinfo_tail;
  if( !callinfo ) goto RETURN_FALSE;

  mrbc_value *regs = callinfo->current_regs + callinfo->reg_offset;

  if( regs[0].tt == MRBC_TT_PROC ) {
    callinfo = regs[0].proc->callinfo_self;
    if( !callinfo ) goto RETURN_FALSE;

    regs = callinfo->current_regs + callinfo->reg_offset;
  }

  SET_BOOL_RETURN( regs[callinfo->n_args].tt == MRBC_TT_PROC );
  return;

 RETURN_FALSE:
  SET_FALSE_RETURN();
}


//================================================================
/*! (method) raise
 *    1. raise
 *    2. raise "param"
 *    3. raise Exception
 *    4. raise Exception, "param"
 */
static void c_object_raise(struct VM *vm, mrbc_value v[], int argc)
{
  if( !vm->exc ){
    // raise exception
    if( argc == 0 ){
      // 1. raise
      vm->exc = mrbc_class_runtimeerror;
      vm->exc_message = mrbc_nil_value();
    } else if( argc == 1 ){
      if( v[1].tt == MRBC_TT_CLASS ){
	// 3. raise Exception
	vm->exc = v[1].cls;
	const char *s = symid_to_str( v[1].cls->sym_id );
	vm->exc_message = mrbc_nil_value();
      } else {
	// 2. raise "param"
	mrbc_dup( &v[1] );
	vm->exc = mrbc_class_runtimeerror;
	vm->exc_message = v[1];
      }
    } else if( argc == 2 ){
      // 4. raise Exception, "param"
      mrbc_dup( &v[2] );
      vm->exc = v[1].cls;
      vm->exc_message = v[2];
    }
  } else {
    // in exception
  }

  // do nothing if no rescue, no ensure
  if( vm->exception_tail == NULL ){
    return;
  }

  // NOT to return to OP_SEND
  mrbc_pop_callinfo(vm);

  mrbc_callinfo *callinfo = vm->exception_tail;
  if( callinfo != NULL ){
    if( callinfo->method_id == 0x7fff ){
      // "rescue"
      // jump to rescue
      vm->exception_tail = callinfo->prev;
      vm->current_regs = callinfo->current_regs;
      vm->pc_irep = callinfo->pc_irep;
      vm->inst = callinfo->inst;
      vm->target_class = callinfo->target_class;
      mrbc_free(vm, callinfo);
      callinfo = vm->exception_tail;
    } else {
      // "ensure"
      // jump to ensure
      vm->exception_tail = callinfo->prev;
      vm->current_regs = callinfo->current_regs;
      vm->pc_irep = callinfo->pc_irep;
      vm->inst = callinfo->inst;
      vm->target_class = callinfo->target_class;
      mrbc_free(vm, callinfo);
      //
      callinfo = vm->exception_tail;
      if( callinfo != NULL ){
	vm->exception_tail = callinfo->prev;
	callinfo->prev = vm->callinfo_tail;
	vm->callinfo_tail = callinfo;
      }
    }
  }
  if( callinfo == NULL ){
    vm->exc_pending = vm->exc;
    vm->exc = 0;
  }
}




#if MRBC_USE_STRING
//================================================================
/*! (method) to_s
 */
static void c_object_to_s(struct VM *vm, mrbc_value v[], int argc)
{
  char buf[32];
  const char *s = buf;

  switch( v->tt ) {
  case MRBC_TT_CLASS:
    s = symid_to_str( v->cls->sym_id );
    break;

  case MRBC_TT_OBJECT:{
    // (NOTE) address part assumes 32bit. but enough for this.
    mrbc_printf pf;

    mrbc_printf_init( &pf, buf, sizeof(buf), "#<%s:%08x>" );
    while( mrbc_printf_main( &pf ) > 0 ) {
      switch(pf.fmt.type) {
      case 's':
	mrbc_printf_str( &pf, symid_to_str(v->instance->cls->sym_id), ' ' );
	break;
      case 'x':
	mrbc_printf_int( &pf, (uint32_t)v->instance, 16 );
	break;
      }
    }
    mrbc_printf_end( &pf );
  } break;

  default:
    s = "";
    break;
  }

  SET_RETURN( mrbc_string_new_cstr( vm, s ) );
}
#endif


#ifdef MRBC_DEBUG
static void c_object_object_id(struct VM *vm, mrbc_value v[], int argc)
{
  // tiny implementation.
  SET_INT_RETURN( GET_INT_ARG(0) );
}


static void c_object_instance_methods(struct VM *vm, mrbc_value v[], int argc)
{
  // TODO: check argument.

  // temporary code for operation check.
  console_printf( "[" );
  int flag_first = 1;

  mrbc_class *cls = find_class_by_object( vm, v );
  mrbc_proc *proc = cls->procs;
  while( proc ) {
    console_printf( "%s:%s", (flag_first ? "" : ", "),
		    symid_to_str(proc->sym_id) );
    flag_first = 0;
    proc = proc->next;
  }

  console_printf( "]" );

  SET_NIL_RETURN();
}


static void c_object_instance_variables(struct VM *vm, mrbc_value v[], int argc)
{
  // temporary code for operation check.
#if 1
  mrbc_kv_handle *kvh = &v[0].instance->ivar;

  console_printf( "n = %d/%d ", kvh->n_stored, kvh->data_size );
  console_printf( "[" );

  int i;
  for( i = 0; i < kvh->n_stored; i++ ) {
    console_printf( "%s:@%s", (i == 0 ? "" : ", "),
		    symid_to_str( kvh->data[i].sym_id ));
  }

  console_printf( "]\n" );
#endif
  SET_NIL_RETURN();
}


#if !defined(MRBC_ALLOC_LIBC)
static void c_object_memory_statistics(struct VM *vm, mrbc_value v[], int argc)
{
  int total, used, free, frag;
  mrbc_alloc_statistics(&total, &used, &free, &frag);

  console_printf("Memory Statistics\n");
  console_printf("  Total: %d\n", total);
  console_printf("  Used : %d\n", used);
  console_printf("  Free : %d\n", free);
  console_printf("  Frag.: %d\n", frag);

  SET_NIL_RETURN();
}
#endif

#endif


//================================================================
/*! Object class
*/
static void mrbc_init_class_object(struct VM *vm)
{
  // Class
  mrbc_class_object = mrbc_define_class(vm, "Object", 0);
  mrbc_class_object->super = 0;		// for in case of repeatedly called.

  // Methods
  mrbc_define_method(vm, mrbc_class_object, "p", c_object_p);
  mrbc_define_method(vm, mrbc_class_object, "print", c_object_print);
  mrbc_define_method(vm, mrbc_class_object, "puts", c_object_puts);
  mrbc_define_method(vm, mrbc_class_object, "!", c_object_not);
  mrbc_define_method(vm, mrbc_class_object, "!=", c_object_neq);
  mrbc_define_method(vm, mrbc_class_object, "<=>", c_object_compare);
  mrbc_define_method(vm, mrbc_class_object, "===", c_object_equal3);
  mrbc_define_method(vm, mrbc_class_object, "class", c_object_class);
  mrbc_define_method(vm, mrbc_class_object, "new", c_object_new);
  mrbc_define_method(vm, mrbc_class_object, "dup", c_object_dup);
  mrbc_define_method(vm, mrbc_class_object, "attr_reader", c_object_attr_reader);
  mrbc_define_method(vm, mrbc_class_object, "attr_accessor", c_object_attr_accessor);
  mrbc_define_method(vm, mrbc_class_object, "is_a?", c_object_kind_of);
  mrbc_define_method(vm, mrbc_class_object, "kind_of?", c_object_kind_of);
  mrbc_define_method(vm, mrbc_class_object, "nil?", c_object_nil);
  mrbc_define_method(vm, mrbc_class_object, "block_given?", c_object_block_given);
  mrbc_define_method(vm, mrbc_class_object, "raise", c_object_raise);

#if MRBC_USE_STRING
  mrbc_define_method(vm, mrbc_class_object, "inspect", c_object_to_s);
  mrbc_define_method(vm, mrbc_class_object, "to_s", c_object_to_s);
#endif

#ifdef MRBC_DEBUG
  mrbc_define_method(vm, mrbc_class_object, "object_id", c_object_object_id);
  mrbc_define_method(vm, mrbc_class_object, "instance_methods", c_object_instance_methods);
  mrbc_define_method(vm, mrbc_class_object, "instance_variables", c_object_instance_variables);
#if !defined(MRBC_ALLOC_LIBC)
  mrbc_define_method(vm, mrbc_class_object, "memory_statistics", c_object_memory_statistics);
#endif

#endif
}



//----------------------------------------------------------------
// Proc class
//----------------------------------------------------------------

//================================================================
/*! constructor

  @param  vm		Pointer to VM.
  @param  irep		Pointer to IREP.
  @return		mrbc_value of Proc object.
*/
mrbc_value mrbc_proc_new(struct VM *vm, void *irep )
{
  mrbc_value val = {.tt = MRBC_TT_PROC};

  val.proc = (mrbc_proc *)mrbc_alloc(vm, sizeof(mrbc_proc));
  if( !val.proc ) return val;	// ENOMEM

  val.proc->ref_count = 1;
  val.proc->c_func = 0;
  val.proc->sym_id = -1;
  val.proc->next = 0;
  val.proc->callinfo = vm->callinfo_tail;

  if(vm->current_regs[0].tt == MRBC_TT_PROC) {
    val.proc->callinfo_self = vm->current_regs[0].proc->callinfo_self;
  } else {
    val.proc->callinfo_self = vm->callinfo_tail;
  }

  val.proc->irep = irep;

  return val;
}


//================================================================
/*! mrbc_instance destructor

  @param  val	pointer to target value
*/
void mrbc_proc_delete(mrbc_value *val)
{
  mrbc_raw_free(val->proc);
}


//================================================================
/*! (method) new
*/
static void c_proc_new(struct VM *vm, mrbc_value v[], int argc)
{
  if( v[1].tt != MRBC_TT_PROC ) {
    console_printf("Not support Proc.new without block.\n");	// raise?
    return;
  }

  v[0] = v[1];
  v[1].tt = MRBC_TT_EMPTY;
}


//================================================================
/*! (method) call
*/
void c_proc_call(struct VM *vm, mrbc_value v[], int argc)
{
  assert( v[0].tt == MRBC_TT_PROC );

  mrbc_callinfo *callinfo_self = v[0].proc->callinfo_self;
  mrbc_callinfo *callinfo = mrbc_push_callinfo(vm,
				(callinfo_self ? callinfo_self->method_id : 0),
				v - vm->current_regs, argc);
  if( !callinfo ) return;

  if( callinfo_self ) {
    callinfo->own_class = callinfo_self->own_class;
  }

  // target irep
  vm->pc_irep = v[0].proc->irep;
  vm->inst = vm->pc_irep->code;

  vm->current_regs = v;
}


#if MRBC_USE_STRING
//================================================================
/*! (method) to_s
*/
static void c_proc_to_s(struct VM *vm, mrbc_value v[], int argc)
{
  // (NOTE) address part assumes 32bit. but enough for this.
  char buf[32];
  mrbc_printf pf;

  mrbc_printf_init( &pf, buf, sizeof(buf), "#<Proc:%08x>" );
  while( mrbc_printf_main( &pf ) > 0 ) {
    mrbc_printf_int( &pf, (uint32_t)v->proc, 16 );
  }
  mrbc_printf_end( &pf );

  SET_RETURN( mrbc_string_new_cstr( vm, buf ) );
}
#endif


//================================================================
/*! Proc class
*/
static void mrbc_init_class_proc(struct VM *vm)
{
  // Class
  mrbc_class_proc= mrbc_define_class(vm, "Proc", mrbc_class_object);
  // Methods
  mrbc_define_method(vm, mrbc_class_proc, "call", c_proc_call);
  mrbc_define_method(vm, mrbc_class_proc, "new", c_proc_new);
#if MRBC_USE_STRING
  mrbc_define_method(vm, mrbc_class_proc, "inspect", c_proc_to_s);
  mrbc_define_method(vm, mrbc_class_proc, "to_s", c_proc_to_s);
#endif
}



//----------------------------------------------------------------
// Nil class
//----------------------------------------------------------------

//================================================================
/*! (method) to_i
*/
static void c_nil_to_i(struct VM *vm, mrbc_value v[], int argc)
{
  v[0] = mrbc_fixnum_value(0);
}


//================================================================
/*! (method) to_a
*/
static void c_nil_to_a(struct VM *vm, mrbc_value v[], int argc)
{
  v[0] = mrbc_array_new(vm, 0);
}


//================================================================
/*! (method) to_h
*/
static void c_nil_to_h(struct VM *vm, mrbc_value v[], int argc)
{
  v[0] = mrbc_hash_new(vm, 0);
}


#if MRBC_USE_FLOAT
//================================================================
/*! (method) to_f
*/
static void c_nil_to_f(struct VM *vm, mrbc_value v[], int argc)
{
  v[0] = mrbc_float_value(0);
}
#endif


#if MRBC_USE_STRING
//================================================================
/*! (method) inspect
*/
static void c_nil_inspect(struct VM *vm, mrbc_value v[], int argc)
{
  v[0] = mrbc_string_new_cstr(vm, "nil");
}


//================================================================
/*! (method) to_s
*/
static void c_nil_to_s(struct VM *vm, mrbc_value v[], int argc)
{
  v[0] = mrbc_string_new(vm, NULL, 0);
}
#endif

//================================================================
/*! Nil class
*/
static void mrbc_init_class_nil(struct VM *vm)
{
  // Class
  mrbc_class_nil = mrbc_define_class(vm, "NilClass", mrbc_class_object);

  // Methods
  mrbc_define_method(vm, mrbc_class_nil, "to_i", c_nil_to_i);
  mrbc_define_method(vm, mrbc_class_nil, "to_a", c_nil_to_a);
  mrbc_define_method(vm, mrbc_class_nil, "to_h", c_nil_to_h);

#if MRBC_USE_FLOAT
  mrbc_define_method(vm, mrbc_class_nil, "to_f", c_nil_to_f);
#endif
#if MRBC_USE_STRING
  mrbc_define_method(vm, mrbc_class_nil, "inspect", c_nil_inspect);
  mrbc_define_method(vm, mrbc_class_nil, "to_s", c_nil_to_s);
#endif
}



//----------------------------------------------------------------
// False class
//----------------------------------------------------------------

#if MRBC_USE_STRING
//================================================================
/*! (method) to_s
*/
static void c_false_to_s(struct VM *vm, mrbc_value v[], int argc)
{
  v[0] = mrbc_string_new_cstr(vm, "false");
}
#endif

//================================================================
/*! False class
*/
static void mrbc_init_class_false(struct VM *vm)
{
  // Class
  mrbc_class_false = mrbc_define_class(vm, "FalseClass", mrbc_class_object);
  // Methods
#if MRBC_USE_STRING
  mrbc_define_method(vm, mrbc_class_false, "inspect", c_false_to_s);
  mrbc_define_method(vm, mrbc_class_false, "to_s", c_false_to_s);
#endif
}



//----------------------------------------------------------------
// True class
//----------------------------------------------------------------

#if MRBC_USE_STRING
//================================================================
/*! (method) to_s
*/
static void c_true_to_s(struct VM *vm, mrbc_value v[], int argc)
{
  v[0] = mrbc_string_new_cstr(vm, "true");
}
#endif


//================================================================
/*! True class
*/
static void mrbc_init_class_true(struct VM *vm)
{
  // Class
  mrbc_class_true = mrbc_define_class(vm, "TrueClass", mrbc_class_object);
  // Methods
#if MRBC_USE_STRING
  mrbc_define_method(vm, mrbc_class_true, "inspect", c_true_to_s);
  mrbc_define_method(vm, mrbc_class_true, "to_s", c_true_to_s);
#endif
}



//================================================================
/*! Ineffect operator / method
*/
void c_ineffect(struct VM *vm, mrbc_value v[], int argc)
{
  // nothing to do.
}


//================================================================
/*! Run mrblib, which is mruby bytecode
*/
void mrbc_run_mrblib(const uint8_t bytecode[])
{
  // instead of mrbc_vm_open()
  mrbc_vm *vm = mrbc_alloc( 0, sizeof(mrbc_vm) );
  if( !vm ) return;	// ENOMEM
  memset(vm, 0, sizeof(mrbc_vm));

  mrbc_load_mrb(vm, bytecode);
  mrbc_vm_begin(vm);
  mrbc_vm_run(vm);

  // not necessary to call mrbc_vm_end()

  // instead of mrbc_vm_close()
  mrbc_raw_free( vm );
}




//================================================================
// initialize

void mrbc_init_class(void)
{
  extern const uint8_t mrblib_bytecode[];

  mrbc_init_class_object(0);
  mrbc_init_class_nil(0);
  mrbc_init_class_proc(0);
  mrbc_init_class_false(0);
  mrbc_init_class_true(0);

  mrbc_init_class_fixnum(0);
  mrbc_init_class_symbol(0);
#if MRBC_USE_FLOAT
  mrbc_init_class_float(0);
#if MRBC_USE_MATH
  mrbc_init_class_math(0);
#endif
#endif
#if MRBC_USE_STRING
  mrbc_init_class_string(0);
#endif
  mrbc_init_class_array(0);
  mrbc_init_class_range(0);
  mrbc_init_class_hash(0);

  mrbc_init_class_exception(0);

  mrbc_run_mrblib(mrblib_bytecode);
}
