; ModuleID = 'ts_module'
source_filename = "ts_module"
target datalayout = "e-m:w-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-windows-msvc"

%RegExpMatchArray_VTable = type { ptr, ptr }
%Hash_VTable = type { ptr, ptr, ptr, ptr, ptr }
%AssertionError_VTable = type { ptr, ptr }
%SocketAddress_VTable = type { ptr, ptr }
%URLSearchParams_VTable = type { ptr, ptr, ptr, ptr, ptr, ptr, ptr, ptr, ptr, ptr, ptr, ptr, ptr, ptr }
%Response_VTable = type { ptr, ptr, ptr, ptr }
%URL_VTable = type { ptr, ptr, ptr, ptr }
%Hmac_VTable = type { ptr, ptr, ptr, ptr }
%Script_VTable = type { ptr, ptr, ptr, ptr, ptr, ptr }
%TsValue = type { i8, [7 x i8], i64 }
%RegExpMatchArray = type { ptr, i64, ptr, ptr, i64 }
%AssertionError = type { ptr, ptr, ptr, ptr, ptr, ptr }
%SocketAddress = type { ptr, ptr, ptr, i64, i64 }
%URLSearchParams = type { ptr, i64 }
%Response = type { ptr, i1, i64, ptr }
%URL = type { ptr, ptr, ptr, ptr, ptr, ptr, ptr, ptr, ptr, ptr, ptr, ptr, ptr }

