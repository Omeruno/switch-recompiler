;; Own micro-module reproducing the client's memory limits, not its code.
(module
 (memory (export "memory") 266 65536)
 (func (export "run") (result i32)
  ;; Initial memory and last word must be accessible.
  i32.const 0 i32.const 305419896 i32.store
  i32.const 17432572 i32.const 11259375 i32.store
  ;; Exceed the port's 256 MiB physical backing quota. Failure must be safe.
  i32.const 4096 memory.grow i32.const -1 i32.ne if unreachable end
  memory.size i32.const 266 i32.ne if unreachable end
  ;; Grow by 1 MiB without moving the memory.
  i32.const 16 memory.grow i32.const 266 i32.ne if unreachable end
  i32.const 0 i32.load i32.const 305419896 i32.ne if unreachable end
  i32.const 17432572 i32.load i32.const 11259375 i32.ne if unreachable end
  i32.const 17432576 i32.load i32.eqz i32.eqz if unreachable end
  i32.const 18481148 i32.load i32.eqz i32.eqz if unreachable end
  i32.const 18481148 i32.const 42 i32.store
  i32.const 18481148 i32.load i32.const 42 i32.ne if unreachable end
  memory.size))
