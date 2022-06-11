#include "mrubyc.h"

#include "master.h"
#include "slave.h" 

#include "mrbc_greeter.h"

#define MEMORY_SIZE (1024*40)

static uint8_t memory_pool[MEMORY_SIZE];

static void
ruby_hello2(mrb_vm* vm, mrb_value* v, int argc)
{
  printf("Hello, world! ver.2\n");
}

void app_main(struct VM* vm) {

  //メモリ読み込み
  mrbc_init(memory_pool, MEMORY_SIZE);

  // C 側のクラス・メソッドの定義
  mrbc_greeter_gem_init(0);

  // Ruby 側のクラス・メソッド定義
  extern const uint8_t my_mrblib_bytecode[];
  mrbc_define_method(0, mrbc_class_object, "c_greet", ruby_hello2);

  // クラスの追加
  mrbc_run_mrblib(my_mrblib_bytecode); 
  
  //メインプログラムの追加
  mrbc_create_task( master, 0 );
  mrbc_create_task( slave, 0 ); 
  mrbc_run();  
}