@Array = external global ptr
@Buffer = external global ptr
@JSON = external global ptr
@Math = external global ptr
@Object = external global ptr
@console = external global ptr
@global = external global ptr
@globalThis = external global ptr
@parseFloat = external global ptr
@parseInt = external global ptr
@process = external global ptr
@timers = global ptr null
@TextEncoder = global ptr null
@Readable = global ptr null
@null = global ptr null
@Int16Array = global ptr null
@"timers/promises" = global ptr null
@undefined = global ptr null
@Infinity = global double 0.000000e+00
@NaN = global double 0.000000e+00
@Map = global ptr null
@Symbol = global ptr null
@Reflect = global ptr null
@Uint16Array = global ptr null
@Int8Array = global ptr null
@Uint8Array = global ptr null
@Number = global ptr null
@Uint8ClampedArray = global ptr null
@BigUint64Array = global ptr null
@Int32Array = global ptr null
@Uint32Array = global ptr null
@Float32Array = global ptr null
@events = global ptr null
@Float64Array = global ptr null
@BigInt64Array = global ptr null
@TextDecoder = global ptr null
@String = global ptr null
@DataView = global ptr null
@https = global ptr null
@buffer = global ptr null
@http = global ptr null
@stream = global ptr null
@fs = global ptr null
@path = global ptr null
@net = global ptr null
@util = global ptr null
@os = global ptr null
@crypto = global ptr null
@querystring = global ptr null
@url = global ptr null
@vm = global ptr null
@v8 = global ptr null
@assert = global ptr null
@StringDecoder = global ptr null
@string_decoder = global ptr null
@__dirname = global ptr null
@__filename = global ptr null
@x_9610421887249242215 = global i64 0
@result_9610421887249242215 = global i64 0
@0 = private unnamed_addr constant [6 x i8] c"index\00", align 1
@1 = private unnamed_addr constant [8 x i8] c"indices\00", align 1
@2 = private unnamed_addr constant [6 x i8] c"input\00", align 1
@3 = private unnamed_addr constant [7 x i8] c"length\00", align 1
@RegExpMatchArray_VTable_Global = constant %RegExpMatchArray_VTable { ptr null, ptr @RegExpMatchArray_get_property }, !type !0
@Hash_VTable_Global = constant %Hash_VTable { ptr null, ptr @Hash_get_property, ptr @Hash_copy, ptr @Hash_digest, ptr @Hash_update }, !type !1
@4 = private unnamed_addr constant [7 x i8] c"actual\00", align 1
@5 = private unnamed_addr constant [5 x i8] c"code\00", align 1
@6 = private unnamed_addr constant [9 x i8] c"expected\00", align 1
@7 = private unnamed_addr constant [8 x i8] c"message\00", align 1
@8 = private unnamed_addr constant [9 x i8] c"operator\00", align 1
@AssertionError_VTable_Global = constant %AssertionError_VTable { ptr null, ptr @AssertionError_get_property }, !type !2
@9 = private unnamed_addr constant [8 x i8] c"address\00", align 1
@10 = private unnamed_addr constant [7 x i8] c"family\00", align 1
@11 = private unnamed_addr constant [10 x i8] c"flowlabel\00", align 1
@12 = private unnamed_addr constant [5 x i8] c"port\00", align 1
@SocketAddress_VTable_Global = constant %SocketAddress_VTable { ptr null, ptr @SocketAddress_get_property }, !type !3
@13 = private unnamed_addr constant [5 x i8] c"size\00", align 1
@URLSearchParams_VTable_Global = constant %URLSearchParams_VTable { ptr null, ptr @URLSearchParams_get_property, ptr @URLSearchParams_append, ptr @URLSearchParams_delete, ptr @URLSearchParams_entries, ptr @URLSearchParams_forEach, ptr @URLSearchParams_get, ptr @URLSearchParams_getAll, ptr @URLSearchParams_has, ptr @URLSearchParams_keys, ptr @URLSearchParams_set, ptr @URLSearchParams_sort, ptr @URLSearchParams_toString, ptr @URLSearchParams_values }, !type !4
@14 = private unnamed_addr constant [3 x i8] c"ok\00", align 1
@15 = private unnamed_addr constant [7 x i8] c"status\00", align 1
@16 = private unnamed_addr constant [11 x i8] c"statusText\00", align 1
@Response_VTable_Global = constant %Response_VTable { ptr null, ptr @Response_get_property, ptr @Response_json, ptr @Response_text }, !type !5
@17 = private unnamed_addr constant [5 x i8] c"hash\00", align 1
@18 = private unnamed_addr constant [5 x i8] c"host\00", align 1
@19 = private unnamed_addr constant [9 x i8] c"hostname\00", align 1
@20 = private unnamed_addr constant [5 x i8] c"href\00", align 1
@21 = private unnamed_addr constant [7 x i8] c"origin\00", align 1
@22 = private unnamed_addr constant [9 x i8] c"password\00", align 1
@23 = private unnamed_addr constant [9 x i8] c"pathname\00", align 1
@24 = private unnamed_addr constant [5 x i8] c"port\00", align 1
@25 = private unnamed_addr constant [9 x i8] c"protocol\00", align 1
@26 = private unnamed_addr constant [7 x i8] c"search\00", align 1
@27 = private unnamed_addr constant [13 x i8] c"searchParams\00", align 1
@28 = private unnamed_addr constant [9 x i8] c"username\00", align 1
@URL_VTable_Global = constant %URL_VTable { ptr null, ptr @URL_get_property, ptr @URL_toJSON, ptr @URL_toString }, !type !6
@Hmac_VTable_Global = constant %Hmac_VTable { ptr null, ptr @Hmac_get_property, ptr @Hmac_digest, ptr @Hmac_update }, !type !7
@Script_VTable_Global = constant %Script_VTable { ptr null, ptr @Script_get_property, ptr @Script_createCachedData, ptr @Script_runInContext, ptr @Script_runInNewContext, ptr @Script_runInThisContext }, !type !8
@29 = private unnamed_addr constant [8 x i8] c"exports\00", align 1
@30 = private unnamed_addr constant [8 x i8] c"exports\00", align 1
@31 = private unnamed_addr constant [8 x i8] c"exports\00", align 1
@__ts_const_undefined_value = private constant %TsValue zeroinitializer
@32 = private unnamed_addr constant [101 x i8] c"E:\\src\\github.com\\cgrinker\\ts-aoc\\tests\\golden_ir\\typescript\\regression\\mutable_variable_reassign.ts\00", align 1

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

declare i64 @DataView_getInt8(ptr, ptr, i64, i1)

declare i64 @DataView_getUint8(ptr, ptr, i64, i1)

declare i64 @DataView_getInt16(ptr, ptr, i64, i1)

declare i64 @DataView_getUint16(ptr, ptr, i64, i1)

declare i64 @DataView_getInt32(ptr, ptr, i64, i1)

declare i64 @DataView_getUint32(ptr, ptr, i64, i1)

declare double @DataView_getFloat32(ptr, ptr, i64, i1)

declare double @DataView_getFloat64(ptr, ptr, i64, i1)

declare void @DataView_setInt8(ptr, ptr, i64, i64, i1)

declare void @DataView_setUint8(ptr, ptr, i64, i64, i1)

declare void @DataView_setInt16(ptr, ptr, i64, i64, i1)

declare void @DataView_setUint16(ptr, ptr, i64, i64, i1)

