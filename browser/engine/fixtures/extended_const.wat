(module (global $answer i32 (i32.add (i32.const 40) (i32.const 2)))
 (func (export "run") (result i32) global.get $answer))
