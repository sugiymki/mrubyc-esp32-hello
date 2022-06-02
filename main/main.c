#include "mrubyc.h"
//#include "models/greeter.h"      //[杉山] コメントアウト
//#include "loops/master.h"    //[杉山] コメントアウト

#define MEMORY_SIZE (1024*40)

static uint8_t memory_pool[MEMORY_SIZE];

void app_main(void) {
  extern const uint8_t my_mrblib_bytecode[]; //[杉山] 追加

  mrbc_init(memory_pool, MEMORY_SIZE);
  
  //mrbc_create_task( greeter, 0 );   //[杉山] コメントアウト
  //mrbc_create_task( master, 0 );    //[杉山] コメントアウト
  //mrbc_run();                       //[杉山] コメントアウト

  mrbc_run_mrblib(my_mrblib_bytecode); //[杉山] 追加
}