declare void @DataView_setInt32(ptr, ptr, i64, i64, i1)

declare void @DataView_setUint32(ptr, ptr, i64, i64, i1)

declare void @DataView_setFloat32(ptr, ptr, i64, double, i1)

declare void @DataView_setFloat64(ptr, ptr, i64, double, i1)

declare ptr @DataView_getBuffer(ptr, ptr)

declare i64 @DataView_getByteLength(ptr, ptr)

declare i64 @DataView_getByteOffset(ptr, ptr)

define ptr @RegExpMatchArray_get_property(ptr %0, ptr %1) #0 {
entry:
  %2 = call ptr @ts_string_create(ptr @0)
  %3 = call i1 @ts_string_eq(ptr %1, ptr %2)
  br i1 %3, label %match_index, label %next_index

match_index:                                      ; preds = %entry
  %4 = getelementptr inbounds %RegExpMatchArray, ptr %0, i32 0, i32 1
  %5 = load i64, ptr %4, align 8
  %6 = call ptr @ts_value_make_int(i64 %5)
  ret ptr %6

next_index:                                       ; preds = %entry
  %7 = call ptr @ts_string_create(ptr @1)
  %8 = call i1 @ts_string_eq(ptr %1, ptr %7)
  br i1 %8, label %match_indices, label %next_indices

match_indices:                                    ; preds = %next_index
  %9 = getelementptr inbounds %RegExpMatchArray, ptr %0, i32 0, i32 2
  %10 = load ptr, ptr %9, align 8
  %11 = call ptr @ts_value_make_array(ptr %10)
  ret ptr %11

next_indices:                                     ; preds = %next_index
  %12 = call ptr @ts_string_create(ptr @2)
  %13 = call i1 @ts_string_eq(ptr %1, ptr %12)
  br i1 %13, label %match_input, label %next_input

match_input:                                      ; preds = %next_indices
  %14 = getelementptr inbounds %RegExpMatchArray, ptr %0, i32 0, i32 3
  %15 = load ptr, ptr %14, align 8
  %16 = call ptr @ts_value_make_string(ptr %15)
  ret ptr %16

next_input:                                       ; preds = %next_indices
  %17 = call ptr @ts_string_create(ptr @3)
  %18 = call i1 @ts_string_eq(ptr %1, ptr %17)
  br i1 %18, label %match_length, label %next_length

match_length:                                     ; preds = %next_input
  %19 = getelementptr inbounds %RegExpMatchArray, ptr %0, i32 0, i32 4
  %20 = load i64, ptr %19, align 8
  %21 = call ptr @ts_value_make_int(i64 %20)
  ret ptr %21

next_length:                                      ; preds = %next_input
  %22 = call ptr @ts_value_make_undefined()
  ret ptr %22
}

declare ptr @ts_string_create(ptr)

declare i1 @ts_string_eq(ptr, ptr)

declare ptr @ts_value_make_undefined()

declare ptr @ts_value_make_int(i64)

declare ptr @ts_value_make_array(ptr)

declare ptr @ts_value_make_string(ptr)

define ptr @Hash_get_property(ptr %0, ptr %1) #0 {
entry:
  %2 = call ptr @ts_value_make_undefined()
  ret ptr %2
}

declare ptr @Hash_copy(ptr, ptr) #0

declare ptr @Hash_digest(ptr, ptr, ptr) #0

declare ptr @Hash_update(ptr, ptr, ptr) #0

define ptr @AssertionError_get_property(ptr %0, ptr %1) #0 {
entry:
  %2 = call ptr @ts_string_create(ptr @4)
  %3 = call i1 @ts_string_eq(ptr %1, ptr %2)
  br i1 %3, label %match_actual, label %next_actual

match_actual:                                     ; preds = %entry
  %4 = getelementptr inbounds %AssertionError, ptr %0, i32 0, i32 1
  %5 = load ptr, ptr %4, align 8
  %6 = call ptr @ts_value_box_any(ptr %5)
  ret ptr %6

next_actual:                                      ; preds = %entry
  %7 = call ptr @ts_string_create(ptr @5)
  %8 = call i1 @ts_string_eq(ptr %1, ptr %7)
  br i1 %8, label %match_code, label %next_code

match_code:                                       ; preds = %next_actual
  %9 = getelementptr inbounds %AssertionError, ptr %0, i32 0, i32 2
  %10 = load ptr, ptr %9, align 8
  %11 = call ptr @ts_value_make_string(ptr %10)
  ret ptr %11

