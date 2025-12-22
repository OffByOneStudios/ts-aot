.\build\src\compiler\Debug\ts-aot.exe : Creating prototype: __module_init_3937545542101398452 with 1 args
At line:1 char:156
+ ... oding utf8; .\build\src\compiler\Debug\ts-aot.exe examples/bce_range_ ...
+                 ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    + CategoryInfo          : NotSpecified: (Creating protot...452 with 1 args:String) [], RemoteException
    + FullyQualifiedErrorId : NativeCommandError
 
Creating prototype: user_main with 1 args
Creating prototype: testRange_Float64Array with 2 args
Generating body for: __module_init_3937545542101398452 isAsync: 0
visitCallExpression
Generating body for: user_main isAsync: 0
visitCallExpression
Generating body for: testRange_Float64Array isAsync: 0
visitBinaryExpression done
visitBinaryExpression done
visitPostfixUnaryExpression: ++
  Storing back to i type: ptr value type: double
visitPostfixUnaryExpression done
; ModuleID = 'ts_module'
source_filename = "ts_module"
target datalayout = "e-m:w-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-windows-msvc"

%DataView_VTable = type { ptr, ptr, ptr }
%Uint8Array_VTable = type { ptr }
%Uint32Array_VTable = type { ptr }
%Float64Array_VTable = type { ptr }
%Buffer_VTable = type { ptr, ptr }
%Response_VTable = type { ptr, ptr, ptr }
%URL_VTable = type { ptr }
%IncomingMessage_VTable = type { ptr }
%ServerResponse_VTable = type { ptr, ptr, ptr, ptr }
%Server_VTable = type { ptr, ptr }
%Float64Array = type { ptr, ptr, i64 }

@undefined = global i8 0
@JSON = global ptr null
@null = global i8 0
@console = global ptr null
@path = global ptr null
@fs = global ptr null
@process = global ptr null
@Math = global ptr null
@Buffer = global ptr null
@http = global ptr null
@DataView_VTable_Global = constant %DataView_VTable { ptr null, ptr @DataView_getUint32, ptr @DataView_setUint32 }, !type !0
@Uint8Array_VTable_Global = constant %Uint8Array_VTable zeroinitializer, !type !1
@Uint32Array_VTable_Global = constant %Uint32Array_VTable zeroinitializer, !type !2
@Float64Array_VTable_Global = constant %Float64Array_VTable zeroinitializer, !type !3
@Buffer_VTable_Global = constant %Buffer_VTable { ptr null, ptr @Buffer_toString }, !type !4
@Response_VTable_Global = constant %Response_VTable { ptr null, ptr @Response_json, ptr @Response_text }, !type !5
@URL_VTable_Global = constant %URL_VTable zeroinitializer, !type !6
@IncomingMessage_VTable_Global = constant %IncomingMessage_VTable zeroinitializer, !type !7
@ServerResponse_VTable_Global = constant %ServerResponse_VTable { ptr null, ptr @ServerResponse_end, ptr @ServerResponse_write, ptr 
@ServerResponse_writeHead }, !type !8
@Server_VTable_Global = constant %Server_VTable { ptr null, ptr @Server_listen }, !type !9

declare i8 @ts_typed_array_get_u8(ptr, i64)

declare void @ts_typed_array_set_u8(ptr, i64, i8)

declare i32 @ts_typed_array_get_u32(ptr, i64)

declare void @ts_typed_array_set_u32(ptr, i64, i32)

declare double @ts_typed_array_get_f64(ptr, i64)

declare void @ts_typed_array_set_f64(ptr, i64, double)

declare ptr @ts_typed_array_create_u8(i64)

declare ptr @ts_typed_array_create_u32(i64)

declare ptr @ts_typed_array_create_f64(i64)

declare ptr @ts_typed_array_from_array_u8(ptr)

declare ptr @ts_typed_array_from_array_u32(ptr)

declare ptr @ts_typed_array_from_array_f64(ptr)

declare i32 @ts_double_to_int32(double)

declare i32 @ts_double_to_uint32(double)

declare ptr @ts_data_view_create(ptr)

declare i64 @DataView_getUint32(ptr, ptr, i64, i1)

declare void @DataView_setUint32(ptr, ptr, i64, i64, i1)

declare ptr @Buffer_toString(ptr, ptr) #0

declare ptr @Response_json(ptr, ptr) #0

declare ptr @Response_text(ptr, ptr) #0

declare ptr @ServerResponse_end(ptr, ptr, ptr) #0

declare ptr @ServerResponse_write(ptr, ptr, ptr) #0

