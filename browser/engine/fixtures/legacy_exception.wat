(module (tag $error (param i32))
 (func (export "run") (result i32)
  (try (result i32) (do i32.const 42 throw $error) (catch $error))))
