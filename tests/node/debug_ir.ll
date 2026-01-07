; ModuleID = 'ts_module'
source_filename = "ts_module"
target datalayout = "e-m:w-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-windows-msvc"

%DataView_VTable = type { ptr, ptr, ptr, ptr }
%Uint8Array_VTable = type { ptr, ptr }
%Uint32Array_VTable = type { ptr, ptr }
%Float64Array_VTable = type { ptr, ptr }
%Generator_VTable = type { ptr, ptr, ptr }
%AsyncGenerator_VTable = type { ptr, ptr, ptr }
%SocketAddress_VTable = type { ptr, ptr }
%URLSearchParams_VTable = type { ptr, ptr, ptr, ptr, ptr, ptr, ptr, ptr, ptr, ptr }
%Response_VTable = type { ptr, ptr, ptr, ptr }
%URL_VTable = type { ptr, ptr, ptr, ptr }
%TsValue = type { i8, [7 x i8], i64 }
%Uint8Array = type { ptr, ptr, i64 }
%Uint32Array = type { ptr, ptr, i64 }
%Float64Array = type { ptr, ptr, i64 }
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
@parseFloat = external global ptr
@parseInt = external global ptr
@process = external global ptr
@TextEncoder = global ptr null
@null = global ptr null
@undefined = global ptr null
@Symbol = global ptr null
@TextDecoder = global ptr null
@https = global ptr null
@events = global ptr null
@http = global ptr null
@stream = global ptr null
@buffer = global ptr null
@fs = global ptr null
@path = global ptr null
@net = global ptr null
@util = global ptr null
@DataView_VTable_Global = constant %DataView_VTable { ptr null, ptr @DataView_get_property, ptr @DataView_getUint32, ptr @DataView_setUint32 }, !type !0
@0 = private unnamed_addr constant [7 x i8] c"buffer\00", align 1
@1 = private unnamed_addr constant [7 x i8] c"length\00", align 1
@Uint8Array_VTable_Global = constant %Uint8Array_VTable { ptr null, ptr @Uint8Array_get_property }, !type !1
@2 = private unnamed_addr constant [7 x i8] c"buffer\00", align 1
@3 = private unnamed_addr constant [7 x i8] c"length\00", align 1
@Uint32Array_VTable_Global = constant %Uint32Array_VTable { ptr null, ptr @Uint32Array_get_property }, !type !2
@4 = private unnamed_addr constant [7 x i8] c"buffer\00", align 1
@5 = private unnamed_addr constant [7 x i8] c"length\00", align 1
@Float64Array_VTable_Global = constant %Float64Array_VTable { ptr null, ptr @Float64Array_get_property }, !type !3
@Generator_VTable_Global = constant %Generator_VTable { ptr null, ptr @Generator_get_property, ptr @Generator_next }, !type !4
@AsyncGenerator_VTable_Global = constant %AsyncGenerator_VTable { ptr null, ptr @AsyncGenerator_get_property, ptr @AsyncGenerator_next }, !type !5
@6 = private unnamed_addr constant [8 x i8] c"address\00", align 1
@7 = private unnamed_addr constant [7 x i8] c"family\00", align 1
@8 = private unnamed_addr constant [10 x i8] c"flowlabel\00", align 1
@9 = private unnamed_addr constant [5 x i8] c"port\00", align 1
@SocketAddress_VTable_Global = constant %SocketAddress_VTable { ptr null, ptr @SocketAddress_get_property }, !type !6
@10 = private unnamed_addr constant [5 x i8] c"size\00", align 1
@URLSearchParams_VTable_Global = constant %URLSearchParams_VTable { ptr null, ptr @URLSearchParams_get_property, ptr @URLSearchParams_append, ptr @URLSearchParams_delete, ptr @URLSearchParams_get, ptr @URLSearchParams_getAll, ptr @URLSearchParams_has, ptr @URLSearchParams_set, ptr @URLSearchParams_sort, ptr @URLSearchParams_toString }, !type !7
@11 = private unnamed_addr constant [3 x i8] c"ok\00", align 1
@12 = private unnamed_addr constant [7 x i8] c"status\00", align 1
@13 = private unnamed_addr constant [11 x i8] c"statusText\00", align 1
@Response_VTable_Global = constant %Response_VTable { ptr null, ptr @Response_get_property, ptr @Response_json, ptr @Response_text }, !type !8
@14 = private unnamed_addr constant [5 x i8] c"hash\00", align 1
@15 = private unnamed_addr constant [5 x i8] c"host\00", align 1
@16 = private unnamed_addr constant [9 x i8] c"hostname\00", align 1
@17 = private unnamed_addr constant [5 x i8] c"href\00", align 1
@18 = private unnamed_addr constant [7 x i8] c"origin\00", align 1
@19 = private unnamed_addr constant [9 x i8] c"password\00", align 1
@20 = private unnamed_addr constant [9 x i8] c"pathname\00", align 1
@21 = private unnamed_addr constant [5 x i8] c"port\00", align 1
@22 = private unnamed_addr constant [9 x i8] c"protocol\00", align 1
@23 = private unnamed_addr constant [7 x i8] c"search\00", align 1
@24 = private unnamed_addr constant [13 x i8] c"searchParams\00", align 1
@25 = private unnamed_addr constant [9 x i8] c"username\00", align 1
@URL_VTable_Global = constant %URL_VTable { ptr null, ptr @URL_get_property, ptr @URL_toJSON, ptr @URL_toString }, !type !9
@26 = private unnamed_addr constant [8 x i8] c"exports\00", align 1
@27 = private unnamed_addr constant [8 x i8] c"exports\00", align 1
@28 = private unnamed_addr constant [27 x i8] c"=== Util Module Tests ===\0A\00", align 1
@29 = private unnamed_addr constant [9 x i8] c"Hello %s\00", align 1
@30 = private unnamed_addr constant [6 x i8] c"World\00", align 1
@31 = private unnamed_addr constant [7 x i8] c"string\00", align 1
@32 = private unnamed_addr constant [39 x i8] c"FAIL: util.format should return string\00", align 1
@33 = private unnamed_addr constant [26 x i8] c"PASS: util.format with %s\00", align 1
@34 = private unnamed_addr constant [10 x i8] c"  Result:\00", align 1
@35 = private unnamed_addr constant [33 x i8] c"FAIL: util.format %s - Exception\00", align 1
@36 = private unnamed_addr constant [10 x i8] c"Count: %d\00", align 1
@37 = private unnamed_addr constant [7 x i8] c"string\00", align 1
@38 = private unnamed_addr constant [39 x i8] c"FAIL: util.format should return string\00", align 1
@39 = private unnamed_addr constant [26 x i8] c"PASS: util.format with %d\00", align 1
@40 = private unnamed_addr constant [10 x i8] c"  Result:\00", align 1
@41 = private unnamed_addr constant [33 x i8] c"FAIL: util.format %d - Exception\00", align 1
@42 = private unnamed_addr constant [16 x i8] c"%s has %d items\00", align 1
@43 = private unnamed_addr constant [5 x i8] c"Cart\00", align 1
@44 = private unnamed_addr constant [7 x i8] c"string\00", align 1
@45 = private unnamed_addr constant [39 x i8] c"FAIL: util.format should return string\00", align 1
@46 = private unnamed_addr constant [40 x i8] c"PASS: util.format multiple placeholders\00", align 1
@47 = private unnamed_addr constant [10 x i8] c"  Result:\00", align 1
@48 = private unnamed_addr constant [39 x i8] c"FAIL: util.format multiple - Exception\00", align 1
@49 = private unnamed_addr constant [6 x i8] c"hello\00", align 1
@50 = private unnamed_addr constant [7 x i8] c"string\00", align 1
@51 = private unnamed_addr constant [40 x i8] c"FAIL: util.inspect should return string\00", align 1
@52 = private unnamed_addr constant [26 x i8] c"PASS: util.inspect string\00", align 1
@53 = private unnamed_addr constant [10 x i8] c"  Result:\00", align 1
@54 = private unnamed_addr constant [31 x i8] c"FAIL: util.inspect - Exception\00", align 1
@55 = private unnamed_addr constant [7 x i8] c"string\00", align 1
@56 = private unnamed_addr constant [40 x i8] c"FAIL: util.inspect should return string\00", align 1
@57 = private unnamed_addr constant [26 x i8] c"PASS: util.inspect number\00", align 1
@58 = private unnamed_addr constant [10 x i8] c"  Result:\00", align 1
@59 = private unnamed_addr constant [38 x i8] c"FAIL: util.inspect number - Exception\00", align 1
@60 = private unnamed_addr constant [2 x i8] c"a\00", align 1
@__ts_const_undefined_value = private constant %TsValue zeroinitializer
@61 = private unnamed_addr constant [2 x i8] c"b\00", align 1
@__ts_const_undefined_value.1 = private constant %TsValue zeroinitializer
@62 = private unnamed_addr constant [7 x i8] c"string\00", align 1
@63 = private unnamed_addr constant [40 x i8] c"FAIL: util.inspect should return string\00", align 1
@64 = private unnamed_addr constant [26 x i8] c"PASS: util.inspect object\00", align 1
@65 = private unnamed_addr constant [10 x i8] c"  Result:\00", align 1
@66 = private unnamed_addr constant [38 x i8] c"FAIL: util.inspect object - Exception\00", align 1
@67 = private unnamed_addr constant [8 x i8] c"boolean\00", align 1
@68 = private unnamed_addr constant [8 x i8] c"boolean\00", align 1
@69 = private unnamed_addr constant [45 x i8] c"FAIL: util.types.isMap should return boolean\00", align 1
@70 = private unnamed_addr constant [23 x i8] c"PASS: util.types.isMap\00", align 1
@71 = private unnamed_addr constant [10 x i8] c"  Result:\00", align 1
@72 = private unnamed_addr constant [35 x i8] c"FAIL: util.types.isMap - Exception\00", align 1
@73 = private unnamed_addr constant [8 x i8] c"boolean\00", align 1
@74 = private unnamed_addr constant [8 x i8] c"boolean\00", align 1
@75 = private unnamed_addr constant [45 x i8] c"FAIL: util.types.isSet should return boolean\00", align 1
@76 = private unnamed_addr constant [23 x i8] c"PASS: util.types.isSet\00", align 1
@77 = private unnamed_addr constant [10 x i8] c"  Result:\00", align 1
@78 = private unnamed_addr constant [35 x i8] c"FAIL: util.types.isSet - Exception\00", align 1
@79 = private unnamed_addr constant [8 x i8] c"boolean\00", align 1
@80 = private unnamed_addr constant [8 x i8] c"boolean\00", align 1
@81 = private unnamed_addr constant [46 x i8] c"FAIL: util.types.isDate should return boolean\00", align 1
@82 = private unnamed_addr constant [24 x i8] c"PASS: util.types.isDate\00", align 1
@83 = private unnamed_addr constant [10 x i8] c"  Result:\00", align 1
@84 = private unnamed_addr constant [36 x i8] c"FAIL: util.types.isDate - Exception\00", align 1
@85 = private unnamed_addr constant [6 x i8] c"hello\00", align 1
@86 = private unnamed_addr constant [8 x i8] c"boolean\00", align 1
@87 = private unnamed_addr constant [8 x i8] c"boolean\00", align 1
@88 = private unnamed_addr constant [52 x i8] c"FAIL: util.types.isTypedArray should return boolean\00", align 1
@89 = private unnamed_addr constant [30 x i8] c"PASS: util.types.isTypedArray\00", align 1
@90 = private unnamed_addr constant [10 x i8] c"  Result:\00", align 1
@91 = private unnamed_addr constant [42 x i8] c"FAIL: util.types.isTypedArray - Exception\00", align 1
@92 = private unnamed_addr constant [17 x i8] c"\0A=== Summary ===\00", align 1
@93 = private unnamed_addr constant [18 x i8] c"All tests passed!\00", align 1
@94 = private unnamed_addr constant [16 x i8] c" test(s) failed\00", align 1
@95 = private unnamed_addr constant [8 x i8] c"exports\00", align 1
@__ts_const_undefined_value.2 = private constant %TsValue zeroinitializer
@96 = private unnamed_addr constant [64 x i8] c"E:\\src\\github.com\\cgrinker\\ts-aoc\\tests\\node\\util\\util_basic.ts\00", align 1

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

