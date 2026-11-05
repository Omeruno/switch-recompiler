(module
 (memory (export "memory") 1 4 shared)
 (func (export "run") (result i32)
  i32.const 0 i32.const 7 i32.atomic.store
  i32.const 0 i32.const 5 i32.atomic.rmw.add drop
  i32.const 0 i32.atomic.load)
 (func (export "increment") (param $n i32)
  (block $done (loop $again
   local.get $n i32.eqz br_if $done
   i32.const 0 i32.const 1 i32.atomic.rmw.add drop
   local.get $n i32.const 1 i32.sub local.set $n br $again)))
 (func (export "read") (result i32) i32.const 0 i32.atomic.load))
