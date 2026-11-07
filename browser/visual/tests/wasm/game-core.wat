;; Owned test game. No third-party game code or ROM data.
(module
 (import "env" "memory" (memory 1 1))
 (import "env" "score" (global $score (mut i32)))
 (import "env" "observe" (func $observe (result i32)))
 (export "memory" (memory 0))
 (export "score" (global $score))
 (global $x (mut i32) (i32.const 64))
 (global $y (mut i32) (i32.const 40))
 (global $cx (mut i32) (i32.const 95))
 (global $cy (mut i32) (i32.const 20))
 (global $tick (mut i32) (i32.const 0))
 (func (export "reset")
  i32.const 64 global.set $x i32.const 40 global.set $y
  i32.const 95 global.set $cx i32.const 20 global.set $cy
  i32.const 0 global.set $score i32.const 0 global.set $tick)
 (func $near (param i32 i32 i32) (result i32)
  local.get 0 local.get 1 i32.sub local.get 2 i32.add
  local.get 2 i32.const 2 i32.mul i32.le_u)
 (func (export "step") (param $dx i32) (param $dy i32) (param $boost i32) (param $tx i32) (param $ty i32) (param $touch i32) (result i32)
  (local $i i32) (local $px i32) (local $py i32) (local $color i32)
  global.get $tick i32.const 1 i32.add global.set $tick
  local.get $touch
  if local.get $tx global.set $x local.get $ty global.set $y
  else
   global.get $x local.get $dx local.get $boost i32.const 1 i32.add i32.mul i32.add global.set $x
   global.get $y local.get $dy local.get $boost i32.const 1 i32.add i32.mul i32.add global.set $y
  end
  global.get $x i32.const 0 i32.lt_s if i32.const 0 global.set $x end
  global.get $y i32.const 0 i32.lt_s if i32.const 0 global.set $y end
  global.get $x i32.const 127 i32.gt_s if i32.const 127 global.set $x end
  global.get $y i32.const 79 i32.gt_s if i32.const 79 global.set $y end
  global.get $x global.get $cx i32.const 5 call $near
  global.get $y global.get $cy i32.const 5 call $near i32.and
  if
   global.get $score i32.const 1 i32.add global.set $score
   call $observe drop
   global.get $cx i32.const 37 i32.add i32.const 110 i32.rem_u i32.const 8 i32.add global.set $cx
   global.get $cy i32.const 23 i32.add i32.const 62 i32.rem_u i32.const 8 i32.add global.set $cy
  end
  (loop $pixels
   local.get $i i32.const 127 i32.and local.set $px
   local.get $i i32.const 7 i32.shr_u local.set $py
   i32.const 0xff20150c local.set $color
   local.get $px i32.const 16 i32.rem_u i32.eqz
   local.get $py i32.const 16 i32.rem_u i32.eqz i32.or
   if i32.const 0xff35271a local.set $color end
   local.get $px global.get $cx i32.const 3 call $near
   local.get $py global.get $cy i32.const 3 call $near i32.and
   if i32.const 0xff48dfff local.set $color end
   local.get $px global.get $x i32.const 3 call $near
   local.get $py global.get $y i32.const 3 call $near i32.and
   if i32.const 0xffffd03a local.set $color end
   local.get $i i32.const 4 i32.mul local.get $color i32.store
   local.get $i i32.const 1 i32.add local.tee $i i32.const 10240 i32.lt_u br_if $pixels)
  i32.const 0)
)
