(module (memory 1 2 shared)
 (func (export "run") (result i32)
  i32.const 0 i32.const 0 i64.const 0 memory.atomic.wait32))