next_code:                                        ; preds = %next_actual
  %12 = call ptr @ts_string_create(ptr @6)
  %13 = call i1 @ts_string_eq(ptr %1, ptr %12)
  br i1 %13, label %match_expected, label %next_expected

match_expected:                                   ; preds = %next_code
  %14 = getelementptr inbounds %AssertionError, ptr %0, i32 0, i32 3
  %15 = load ptr, ptr %14, align 8
  %16 = call ptr @ts_value_box_any(ptr %15)
  ret ptr %16

next_expected:                                    ; preds = %next_code
  %17 = call ptr @ts_string_create(ptr @7)
  %18 = call i1 @ts_string_eq(ptr %1, ptr %17)
  br i1 %18, label %match_message, label %next_message

match_message:                                    ; preds = %next_expected
  %19 = getelementptr inbounds %AssertionError, ptr %0, i32 0, i32 4
  %20 = load ptr, ptr %19, align 8
  %21 = call ptr @ts_value_make_string(ptr %20)
  ret ptr %21

next_message:                                     ; preds = %next_expected
  %22 = call ptr @ts_string_create(ptr @8)
  %23 = call i1 @ts_string_eq(ptr %1, ptr %22)
  br i1 %23, label %match_operator, label %next_operator

match_operator:                                   ; preds = %next_message
  %24 = getelementptr inbounds %AssertionError, ptr %0, i32 0, i32 5
  %25 = load ptr, ptr %24, align 8
  %26 = call ptr @ts_value_make_string(ptr %25)
  ret ptr %26

next_operator:                                    ; preds = %next_message
  %27 = call ptr @ts_value_make_undefined()
  ret ptr %27
}

declare ptr @ts_value_box_any(ptr)

define ptr @SocketAddress_get_property(ptr %0, ptr %1) #0 {
entry:
  %2 = call ptr @ts_string_create(ptr @9)
  %3 = call i1 @ts_string_eq(ptr %1, ptr %2)
  br i1 %3, label %match_address, label %next_address

match_address:                                    ; preds = %entry
  %4 = getelementptr inbounds %SocketAddress, ptr %0, i32 0, i32 1
  %5 = load ptr, ptr %4, align 8
  %6 = call ptr @ts_value_make_string(ptr %5)
  ret ptr %6

next_address:                                     ; preds = %entry
  %7 = call ptr @ts_string_create(ptr @10)
  %8 = call i1 @ts_string_eq(ptr %1, ptr %7)
  br i1 %8, label %match_family, label %next_family

match_family:                                     ; preds = %next_address
  %9 = getelementptr inbounds %SocketAddress, ptr %0, i32 0, i32 2
  %10 = load ptr, ptr %9, align 8
  %11 = call ptr @ts_value_make_string(ptr %10)
  ret ptr %11

next_family:                                      ; preds = %next_address
  %12 = call ptr @ts_string_create(ptr @11)
  %13 = call i1 @ts_string_eq(ptr %1, ptr %12)
  br i1 %13, label %match_flowlabel, label %next_flowlabel

match_flowlabel:                                  ; preds = %next_family
  %14 = getelementptr inbounds %SocketAddress, ptr %0, i32 0, i32 3
  %15 = load i64, ptr %14, align 8
  %16 = call ptr @ts_value_make_int(i64 %15)
  ret ptr %16

next_flowlabel:                                   ; preds = %next_family
  %17 = call ptr @ts_string_create(ptr @12)
  %18 = call i1 @ts_string_eq(ptr %1, ptr %17)
  br i1 %18, label %match_port, label %next_port

match_port:                                       ; preds = %next_flowlabel
  %19 = getelementptr inbounds %SocketAddress, ptr %0, i32 0, i32 4
  %20 = load i64, ptr %19, align 8
  %21 = call ptr @ts_value_make_int(i64 %20)
  ret ptr %21

next_port:                                        ; preds = %next_flowlabel
  %22 = call ptr @ts_value_make_undefined()
  ret ptr %22
}

define ptr @URLSearchParams_get_property(ptr %0, ptr %1) #0 {
entry:
  %2 = call ptr @ts_string_create(ptr @13)
  %3 = call i1 @ts_string_eq(ptr %1, ptr %2)
  br i1 %3, label %match_size, label %next_size

match_size:                                       ; preds = %entry
  %4 = getelementptr inbounds %URLSearchParams, ptr %0, i32 0, i32 1
  %5 = load i64, ptr %4, align 8
  %6 = call ptr @ts_value_make_int(i64 %5)
  ret ptr %6

