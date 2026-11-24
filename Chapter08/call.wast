(module

  (func $const-i32 (result i32) (i32.const 0x132))
  (func $const-i64 (result i64) (i64.const 0x164))

  (func (export "type-i32") (result i32) (call $const-i32))
  (func (export "type-i64") (result i64) (call $const-i64))
 )


(assert_return (invoke "type-i32") (i32.const 0x132))
(assert_return (invoke "type-i64") (i64.const 0x164))
