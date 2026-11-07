;; Owned companion module: observe the same memory and score as the game core.
(module
 (import "env" "memory" (memory 1 1))
 (import "env" "score" (global $score (mut i32)))
 (func (export "score") (result i32) global.get $score)
 (func (export "pixel") (param i32 i32) (result i32)
  local.get 1 i32.const 128 i32.mul local.get 0 i32.add i32.const 4 i32.mul i32.load))