next_size:                                        ; preds = %entry
  %7 = call ptr @ts_value_make_undefined()
  ret ptr %7
}

declare void @URLSearchParams_append(ptr, ptr, ptr, ptr) #0

declare void @URLSearchParams_delete(ptr, ptr, ptr) #0

declare ptr @URLSearchParams_entries(ptr, ptr) #0

declare void @URLSearchParams_forEach(ptr, ptr, ptr, ptr) #0

declare ptr @URLSearchParams_get(ptr, ptr, ptr) #0

declare ptr @URLSearchParams_getAll(ptr, ptr, ptr) #0

declare i1 @URLSearchParams_has(ptr, ptr, ptr) #0

declare ptr @URLSearchParams_keys(ptr, ptr) #0

declare void @URLSearchParams_set(ptr, ptr, ptr, ptr) #0

declare void @URLSearchParams_sort(ptr, ptr) #0

declare ptr @URLSearchParams_toString(ptr, ptr) #0

declare ptr @URLSearchParams_values(ptr, ptr) #0

define ptr @Response_get_property(ptr %0, ptr %1) #0 {
entry:
  %2 = call ptr @ts_string_create(ptr @14)
  %3 = call i1 @ts_string_eq(ptr %1, ptr %2)
  br i1 %3, label %match_ok, label %next_ok

match_ok:                                         ; preds = %entry
  %4 = getelementptr inbounds %Response, ptr %0, i32 0, i32 1
  %5 = load i1, ptr %4, align 1
  %6 = call ptr @ts_value_make_bool(i1 %5)
  ret ptr %6

next_ok:                                          ; preds = %entry
  %7 = call ptr @ts_string_create(ptr @15)
  %8 = call i1 @ts_string_eq(ptr %1, ptr %7)
  br i1 %8, label %match_status, label %next_status

match_status:                                     ; preds = %next_ok
  %9 = getelementptr inbounds %Response, ptr %0, i32 0, i32 2
  %10 = load i64, ptr %9, align 8
  %11 = call ptr @ts_value_make_int(i64 %10)
  ret ptr %11

next_status:                                      ; preds = %next_ok
  %12 = call ptr @ts_string_create(ptr @16)
  %13 = call i1 @ts_string_eq(ptr %1, ptr %12)
  br i1 %13, label %match_statusText, label %next_statusText

match_statusText:                                 ; preds = %next_status
  %14 = getelementptr inbounds %Response, ptr %0, i32 0, i32 3
  %15 = load ptr, ptr %14, align 8
  %16 = call ptr @ts_value_make_string(ptr %15)
  ret ptr %16

next_statusText:                                  ; preds = %next_status
  %17 = call ptr @ts_value_make_undefined()
  ret ptr %17
}

declare ptr @ts_value_make_bool(i1)

declare ptr @Response_json(ptr, ptr) #0

declare ptr @Response_text(ptr, ptr) #0

define ptr @URL_get_property(ptr %0, ptr %1) #0 {
entry:
  %2 = call ptr @ts_string_create(ptr @17)
  %3 = call i1 @ts_string_eq(ptr %1, ptr %2)
  br i1 %3, label %match_hash, label %next_hash

match_hash:                                       ; preds = %entry
  %4 = getelementptr inbounds %URL, ptr %0, i32 0, i32 1
  %5 = load ptr, ptr %4, align 8
  %6 = call ptr @ts_value_make_string(ptr %5)
  ret ptr %6

next_hash:                                        ; preds = %entry
  %7 = call ptr @ts_string_create(ptr @18)
  %8 = call i1 @ts_string_eq(ptr %1, ptr %7)
  br i1 %8, label %match_host, label %next_host

match_host:                                       ; preds = %next_hash
  %9 = getelementptr inbounds %URL, ptr %0, i32 0, i32 2
  %10 = load ptr, ptr %9, align 8
  %11 = call ptr @ts_value_make_string(ptr %10)
  ret ptr %11

next_host:                                        ; preds = %next_hash
  %12 = call ptr @ts_string_create(ptr @19)
  %13 = call i1 @ts_string_eq(ptr %1, ptr %12)
  br i1 %13, label %match_hostname, label %next_hostname

match_hostname:                                   ; preds = %next_host
  %14 = getelementptr inbounds %URL, ptr %0, i32 0, i32 3
  %15 = load ptr, ptr %14, align 8
  %16 = call ptr @ts_value_make_string(ptr %15)
  ret ptr %16