define ptr @DataView_get_property(ptr %0, ptr %1) #0 {
entry:
  %2 = call ptr @ts_value_make_undefined()
  ret ptr %2
}

declare ptr @ts_string_create(ptr)

declare i1 @ts_string_eq(ptr, ptr)

declare ptr @ts_value_make_undefined()

define ptr @Uint8Array_get_property(ptr %0, ptr %1) #0 {
entry:
  %2 = call ptr @ts_string_create(ptr @0)
  %3 = call i1 @ts_string_eq(ptr %1, ptr %2)
  br i1 %3, label %match_buffer, label %next_buffer

match_buffer:                                     ; preds = %entry
  %4 = getelementptr inbounds %Uint8Array, ptr %0, i32 0, i32 1
  %5 = load ptr, ptr %4, align 8
  %6 = call ptr @ts_value_box_any(ptr %5)
  ret ptr %6

next_buffer:                                      ; preds = %entry
  %7 = call ptr @ts_string_create(ptr @1)
  %8 = call i1 @ts_string_eq(ptr %1, ptr %7)
  br i1 %8, label %match_length, label %next_length

match_length:                                     ; preds = %next_buffer
  %9 = getelementptr inbounds %Uint8Array, ptr %0, i32 0, i32 2
  %10 = load i64, ptr %9, align 8
  %11 = call ptr @ts_value_make_int(i64 %10)
  ret ptr %11

next_length:                                      ; preds = %next_buffer
  %12 = call ptr @ts_value_make_undefined()
  ret ptr %12
}

declare ptr @ts_value_box_any(ptr)

declare ptr @ts_value_make_int(i64)

define ptr @Uint32Array_get_property(ptr %0, ptr %1) #0 {
entry:
  %2 = call ptr @ts_string_create(ptr @2)
  %3 = call i1 @ts_string_eq(ptr %1, ptr %2)
  br i1 %3, label %match_buffer, label %next_buffer

match_buffer:                                     ; preds = %entry
  %4 = getelementptr inbounds %Uint32Array, ptr %0, i32 0, i32 1
  %5 = load ptr, ptr %4, align 8
  %6 = call ptr @ts_value_box_any(ptr %5)
  ret ptr %6

next_buffer:                                      ; preds = %entry
  %7 = call ptr @ts_string_create(ptr @3)
  %8 = call i1 @ts_string_eq(ptr %1, ptr %7)
  br i1 %8, label %match_length, label %next_length

match_length:                                     ; preds = %next_buffer
  %9 = getelementptr inbounds %Uint32Array, ptr %0, i32 0, i32 2
  %10 = load i64, ptr %9, align 8
  %11 = call ptr @ts_value_make_int(i64 %10)
  ret ptr %11

next_length:                                      ; preds = %next_buffer
  %12 = call ptr @ts_value_make_undefined()
  ret ptr %12
}

define ptr @Float64Array_get_property(ptr %0, ptr %1) #0 {
entry:
  %2 = call ptr @ts_string_create(ptr @4)
  %3 = call i1 @ts_string_eq(ptr %1, ptr %2)
  br i1 %3, label %match_buffer, label %next_buffer

match_buffer:                                     ; preds = %entry
  %4 = getelementptr inbounds %Float64Array, ptr %0, i32 0, i32 1
  %5 = load ptr, ptr %4, align 8
  %6 = call ptr @ts_value_box_any(ptr %5)
  ret ptr %6

next_buffer:                                      ; preds = %entry
  %7 = call ptr @ts_string_create(ptr @5)
  %8 = call i1 @ts_string_eq(ptr %1, ptr %7)
  br i1 %8, label %match_length, label %next_length

match_length:                                     ; preds = %next_buffer
  %9 = getelementptr inbounds %Float64Array, ptr %0, i32 0, i32 2
  %10 = load i64, ptr %9, align 8
  %11 = call ptr @ts_value_make_int(i64 %10)
  ret ptr %11

next_length:                                      ; preds = %next_buffer
  %12 = call ptr @ts_value_make_undefined()
  ret ptr %12
}

define ptr @Generator_get_property(ptr %0, ptr %1) #0 {
entry:
  %2 = call ptr @ts_value_make_undefined()
  ret ptr %2
}

declare ptr @Generator_next(ptr, ptr, ptr) #0

define ptr @AsyncGenerator_get_property(ptr %0, ptr %1) #0 {
entry:
  %2 = call ptr @ts_value_make_undefined()
  ret ptr %2
}

declare ptr @AsyncGenerator_next(ptr, ptr, ptr) #0

define ptr @SocketAddress_get_property(ptr %0, ptr %1) #0 {
entry:
  %2 = call ptr @ts_string_create(ptr @6)
  %3 = call i1 @ts_string_eq(ptr %1, ptr %2)
  br i1 %3, label %match_address, label %next_address

match_address:                                    ; preds = %entry
  %4 = getelementptr inbounds %SocketAddress, ptr %0, i32 0, i32 1
  %5 = load ptr, ptr %4, align 8
  %6 = call ptr @ts_value_make_string(ptr %5)
  ret ptr %6

next_address:                                     ; preds = %entry
  %7 = call ptr @ts_string_create(ptr @7)
  %8 = call i1 @ts_string_eq(ptr %1, ptr %7)
  br i1 %8, label %match_family, label %next_family

match_family:                                     ; preds = %next_address
  %9 = getelementptr inbounds %SocketAddress, ptr %0, i32 0, i32 2
  %10 = load ptr, ptr %9, align 8
  %11 = call ptr @ts_value_make_string(ptr %10)
  ret ptr %11

