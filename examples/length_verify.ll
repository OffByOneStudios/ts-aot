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
%TsArray = type { i32, ptr, i64, i64, i64 }
%TsString = type { i32, i32 }
%Uint8Array = type { ptr, ptr, i64 }
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
@ServerResponse_VTable_Global = constant %ServerResponse_VTable { ptr null, ptr @ServerResponse_end, ptr @ServerResponse_write, ptr @ServerResponse_writeHead }, !type !8
@Server_VTable_Global = constant %Server_VTable { ptr null, ptr @Server_listen }, !type !9
@0 = private unnamed_addr constant [15 x i8] c"Array length: \00", align 1
@1 = private unnamed_addr constant [6 x i8] c"hello\00", align 1
@2 = private unnamed_addr constant [16 x i8] c"String length: \00", align 1
@3 = private unnamed_addr constant [20 x i8] c"Uint8Array length: \00", align 1
@4 = private unnamed_addr constant [22 x i8] c"Float64Array length: \00", align 1

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

define ptr @__module_init_12973341669594571465(ptr %context) #0 !type !10 {
entry:
  %0 = call ptr @test(ptr null)
  %1 = call ptr @ts_value_make_object(ptr %0)
  ret ptr %1
}

define ptr @user_main(ptr %context) #0 !type !11 {
entry:
  %0 = call ptr @__module_init_12973341669594571465(ptr null)
  %1 = call ptr @ts_value_make_object(ptr %0)
  ret ptr %1
}

define ptr @test(ptr %context) #0 !type !12 {
entry:
  %f64 = alloca ptr, align 8
  %u8 = alloca ptr, align 8
  %str = alloca ptr, align 8
  %arr = alloca ptr, align 8
  %0 = call ptr @ts_array_create()
  %1 = call ptr @ts_value_make_double(double 1.000000e+00)
  call void @ts_array_push(ptr %0, ptr %1)
  %2 = call ptr @ts_value_make_double(double 2.000000e+00)
  call void @ts_array_push(ptr %0, ptr %2)
  %3 = call ptr @ts_value_make_double(double 3.000000e+00)
  call void @ts_array_push(ptr %0, ptr %3)
  store ptr %0, ptr %arr, align 8
  %4 = call ptr @ts_string_create(ptr @0)
  %arr1 = load ptr, ptr %arr, align 8
  %5 = getelementptr inbounds %TsArray, ptr %arr1, i32 0, i32 2
  %6 = load i64, ptr %5, align 8, !range !13
  %7 = call ptr @ts_string_from_int(i64 %6)
  %8 = call ptr @ts_string_concat(ptr %4, ptr %7)
  call void @ts_console_log(ptr %8)
  %9 = call ptr @ts_string_create(ptr @1)
  store ptr %9, ptr %str, align 8
  %10 = call ptr @ts_string_create(ptr @2)
  %str2 = load ptr, ptr %str, align 8
  %11 = getelementptr inbounds %TsString, ptr %str2, i32 0, i32 1
  %12 = load i32, ptr %11, align 4, !range !14
  %13 = zext i32 %12 to i64
  %14 = call ptr @ts_string_from_int(i64 %13)
  %15 = call ptr @ts_string_concat(ptr %10, ptr %14)
  call void @ts_console_log(ptr %15)
  %16 = call ptr @ts_typed_array_create_u8(i64 10)
  store ptr %16, ptr %u8, align 8
  %17 = call ptr @ts_string_create(ptr @3)
  %u83 = load ptr, ptr %u8, align 8
  %18 = getelementptr inbounds %Uint8Array, ptr %u83, i32 0, i32 2
  %19 = load i64, ptr %18, align 8, !range !13
  %20 = call ptr @ts_string_from_int(i64 %19)
  %21 = call ptr @ts_string_concat(ptr %17, ptr %20)
  call void @ts_console_log(ptr %21)
  %22 = call ptr @ts_typed_array_create_f64(i64 5)
  store ptr %22, ptr %f64, align 8
  %23 = call ptr @ts_string_create(ptr @4)
  %f644 = load ptr, ptr %f64, align 8
  %24 = getelementptr inbounds %Float64Array, ptr %f644, i32 0, i32 2
  %25 = load i64, ptr %24, align 8, !range !13
  %26 = call ptr @ts_string_from_int(i64 %25)
  %27 = call ptr @ts_string_concat(ptr %23, ptr %26)
  call void @ts_console_log(ptr %27)
  ret ptr null
}

declare ptr @ts_value_make_object(ptr)

declare ptr @ts_array_create()

declare void @ts_array_push(ptr, ptr)

declare ptr @ts_value_make_double(double)

declare ptr @ts_string_create(ptr)

declare ptr @ts_string_from_int(i64)

declare ptr @ts_string_concat(ptr, ptr)

declare void @ts_console_log(ptr)

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
!10 = !{i64 0, !"__module_init_12973341669594571465"}
!11 = !{i64 0, !"user_main"}
!12 = !{i64 0, !"test"}
!13 = !{i64 0, i64 9223372036854775807}
!14 = !{i32 0, i32 2147483647}