next_hostname:                                    ; preds = %next_host
  %17 = call ptr @ts_string_create(ptr @20)
  %18 = call i1 @ts_string_eq(ptr %1, ptr %17)
  br i1 %18, label %match_href, label %next_href

match_href:                                       ; preds = %next_hostname
  %19 = getelementptr inbounds %URL, ptr %0, i32 0, i32 4
  %20 = load ptr, ptr %19, align 8
  %21 = call ptr @ts_value_make_string(ptr %20)
  ret ptr %21

next_href:                                        ; preds = %next_hostname
  %22 = call ptr @ts_string_create(ptr @21)
  %23 = call i1 @ts_string_eq(ptr %1, ptr %22)
  br i1 %23, label %match_origin, label %next_origin

match_origin:                                     ; preds = %next_href
  %24 = getelementptr inbounds %URL, ptr %0, i32 0, i32 5
  %25 = load ptr, ptr %24, align 8
  %26 = call ptr @ts_value_make_string(ptr %25)
  ret ptr %26

next_origin:                                      ; preds = %next_href
  %27 = call ptr @ts_string_create(ptr @22)
  %28 = call i1 @ts_string_eq(ptr %1, ptr %27)
  br i1 %28, label %match_password, label %next_password

match_password:                                   ; preds = %next_origin
  %29 = getelementptr inbounds %URL, ptr %0, i32 0, i32 6
  %30 = load ptr, ptr %29, align 8
  %31 = call ptr @ts_value_make_string(ptr %30)
  ret ptr %31

next_password:                                    ; preds = %next_origin
  %32 = call ptr @ts_string_create(ptr @23)
  %33 = call i1 @ts_string_eq(ptr %1, ptr %32)
  br i1 %33, label %match_pathname, label %next_pathname

match_pathname:                                   ; preds = %next_password
  %34 = getelementptr inbounds %URL, ptr %0, i32 0, i32 7
  %35 = load ptr, ptr %34, align 8
  %36 = call ptr @ts_value_make_string(ptr %35)
  ret ptr %36

next_pathname:                                    ; preds = %next_password
  %37 = call ptr @ts_string_create(ptr @24)
  %38 = call i1 @ts_string_eq(ptr %1, ptr %37)
  br i1 %38, label %match_port, label %next_port

match_port:                                       ; preds = %next_pathname
  %39 = getelementptr inbounds %URL, ptr %0, i32 0, i32 8
  %40 = load ptr, ptr %39, align 8
  %41 = call ptr @ts_value_make_string(ptr %40)
  ret ptr %41

next_port:                                        ; preds = %next_pathname
  %42 = call ptr @ts_string_create(ptr @25)
  %43 = call i1 @ts_string_eq(ptr %1, ptr %42)
  br i1 %43, label %match_protocol, label %next_protocol

match_protocol:                                   ; preds = %next_port
  %44 = getelementptr inbounds %URL, ptr %0, i32 0, i32 9
  %45 = load ptr, ptr %44, align 8
  %46 = call ptr @ts_value_make_string(ptr %45)
  ret ptr %46

next_protocol:                                    ; preds = %next_port
  %47 = call ptr @ts_string_create(ptr @26)
  %48 = call i1 @ts_string_eq(ptr %1, ptr %47)
  br i1 %48, label %match_search, label %next_search

match_search:                                     ; preds = %next_protocol
  %49 = getelementptr inbounds %URL, ptr %0, i32 0, i32 10
  %50 = load ptr, ptr %49, align 8
  %51 = call ptr @ts_value_make_string(ptr %50)
  ret ptr %51

next_search:                                      ; preds = %next_protocol
  %52 = call ptr @ts_string_create(ptr @27)
  %53 = call i1 @ts_string_eq(ptr %1, ptr %52)
  br i1 %53, label %match_searchParams, label %next_searchParams

match_searchParams:                               ; preds = %next_search
  %54 = getelementptr inbounds %URL, ptr %0, i32 0, i32 11
  %55 = load ptr, ptr %54, align 8
  %56 = call ptr @ts_value_make_object(ptr %55)
  ret ptr %56

next_searchParams:                                ; preds = %next_search
  %57 = call ptr @ts_string_create(ptr @28)
  %58 = call i1 @ts_string_eq(ptr %1, ptr %57)
  br i1 %58, label %match_username, label %next_username