next_family:                                      ; preds = %next_address
  %12 = call ptr @ts_string_create(ptr @8)
  %13 = call i1 @ts_string_eq(ptr %1, ptr %12)
  br i1 %13, label %match_flowlabel, label %next_flowlabel

match_flowlabel:                                  ; preds = %next_family
  %14 = getelementptr inbounds %SocketAddress, ptr %0, i32 0, i32 3
  %15 = load i64, ptr %14, align 8
  %16 = call ptr @ts_value_make_int(i64 %15)
  ret ptr %16

next_flowlabel:                                   ; preds = %next_family
  %17 = call ptr @ts_string_create(ptr @9)
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

declare ptr @ts_value_make_string(ptr)

define ptr @URLSearchParams_get_property(ptr %0, ptr %1) #0 {
entry:
  %2 = call ptr @ts_string_create(ptr @10)
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

declare ptr @URLSearchParams_get(ptr, ptr, ptr) #0

declare ptr @URLSearchParams_getAll(ptr, ptr, ptr) #0

declare i1 @URLSearchParams_has(ptr, ptr, ptr) #0

declare void @URLSearchParams_set(ptr, ptr, ptr, ptr) #0

declare void @URLSearchParams_sort(ptr, ptr) #0

declare ptr @URLSearchParams_toString(ptr, ptr) #0

define ptr @Response_get_property(ptr %0, ptr %1) #0 {
entry:
  %2 = call ptr @ts_string_create(ptr @11)
  %3 = call i1 @ts_string_eq(ptr %1, ptr %2)
  br i1 %3, label %match_ok, label %next_ok

match_ok:                                         ; preds = %entry
  %4 = getelementptr inbounds %Response, ptr %0, i32 0, i32 1
  %5 = load i1, ptr %4, align 1
  %6 = call ptr @ts_value_make_bool(i1 %5)
  ret ptr %6

next_ok:                                          ; preds = %entry
  %7 = call ptr @ts_string_create(ptr @12)
  %8 = call i1 @ts_string_eq(ptr %1, ptr %7)
  br i1 %8, label %match_status, label %next_status

match_status:                                     ; preds = %next_ok
  %9 = getelementptr inbounds %Response, ptr %0, i32 0, i32 2
  %10 = load i64, ptr %9, align 8
  %11 = call ptr @ts_value_make_int(i64 %10)
  ret ptr %11

next_status:                                      ; preds = %next_ok
  %12 = call ptr @ts_string_create(ptr @13)
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
  %2 = call ptr @ts_string_create(ptr @14)
  %3 = call i1 @ts_string_eq(ptr %1, ptr %2)
  br i1 %3, label %match_hash, label %next_hash

match_hash:                                       ; preds = %entry
  %4 = getelementptr inbounds %URL, ptr %0, i32 0, i32 1
  %5 = load ptr, ptr %4, align 8
  %6 = call ptr @ts_value_make_string(ptr %5)
  ret ptr %6

next_hash:                                        ; preds = %entry
  %7 = call ptr @ts_string_create(ptr @15)
  %8 = call i1 @ts_string_eq(ptr %1, ptr %7)
  br i1 %8, label %match_host, label %next_host

match_host:                                       ; preds = %next_hash
  %9 = getelementptr inbounds %URL, ptr %0, i32 0, i32 2
  %10 = load ptr, ptr %9, align 8
  %11 = call ptr @ts_value_make_string(ptr %10)
  ret ptr %11

next_host:                                        ; preds = %next_hash
  %12 = call ptr @ts_string_create(ptr @16)
  %13 = call i1 @ts_string_eq(ptr %1, ptr %12)
  br i1 %13, label %match_hostname, label %next_hostname

match_hostname:                                   ; preds = %next_host
  %14 = getelementptr inbounds %URL, ptr %0, i32 0, i32 3
  %15 = load ptr, ptr %14, align 8
  %16 = call ptr @ts_value_make_string(ptr %15)
  ret ptr %16

next_hostname:                                    ; preds = %next_host
  %17 = call ptr @ts_string_create(ptr @17)
  %18 = call i1 @ts_string_eq(ptr %1, ptr %17)
  br i1 %18, label %match_href, label %next_href

match_href:                                       ; preds = %next_hostname
  %19 = getelementptr inbounds %URL, ptr %0, i32 0, i32 4
  %20 = load ptr, ptr %19, align 8
  %21 = call ptr @ts_value_make_string(ptr %20)
  ret ptr %21

next_href:                                        ; preds = %next_hostname
  %22 = call ptr @ts_string_create(ptr @18)
  %23 = call i1 @ts_string_eq(ptr %1, ptr %22)
  br i1 %23, label %match_origin, label %next_origin

match_origin:                                     ; preds = %next_href
  %24 = getelementptr inbounds %URL, ptr %0, i32 0, i32 5
  %25 = load ptr, ptr %24, align 8
  %26 = call ptr @ts_value_make_string(ptr %25)
  ret ptr %26

next_origin:                                      ; preds = %next_href
  %27 = call ptr @ts_string_create(ptr @19)
  %28 = call i1 @ts_string_eq(ptr %1, ptr %27)
  br i1 %28, label %match_password, label %next_password

match_password:                                   ; preds = %next_origin
  %29 = getelementptr inbounds %URL, ptr %0, i32 0, i32 6
  %30 = load ptr, ptr %29, align 8
  %31 = call ptr @ts_value_make_string(ptr %30)
  ret ptr %31

next_password:                                    ; preds = %next_origin
  %32 = call ptr @ts_string_create(ptr @20)
  %33 = call i1 @ts_string_eq(ptr %1, ptr %32)
  br i1 %33, label %match_pathname, label %next_pathname

match_pathname:                                   ; preds = %next_password
  %34 = getelementptr inbounds %URL, ptr %0, i32 0, i32 7
  %35 = load ptr, ptr %34, align 8
  %36 = call ptr @ts_value_make_string(ptr %35)
  ret ptr %36

next_pathname:                                    ; preds = %next_password
  %37 = call ptr @ts_string_create(ptr @21)
  %38 = call i1 @ts_string_eq(ptr %1, ptr %37)
  br i1 %38, label %match_port, label %next_port

match_port:                                       ; preds = %next_pathname
  %39 = getelementptr inbounds %URL, ptr %0, i32 0, i32 8
  %40 = load ptr, ptr %39, align 8
  %41 = call ptr @ts_value_make_string(ptr %40)
  ret ptr %41

next_port:                                        ; preds = %next_pathname
  %42 = call ptr @ts_string_create(ptr @22)
  %43 = call i1 @ts_string_eq(ptr %1, ptr %42)
  br i1 %43, label %match_protocol, label %next_protocol

match_protocol:                                   ; preds = %next_port
  %44 = getelementptr inbounds %URL, ptr %0, i32 0, i32 9
  %45 = load ptr, ptr %44, align 8
  %46 = call ptr @ts_value_make_string(ptr %45)
  ret ptr %46

next_protocol:                                    ; preds = %next_port
  %47 = call ptr @ts_string_create(ptr @23)
  %48 = call i1 @ts_string_eq(ptr %1, ptr %47)
  br i1 %48, label %match_search, label %next_search

match_search:                                     ; preds = %next_protocol
  %49 = getelementptr inbounds %URL, ptr %0, i32 0, i32 10
  %50 = load ptr, ptr %49, align 8
  %51 = call ptr @ts_value_make_string(ptr %50)
  ret ptr %51

next_search:                                      ; preds = %next_protocol
  %52 = call ptr @ts_string_create(ptr @24)
  %53 = call i1 @ts_string_eq(ptr %1, ptr %52)
  br i1 %53, label %match_searchParams, label %next_searchParams

match_searchParams:                               ; preds = %next_search
  %54 = getelementptr inbounds %URL, ptr %0, i32 0, i32 11
  %55 = load ptr, ptr %54, align 8
  %56 = call ptr @ts_value_make_object(ptr %55)
  ret ptr %56

next_searchParams:                                ; preds = %next_search
  %57 = call ptr @ts_string_create(ptr @25)
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

define ptr @__module_init_13333308732889039661_any(ptr %context, ptr %module) #0 !type !14 {
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
  %2 = call ptr @ts_string_create(ptr @26)
  %3 = call ptr @ts_value_make_string(ptr %2)
  %4 = call ptr @ts_object_get_dynamic(ptr %1, ptr %3)
  store ptr %4, ptr %exports, align 8
  %module3 = load ptr, ptr %module1, align 8
  %5 = call ptr @ts_value_box_any(ptr %module3)
  %6 = call ptr @ts_string_create(ptr @27)
  %7 = call ptr @ts_value_make_string(ptr %6)
  %8 = call ptr @ts_object_get_dynamic(ptr %5, ptr %7)
  ret ptr %8

return:                                           ; preds = %dead
  %9 = load ptr, ptr %returnValue, align 8
  ret ptr %9

dead:                                             ; No predecessors!
  br label %return
}