declare ptr @ServerResponse_writeHead(ptr, ptr, i64, ptr) #0

declare ptr @Server_listen(ptr, ptr, i64, ptr) #0

define ptr @__module_init_3937545542101398452(ptr %context) #0 !type !10 {
entry:
  %0 = call ptr @ts_array_create()
  %1 = call ptr @ts_value_make_double(double 1.000000e+00)
  call void @ts_array_push(ptr %0, ptr %1)
  %2 = call ptr @ts_value_make_double(double 2.000000e+00)
  call void @ts_array_push(ptr %0, ptr %2)
  %3 = call ptr @ts_value_make_double(double 3.000000e+00)
  call void @ts_array_push(ptr %0, ptr %3)
  %4 = call ptr @ts_typed_array_from_array_f64(ptr %0)
  %5 = call double @testRange_Float64Array(ptr null, ptr %4)
  %6 = call ptr @ts_value_make_double(double %5)
  ret ptr %6
}

define ptr @user_main(ptr %context) #0 !type !11 {
entry:
  %0 = call ptr @__module_init_3937545542101398452(ptr null)
  %1 = call ptr @ts_value_make_object(ptr %0)
  ret ptr %1
}

define double @testRange_Float64Array(ptr %context, ptr %arr) #0 !type !12 {
entry:
  %i = alloca double, align 8
  %sum = alloca double, align 8
  %arr1 = alloca ptr, align 8
  store ptr %arr, ptr %arr1, align 8
  store double 0.000000e+00, ptr %sum, align 8
  store double 0.000000e+00, ptr %i, align 8
  br label %forcond

forcond:                                          ; preds = %forinc, %entry
  %i2 = load double, ptr %i, align 8
  %arr3 = load ptr, ptr %arr1, align 8
  %0 = getelementptr inbounds %Float64Array, ptr %arr3, i32 0, i32 2
  %1 = load i64, ptr %0, align 8
  %2 = sitofp i64 %1 to double
  %cmptmp = fcmp olt double %i2, %2
  br i1 %cmptmp, label %forloop, label %forafter

forloop:                                          ; preds = %forcond
  %sum4 = load double, ptr %sum, align 8
  %arr5 = load ptr, ptr %arr1, align 8
  %i6 = load double, ptr %i, align 8
  %3 = fptosi double %i6 to i64
  %4 = call double @ts_typed_array_get_f64(ptr %arr5, i64 %3)
  %addtmp = fadd double %sum4, %4
  store double %addtmp, ptr %sum, align 8
  br label %forinc

forinc:                                           ; preds = %forloop
  %i7 = load double, ptr %i, align 8
  %incdec = fadd double %i7, 1.000000e+00
  store double %incdec, ptr %i, align 8
  br label %forcond

forafter:                                         ; preds = %forcond
  %sum8 = load double, ptr %sum, align 8
  ret double %sum8
}

declare ptr @ts_array_create()

declare void @ts_array_push(ptr, ptr)

declare ptr @ts_value_make_double(double)

declare ptr @ts_value_make_object(ptr)

declare i32 @ts_main(i32, ptr, ptr) #0

define i32 @main(i32 %0, ptr %1) #0 {
entry:
  %2 = call i32 @ts_main(i32 %0, ptr %1, ptr @user_main)
  ret i32 0
}

attributes #0 = { "sspstrong" "stack-protector-buffer-size"="8" }

!0 = !{i64 0, !"DataView"}
!1 = !{i64 0, !"Uint8Array"}
!2 = !{i64 0, !"Uint32Array"}
!3 = !{i64 0, !"Float64Array"}
!4 = !{i64 0, !"Buffer"}
!5 = !{i64 0, !"Response"}
!6 = !{i64 0, !"URL"}
!7 = !{i64 0, !"IncomingMessage"}
!8 = !{i64 0, !"ServerResponse"}
!9 = !{i64 0, !"Server"}
!10 = !{i64 0, !"__module_init_3937545542101398452"}
!11 = !{i64 0, !"user_main"}
!12 = !{i64 0, !"testRange_Float64Array"}
Processing module: E:\src\github.com\cgrinker\ts-aoc\examples\bce_range_test.json
Moving ExpressionStatement to module init
Module init now has 1 statements
Monomorphizing function: testRange
Generated 3 specializations:
  __module_init_3937545542101398452 -> __module_init_3937545542101398452
  user_main -> user_main
  testRange -> testRange_Float64Array

--- Generated IR ---
Successfully loaded AST from examples/bce_range_test.json
