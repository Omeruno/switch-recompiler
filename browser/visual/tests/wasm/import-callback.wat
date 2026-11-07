(module
 (import "env" "memory" (memory 1 4))
 (import "env" "notify" (func $notify))
 (func (export "probe") (param i32) (result i32) call $notify local.get 0 i32.load8_u)
 (func (export "grow") (param i32) (result i32) local.get 0 memory.grow))