define double @user_main(ptr %context) #0 !type !15 {
entry:
  %buf = alloca ptr, align 8
  %pendingExc185 = alloca ptr, align 8
  %date = alloca ptr, align 8
  %pendingExc164 = alloca ptr, align 8
  %set = alloca ptr, align 8
  %pendingExc143 = alloca ptr, align 8
  %result = alloca i1, align 1
  %map = alloca ptr, align 8
  %pendingExc122 = alloca ptr, align 8
  %pendingExc90 = alloca ptr, align 8
  %pendingExc70 = alloca ptr, align 8
  %inspected = alloca ptr, align 8
  %pendingExc50 = alloca ptr, align 8
  %pendingExc30 = alloca ptr, align 8
  %pendingExc10 = alloca ptr, align 8
  %e = alloca ptr, align 8
  %formatted = alloca ptr, align 8
  %pendingExc = alloca ptr, align 8
  %failures = alloca i64, align 8
  %returnValue = alloca double, align 8
  %continueTarget = alloca ptr, align 8
  %breakTarget = alloca ptr, align 8
  %shouldContinue = alloca i1, align 1
  %shouldBreak = alloca i1, align 1
  %shouldReturn = alloca i1, align 1
  store i1 false, ptr %shouldReturn, align 1
  store i1 false, ptr %shouldBreak, align 1
  store i1 false, ptr %shouldContinue, align 1
  store double 0.000000e+00, ptr %returnValue, align 8
  store i64 0, ptr %failures, align 8
  %0 = call ptr @ts_string_create(ptr @28)
  call void @ts_console_log(ptr %0)
  store ptr null, ptr %pendingExc, align 8
  %1 = call ptr @ts_push_exception_handler()
  %2 = call i32 @_setjmp(ptr %1, ptr null)
  %3 = icmp ne i32 %2, 0
  br i1 %3, label %catch, label %try

return:                                           ; preds = %dead, %check_return199, %check_return178, %check_return157, %check_return136, %check_return115, %check_return83, %check_return63, %check_return43, %check_return23, %check_return
  %4 = load double, ptr %returnValue, align 8
  ret double %4

try:                                              ; preds = %entry
  %5 = call ptr @ts_string_create(ptr @29)
  %6 = call ptr @ts_array_create_sized(i64 1)
  %7 = call ptr @ts_string_create(ptr @30)
  %8 = call ptr @ts_value_make_string(ptr %7)
  %9 = ptrtoint ptr %8 to i64
  call void @ts_array_push(ptr %6, i64 %9)
  %10 = call ptr @ts_util_format(ptr %5, ptr %6)
  store ptr %10, ptr %formatted, align 8
  %formatted1 = load ptr, ptr %formatted, align 8
  %11 = call ptr @ts_typeof(ptr %formatted1)
  %12 = call ptr @ts_string_create(ptr @31)
  %13 = call i1 @ts_string_eq(ptr %11, ptr %12)
  %14 = xor i1 %13, true
  br i1 %14, label %then, label %else

catch:                                            ; preds = %entry
  %15 = call ptr @ts_get_exception()
  %16 = call ptr @ts_push_exception_handler()
  %17 = call i32 @_setjmp(ptr %16, ptr null)
  %18 = icmp ne i32 %17, 0
  br i1 %18, label %catch_throw, label %catch_body

finally:                                          ; preds = %catch_throw, %catch_body, %ifcont
  %19 = load ptr, ptr %pendingExc, align 8
  %20 = icmp ne ptr %19, null
  br i1 %20, label %rethrow, label %check_return

try_merge:                                        ; preds = %check_continue
  store ptr null, ptr %pendingExc10, align 8
  %21 = call ptr @ts_push_exception_handler()
  %22 = call i32 @_setjmp(ptr %21, ptr null)
  %23 = icmp ne i32 %22, 0
  br i1 %23, label %catch7, label %try6

then:                                             ; preds = %try
  %24 = call ptr @ts_string_create(ptr @32)
  call void @ts_console_log(ptr %24)
  %failures2 = load i64, ptr %failures, align 8
  %incdec = add i64 %failures2, 1
  store i64 %incdec, ptr %failures, align 8
  br label %ifcont

else:                                             ; preds = %try
  %25 = call ptr @ts_string_create(ptr @33)
  call void @ts_console_log(ptr %25)
  %26 = call ptr @ts_string_create(ptr @34)
  call void @ts_console_log(ptr %26)
  %formatted3 = load ptr, ptr %formatted, align 8
  call void @ts_console_log(ptr %formatted3)
  br label %ifcont

ifcont:                                           ; preds = %else, %then
  call void @ts_pop_exception_handler()
  br label %finally

catch_body:                                       ; preds = %catch
  store ptr %15, ptr %e, align 8
  %27 = call ptr @ts_string_create(ptr @35)
  call void @ts_console_log(ptr %27)
  %failures4 = load i64, ptr %failures, align 8
  %incdec5 = add i64 %failures4, 1
  store i64 %incdec5, ptr %failures, align 8
  call void @ts_pop_exception_handler()
  br label %finally

catch_throw:                                      ; preds = %catch
  %28 = call ptr @ts_get_exception()
  store ptr %28, ptr %pendingExc, align 8
  br label %finally

rethrow:                                          ; preds = %finally
  call void @ts_throw(ptr %19)
  unreachable

check_return:                                     ; preds = %finally
  %29 = load i1, ptr %shouldReturn, align 1
  br i1 %29, label %return, label %check_break

check_break:                                      ; preds = %check_return
  %30 = load i1, ptr %shouldBreak, align 1
  br label %check_continue

check_continue:                                   ; preds = %check_break
  %31 = load i1, ptr %shouldContinue, align 1
  br label %try_merge

try6:                                             ; preds = %try_merge
  %32 = call ptr @ts_string_create(ptr @36)
  %33 = call ptr @ts_array_create_sized(i64 1)
  %34 = call ptr @ts_value_make_int(i64 42)
  %35 = ptrtoint ptr %34 to i64
  call void @ts_array_push(ptr %33, i64 %35)
  %36 = call ptr @ts_util_format(ptr %32, ptr %33)
  store ptr %36, ptr %formatted, align 8
  %formatted11 = load ptr, ptr %formatted, align 8
  %37 = call ptr @ts_typeof(ptr %formatted11)
  %38 = call ptr @ts_string_create(ptr @37)
  %39 = call i1 @ts_string_eq(ptr %37, ptr %38)
  %40 = xor i1 %39, true
  br i1 %40, label %then12, label %else15

catch7:                                           ; preds = %try_merge
  %41 = call ptr @ts_get_exception()
  %42 = call ptr @ts_push_exception_handler()
  %43 = call i32 @_setjmp(ptr %42, ptr null)
  %44 = icmp ne i32 %43, 0
  br i1 %44, label %catch_throw19, label %catch_body18

finally8:                                         ; preds = %catch_throw19, %catch_body18, %ifcont17
  %45 = load ptr, ptr %pendingExc10, align 8
  %46 = icmp ne ptr %45, null
  br i1 %46, label %rethrow22, label %check_return23

try_merge9:                                       ; preds = %check_continue25
  store ptr null, ptr %pendingExc30, align 8
  %47 = call ptr @ts_push_exception_handler()
  %48 = call i32 @_setjmp(ptr %47, ptr null)
  %49 = icmp ne i32 %48, 0
  br i1 %49, label %catch27, label %try26

then12:                                           ; preds = %try6
  %50 = call ptr @ts_string_create(ptr @38)
  call void @ts_console_log(ptr %50)
  %failures13 = load i64, ptr %failures, align 8
  %incdec14 = add i64 %failures13, 1
  store i64 %incdec14, ptr %failures, align 8
  br label %ifcont17

else15:                                           ; preds = %try6
  %51 = call ptr @ts_string_create(ptr @39)
  call void @ts_console_log(ptr %51)
  %52 = call ptr @ts_string_create(ptr @40)
  call void @ts_console_log(ptr %52)
  %formatted16 = load ptr, ptr %formatted, align 8
  call void @ts_console_log(ptr %formatted16)
  br label %ifcont17

ifcont17:                                         ; preds = %else15, %then12
  call void @ts_pop_exception_handler()
  br label %finally8

catch_body18:                                     ; preds = %catch7
  store ptr %41, ptr %e, align 8
  %53 = call ptr @ts_string_create(ptr @41)
  call void @ts_console_log(ptr %53)
  %failures20 = load i64, ptr %failures, align 8
  %incdec21 = add i64 %failures20, 1
  store i64 %incdec21, ptr %failures, align 8
  call void @ts_pop_exception_handler()
  br label %finally8

catch_throw19:                                    ; preds = %catch7
  %54 = call ptr @ts_get_exception()
  store ptr %54, ptr %pendingExc10, align 8
  br label %finally8

rethrow22:                                        ; preds = %finally8
  call void @ts_throw(ptr %45)
  unreachable

