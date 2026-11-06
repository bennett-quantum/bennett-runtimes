; Exact aggregate and pointer signatures, with field addresses determined by
; the link's DataLayout. Unaligned supplied record storage remains legal.
@__bennett_rt_eh = external global { i32, i32, ptr }, align 8

define void @__bennett_rt_eh_resume({ ptr, i32 } %pair) {
  %object = extractvalue { ptr, i32 } %pair, 0
  %selector = extractvalue { ptr, i32 } %pair, 1
  %s = getelementptr { i32, i32, ptr }, ptr @__bennett_rt_eh, i32 0, i32 1
  %o = getelementptr { i32, i32, ptr }, ptr @__bennett_rt_eh, i32 0, i32 2
  store ptr %object, ptr %o, align 1
  store i32 %selector, ptr %s, align 1
  store i32 1, ptr @__bennett_rt_eh, align 1
  ret void
}
define void @__bennett_rt_setjmp(ptr %buffer, ptr %frame, i32 %site) {
  %s = getelementptr { ptr, i32 }, ptr %buffer, i32 0, i32 1
  store ptr %frame, ptr %buffer, align 1
  store i32 %site, ptr %s, align 1
  ret void
}
define void @__bennett_rt_longjmp(ptr %buffer, i32 %value) {
  %zero = icmp eq i32 %value, 0
  %v = select i1 %zero, i32 1, i32 %value
  %s = getelementptr { i32, i32, ptr }, ptr @__bennett_rt_eh, i32 0, i32 1
  %o = getelementptr { i32, i32, ptr }, ptr @__bennett_rt_eh, i32 0, i32 2
  store ptr %buffer, ptr %o, align 1
  store i32 %v, ptr %s, align 1
  store i32 2, ptr @__bennett_rt_eh, align 1
  ret void
}
define void @__bennett_rt_tramp_init(ptr %buffer, ptr %entry, ptr %nest) {
  %f = getelementptr { i32, ptr, ptr }, ptr %buffer, i32 0, i32 1
  %n = getelementptr { i32, ptr, ptr }, ptr %buffer, i32 0, i32 2
  store i32 1111838292, ptr %buffer, align 1
  store ptr %entry, ptr %f, align 1
  store ptr %nest, ptr %n, align 1
  ret void
}
