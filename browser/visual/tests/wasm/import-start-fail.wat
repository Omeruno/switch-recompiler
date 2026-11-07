(module
 (import "env" "memory" (memory 1 4))
 (import "env" "boom" (func $boom))
 (func $start i32.const 256 i32.const 85 i32.store8 i32.const 0 memory.grow drop call $boom)
 (start $start))