check_return23:                                   ; preds = %finally8
  %55 = load i1, ptr %shouldReturn, align 1
  br i1 %55, label %return, label %check_break24

check_break24:                                    ; preds = %check_return23
  %56 = load i1, ptr %shouldBreak, align 1
  br label %check_continue25

check_continue25:                                 ; preds = %check_break24
  %57 = load i1, ptr %shouldContinue, align 1
  br label %try_merge9

try26:                                            ; preds = %try_merge9
  %58 = call ptr @ts_string_create(ptr @42)
  %59 = call ptr @ts_array_create_sized(i64 2)
  %60 = call ptr @ts_string_create(ptr @43)
  %61 = call ptr @ts_value_make_string(ptr %60)
  %62 = ptrtoint ptr %61 to i64
  call void @ts_array_push(ptr %59, i64 %62)
  %63 = call ptr @ts_value_make_int(i64 5)
  %64 = ptrtoint ptr %63 to i64
  call void @ts_array_push(ptr %59, i64 %64)
  %65 = call ptr @ts_util_format(ptr %58, ptr %59)
  store ptr %65, ptr %formatted, align 8
  %formatted31 = load ptr, ptr %formatted, align 8
  %66 = call ptr @ts_typeof(ptr %formatted31)
  %67 = call ptr @ts_string_create(ptr @44)
  %68 = call i1 @ts_string_eq(ptr %66, ptr %67)
  %69 = xor i1 %68, true
  br i1 %69, label %then32, label %else35

catch27:                                          ; preds = %try_merge9
  %70 = call ptr @ts_get_exception()
  %71 = call ptr @ts_push_exception_handler()
  %72 = call i32 @_setjmp(ptr %71, ptr null)
  %73 = icmp ne i32 %72, 0
  br i1 %73, label %catch_throw39, label %catch_body38

finally28:                                        ; preds = %catch_throw39, %catch_body38, %ifcont37
  %74 = load ptr, ptr %pendingExc30, align 8
  %75 = icmp ne ptr %74, null
  br i1 %75, label %rethrow42, label %check_return43

try_merge29:                                      ; preds = %check_continue45
  store ptr null, ptr %pendingExc50, align 8
  %76 = call ptr @ts_push_exception_handler()
  %77 = call i32 @_setjmp(ptr %76, ptr null)
  %78 = icmp ne i32 %77, 0
  br i1 %78, label %catch47, label %try46

then32:                                           ; preds = %try26
  %79 = call ptr @ts_string_create(ptr @45)
  call void @ts_console_log(ptr %79)
  %failures33 = load i64, ptr %failures, align 8
  %incdec34 = add i64 %failures33, 1
  store i64 %incdec34, ptr %failures, align 8
  br label %ifcont37

else35:                                           ; preds = %try26
  %80 = call ptr @ts_string_create(ptr @46)
  call void @ts_console_log(ptr %80)
  %81 = call ptr @ts_string_create(ptr @47)
  call void @ts_console_log(ptr %81)
  %formatted36 = load ptr, ptr %formatted, align 8
  call void @ts_console_log(ptr %formatted36)
  br label %ifcont37

ifcont37:                                         ; preds = %else35, %then32
  call void @ts_pop_exception_handler()
  br label %finally28

catch_body38:                                     ; preds = %catch27
  store ptr %70, ptr %e, align 8
  %82 = call ptr @ts_string_create(ptr @48)
  call void @ts_console_log(ptr %82)
  %failures40 = load i64, ptr %failures, align 8
  %incdec41 = add i64 %failures40, 1
  store i64 %incdec41, ptr %failures, align 8
  call void @ts_pop_exception_handler()
  br label %finally28

catch_throw39:                                    ; preds = %catch27
  %83 = call ptr @ts_get_exception()
  store ptr %83, ptr %pendingExc30, align 8
  br label %finally28

rethrow42:                                        ; preds = %finally28
  call void @ts_throw(ptr %74)
  unreachable

check_return43:                                   ; preds = %finally28
  %84 = load i1, ptr %shouldReturn, align 1
  br i1 %84, label %return, label %check_break44

check_break44:                                    ; preds = %check_return43
  %85 = load i1, ptr %shouldBreak, align 1
  br label %check_continue45

check_continue45:                                 ; preds = %check_break44
  %86 = load i1, ptr %shouldContinue, align 1
  br label %try_merge29

try46:                                            ; preds = %try_merge29
  %87 = call ptr @ts_string_create(ptr @49)
  %88 = call ptr @ts_util_inspect(ptr %87, ptr null)
  store ptr %88, ptr %inspected, align 8
  %inspected51 = load ptr, ptr %inspected, align 8
  %89 = call ptr @ts_typeof(ptr %inspected51)
  %90 = call ptr @ts_string_create(ptr @50)
  %91 = call i1 @ts_string_eq(ptr %89, ptr %90)
  %92 = xor i1 %91, true
  br i1 %92, label %then52, label %else55

catch47:                                          ; preds = %try_merge29
  %93 = call ptr @ts_get_exception()
  %94 = call ptr @ts_push_exception_handler()
  %95 = call i32 @_setjmp(ptr %94, ptr null)
  %96 = icmp ne i32 %95, 0
  br i1 %96, label %catch_throw59, label %catch_body58

finally48:                                        ; preds = %catch_throw59, %catch_body58, %ifcont57
  %97 = load ptr, ptr %pendingExc50, align 8
  %98 = icmp ne ptr %97, null
  br i1 %98, label %rethrow62, label %check_return63

try_merge49:                                      ; preds = %check_continue65
  store ptr null, ptr %pendingExc70, align 8
  %99 = call ptr @ts_push_exception_handler()
  %100 = call i32 @_setjmp(ptr %99, ptr null)
  %101 = icmp ne i32 %100, 0
  br i1 %101, label %catch67, label %try66

then52:                                           ; preds = %try46
  %102 = call ptr @ts_string_create(ptr @51)
  call void @ts_console_log(ptr %102)
  %failures53 = load i64, ptr %failures, align 8
  %incdec54 = add i64 %failures53, 1
  store i64 %incdec54, ptr %failures, align 8
  br label %ifcont57

else55:                                           ; preds = %try46
  %103 = call ptr @ts_string_create(ptr @52)
  call void @ts_console_log(ptr %103)
  %104 = call ptr @ts_string_create(ptr @53)
  call void @ts_console_log(ptr %104)
  %inspected56 = load ptr, ptr %inspected, align 8
  call void @ts_console_log(ptr %inspected56)
  br label %ifcont57

ifcont57:                                         ; preds = %else55, %then52
  call void @ts_pop_exception_handler()
  br label %finally48

catch_body58:                                     ; preds = %catch47
  store ptr %93, ptr %e, align 8
  %105 = call ptr @ts_string_create(ptr @54)
  call void @ts_console_log(ptr %105)
  %failures60 = load i64, ptr %failures, align 8
  %incdec61 = add i64 %failures60, 1
  store i64 %incdec61, ptr %failures, align 8
  call void @ts_pop_exception_handler()
  br label %finally48

catch_throw59:                                    ; preds = %catch47
  %106 = call ptr @ts_get_exception()
  store ptr %106, ptr %pendingExc50, align 8
  br label %finally48

rethrow62:                                        ; preds = %finally48
  call void @ts_throw(ptr %97)
  unreachable

check_return63:                                   ; preds = %finally48
  %107 = load i1, ptr %shouldReturn, align 1
  br i1 %107, label %return, label %check_break64

check_break64:                                    ; preds = %check_return63
  %108 = load i1, ptr %shouldBreak, align 1
  br label %check_continue65

check_continue65:                                 ; preds = %check_break64
  %109 = load i1, ptr %shouldContinue, align 1
  br label %try_merge49

try66:                                            ; preds = %try_merge49
  %110 = call ptr @ts_value_make_int(i64 42)
  %111 = call ptr @ts_util_inspect(ptr %110, ptr null)
  store ptr %111, ptr %inspected, align 8
  %inspected71 = load ptr, ptr %inspected, align 8
  %112 = call ptr @ts_typeof(ptr %inspected71)
  %113 = call ptr @ts_string_create(ptr @55)
  %114 = call i1 @ts_string_eq(ptr %112, ptr %113)
  %115 = xor i1 %114, true
  br i1 %115, label %then72, label %else75

catch67:                                          ; preds = %try_merge49
  %116 = call ptr @ts_get_exception()
  %117 = call ptr @ts_push_exception_handler()
  %118 = call i32 @_setjmp(ptr %117, ptr null)
  %119 = icmp ne i32 %118, 0
  br i1 %119, label %catch_throw79, label %catch_body78

finally68:                                        ; preds = %catch_throw79, %catch_body78, %ifcont77
  %120 = load ptr, ptr %pendingExc70, align 8
  %121 = icmp ne ptr %120, null
  br i1 %121, label %rethrow82, label %check_return83

