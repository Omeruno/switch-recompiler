(module (memory 1) (data $data "\2a\00\00\00")
 (func (export "run") (result i32)
  i32.const 0 i32.const 0 i32.const 4 memory.init $data
  data.drop $data i32.const 0 i32.load))