match_username:                                   ; preds = %next_searchParams
  %59 = getelementptr inbounds %URL, ptr %0, i32 0, i32 12
  %60 = load ptr, ptr %59, align 8
  %61 = call ptr @ts_value_make_string(ptr %60)
  ret ptr %61

next_username:                                    ; preds = %next_searchParams
  %62 = call ptr @ts_value_make_undefined()
  ret ptr %62
}

declare ptr @ts_value_make_object(ptr)

declare ptr @URL_toJSON(ptr, ptr) #0

declare ptr @URL_toString(ptr, ptr) #0

define ptr @Hmac_get_property(ptr %0, ptr %1) #0 {
entry:
  %2 = call ptr @ts_value_make_undefined()
  ret ptr %2
}

declare ptr @Hmac_digest(ptr, ptr, ptr) #0

declare ptr @Hmac_update(ptr, ptr, ptr) #0

define ptr @Script_get_property(ptr %0, ptr %1) #0 {
entry:
  %2 = call ptr @ts_value_make_undefined()
  ret ptr %2
}

declare ptr @Script_createCachedData(ptr, ptr) #0

declare ptr @Script_runInContext(ptr, ptr, ptr, ptr) #0

declare ptr @Script_runInNewContext(ptr, ptr, ptr, ptr) #0

declare ptr @Script_runInThisContext(ptr, ptr, ptr) #0

define ptr @__module_init_9610421887249242215_any(ptr %context, ptr %module) #0 !type !13 {
entry:
  %exports = alloca ptr, align 8
  %module1 = alloca ptr, align 8
  %returnValue = alloca ptr, align 8
  %continueTarget = alloca ptr, align 8
  %breakTarget = alloca ptr, align 8
  %shouldContinue = alloca i1, align 1
  %shouldBreak = alloca i1, align 1
  %shouldReturn = alloca i1, align 1
  store i1 false, ptr %shouldReturn, align 1
  store i1 false, ptr %shouldBreak, align 1
  store i1 false, ptr %shouldContinue, align 1
  %0 = call ptr @ts_value_make_undefined()
  store ptr %0, ptr %returnValue, align 8
  store ptr %module, ptr %module1, align 8
  %module2 = load ptr, ptr %module1, align 8
  %1 = call ptr @ts_value_box_any(ptr %module2)
  %2 = call ptr @ts_string_create(ptr @29)
  %3 = call ptr @ts_value_make_string(ptr %2)
  %4 = call ptr @ts_object_get_dynamic(ptr %1, ptr %3)
  store ptr %4, ptr %exports, align 8
  store i64 2, ptr @x_9610421887249242215, align 8
  store i64 0, ptr @result_9610421887249242215, align 8
  %x = load i64, ptr @x_9610421887249242215, align 8
  switch i64 %x, label %case4 [
    i64 1, label %case
    i64 2, label %case3
  ]

return:                                           ; preds = %dead8
  %5 = load ptr, ptr %returnValue, align 8
  ret ptr %5

case:                                             ; preds = %entry
  store i64 10, ptr @result_9610421887249242215, align 8
  br label %switch.end

case3:                                            ; preds = %dead, %entry
  store i64 20, ptr @result_9610421887249242215, align 8
  br label %switch.end

case4:                                            ; preds = %dead5, %entry
  store i64 30, ptr @result_9610421887249242215, align 8
  br label %switch.end

switch.end:                                       ; preds = %dead6, %case4, %case3, %case
  store i1 false, ptr %shouldBreak, align 1
  %result = load i64, ptr @result_9610421887249242215, align 8
  call void @ts_console_log_int(i64 %result)
  %module7 = load ptr, ptr %module1, align 8
  %6 = call ptr @ts_value_box_any(ptr %module7)
  %7 = call ptr @ts_string_create(ptr @30)
  %8 = call ptr @ts_value_make_string(ptr %7)
  %9 = call ptr @ts_object_get_dynamic(ptr %6, ptr %8)
  ret ptr %9

dead:                                             ; No predecessors!
  br label %case3

dead5:                                            ; No predecessors!
  br label %case4

dead6:                                            ; No predecessors!
  br label %switch.end

dead8:                                            ; No predecessors!
  br label %return
}