try_merge69:                                      ; preds = %check_continue85
  store ptr null, ptr %pendingExc90, align 8
  %122 = call ptr @ts_push_exception_handler()
  %123 = call i32 @_setjmp(ptr %122, ptr null)
  %124 = icmp ne i32 %123, 0
  br i1 %124, label %catch87, label %try86

then72:                                           ; preds = %try66
  %125 = call ptr @ts_string_create(ptr @56)
  call void @ts_console_log(ptr %125)
  %failures73 = load i64, ptr %failures, align 8
  %incdec74 = add i64 %failures73, 1
  store i64 %incdec74, ptr %failures, align 8
  br label %ifcont77

else75:                                           ; preds = %try66
  %126 = call ptr @ts_string_create(ptr @57)
  call void @ts_console_log(ptr %126)
  %127 = call ptr @ts_string_create(ptr @58)
  call void @ts_console_log(ptr %127)
  %inspected76 = load ptr, ptr %inspected, align 8
  call void @ts_console_log(ptr %inspected76)
  br label %ifcont77

ifcont77:                                         ; preds = %else75, %then72
  call void @ts_pop_exception_handler()
  br label %finally68

catch_body78:                                     ; preds = %catch67
  store ptr %116, ptr %e, align 8
  %128 = call ptr @ts_string_create(ptr @59)
  call void @ts_console_log(ptr %128)
  %failures80 = load i64, ptr %failures, align 8
  %incdec81 = add i64 %failures80, 1
  store i64 %incdec81, ptr %failures, align 8
  call void @ts_pop_exception_handler()
  br label %finally68

catch_throw79:                                    ; preds = %catch67
  %129 = call ptr @ts_get_exception()
  store ptr %129, ptr %pendingExc70, align 8
  br label %finally68

rethrow82:                                        ; preds = %finally68
  call void @ts_throw(ptr %120)
  unreachable

check_return83:                                   ; preds = %finally68
  %130 = load i1, ptr %shouldReturn, align 1
  br i1 %130, label %return, label %check_break84

check_break84:                                    ; preds = %check_return83
  %131 = load i1, ptr %shouldBreak, align 1
  br label %check_continue85

check_continue85:                                 ; preds = %check_break84
  %132 = load i1, ptr %shouldContinue, align 1
  br label %try_merge69

try86:                                            ; preds = %try_merge69
  %133 = call ptr @ts_map_create()
  %134 = call ptr @ts_value_make_int(i64 1)
  %135 = call ptr @ts_string_create(ptr @60)
  %136 = call ptr @ts_value_make_string(ptr %135)
  %137 = icmp eq ptr %136, null
  %138 = icmp eq ptr %134, null
  %139 = select i1 %137, ptr @__ts_const_undefined_value, ptr %136
  %140 = select i1 %138, ptr @__ts_const_undefined_value, ptr %134
  %typePtr = getelementptr inbounds %TsValue, ptr %139, i32 0, i32 0
  %type = load i8, ptr %typePtr, align 1
  %unionPtr = getelementptr inbounds %TsValue, ptr %139, i32 0, i32 2
  %unionVal = load i64, ptr %unionPtr, align 8
  %typePtr91 = getelementptr inbounds %TsValue, ptr %140, i32 0, i32 0
  %type92 = load i8, ptr %typePtr91, align 1
  %unionPtr93 = getelementptr inbounds %TsValue, ptr %140, i32 0, i32 2
  %unionVal94 = load i64, ptr %unionPtr93, align 8
  call void @__ts_map_set_at(ptr %133, i64 %unionVal, i8 %type, i64 %unionVal, i8 %type92, i64 %unionVal94)
  %141 = call ptr @ts_value_make_int(i64 2)
  %142 = call ptr @ts_string_create(ptr @61)
  %143 = call ptr @ts_value_make_string(ptr %142)
  %144 = icmp eq ptr %143, null
  %145 = icmp eq ptr %141, null
  %146 = select i1 %144, ptr @__ts_const_undefined_value.1, ptr %143
  %147 = select i1 %145, ptr @__ts_const_undefined_value.1, ptr %141
  %typePtr95 = getelementptr inbounds %TsValue, ptr %146, i32 0, i32 0
  %type96 = load i8, ptr %typePtr95, align 1
  %unionPtr97 = getelementptr inbounds %TsValue, ptr %146, i32 0, i32 2
  %unionVal98 = load i64, ptr %unionPtr97, align 8
  %typePtr99 = getelementptr inbounds %TsValue, ptr %147, i32 0, i32 0
  %type100 = load i8, ptr %typePtr99, align 1
  %unionPtr101 = getelementptr inbounds %TsValue, ptr %147, i32 0, i32 2
  %unionVal102 = load i64, ptr %unionPtr101, align 8
  call void @__ts_map_set_at(ptr %133, i64 %unionVal98, i8 %type96, i64 %unionVal98, i8 %type100, i64 %unionVal102)
  %148 = call ptr @ts_util_inspect(ptr %133, ptr null)
  store ptr %148, ptr %inspected, align 8
  %inspected103 = load ptr, ptr %inspected, align 8
  %149 = call ptr @ts_typeof(ptr %inspected103)
  %150 = call ptr @ts_string_create(ptr @62)
  %151 = call i1 @ts_string_eq(ptr %149, ptr %150)
  %152 = xor i1 %151, true
  br i1 %152, label %then104, label %else107

catch87:                                          ; preds = %try_merge69
  %153 = call ptr @ts_get_exception()
  %154 = call ptr @ts_push_exception_handler()
  %155 = call i32 @_setjmp(ptr %154, ptr null)
  %156 = icmp ne i32 %155, 0
  br i1 %156, label %catch_throw111, label %catch_body110

finally88:                                        ; preds = %catch_throw111, %catch_body110, %ifcont109
  %157 = load ptr, ptr %pendingExc90, align 8
  %158 = icmp ne ptr %157, null
  br i1 %158, label %rethrow114, label %check_return115

try_merge89:                                      ; preds = %check_continue117
  store ptr null, ptr %pendingExc122, align 8
  %159 = call ptr @ts_push_exception_handler()
  %160 = call i32 @_setjmp(ptr %159, ptr null)
  %161 = icmp ne i32 %160, 0
  br i1 %161, label %catch119, label %try118

then104:                                          ; preds = %try86
  %162 = call ptr @ts_string_create(ptr @63)
  call void @ts_console_log(ptr %162)
  %failures105 = load i64, ptr %failures, align 8
  %incdec106 = add i64 %failures105, 1
  store i64 %incdec106, ptr %failures, align 8
  br label %ifcont109

else107:                                          ; preds = %try86
  %163 = call ptr @ts_string_create(ptr @64)
  call void @ts_console_log(ptr %163)
  %164 = call ptr @ts_string_create(ptr @65)
  call void @ts_console_log(ptr %164)
  %inspected108 = load ptr, ptr %inspected, align 8
  call void @ts_console_log(ptr %inspected108)
  br label %ifcont109

ifcont109:                                        ; preds = %else107, %then104
  call void @ts_pop_exception_handler()
  br label %finally88

catch_body110:                                    ; preds = %catch87
  store ptr %153, ptr %e, align 8
  %165 = call ptr @ts_string_create(ptr @66)
  call void @ts_console_log(ptr %165)
  %failures112 = load i64, ptr %failures, align 8
  %incdec113 = add i64 %failures112, 1
  store i64 %incdec113, ptr %failures, align 8
  call void @ts_pop_exception_handler()
  br label %finally88

catch_throw111:                                   ; preds = %catch87
  %166 = call ptr @ts_get_exception()
  store ptr %166, ptr %pendingExc90, align 8
  br label %finally88

rethrow114:                                       ; preds = %finally88
  call void @ts_throw(ptr %157)
  unreachable

check_return115:                                  ; preds = %finally88
  %167 = load i1, ptr %shouldReturn, align 1
  br i1 %167, label %return, label %check_break116

check_break116:                                   ; preds = %check_return115
  %168 = load i1, ptr %shouldBreak, align 1
  br label %check_continue117

check_continue117:                                ; preds = %check_break116
  %169 = load i1, ptr %shouldContinue, align 1
  br label %try_merge89

try118:                                           ; preds = %try_merge89
  %170 = call ptr @ts_map_create()
  store ptr %170, ptr %map, align 8
  %map123 = load ptr, ptr %map, align 8
  %171 = call i1 @ts_util_types_is_map(ptr %map123)
  store i1 %171, ptr %result, align 1
  %result124 = load i1, ptr %result, align 1
  %172 = call ptr @ts_string_create(ptr @67)
  %173 = call ptr @ts_string_create(ptr @68)
  %174 = call i1 @ts_string_eq(ptr %172, ptr %173)
  %175 = xor i1 %174, true
  br i1 %175, label %then125, label %else128

catch119:                                         ; preds = %try_merge89
  %176 = call ptr @ts_get_exception()
  %177 = call ptr @ts_push_exception_handler()
  %178 = call i32 @_setjmp(ptr %177, ptr null)
  %179 = icmp ne i32 %178, 0
  br i1 %179, label %catch_throw132, label %catch_body131

