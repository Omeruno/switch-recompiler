(module (memory 1 4 shared)
 (func (export "run") (result i32) i32.const 1 memory.grow drop memory.size))
