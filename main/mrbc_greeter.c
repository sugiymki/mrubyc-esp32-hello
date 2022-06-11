#include "mrubyc.h"

static struct RClass* mrbc_class_esp32_greeter;

/* C 言語での "Hello World"*/
void c_hello(){
  printf("Hello, world!\n");
}

/* ラッパープログラム */
static void
ruby_hello(mrb_vm* vm, mrb_value* v, int argc)
{
  c_hello();  //別プログラムを呼び出す
}

void
mrbc_greeter_gem_init(struct VM* vm)
{
  mrbc_class_esp32_greeter = mrbc_define_class(vm, "Greeter", mrbc_class_object);
  mrbc_define_method(vm, mrbc_class_esp32_greeter, "greet", ruby_hello);
}