finally120:                                       ; preds = %catch_throw132, %catch_body131, %ifcont130
  %180 = load ptr, ptr %pendingExc122, align 8
  %181 = icmp ne ptr %180, null
  br i1 %181, label %rethrow135, label %check_return136

try_merge121:                                     ; preds = %check_continue138
  store ptr null, ptr %pendingExc143, align 8
  %182 = call ptr @ts_push_exception_handler()
  %183 = call i32 @_setjmp(ptr %182, ptr null)
  %184 = icmp ne i32 %183, 0
  br i1 %184, label %catch140, label %try139

then125:                                          ; preds = %try118
  %185 = call ptr @ts_string_create(ptr @69)
  call void @ts_console_log(ptr %185)
  %failures126 = load i64, ptr %failures, align 8
  %incdec127 = add i64 %failures126, 1
  store i64 %incdec127, ptr %failures, align 8
  br label %ifcont130

else128:                                          ; preds = %try118
  %186 = call ptr @ts_string_create(ptr @70)
  call void @ts_console_log(ptr %186)
  %187 = call ptr @ts_string_create(ptr @71)
  call void @ts_console_log(ptr %187)
  %result129 = load i1, ptr %result, align 1
  call void @ts_console_log_bool(i1 %result129)
  br label %ifcont130

ifcont130:                                        ; preds = %else128, %then125
  call void @ts_pop_exception_handler()
  br label %finally120

catch_body131:                                    ; preds = %catch119
  store ptr %176, ptr %e, align 8
  %188 = call ptr @ts_string_create(ptr @72)
  call void @ts_console_log(ptr %188)
  %failures133 = load i64, ptr %failures, align 8
  %incdec134 = add i64 %failures133, 1
  store i64 %incdec134, ptr %failures, align 8
  call void @ts_pop_exception_handler()
  br label %finally120

catch_throw132:                                   ; preds = %catch119
  %189 = call ptr @ts_get_exception()
  store ptr %189, ptr %pendingExc122, align 8
  br label %finally120

rethrow135:                                       ; preds = %finally120
  call void @ts_throw(ptr %180)
  unreachable

check_return136:                                  ; preds = %finally120
  %190 = load i1, ptr %shouldReturn, align 1
  br i1 %190, label %return, label %check_break137

check_break137:                                   ; preds = %check_return136
  %191 = load i1, ptr %shouldBreak, align 1
  br label %check_continue138

check_continue138:                                ; preds = %check_break137
  %192 = load i1, ptr %shouldContinue, align 1
  br label %try_merge121

try139:                                           ; preds = %try_merge121
  %193 = call ptr @ts_set_create()
  store ptr %193, ptr %set, align 8
  %set144 = load ptr, ptr %set, align 8
  %194 = call i1 @ts_util_types_is_set(ptr %set144)
  store i1 %194, ptr %result, align 1
  %result145 = load i1, ptr %result, align 1
  %195 = call ptr @ts_string_create(ptr @73)
  %196 = call ptr @ts_string_create(ptr @74)
  %197 = call i1 @ts_string_eq(ptr %195, ptr %196)
  %198 = xor i1 %197, true
  br i1 %198, label %then146, label %else149

catch140:                                         ; preds = %try_merge121
  %199 = call ptr @ts_get_exception()
  %200 = call ptr @ts_push_exception_handler()
  %201 = call i32 @_setjmp(ptr %200, ptr null)
  %202 = icmp ne i32 %201, 0
  br i1 %202, label %catch_throw153, label %catch_body152

finally141:                                       ; preds = %catch_throw153, %catch_body152, %ifcont151
  %203 = load ptr, ptr %pendingExc143, align 8
  %204 = icmp ne ptr %203, null
  br i1 %204, label %rethrow156, label %check_return157

try_merge142:                                     ; preds = %check_continue159
  store ptr null, ptr %pendingExc164, align 8
  %205 = call ptr @ts_push_exception_handler()
  %206 = call i32 @_setjmp(ptr %205, ptr null)
  %207 = icmp ne i32 %206, 0
  br i1 %207, label %catch161, label %try160

then146:                                          ; preds = %try139
  %208 = call ptr @ts_string_create(ptr @75)
  call void @ts_console_log(ptr %208)
  %failures147 = load i64, ptr %failures, align 8
  %incdec148 = add i64 %failures147, 1
  store i64 %incdec148, ptr %failures, align 8
  br label %ifcont151

else149:                                          ; preds = %try139
  %209 = call ptr @ts_string_create(ptr @76)
  call void @ts_console_log(ptr %209)
  %210 = call ptr @ts_string_create(ptr @77)
  call void @ts_console_log(ptr %210)
  %result150 = load i1, ptr %result, align 1
  call void @ts_console_log_bool(i1 %result150)
  br label %ifcont151

ifcont151:                                        ; preds = %else149, %then146
  call void @ts_pop_exception_handler()
  br label %finally141

catch_body152:                                    ; preds = %catch140
  store ptr %199, ptr %e, align 8
  %211 = call ptr @ts_string_create(ptr @78)
  call void @ts_console_log(ptr %211)
  %failures154 = load i64, ptr %failures, align 8
  %incdec155 = add i64 %failures154, 1
  store i64 %incdec155, ptr %failures, align 8
  call void @ts_pop_exception_handler()
  br label %finally141

catch_throw153:                                   ; preds = %catch140
  %212 = call ptr @ts_get_exception()
  store ptr %212, ptr %pendingExc143, align 8
  br label %finally141

rethrow156:                                       ; preds = %finally141
  call void @ts_throw(ptr %203)
  unreachable

check_return157:                                  ; preds = %finally141
  %213 = load i1, ptr %shouldReturn, align 1
  br i1 %213, label %return, label %check_break158

check_break158:                                   ; preds = %check_return157
  %214 = load i1, ptr %shouldBreak, align 1
  br label %check_continue159

check_continue159:                                ; preds = %check_break158
  %215 = load i1, ptr %shouldContinue, align 1
  br label %try_merge142

try160:                                           ; preds = %try_merge142
  %216 = call ptr @ts_date_create()
  store ptr %216, ptr %date, align 8
  %date165 = load ptr, ptr %date, align 8
  %217 = call i1 @ts_util_types_is_date(ptr %date165)
  store i1 %217, ptr %result, align 1
  %result166 = load i1, ptr %result, align 1
  %218 = call ptr @ts_string_create(ptr @79)
  %219 = call ptr @ts_string_create(ptr @80)
  %220 = call i1 @ts_string_eq(ptr %218, ptr %219)
  %221 = xor i1 %220, true
  br i1 %221, label %then167, label %else170

catch161:                                         ; preds = %try_merge142
  %222 = call ptr @ts_get_exception()
  %223 = call ptr @ts_push_exception_handler()
  %224 = call i32 @_setjmp(ptr %223, ptr null)
  %225 = icmp ne i32 %224, 0
  br i1 %225, label %catch_throw174, label %catch_body173

finally162:                                       ; preds = %catch_throw174, %catch_body173, %ifcont172
  %226 = load ptr, ptr %pendingExc164, align 8
  %227 = icmp ne ptr %226, null
  br i1 %227, label %rethrow177, label %check_return178

try_merge163:                                     ; preds = %check_continue180
  store ptr null, ptr %pendingExc185, align 8
  %228 = call ptr @ts_push_exception_handler()
  %229 = call i32 @_setjmp(ptr %228, ptr null)
  %230 = icmp ne i32 %229, 0
  br i1 %230, label %catch182, label %try181

then167:                                          ; preds = %try160
  %231 = call ptr @ts_string_create(ptr @81)
  call void @ts_console_log(ptr %231)
  %failures168 = load i64, ptr %failures, align 8
  %incdec169 = add i64 %failures168, 1
  store i64 %incdec169, ptr %failures, align 8
  br label %ifcont172

else170:                                          ; preds = %try160
  %232 = call ptr @ts_string_create(ptr @82)
  call void @ts_console_log(ptr %232)
  %233 = call ptr @ts_string_create(ptr @83)
  call void @ts_console_log(ptr %233)
  %result171 = load i1, ptr %result, align 1
  call void @ts_console_log_bool(i1 %result171)
  br label %ifcont172

ifcont172:                                        ; preds = %else170, %then167
  call void @ts_pop_exception_handler()
  br label %finally162

catch_body173:                                    ; preds = %catch161
  store ptr %222, ptr %e, align 8
  %234 = call ptr @ts_string_create(ptr @84)
  call void @ts_console_log(ptr %234)
  %failures175 = load i64, ptr %failures, align 8
  %incdec176 = add i64 %failures175, 1
  store i64 %incdec176, ptr %failures, align 8
  call void @ts_pop_exception_handler()
  br label %finally162

catch_throw174:                                   ; preds = %catch161
  %235 = call ptr @ts_get_exception()
  store ptr %235, ptr %pendingExc164, align 8
  br label %finally162

rethrow177:                                       ; preds = %finally162
  call void @ts_throw(ptr %226)
  unreachable