define ptr @user_main(ptr %context) #0 !type !14 {
entry:
  %__module_res_0 = alloca ptr, align 8
  %__module_obj_0 = alloca ptr, align 8
  %returnValue = alloca ptr, align 8
  %continueTarget = alloca ptr, align 8
  %breakTarget = alloca ptr, align 8
  %shouldContinue = alloca i1, align 1
  %shouldBreak = alloca i1, align 1
  %shouldReturn = alloca i1, align 1
  store i1 false, ptr %shouldReturn, align 1
  store i1 false, ptr %shouldBreak, align 1
  store i1 false, ptr %shouldContinue, align 1
  %0 = call ptr @ts_value_make_undefined()
  store ptr %0, ptr %returnValue, align 8
  %1 = call ptr @ts_map_create()
  %2 = call ptr @ts_map_create()
  %3 = call ptr @ts_value_make_object(ptr %2)
  %4 = call ptr @ts_string_create(ptr @31)
  %5 = call ptr @ts_value_make_string(ptr %4)
  %6 = icmp eq ptr %5, null
  %7 = icmp eq ptr %3, null
  %8 = select i1 %6, ptr @__ts_const_undefined_value, ptr %5
  %9 = select i1 %7, ptr @__ts_const_undefined_value, ptr %3
  %typePtr = getelementptr inbounds %TsValue, ptr %8, i32 0, i32 0
  %type = load i8, ptr %typePtr, align 1
  %unionPtr = getelementptr inbounds %TsValue, ptr %8, i32 0, i32 2
  %unionVal = load i64, ptr %unionPtr, align 8
  %typePtr1 = getelementptr inbounds %TsValue, ptr %9, i32 0, i32 0
  %type2 = load i8, ptr %typePtr1, align 1
  %unionPtr3 = getelementptr inbounds %TsValue, ptr %9, i32 0, i32 2
  %unionVal4 = load i64, ptr %unionPtr3, align 8
  call void @__ts_map_set_at(ptr %1, i64 %unionVal, i8 %type, i64 %unionVal, i8 %type2, i64 %unionVal4)
  store ptr %1, ptr %__module_obj_0, align 8
  %10 = call ptr @ts_string_create(ptr @32)
  %11 = call ptr @ts_value_make_string(ptr %10)
  %__module_obj_05 = load ptr, ptr %__module_obj_0, align 8
  %12 = call ptr @ts_value_box_any(ptr %__module_obj_05)
  call void @ts_module_register(ptr %11, ptr %12)
  %__module_obj_06 = load ptr, ptr %__module_obj_0, align 8
  %13 = call ptr @__module_init_9610421887249242215_any(ptr %context, ptr %__module_obj_06)
  store ptr %13, ptr %__module_res_0, align 8
  %__module_res_07 = load ptr, ptr %__module_res_0, align 8
  %14 = call ptr @ts_value_box_any(ptr %__module_res_07)
  ret ptr %14

return:                                           ; preds = %dead
  %15 = load ptr, ptr %returnValue, align 8
  ret ptr %15

dead:                                             ; No predecessors!
  br label %return
}

declare ptr @ts_object_get_dynamic(ptr, ptr)

declare void @ts_console_log_int(i64)

declare ptr @ts_map_create()

declare void @__ts_map_set_at(ptr, i64, i8, i64, i8, i64)

declare void @ts_module_register(ptr, ptr)

declare i32 @ts_main(i32, ptr, ptr) #0

define i32 @main(i32 %0, ptr %1) #0 {
entry:
  %2 = call i32 @ts_main(i32 %0, ptr %1, ptr @user_main)
  ret i32 0
}

attributes #0 = { "sspstrong" "stack-protector-buffer-size"="8" }

!llvm.linker.options = !{!9, !10, !11, !12}

!0 = !{i64 0, !"RegExpMatchArray"}
!1 = !{i64 0, !"Hash"}
!2 = !{i64 0, !"AssertionError"}
!3 = !{i64 0, !"SocketAddress"}
!4 = !{i64 0, !"URLSearchParams"}
!5 = !{i64 0, !"Response"}
!6 = !{i64 0, !"URL"}
!7 = !{i64 0, !"Hmac"}
!8 = !{i64 0, !"Script"}
!9 = !{!"/FAILIFMISMATCH:\22_ITERATOR_DEBUG_LEVEL=0\22"}
!10 = !{!"/DEFAULTLIB:libcmt.lib"}
!11 = !{!"/NODEFAULTLIB:libcmtd.lib"}
!12 = !{!"/FAILIFMISMATCH:\22RuntimeLibrary=MT_StaticRelease\22"}
!13 = !{i64 0, !"__module_init_9610421887249242215_any"}
!14 = !{i64 0, !"user_main"}
