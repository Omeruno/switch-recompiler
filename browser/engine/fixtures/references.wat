(module (table 1 externref)
 (func (export "run") (result i32)
  i32.const 0 ref.null extern table.set 0
  i32.const 0 table.get 0 ref.is_null))