check_return178:                                  ; preds = %finally162
  %236 = load i1, ptr %shouldReturn, align 1
  br i1 %236, label %return, label %check_break179

check_break179:                                   ; preds = %check_return178
  %237 = load i1, ptr %shouldBreak, align 1
  br label %check_continue180

check_continue180:                                ; preds = %check_break179
  %238 = load i1, ptr %shouldContinue, align 1
  br label %try_merge163

try181:                                           ; preds = %try_merge163
  %239 = call ptr @ts_string_create(ptr @85)
  %240 = call ptr @ts_buffer_from_string(ptr %239, ptr null)
  store ptr %240, ptr %buf, align 8
  %buf186 = load ptr, ptr %buf, align 8
  %241 = call i1 @ts_util_types_is_typed_array(ptr %buf186)
  store i1 %241, ptr %result, align 1
  %result187 = load i1, ptr %result, align 1
  %242 = call ptr @ts_string_create(ptr @86)
  %243 = call ptr @ts_string_create(ptr @87)
  %244 = call i1 @ts_string_eq(ptr %242, ptr %243)
  %245 = xor i1 %244, true
  br i1 %245, label %then188, label %else191

catch182:                                         ; preds = %try_merge163
  %246 = call ptr @ts_get_exception()
  %247 = call ptr @ts_push_exception_handler()
  %248 = call i32 @_setjmp(ptr %247, ptr null)
  %249 = icmp ne i32 %248, 0
  br i1 %249, label %catch_throw195, label %catch_body194

finally183:                                       ; preds = %catch_throw195, %catch_body194, %ifcont193
  %250 = load ptr, ptr %pendingExc185, align 8
  %251 = icmp ne ptr %250, null
  br i1 %251, label %rethrow198, label %check_return199

try_merge184:                                     ; preds = %check_continue201
  %252 = call ptr @ts_string_create(ptr @92)
  call void @ts_console_log(ptr %252)
  %failures202 = load i64, ptr %failures, align 8
  %cmptmp = icmp eq i64 %failures202, 0
  br i1 %cmptmp, label %then203, label %else204

then188:                                          ; preds = %try181
  %253 = call ptr @ts_string_create(ptr @88)
  call void @ts_console_log(ptr %253)
  %failures189 = load i64, ptr %failures, align 8
  %incdec190 = add i64 %failures189, 1
  store i64 %incdec190, ptr %failures, align 8
  br label %ifcont193

else191:                                          ; preds = %try181
  %254 = call ptr @ts_string_create(ptr @89)
  call void @ts_console_log(ptr %254)
  %255 = call ptr @ts_string_create(ptr @90)
  call void @ts_console_log(ptr %255)
  %result192 = load i1, ptr %result, align 1
  call void @ts_console_log_bool(i1 %result192)
  br label %ifcont193

ifcont193:                                        ; preds = %else191, %then188
  call void @ts_pop_exception_handler()
  br label %finally183

catch_body194:                                    ; preds = %catch182
  store ptr %246, ptr %e, align 8
  %256 = call ptr @ts_string_create(ptr @91)
  call void @ts_console_log(ptr %256)
  %failures196 = load i64, ptr %failures, align 8
  %incdec197 = add i64 %failures196, 1
  store i64 %incdec197, ptr %failures, align 8
  call void @ts_pop_exception_handler()
  br label %finally183

catch_throw195:                                   ; preds = %catch182
  %257 = call ptr @ts_get_exception()
  store ptr %257, ptr %pendingExc185, align 8
  br label %finally183

rethrow198:                                       ; preds = %finally183
  call void @ts_throw(ptr %250)
  unreachable

check_return199:                                  ; preds = %finally183
  %258 = load i1, ptr %shouldReturn, align 1
  br i1 %258, label %return, label %check_break200

check_break200:                                   ; preds = %check_return199
  %259 = load i1, ptr %shouldBreak, align 1
  br label %check_continue201

check_continue201:                                ; preds = %check_break200
  %260 = load i1, ptr %shouldContinue, align 1
  br label %try_merge184

then203:                                          ; preds = %try_merge184
  %261 = call ptr @ts_string_create(ptr @93)
  call void @ts_console_log(ptr %261)
  br label %ifcont206

else204:                                          ; preds = %try_merge184
  %failures205 = load i64, ptr %failures, align 8
  %262 = call ptr @ts_string_create(ptr @94)
  %263 = call ptr @ts_string_from_int(i64 %failures205)
  %264 = call ptr @ts_string_concat(ptr %263, ptr %262)
  call void @ts_console_log(ptr %264)
  br label %ifcont206

ifcont206:                                        ; preds = %else204, %then203
  %failures207 = load i64, ptr %failures, align 8
  %265 = sitofp i64 %failures207 to double
  ret double %265

dead:                                             ; No predecessors!
  br label %return
}

define ptr @__synthetic_user_main(ptr %context) #0 !type !16 {
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
  %4 = call ptr @ts_string_create(ptr @95)
  %5 = call ptr @ts_value_make_string(ptr %4)
  %6 = icmp eq ptr %5, null
  %7 = icmp eq ptr %3, null
  %8 = select i1 %6, ptr @__ts_const_undefined_value.2, ptr %5
  %9 = select i1 %7, ptr @__ts_const_undefined_value.2, ptr %3
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
  %10 = call ptr @ts_string_create(ptr @96)
  %11 = call ptr @ts_value_make_string(ptr %10)
  %__module_obj_05 = load ptr, ptr %__module_obj_0, align 8
  %12 = call ptr @ts_value_box_any(ptr %__module_obj_05)
  call void @ts_module_register(ptr %11, ptr %12)
  %__module_obj_06 = load ptr, ptr %__module_obj_0, align 8
  %13 = call ptr @__module_init_13333308732889039661_any(ptr %context, ptr %__module_obj_06)
  store ptr %13, ptr %__module_res_0, align 8
  %14 = call fast double @user_main(ptr %context)
  %15 = call ptr @ts_value_make_double(double %14)
  ret ptr %15

return:                                           ; preds = %dead
  %16 = load ptr, ptr %returnValue, align 8
  ret ptr %16

dead:                                             ; No predecessors!
  br label %return
}

declare ptr @ts_object_get_dynamic(ptr, ptr)

declare void @ts_console_log(ptr)

declare ptr @ts_push_exception_handler()

declare void @ts_pop_exception_handler()

declare i32 @_setjmp(ptr, ptr)

declare ptr @ts_get_exception()

declare ptr @ts_array_create_sized(i64)

declare void @ts_array_push(ptr, i64)

declare ptr @ts_util_format(ptr, ptr)

declare ptr @ts_typeof(ptr)

declare void @ts_throw(ptr)

declare ptr @ts_util_inspect(ptr, ptr)

declare ptr @ts_map_create()

declare void @__ts_map_set_at(ptr, i64, i8, i64, i8, i64)

declare i1 @ts_util_types_is_map(ptr)

declare void @ts_console_log_bool(i1)

declare ptr @ts_set_create()

declare i1 @ts_util_types_is_set(ptr)

declare ptr @ts_date_create()

declare i1 @ts_util_types_is_date(ptr)

declare ptr @ts_buffer_from_string(ptr, ptr)

declare i1 @ts_util_types_is_typed_array(ptr)

declare ptr @ts_string_from_int(i64)

declare ptr @ts_string_concat(ptr, ptr)

declare void @ts_module_register(ptr, ptr)

declare ptr @ts_value_make_double(double)

declare i32 @ts_main(i32, ptr, ptr) #0

define i32 @main(i32 %0, ptr %1) #0 {
entry:
  %2 = call i32 @ts_main(i32 %0, ptr %1, ptr @__synthetic_user_main)
  ret i32 0
}

attributes #0 = { "sspstrong" "stack-protector-buffer-size"="8" }

!llvm.linker.options = !{!10, !11, !12, !13}

!0 = !{i64 0, !"DataView"}
!1 = !{i64 0, !"Uint8Array"}
!2 = !{i64 0, !"Uint32Array"}
!3 = !{i64 0, !"Float64Array"}
!4 = !{i64 0, !"Generator"}
!5 = !{i64 0, !"AsyncGenerator"}
!6 = !{i64 0, !"SocketAddress"}
!7 = !{i64 0, !"URLSearchParams"}
!8 = !{i64 0, !"Response"}
!9 = !{i64 0, !"URL"}
!10 = !{!"/FAILIFMISMATCH:\22_ITERATOR_DEBUG_LEVEL=0\22"}
!11 = !{!"/DEFAULTLIB:libcmt.lib"}
!12 = !{!"/NODEFAULTLIB:libcmtd.lib"}
!13 = !{!"/FAILIFMISMATCH:\22RuntimeLibrary=MT_StaticRelease\22"}
!14 = !{i64 0, !"__module_init_13333308732889039661_any"}
!15 = !{i64 0, !"user_main"}
!16 = !{i64 0, !"__synthetic_user_main"}
