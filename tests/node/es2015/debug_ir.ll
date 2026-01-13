; ModuleID = 'ts_module'
source_filename = "ts_module"
target datalayout = "e-m:w-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-windows-msvc"

%RegExpMatchArray_VTable = type { ptr, ptr }
%SocketAddress_VTable = type { ptr, ptr }
%URLSearchParams_VTable = type { ptr, ptr, ptr, ptr, ptr, ptr, ptr, ptr, ptr, ptr }
%URL_VTable = type { ptr, ptr, ptr, ptr }
%Response_VTable = type { ptr, ptr, ptr, ptr }
%TsValue = type { i8, [7 x i8], i64 }
%RegExpMatchArray = type { ptr, i64, ptr, ptr, i64 }
%SocketAddress = type { ptr, ptr, ptr, i64, i64 }
%URLSearchParams = type { ptr, i64 }
%URL = type { ptr, ptr, ptr, ptr, ptr, ptr, ptr, ptr, ptr, ptr, ptr, ptr, ptr }
%Response = type { ptr, i1, i64, ptr }

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
@TextEncoder = global ptr null
@null = global ptr null
@undefined = global ptr null
@Infinity = global double 0.000000e+00
@NaN = global double 0.000000e+00
@Symbol = global ptr null
@Map = global ptr null
@Reflect = global ptr null
@Int8Array = global ptr null
@Uint16Array = global ptr null
@Uint8Array = global ptr null
@Uint8ClampedArray = global ptr null
@Number = global ptr null
@Int16Array = global ptr null
@Int32Array = global ptr null
@BigUint64Array = global ptr null
@Uint32Array = global ptr null
@Float32Array = global ptr null
@Float64Array = global ptr null
@events = global ptr null
@BigInt64Array = global ptr null
@TextDecoder = global ptr null
@DataView = global ptr null
@String = global ptr null
@https = global ptr null
@buffer = global ptr null
@stream = global ptr null
@http = global ptr null
@fs = global ptr null
@path = global ptr null
@net = global ptr null
@util = global ptr null
@0 = private unnamed_addr constant [6 x i8] c"index\00", align 1
@1 = private unnamed_addr constant [8 x i8] c"indices\00", align 1
@2 = private unnamed_addr constant [6 x i8] c"input\00", align 1
@3 = private unnamed_addr constant [7 x i8] c"length\00", align 1
@RegExpMatchArray_VTable_Global = constant %RegExpMatchArray_VTable { ptr null, ptr @RegExpMatchArray_get_property }, !type !0
@4 = private unnamed_addr constant [8 x i8] c"address\00", align 1
@5 = private unnamed_addr constant [7 x i8] c"family\00", align 1
@6 = private unnamed_addr constant [10 x i8] c"flowlabel\00", align 1
@7 = private unnamed_addr constant [5 x i8] c"port\00", align 1
@SocketAddress_VTable_Global = constant %SocketAddress_VTable { ptr null, ptr @SocketAddress_get_property }, !type !1
@8 = private unnamed_addr constant [5 x i8] c"size\00", align 1
@URLSearchParams_VTable_Global = constant %URLSearchParams_VTable { ptr null, ptr @URLSearchParams_get_property, ptr @URLSearchParams_append, ptr @URLSearchParams_delete, ptr @URLSearchParams_get, ptr @URLSearchParams_getAll, ptr @URLSearchParams_has, ptr @URLSearchParams_set, ptr @URLSearchParams_sort, ptr @URLSearchParams_toString }, !type !2
@9 = private unnamed_addr constant [5 x i8] c"hash\00", align 1
@10 = private unnamed_addr constant [5 x i8] c"host\00", align 1
@11 = private unnamed_addr constant [9 x i8] c"hostname\00", align 1
@12 = private unnamed_addr constant [5 x i8] c"href\00", align 1
@13 = private unnamed_addr constant [7 x i8] c"origin\00", align 1
@14 = private unnamed_addr constant [9 x i8] c"password\00", align 1
@15 = private unnamed_addr constant [9 x i8] c"pathname\00", align 1
@16 = private unnamed_addr constant [5 x i8] c"port\00", align 1
@17 = private unnamed_addr constant [9 x i8] c"protocol\00", align 1
@18 = private unnamed_addr constant [7 x i8] c"search\00", align 1
@19 = private unnamed_addr constant [13 x i8] c"searchParams\00", align 1
@20 = private unnamed_addr constant [9 x i8] c"username\00", align 1
@URL_VTable_Global = constant %URL_VTable { ptr null, ptr @URL_get_property, ptr @URL_toJSON, ptr @URL_toString }, !type !3
@21 = private unnamed_addr constant [3 x i8] c"ok\00", align 1
@22 = private unnamed_addr constant [7 x i8] c"status\00", align 1
@23 = private unnamed_addr constant [11 x i8] c"statusText\00", align 1
@Response_VTable_Global = constant %Response_VTable { ptr null, ptr @Response_get_property, ptr @Response_json, ptr @Response_text }, !type !4
@24 = private unnamed_addr constant [8 x i8] c"exports\00", align 1
@25 = private unnamed_addr constant [8 x i8] c"exports\00", align 1
@26 = private unnamed_addr constant [29 x i8] c"Testing Proxy and Reflect...\00", align 1
@27 = private unnamed_addr constant [31 x i8] c"\0A1. Basic Proxy with get trap:\00", align 1
@28 = private unnamed_addr constant [6 x i8] c"hello\00", align 1
@29 = private unnamed_addr constant [8 x i8] c"message\00", align 1
@__ts_const_undefined_value = private constant %TsValue zeroinitializer
@30 = private unnamed_addr constant [6 x i8] c"value\00", align 1
@__ts_const_undefined_value.1 = private constant %TsValue zeroinitializer
@31 = private unnamed_addr constant [22 x i8] c"get trap called for: \00", align 1
@32 = private unnamed_addr constant [4 x i8] c"get\00", align 1
@__ts_const_undefined_value.2 = private constant %TsValue zeroinitializer
@33 = private unnamed_addr constant [18 x i8] c"proxy1.message = \00", align 1
@34 = private unnamed_addr constant [8 x i8] c"message\00", align 1
@35 = private unnamed_addr constant [16 x i8] c"proxy1.value = \00", align 1
@36 = private unnamed_addr constant [6 x i8] c"value\00", align 1
@37 = private unnamed_addr constant [25 x i8] c"\0A2. Proxy with set trap:\00", align 1
@38 = private unnamed_addr constant [6 x i8] c"count\00", align 1
@__ts_const_undefined_value.3 = private constant %TsValue zeroinitializer
@39 = private unnamed_addr constant [11 x i8] c"set trap: \00", align 1
@40 = private unnamed_addr constant [4 x i8] c" = \00", align 1
@41 = private unnamed_addr constant [4 x i8] c"set\00", align 1
@__ts_const_undefined_value.4 = private constant %TsValue zeroinitializer
@42 = private unnamed_addr constant [6 x i8] c"count\00", align 1
@43 = private unnamed_addr constant [17 x i8] c"target2.count = \00", align 1
@44 = private unnamed_addr constant [6 x i8] c"count\00", align 1
@45 = private unnamed_addr constant [33 x i8] c"\0A3. Reflect.get and Reflect.set:\00", align 1
@46 = private unnamed_addr constant [2 x i8] c"x\00", align 1
@__ts_const_undefined_value.5 = private constant %TsValue zeroinitializer
@47 = private unnamed_addr constant [2 x i8] c"y\00", align 1
@__ts_const_undefined_value.6 = private constant %TsValue zeroinitializer
@48 = private unnamed_addr constant [26 x i8] c"Reflect.get(obj3, 'x') = \00", align 1
@49 = private unnamed_addr constant [2 x i8] c"x\00", align 1
@50 = private unnamed_addr constant [2 x i8] c"x\00", align 1
@51 = private unnamed_addr constant [29 x i8] c"After Reflect.set: obj3.x = \00", align 1
@52 = private unnamed_addr constant [30 x i8] c"Null or undefined dereference\00", align 1
@53 = private unnamed_addr constant [2 x i8] c"x\00", align 1
@__ts_const_undefined_value.7 = private constant %TsValue zeroinitializer
@54 = private unnamed_addr constant [17 x i8] c"\0A4. Reflect.has:\00", align 1
@55 = private unnamed_addr constant [5 x i8] c"test\00", align 1
@56 = private unnamed_addr constant [5 x i8] c"name\00", align 1
@__ts_const_undefined_value.8 = private constant %TsValue zeroinitializer
@57 = private unnamed_addr constant [29 x i8] c"Reflect.has(obj4, 'name') = \00", align 1
@58 = private unnamed_addr constant [5 x i8] c"name\00", align 1
@59 = private unnamed_addr constant [32 x i8] c"Reflect.has(obj4, 'missing') = \00", align 1
@60 = private unnamed_addr constant [8 x i8] c"missing\00", align 1
@61 = private unnamed_addr constant [28 x i8] c"\0A5. Reflect.deleteProperty:\00", align 1
@62 = private unnamed_addr constant [2 x i8] c"a\00", align 1
@__ts_const_undefined_value.9 = private constant %TsValue zeroinitializer
@63 = private unnamed_addr constant [2 x i8] c"b\00", align 1
@__ts_const_undefined_value.10 = private constant %TsValue zeroinitializer
@64 = private unnamed_addr constant [16 x i8] c"Before delete: \00", align 1
@65 = private unnamed_addr constant [3 x i8] c", \00", align 1
@66 = private unnamed_addr constant [2 x i8] c"a\00", align 1
@67 = private unnamed_addr constant [19 x i8] c"After delete 'a': \00", align 1
@68 = private unnamed_addr constant [3 x i8] c", \00", align 1
@69 = private unnamed_addr constant [21 x i8] c"\0A6. Reflect.ownKeys:\00", align 1
@70 = private unnamed_addr constant [4 x i8] c"foo\00", align 1
@__ts_const_undefined_value.11 = private constant %TsValue zeroinitializer
@71 = private unnamed_addr constant [4 x i8] c"bar\00", align 1
@__ts_const_undefined_value.12 = private constant %TsValue zeroinitializer
@72 = private unnamed_addr constant [4 x i8] c"baz\00", align 1
@__ts_const_undefined_value.13 = private constant %TsValue zeroinitializer
@73 = private unnamed_addr constant [18 x i8] c"Reflect.ownKeys: \00", align 1
@74 = private unnamed_addr constant [3 x i8] c", \00", align 1
@75 = private unnamed_addr constant [25 x i8] c"\0A7. Proxy with has trap:\00", align 1
@76 = private unnamed_addr constant [7 x i8] c"hidden\00", align 1
@77 = private unnamed_addr constant [7 x i8] c"secret\00", align 1
@__ts_const_undefined_value.14 = private constant %TsValue zeroinitializer
@78 = private unnamed_addr constant [6 x i8] c"shown\00", align 1
@79 = private unnamed_addr constant [8 x i8] c"visible\00", align 1
@__ts_const_undefined_value.15 = private constant %TsValue zeroinitializer
@80 = private unnamed_addr constant [7 x i8] c"secret\00", align 1
@81 = private unnamed_addr constant [26 x i8] c"has trap: hiding 'secret'\00", align 1
@82 = private unnamed_addr constant [4 x i8] c"has\00", align 1
@__ts_const_undefined_value.16 = private constant %TsValue zeroinitializer
@83 = private unnamed_addr constant [22 x i8] c"'visible' in proxy7: \00", align 1
@84 = private unnamed_addr constant [8 x i8] c"visible\00", align 1
@85 = private unnamed_addr constant [21 x i8] c"'secret' in proxy7: \00", align 1
@86 = private unnamed_addr constant [7 x i8] c"secret\00", align 1
@87 = private unnamed_addr constant [21 x i8] c"\0A8. Proxy.revocable:\00", align 1
@88 = private unnamed_addr constant [10 x i8] c"important\00", align 1
@89 = private unnamed_addr constant [5 x i8] c"data\00", align 1
@__ts_const_undefined_value.17 = private constant %TsValue zeroinitializer
@90 = private unnamed_addr constant [6 x i8] c"proxy\00", align 1
@91 = private unnamed_addr constant [7 x i8] c"revoke\00", align 1
@92 = private unnamed_addr constant [30 x i8] c"Before revoke: proxy8.data = \00", align 1
@93 = private unnamed_addr constant [5 x i8] c"data\00", align 1
@94 = private unnamed_addr constant [41 x i8] c"After revoke: proxy operations will fail\00", align 1
@95 = private unnamed_addr constant [37 x i8] c"\0AAll Proxy and Reflect tests passed!\00", align 1
@96 = private unnamed_addr constant [8 x i8] c"exports\00", align 1
@__ts_const_undefined_value.18 = private constant %TsValue zeroinitializer
@97 = private unnamed_addr constant [74 x i8] c"E:\\src\\github.com\\cgrinker\\ts-aoc\\tests\\node\\es2015\\test_proxy_reflect.ts\00", align 1

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

define ptr @SocketAddress_get_property(ptr %0, ptr %1) #0 {
entry:
  %2 = call ptr @ts_string_create(ptr @4)
  %3 = call i1 @ts_string_eq(ptr %1, ptr %2)
  br i1 %3, label %match_address, label %next_address

match_address:                                    ; preds = %entry
  %4 = getelementptr inbounds %SocketAddress, ptr %0, i32 0, i32 1
  %5 = load ptr, ptr %4, align 8
  %6 = call ptr @ts_value_make_string(ptr %5)
  ret ptr %6

next_address:                                     ; preds = %entry
  %7 = call ptr @ts_string_create(ptr @5)
  %8 = call i1 @ts_string_eq(ptr %1, ptr %7)
  br i1 %8, label %match_family, label %next_family

match_family:                                     ; preds = %next_address
  %9 = getelementptr inbounds %SocketAddress, ptr %0, i32 0, i32 2
  %10 = load ptr, ptr %9, align 8
  %11 = call ptr @ts_value_make_string(ptr %10)
  ret ptr %11

next_family:                                      ; preds = %next_address
  %12 = call ptr @ts_string_create(ptr @6)
  %13 = call i1 @ts_string_eq(ptr %1, ptr %12)
  br i1 %13, label %match_flowlabel, label %next_flowlabel

match_flowlabel:                                  ; preds = %next_family
  %14 = getelementptr inbounds %SocketAddress, ptr %0, i32 0, i32 3
  %15 = load i64, ptr %14, align 8
  %16 = call ptr @ts_value_make_int(i64 %15)
  ret ptr %16

next_flowlabel:                                   ; preds = %next_family
  %17 = call ptr @ts_string_create(ptr @7)
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
  %2 = call ptr @ts_string_create(ptr @8)
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

define ptr @URL_get_property(ptr %0, ptr %1) #0 {
entry:
  %2 = call ptr @ts_string_create(ptr @9)
  %3 = call i1 @ts_string_eq(ptr %1, ptr %2)
  br i1 %3, label %match_hash, label %next_hash

match_hash:                                       ; preds = %entry
  %4 = getelementptr inbounds %URL, ptr %0, i32 0, i32 1
  %5 = load ptr, ptr %4, align 8
  %6 = call ptr @ts_value_make_string(ptr %5)
  ret ptr %6

next_hash:                                        ; preds = %entry
  %7 = call ptr @ts_string_create(ptr @10)
  %8 = call i1 @ts_string_eq(ptr %1, ptr %7)
  br i1 %8, label %match_host, label %next_host

match_host:                                       ; preds = %next_hash
  %9 = getelementptr inbounds %URL, ptr %0, i32 0, i32 2
  %10 = load ptr, ptr %9, align 8
  %11 = call ptr @ts_value_make_string(ptr %10)
  ret ptr %11

next_host:                                        ; preds = %next_hash
  %12 = call ptr @ts_string_create(ptr @11)
  %13 = call i1 @ts_string_eq(ptr %1, ptr %12)
  br i1 %13, label %match_hostname, label %next_hostname

match_hostname:                                   ; preds = %next_host
  %14 = getelementptr inbounds %URL, ptr %0, i32 0, i32 3
  %15 = load ptr, ptr %14, align 8
  %16 = call ptr @ts_value_make_string(ptr %15)
  ret ptr %16

next_hostname:                                    ; preds = %next_host
  %17 = call ptr @ts_string_create(ptr @12)
  %18 = call i1 @ts_string_eq(ptr %1, ptr %17)
  br i1 %18, label %match_href, label %next_href

match_href:                                       ; preds = %next_hostname
  %19 = getelementptr inbounds %URL, ptr %0, i32 0, i32 4
  %20 = load ptr, ptr %19, align 8
  %21 = call ptr @ts_value_make_string(ptr %20)
  ret ptr %21

next_href:                                        ; preds = %next_hostname
  %22 = call ptr @ts_string_create(ptr @13)
  %23 = call i1 @ts_string_eq(ptr %1, ptr %22)
  br i1 %23, label %match_origin, label %next_origin

match_origin:                                     ; preds = %next_href
  %24 = getelementptr inbounds %URL, ptr %0, i32 0, i32 5
  %25 = load ptr, ptr %24, align 8
  %26 = call ptr @ts_value_make_string(ptr %25)
  ret ptr %26

next_origin:                                      ; preds = %next_href
  %27 = call ptr @ts_string_create(ptr @14)
  %28 = call i1 @ts_string_eq(ptr %1, ptr %27)
  br i1 %28, label %match_password, label %next_password

match_password:                                   ; preds = %next_origin
  %29 = getelementptr inbounds %URL, ptr %0, i32 0, i32 6
  %30 = load ptr, ptr %29, align 8
  %31 = call ptr @ts_value_make_string(ptr %30)
  ret ptr %31

next_password:                                    ; preds = %next_origin
  %32 = call ptr @ts_string_create(ptr @15)
  %33 = call i1 @ts_string_eq(ptr %1, ptr %32)
  br i1 %33, label %match_pathname, label %next_pathname

match_pathname:                                   ; preds = %next_password
  %34 = getelementptr inbounds %URL, ptr %0, i32 0, i32 7
  %35 = load ptr, ptr %34, align 8
  %36 = call ptr @ts_value_make_string(ptr %35)
  ret ptr %36

next_pathname:                                    ; preds = %next_password
  %37 = call ptr @ts_string_create(ptr @16)
  %38 = call i1 @ts_string_eq(ptr %1, ptr %37)
  br i1 %38, label %match_port, label %next_port

match_port:                                       ; preds = %next_pathname
  %39 = getelementptr inbounds %URL, ptr %0, i32 0, i32 8
  %40 = load ptr, ptr %39, align 8
  %41 = call ptr @ts_value_make_string(ptr %40)
  ret ptr %41

next_port:                                        ; preds = %next_pathname
  %42 = call ptr @ts_string_create(ptr @17)
  %43 = call i1 @ts_string_eq(ptr %1, ptr %42)
  br i1 %43, label %match_protocol, label %next_protocol

match_protocol:                                   ; preds = %next_port
  %44 = getelementptr inbounds %URL, ptr %0, i32 0, i32 9
  %45 = load ptr, ptr %44, align 8
  %46 = call ptr @ts_value_make_string(ptr %45)
  ret ptr %46

next_protocol:                                    ; preds = %next_port
  %47 = call ptr @ts_string_create(ptr @18)
  %48 = call i1 @ts_string_eq(ptr %1, ptr %47)
  br i1 %48, label %match_search, label %next_search

match_search:                                     ; preds = %next_protocol
  %49 = getelementptr inbounds %URL, ptr %0, i32 0, i32 10
  %50 = load ptr, ptr %49, align 8
  %51 = call ptr @ts_value_make_string(ptr %50)
  ret ptr %51

next_search:                                      ; preds = %next_protocol
  %52 = call ptr @ts_string_create(ptr @19)
  %53 = call i1 @ts_string_eq(ptr %1, ptr %52)
  br i1 %53, label %match_searchParams, label %next_searchParams

match_searchParams:                               ; preds = %next_search
  %54 = getelementptr inbounds %URL, ptr %0, i32 0, i32 11
  %55 = load ptr, ptr %54, align 8
  %56 = call ptr @ts_value_make_object(ptr %55)
  ret ptr %56

next_searchParams:                                ; preds = %next_search
  %57 = call ptr @ts_string_create(ptr @20)
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

define ptr @Response_get_property(ptr %0, ptr %1) #0 {
entry:
  %2 = call ptr @ts_string_create(ptr @21)
  %3 = call i1 @ts_string_eq(ptr %1, ptr %2)
  br i1 %3, label %match_ok, label %next_ok

match_ok:                                         ; preds = %entry
  %4 = getelementptr inbounds %Response, ptr %0, i32 0, i32 1
  %5 = load i1, ptr %4, align 1
  %6 = call ptr @ts_value_make_bool(i1 %5)
  ret ptr %6

next_ok:                                          ; preds = %entry
  %7 = call ptr @ts_string_create(ptr @22)
  %8 = call i1 @ts_string_eq(ptr %1, ptr %7)
  br i1 %8, label %match_status, label %next_status

match_status:                                     ; preds = %next_ok
  %9 = getelementptr inbounds %Response, ptr %0, i32 0, i32 2
  %10 = load i64, ptr %9, align 8
  %11 = call ptr @ts_value_make_int(i64 %10)
  ret ptr %11

next_status:                                      ; preds = %next_ok
  %12 = call ptr @ts_string_create(ptr @23)
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

define ptr @__module_init_13086476663298097481_any(ptr %context, ptr %module) #0 !type !9 {
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
  %2 = call ptr @ts_string_create(ptr @24)
  %3 = call ptr @ts_value_make_string(ptr %2)
  %4 = call ptr @ts_object_get_dynamic(ptr %1, ptr %3)
  store ptr %4, ptr %exports, align 8
  %module3 = load ptr, ptr %module1, align 8
  %5 = call ptr @ts_value_box_any(ptr %module3)
  %6 = call ptr @ts_string_create(ptr @25)
  %7 = call ptr @ts_value_make_string(ptr %6)
  %8 = call ptr @ts_object_get_dynamic(ptr %5, ptr %7)
  ret ptr %8

return:                                           ; preds = %dead
  %9 = load ptr, ptr %returnValue, align 8
  ret ptr %9

dead:                                             ; No predecessors!
  br label %return
}

define double @user_main(ptr %context) #0 !type !10 {
entry:
  %revoke = alloca ptr, align 8
  %proxy8 = alloca ptr, align 8
  %target8 = alloca ptr, align 8
  %proxy7 = alloca ptr, align 8
  %handler7 = alloca ptr, align 8
  %target7 = alloca ptr, align 8
  %keys = alloca ptr, align 8
  %obj6 = alloca ptr, align 8
  %obj5 = alloca ptr, align 8
  %obj4 = alloca ptr, align 8
  %mapGetResult = alloca %TsValue, align 8
  %obj3 = alloca ptr, align 8
  %proxy2 = alloca ptr, align 8
  %handler2 = alloca ptr, align 8
  %target2 = alloca ptr, align 8
  %proxy1 = alloca ptr, align 8
  %handler1 = alloca ptr, align 8
  %target1 = alloca ptr, align 8
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
  %0 = call ptr @ts_string_create(ptr @26)
  call void @ts_console_log(ptr %0)
  %1 = call ptr @ts_string_create(ptr @27)
  call void @ts_console_log(ptr %1)
  %2 = call ptr @ts_map_create()
  %3 = call ptr @ts_string_create(ptr @28)
  %4 = call ptr @ts_value_make_string(ptr %3)
  %5 = call ptr @ts_string_create(ptr @29)
  %6 = call ptr @ts_value_make_string(ptr %5)
  %7 = icmp eq ptr %6, null
  %8 = icmp eq ptr %4, null
  %9 = select i1 %7, ptr @__ts_const_undefined_value, ptr %6
  %10 = select i1 %8, ptr @__ts_const_undefined_value, ptr %4
  %typePtr = getelementptr inbounds %TsValue, ptr %9, i32 0, i32 0
  %type = load i8, ptr %typePtr, align 1
  %unionPtr = getelementptr inbounds %TsValue, ptr %9, i32 0, i32 2
  %unionVal = load i64, ptr %unionPtr, align 8
  %typePtr1 = getelementptr inbounds %TsValue, ptr %10, i32 0, i32 0
  %type2 = load i8, ptr %typePtr1, align 1
  %unionPtr3 = getelementptr inbounds %TsValue, ptr %10, i32 0, i32 2
  %unionVal4 = load i64, ptr %unionPtr3, align 8
  call void @__ts_map_set_at(ptr %2, i64 %unionVal, i8 %type, i64 %unionVal, i8 %type2, i64 %unionVal4)
  %11 = call ptr @ts_value_make_int(i64 42)
  %12 = call ptr @ts_string_create(ptr @30)
  %13 = call ptr @ts_value_make_string(ptr %12)
  %14 = icmp eq ptr %13, null
  %15 = icmp eq ptr %11, null
  %16 = select i1 %14, ptr @__ts_const_undefined_value.1, ptr %13
  %17 = select i1 %15, ptr @__ts_const_undefined_value.1, ptr %11
  %typePtr5 = getelementptr inbounds %TsValue, ptr %16, i32 0, i32 0
  %type6 = load i8, ptr %typePtr5, align 1
  %unionPtr7 = getelementptr inbounds %TsValue, ptr %16, i32 0, i32 2
  %unionVal8 = load i64, ptr %unionPtr7, align 8
  %typePtr9 = getelementptr inbounds %TsValue, ptr %17, i32 0, i32 0
  %type10 = load i8, ptr %typePtr9, align 1
  %unionPtr11 = getelementptr inbounds %TsValue, ptr %17, i32 0, i32 2
  %unionVal12 = load i64, ptr %unionPtr11, align 8
  call void @__ts_map_set_at(ptr %2, i64 %unionVal8, i8 %type6, i64 %unionVal8, i8 %type10, i64 %unionVal12)
  store ptr %2, ptr %target1, align 8
  %18 = call ptr @ts_map_create()
  %19 = call ptr @ts_value_make_function(ptr @func_expr_0, ptr null)
  %20 = call ptr @ts_string_create(ptr @32)
  %21 = call ptr @ts_value_make_string(ptr %20)
  %22 = icmp eq ptr %21, null
  %23 = icmp eq ptr %19, null
  %24 = select i1 %22, ptr @__ts_const_undefined_value.2, ptr %21
  %25 = select i1 %23, ptr @__ts_const_undefined_value.2, ptr %19
  %typePtr13 = getelementptr inbounds %TsValue, ptr %24, i32 0, i32 0
  %type14 = load i8, ptr %typePtr13, align 1
  %unionPtr15 = getelementptr inbounds %TsValue, ptr %24, i32 0, i32 2
  %unionVal16 = load i64, ptr %unionPtr15, align 8
  %typePtr17 = getelementptr inbounds %TsValue, ptr %25, i32 0, i32 0
  %type18 = load i8, ptr %typePtr17, align 1
  %unionPtr19 = getelementptr inbounds %TsValue, ptr %25, i32 0, i32 2
  %unionVal20 = load i64, ptr %unionPtr19, align 8
  call void @__ts_map_set_at(ptr %18, i64 %unionVal16, i8 %type14, i64 %unionVal16, i8 %type18, i64 %unionVal20)
  store ptr %18, ptr %handler1, align 8
  %target121 = load ptr, ptr %target1, align 8
  %26 = call ptr @ts_value_make_object(ptr %target121)
  %handler122 = load ptr, ptr %handler1, align 8
  %27 = call ptr @ts_value_make_object(ptr %handler122)
  %28 = call ptr @ts_proxy_create(ptr %26, ptr %27)
  store ptr %28, ptr %proxy1, align 8
  %29 = call ptr @ts_string_create(ptr @33)
  %proxy123 = load ptr, ptr %proxy1, align 8
  %30 = call ptr @ts_string_create(ptr @34)
  %31 = call ptr @ts_value_make_string(ptr %30)
  %32 = call ptr @ts_object_get_dynamic(ptr %proxy123, ptr %31)
  %33 = call ptr @ts_value_make_string(ptr %29)
  %34 = call ptr @ts_value_add(ptr %33, ptr %32)
  call void @ts_console_log_value(ptr %34)
  %35 = call ptr @ts_string_create(ptr @35)
  %proxy124 = load ptr, ptr %proxy1, align 8
  %36 = call ptr @ts_string_create(ptr @36)
  %37 = call ptr @ts_value_make_string(ptr %36)
  %38 = call ptr @ts_object_get_dynamic(ptr %proxy124, ptr %37)
  %39 = call ptr @ts_value_make_string(ptr %35)
  %40 = call ptr @ts_value_add(ptr %39, ptr %38)
  call void @ts_console_log_value(ptr %40)
  %41 = call ptr @ts_string_create(ptr @37)
  call void @ts_console_log(ptr %41)
  %42 = call ptr @ts_map_create()
  %43 = call ptr @ts_value_make_int(i64 0)
  %44 = call ptr @ts_string_create(ptr @38)
  %45 = call ptr @ts_value_make_string(ptr %44)
  %46 = icmp eq ptr %45, null
  %47 = icmp eq ptr %43, null
  %48 = select i1 %46, ptr @__ts_const_undefined_value.3, ptr %45
  %49 = select i1 %47, ptr @__ts_const_undefined_value.3, ptr %43
  %typePtr25 = getelementptr inbounds %TsValue, ptr %48, i32 0, i32 0
  %type26 = load i8, ptr %typePtr25, align 1
  %unionPtr27 = getelementptr inbounds %TsValue, ptr %48, i32 0, i32 2
  %unionVal28 = load i64, ptr %unionPtr27, align 8
  %typePtr29 = getelementptr inbounds %TsValue, ptr %49, i32 0, i32 0
  %type30 = load i8, ptr %typePtr29, align 1
  %unionPtr31 = getelementptr inbounds %TsValue, ptr %49, i32 0, i32 2
  %unionVal32 = load i64, ptr %unionPtr31, align 8
  call void @__ts_map_set_at(ptr %42, i64 %unionVal28, i8 %type26, i64 %unionVal28, i8 %type30, i64 %unionVal32)
  store ptr %42, ptr %target2, align 8
  %50 = call ptr @ts_map_create()
  %51 = call ptr @ts_value_make_function(ptr @func_expr_1, ptr null)
  %52 = call ptr @ts_string_create(ptr @41)
  %53 = call ptr @ts_value_make_string(ptr %52)
  %54 = icmp eq ptr %53, null
  %55 = icmp eq ptr %51, null
  %56 = select i1 %54, ptr @__ts_const_undefined_value.4, ptr %53
  %57 = select i1 %55, ptr @__ts_const_undefined_value.4, ptr %51
  %typePtr33 = getelementptr inbounds %TsValue, ptr %56, i32 0, i32 0
  %type34 = load i8, ptr %typePtr33, align 1
  %unionPtr35 = getelementptr inbounds %TsValue, ptr %56, i32 0, i32 2
  %unionVal36 = load i64, ptr %unionPtr35, align 8
  %typePtr37 = getelementptr inbounds %TsValue, ptr %57, i32 0, i32 0
  %type38 = load i8, ptr %typePtr37, align 1
  %unionPtr39 = getelementptr inbounds %TsValue, ptr %57, i32 0, i32 2
  %unionVal40 = load i64, ptr %unionPtr39, align 8
  call void @__ts_map_set_at(ptr %50, i64 %unionVal36, i8 %type34, i64 %unionVal36, i8 %type38, i64 %unionVal40)
  store ptr %50, ptr %handler2, align 8
  %target241 = load ptr, ptr %target2, align 8
  %58 = call ptr @ts_value_box_any(ptr %target241)
  %handler242 = load ptr, ptr %handler2, align 8
  %59 = call ptr @ts_value_make_object(ptr %handler242)
  %60 = call ptr @ts_proxy_create(ptr %58, ptr %59)
  store ptr %60, ptr %proxy2, align 8
  %proxy243 = load ptr, ptr %proxy2, align 8
  %61 = call ptr @ts_string_create(ptr @42)
  %62 = call ptr @ts_value_make_string(ptr %61)
  %63 = call ptr @ts_value_make_int(i64 5)
  call void @ts_object_set_dynamic(ptr %proxy243, ptr %62, ptr %63)
  %64 = call ptr @ts_string_create(ptr @43)
  %target244 = load ptr, ptr %target2, align 8
  %65 = call ptr @ts_value_box_any(ptr %target244)
  %66 = call ptr @ts_string_create(ptr @44)
  %67 = call ptr @ts_value_make_string(ptr %66)
  %68 = call ptr @ts_object_get_dynamic(ptr %65, ptr %67)
  %69 = call ptr @ts_value_make_string(ptr %64)
  %70 = call ptr @ts_value_add(ptr %69, ptr %68)
  call void @ts_console_log_value(ptr %70)
  %71 = call ptr @ts_string_create(ptr @45)
  call void @ts_console_log(ptr %71)
  %72 = call ptr @ts_map_create()
  %73 = call ptr @ts_value_make_int(i64 10)
  %74 = call ptr @ts_string_create(ptr @46)
  %75 = call ptr @ts_value_make_string(ptr %74)
  %76 = icmp eq ptr %75, null
  %77 = icmp eq ptr %73, null
  %78 = select i1 %76, ptr @__ts_const_undefined_value.5, ptr %75
  %79 = select i1 %77, ptr @__ts_const_undefined_value.5, ptr %73
  %typePtr45 = getelementptr inbounds %TsValue, ptr %78, i32 0, i32 0
  %type46 = load i8, ptr %typePtr45, align 1
  %unionPtr47 = getelementptr inbounds %TsValue, ptr %78, i32 0, i32 2
  %unionVal48 = load i64, ptr %unionPtr47, align 8
  %typePtr49 = getelementptr inbounds %TsValue, ptr %79, i32 0, i32 0
  %type50 = load i8, ptr %typePtr49, align 1
  %unionPtr51 = getelementptr inbounds %TsValue, ptr %79, i32 0, i32 2
  %unionVal52 = load i64, ptr %unionPtr51, align 8
  call void @__ts_map_set_at(ptr %72, i64 %unionVal48, i8 %type46, i64 %unionVal48, i8 %type50, i64 %unionVal52)
  %80 = call ptr @ts_value_make_int(i64 20)
  %81 = call ptr @ts_string_create(ptr @47)
  %82 = call ptr @ts_value_make_string(ptr %81)
  %83 = icmp eq ptr %82, null
  %84 = icmp eq ptr %80, null
  %85 = select i1 %83, ptr @__ts_const_undefined_value.6, ptr %82
  %86 = select i1 %84, ptr @__ts_const_undefined_value.6, ptr %80
  %typePtr53 = getelementptr inbounds %TsValue, ptr %85, i32 0, i32 0
  %type54 = load i8, ptr %typePtr53, align 1
  %unionPtr55 = getelementptr inbounds %TsValue, ptr %85, i32 0, i32 2
  %unionVal56 = load i64, ptr %unionPtr55, align 8
  %typePtr57 = getelementptr inbounds %TsValue, ptr %86, i32 0, i32 0
  %type58 = load i8, ptr %typePtr57, align 1
  %unionPtr59 = getelementptr inbounds %TsValue, ptr %86, i32 0, i32 2
  %unionVal60 = load i64, ptr %unionPtr59, align 8
  call void @__ts_map_set_at(ptr %72, i64 %unionVal56, i8 %type54, i64 %unionVal56, i8 %type58, i64 %unionVal60)
  store ptr %72, ptr %obj3, align 8
  %87 = call ptr @ts_string_create(ptr @48)
  %obj361 = load ptr, ptr %obj3, align 8
  %88 = call ptr @ts_value_make_object(ptr %obj361)
  %89 = call ptr @ts_string_create(ptr @49)
  %90 = call ptr @ts_value_make_string(ptr %89)
  %91 = call ptr @ts_reflect_get(ptr %88, ptr %90, ptr %88)
  %92 = call ptr @ts_string_from_value(ptr %91)
  %93 = call ptr @ts_string_concat(ptr %87, ptr %92)
  call void @ts_console_log(ptr %93)
  %obj362 = load ptr, ptr %obj3, align 8
  %94 = call ptr @ts_value_make_object(ptr %obj362)
  %95 = call ptr @ts_string_create(ptr @50)
  %96 = call ptr @ts_value_make_string(ptr %95)
  %97 = call ptr @ts_value_make_int(i64 100)
  %98 = call i64 @ts_reflect_set(ptr %94, ptr %96, ptr %97, ptr %94)
  %99 = icmp ne i64 %98, 0
  %100 = call ptr @ts_string_create(ptr @51)
  %obj363 = load ptr, ptr %obj3, align 8
  %101 = call ptr @ts_value_get_object(ptr %obj363)
  %102 = icmp eq ptr %101, null
  br i1 %102, label %null_fail, label %null_cont

return:                                           ; preds = %dead
  %103 = load double, ptr %returnValue, align 8
  ret double %103

null_fail:                                        ; preds = %entry
  call void @ts_panic(ptr @52)
  unreachable

null_cont:                                        ; preds = %entry
  %104 = call ptr @ts_string_create(ptr @53)
  %105 = call ptr @ts_value_make_string(ptr %104)
  %106 = icmp eq ptr %105, null
  %107 = select i1 %106, ptr @__ts_const_undefined_value.7, ptr %105
  %typePtr64 = getelementptr inbounds %TsValue, ptr %107, i32 0, i32 0
  %type65 = load i8, ptr %typePtr64, align 1
  %unionPtr66 = getelementptr inbounds %TsValue, ptr %107, i32 0, i32 2
  %unionVal67 = load i64, ptr %unionPtr66, align 8
  %108 = call i64 @__ts_map_find_bucket(ptr %101, i64 %unionVal67, i8 %type65, i64 %unionVal67)
  %notFound = icmp slt i64 %108, 0
  br i1 %notFound, label %map.notfound, label %map.found

map.found:                                        ; preds = %null_cont
  %109 = alloca i8, align 1
  %110 = alloca i64, align 8
  call void @__ts_map_get_value_at(ptr %101, i64 %108, ptr %109, ptr %110)
  %111 = load i64, ptr %110, align 8
  %112 = load i8, ptr %109, align 1
  %113 = getelementptr inbounds %TsValue, ptr %mapGetResult, i32 0, i32 0
  store i8 %112, ptr %113, align 1
  %114 = getelementptr inbounds %TsValue, ptr %mapGetResult, i32 0, i32 2
  store i64 %111, ptr %114, align 8
  br label %map.done

map.notfound:                                     ; preds = %null_cont
  %115 = getelementptr inbounds %TsValue, ptr %mapGetResult, i32 0, i32 0
  store i8 0, ptr %115, align 1
  %116 = getelementptr inbounds %TsValue, ptr %mapGetResult, i32 0, i32 2
  store i64 0, ptr %116, align 8
  br label %map.done

map.done:                                         ; preds = %map.notfound, %map.found
  %117 = call i64 @ts_value_get_int(ptr %mapGetResult)
  %118 = call ptr @ts_string_from_int(i64 %117)
  %119 = call ptr @ts_string_concat(ptr %100, ptr %118)
  call void @ts_console_log(ptr %119)
  %120 = call ptr @ts_string_create(ptr @54)
  call void @ts_console_log(ptr %120)
  %121 = call ptr @ts_map_create()
  %122 = call ptr @ts_string_create(ptr @55)
  %123 = call ptr @ts_value_make_string(ptr %122)
  %124 = call ptr @ts_string_create(ptr @56)
  %125 = call ptr @ts_value_make_string(ptr %124)
  %126 = icmp eq ptr %125, null
  %127 = icmp eq ptr %123, null
  %128 = select i1 %126, ptr @__ts_const_undefined_value.8, ptr %125
  %129 = select i1 %127, ptr @__ts_const_undefined_value.8, ptr %123
  %typePtr68 = getelementptr inbounds %TsValue, ptr %128, i32 0, i32 0
  %type69 = load i8, ptr %typePtr68, align 1
  %unionPtr70 = getelementptr inbounds %TsValue, ptr %128, i32 0, i32 2
  %unionVal71 = load i64, ptr %unionPtr70, align 8
  %typePtr72 = getelementptr inbounds %TsValue, ptr %129, i32 0, i32 0
  %type73 = load i8, ptr %typePtr72, align 1
  %unionPtr74 = getelementptr inbounds %TsValue, ptr %129, i32 0, i32 2
  %unionVal75 = load i64, ptr %unionPtr74, align 8
  call void @__ts_map_set_at(ptr %121, i64 %unionVal71, i8 %type69, i64 %unionVal71, i8 %type73, i64 %unionVal75)
  store ptr %121, ptr %obj4, align 8
  %130 = call ptr @ts_string_create(ptr @57)
  %obj476 = load ptr, ptr %obj4, align 8
  %131 = call ptr @ts_value_make_object(ptr %obj476)
  %132 = call ptr @ts_string_create(ptr @58)
  %133 = call ptr @ts_value_make_string(ptr %132)
  %134 = call i64 @ts_reflect_has(ptr %131, ptr %133)
  %135 = icmp ne i64 %134, 0
  %136 = call ptr @ts_string_from_bool(i1 %135)
  %137 = call ptr @ts_string_concat(ptr %130, ptr %136)
  call void @ts_console_log(ptr %137)
  %138 = call ptr @ts_string_create(ptr @59)
  %obj477 = load ptr, ptr %obj4, align 8
  %139 = call ptr @ts_value_make_object(ptr %obj477)
  %140 = call ptr @ts_string_create(ptr @60)
  %141 = call ptr @ts_value_make_string(ptr %140)
  %142 = call i64 @ts_reflect_has(ptr %139, ptr %141)
  %143 = icmp ne i64 %142, 0
  %144 = call ptr @ts_string_from_bool(i1 %143)
  %145 = call ptr @ts_string_concat(ptr %138, ptr %144)
  call void @ts_console_log(ptr %145)
  %146 = call ptr @ts_string_create(ptr @61)
  call void @ts_console_log(ptr %146)
  %147 = call ptr @ts_map_create()
  %148 = call ptr @ts_value_make_int(i64 1)
  %149 = call ptr @ts_string_create(ptr @62)
  %150 = call ptr @ts_value_make_string(ptr %149)
  %151 = icmp eq ptr %150, null
  %152 = icmp eq ptr %148, null
  %153 = select i1 %151, ptr @__ts_const_undefined_value.9, ptr %150
  %154 = select i1 %152, ptr @__ts_const_undefined_value.9, ptr %148
  %typePtr78 = getelementptr inbounds %TsValue, ptr %153, i32 0, i32 0
  %type79 = load i8, ptr %typePtr78, align 1
  %unionPtr80 = getelementptr inbounds %TsValue, ptr %153, i32 0, i32 2
  %unionVal81 = load i64, ptr %unionPtr80, align 8
  %typePtr82 = getelementptr inbounds %TsValue, ptr %154, i32 0, i32 0
  %type83 = load i8, ptr %typePtr82, align 1
  %unionPtr84 = getelementptr inbounds %TsValue, ptr %154, i32 0, i32 2
  %unionVal85 = load i64, ptr %unionPtr84, align 8
  call void @__ts_map_set_at(ptr %147, i64 %unionVal81, i8 %type79, i64 %unionVal81, i8 %type83, i64 %unionVal85)
  %155 = call ptr @ts_value_make_int(i64 2)
  %156 = call ptr @ts_string_create(ptr @63)
  %157 = call ptr @ts_value_make_string(ptr %156)
  %158 = icmp eq ptr %157, null
  %159 = icmp eq ptr %155, null
  %160 = select i1 %158, ptr @__ts_const_undefined_value.10, ptr %157
  %161 = select i1 %159, ptr @__ts_const_undefined_value.10, ptr %155
  %typePtr86 = getelementptr inbounds %TsValue, ptr %160, i32 0, i32 0
  %type87 = load i8, ptr %typePtr86, align 1
  %unionPtr88 = getelementptr inbounds %TsValue, ptr %160, i32 0, i32 2
  %unionVal89 = load i64, ptr %unionPtr88, align 8
  %typePtr90 = getelementptr inbounds %TsValue, ptr %161, i32 0, i32 0
  %type91 = load i8, ptr %typePtr90, align 1
  %unionPtr92 = getelementptr inbounds %TsValue, ptr %161, i32 0, i32 2
  %unionVal93 = load i64, ptr %unionPtr92, align 8
  call void @__ts_map_set_at(ptr %147, i64 %unionVal89, i8 %type87, i64 %unionVal89, i8 %type91, i64 %unionVal93)
  store ptr %147, ptr %obj5, align 8
  %162 = call ptr @ts_string_create(ptr @64)
  %obj594 = load ptr, ptr %obj5, align 8
  %163 = call ptr @ts_value_box_any(ptr %obj594)
  %164 = call ptr @ts_object_keys(ptr %163)
  %165 = call ptr @ts_string_create(ptr @65)
  %166 = call ptr @ts_array_join(ptr %164, ptr %165)
  %167 = call ptr @ts_string_concat(ptr %162, ptr %166)
  call void @ts_console_log(ptr %167)
  %obj595 = load ptr, ptr %obj5, align 8
  %168 = call ptr @ts_value_box_any(ptr %obj595)
  %169 = call ptr @ts_string_create(ptr @66)
  %170 = call ptr @ts_value_make_string(ptr %169)
  %171 = call i64 @ts_reflect_deleteProperty(ptr %168, ptr %170)
  %172 = icmp ne i64 %171, 0
  %173 = call ptr @ts_string_create(ptr @67)
  %obj596 = load ptr, ptr %obj5, align 8
  %174 = call ptr @ts_value_box_any(ptr %obj596)
  %175 = call ptr @ts_object_keys(ptr %174)
  %176 = call ptr @ts_string_create(ptr @68)
  %177 = call ptr @ts_array_join(ptr %175, ptr %176)
  %178 = call ptr @ts_string_concat(ptr %173, ptr %177)
  call void @ts_console_log(ptr %178)
  %179 = call ptr @ts_string_create(ptr @69)
  call void @ts_console_log(ptr %179)
  %180 = call ptr @ts_map_create()
  %181 = call ptr @ts_value_make_int(i64 1)
  %182 = call ptr @ts_string_create(ptr @70)
  %183 = call ptr @ts_value_make_string(ptr %182)
  %184 = icmp eq ptr %183, null
  %185 = icmp eq ptr %181, null
  %186 = select i1 %184, ptr @__ts_const_undefined_value.11, ptr %183
  %187 = select i1 %185, ptr @__ts_const_undefined_value.11, ptr %181
  %typePtr97 = getelementptr inbounds %TsValue, ptr %186, i32 0, i32 0
  %type98 = load i8, ptr %typePtr97, align 1
  %unionPtr99 = getelementptr inbounds %TsValue, ptr %186, i32 0, i32 2
  %unionVal100 = load i64, ptr %unionPtr99, align 8
  %typePtr101 = getelementptr inbounds %TsValue, ptr %187, i32 0, i32 0
  %type102 = load i8, ptr %typePtr101, align 1
  %unionPtr103 = getelementptr inbounds %TsValue, ptr %187, i32 0, i32 2
  %unionVal104 = load i64, ptr %unionPtr103, align 8
  call void @__ts_map_set_at(ptr %180, i64 %unionVal100, i8 %type98, i64 %unionVal100, i8 %type102, i64 %unionVal104)
  %188 = call ptr @ts_value_make_int(i64 2)
  %189 = call ptr @ts_string_create(ptr @71)
  %190 = call ptr @ts_value_make_string(ptr %189)
  %191 = icmp eq ptr %190, null
  %192 = icmp eq ptr %188, null
  %193 = select i1 %191, ptr @__ts_const_undefined_value.12, ptr %190
  %194 = select i1 %192, ptr @__ts_const_undefined_value.12, ptr %188
  %typePtr105 = getelementptr inbounds %TsValue, ptr %193, i32 0, i32 0
  %type106 = load i8, ptr %typePtr105, align 1
  %unionPtr107 = getelementptr inbounds %TsValue, ptr %193, i32 0, i32 2
  %unionVal108 = load i64, ptr %unionPtr107, align 8
  %typePtr109 = getelementptr inbounds %TsValue, ptr %194, i32 0, i32 0
  %type110 = load i8, ptr %typePtr109, align 1
  %unionPtr111 = getelementptr inbounds %TsValue, ptr %194, i32 0, i32 2
  %unionVal112 = load i64, ptr %unionPtr111, align 8
  call void @__ts_map_set_at(ptr %180, i64 %unionVal108, i8 %type106, i64 %unionVal108, i8 %type110, i64 %unionVal112)
  %195 = call ptr @ts_value_make_int(i64 3)
  %196 = call ptr @ts_string_create(ptr @72)
  %197 = call ptr @ts_value_make_string(ptr %196)
  %198 = icmp eq ptr %197, null
  %199 = icmp eq ptr %195, null
  %200 = select i1 %198, ptr @__ts_const_undefined_value.13, ptr %197
  %201 = select i1 %199, ptr @__ts_const_undefined_value.13, ptr %195
  %typePtr113 = getelementptr inbounds %TsValue, ptr %200, i32 0, i32 0
  %type114 = load i8, ptr %typePtr113, align 1
  %unionPtr115 = getelementptr inbounds %TsValue, ptr %200, i32 0, i32 2
  %unionVal116 = load i64, ptr %unionPtr115, align 8
  %typePtr117 = getelementptr inbounds %TsValue, ptr %201, i32 0, i32 0
  %type118 = load i8, ptr %typePtr117, align 1
  %unionPtr119 = getelementptr inbounds %TsValue, ptr %201, i32 0, i32 2
  %unionVal120 = load i64, ptr %unionPtr119, align 8
  call void @__ts_map_set_at(ptr %180, i64 %unionVal116, i8 %type114, i64 %unionVal116, i8 %type118, i64 %unionVal120)
  store ptr %180, ptr %obj6, align 8
  %obj6121 = load ptr, ptr %obj6, align 8
  %202 = call ptr @ts_value_make_object(ptr %obj6121)
  %203 = call ptr @ts_reflect_ownKeys(ptr %202)
  store ptr %203, ptr %keys, align 8
  %204 = call ptr @ts_string_create(ptr @73)
  %keys122 = load ptr, ptr %keys, align 8
  %205 = call ptr @ts_string_create(ptr @74)
  %206 = call ptr @ts_array_join(ptr %keys122, ptr %205)
  %207 = call ptr @ts_string_concat(ptr %204, ptr %206)
  call void @ts_console_log(ptr %207)
  %208 = call ptr @ts_string_create(ptr @75)
  call void @ts_console_log(ptr %208)
  %209 = call ptr @ts_map_create()
  %210 = call ptr @ts_string_create(ptr @76)
  %211 = call ptr @ts_value_make_string(ptr %210)
  %212 = call ptr @ts_string_create(ptr @77)
  %213 = call ptr @ts_value_make_string(ptr %212)
  %214 = icmp eq ptr %213, null
  %215 = icmp eq ptr %211, null
  %216 = select i1 %214, ptr @__ts_const_undefined_value.14, ptr %213
  %217 = select i1 %215, ptr @__ts_const_undefined_value.14, ptr %211
  %typePtr123 = getelementptr inbounds %TsValue, ptr %216, i32 0, i32 0
  %type124 = load i8, ptr %typePtr123, align 1
  %unionPtr125 = getelementptr inbounds %TsValue, ptr %216, i32 0, i32 2
  %unionVal126 = load i64, ptr %unionPtr125, align 8
  %typePtr127 = getelementptr inbounds %TsValue, ptr %217, i32 0, i32 0
  %type128 = load i8, ptr %typePtr127, align 1
  %unionPtr129 = getelementptr inbounds %TsValue, ptr %217, i32 0, i32 2
  %unionVal130 = load i64, ptr %unionPtr129, align 8
  call void @__ts_map_set_at(ptr %209, i64 %unionVal126, i8 %type124, i64 %unionVal126, i8 %type128, i64 %unionVal130)
  %218 = call ptr @ts_string_create(ptr @78)
  %219 = call ptr @ts_value_make_string(ptr %218)
  %220 = call ptr @ts_string_create(ptr @79)
  %221 = call ptr @ts_value_make_string(ptr %220)
  %222 = icmp eq ptr %221, null
  %223 = icmp eq ptr %219, null
  %224 = select i1 %222, ptr @__ts_const_undefined_value.15, ptr %221
  %225 = select i1 %223, ptr @__ts_const_undefined_value.15, ptr %219
  %typePtr131 = getelementptr inbounds %TsValue, ptr %224, i32 0, i32 0
  %type132 = load i8, ptr %typePtr131, align 1
  %unionPtr133 = getelementptr inbounds %TsValue, ptr %224, i32 0, i32 2
  %unionVal134 = load i64, ptr %unionPtr133, align 8
  %typePtr135 = getelementptr inbounds %TsValue, ptr %225, i32 0, i32 0
  %type136 = load i8, ptr %typePtr135, align 1
  %unionPtr137 = getelementptr inbounds %TsValue, ptr %225, i32 0, i32 2
  %unionVal138 = load i64, ptr %unionPtr137, align 8
  call void @__ts_map_set_at(ptr %209, i64 %unionVal134, i8 %type132, i64 %unionVal134, i8 %type136, i64 %unionVal138)
  store ptr %209, ptr %target7, align 8
  %226 = call ptr @ts_map_create()
  %227 = call ptr @ts_value_make_function(ptr @func_expr_2, ptr null)
  %228 = call ptr @ts_string_create(ptr @82)
  %229 = call ptr @ts_value_make_string(ptr %228)
  %230 = icmp eq ptr %229, null
  %231 = icmp eq ptr %227, null
  %232 = select i1 %230, ptr @__ts_const_undefined_value.16, ptr %229
  %233 = select i1 %231, ptr @__ts_const_undefined_value.16, ptr %227
  %typePtr139 = getelementptr inbounds %TsValue, ptr %232, i32 0, i32 0
  %type140 = load i8, ptr %typePtr139, align 1
  %unionPtr141 = getelementptr inbounds %TsValue, ptr %232, i32 0, i32 2
  %unionVal142 = load i64, ptr %unionPtr141, align 8
  %typePtr143 = getelementptr inbounds %TsValue, ptr %233, i32 0, i32 0
  %type144 = load i8, ptr %typePtr143, align 1
  %unionPtr145 = getelementptr inbounds %TsValue, ptr %233, i32 0, i32 2
  %unionVal146 = load i64, ptr %unionPtr145, align 8
  call void @__ts_map_set_at(ptr %226, i64 %unionVal142, i8 %type140, i64 %unionVal142, i8 %type144, i64 %unionVal146)
  store ptr %226, ptr %handler7, align 8
  %target7147 = load ptr, ptr %target7, align 8
  %234 = call ptr @ts_value_make_object(ptr %target7147)
  %handler7148 = load ptr, ptr %handler7, align 8
  %235 = call ptr @ts_value_make_object(ptr %handler7148)
  %236 = call ptr @ts_proxy_create(ptr %234, ptr %235)
  store ptr %236, ptr %proxy7, align 8
  %237 = call ptr @ts_string_create(ptr @83)
  %238 = call ptr @ts_string_create(ptr @84)
  %proxy7149 = load ptr, ptr %proxy7, align 8
  %239 = call ptr @ts_value_make_string(ptr %238)
  %240 = call i1 @ts_object_has_prop(ptr %proxy7149, ptr %239)
  %241 = call ptr @ts_string_from_bool(i1 %240)
  %242 = call ptr @ts_string_concat(ptr %237, ptr %241)
  call void @ts_console_log(ptr %242)
  %243 = call ptr @ts_string_create(ptr @85)
  %244 = call ptr @ts_string_create(ptr @86)
  %proxy7150 = load ptr, ptr %proxy7, align 8
  %245 = call ptr @ts_value_make_string(ptr %244)
  %246 = call i1 @ts_object_has_prop(ptr %proxy7150, ptr %245)
  %247 = call ptr @ts_string_from_bool(i1 %246)
  %248 = call ptr @ts_string_concat(ptr %243, ptr %247)
  call void @ts_console_log(ptr %248)
  %249 = call ptr @ts_string_create(ptr @87)
  call void @ts_console_log(ptr %249)
  %250 = call ptr @ts_map_create()
  %251 = call ptr @ts_string_create(ptr @88)
  %252 = call ptr @ts_value_make_string(ptr %251)
  %253 = call ptr @ts_string_create(ptr @89)
  %254 = call ptr @ts_value_make_string(ptr %253)
  %255 = icmp eq ptr %254, null
  %256 = icmp eq ptr %252, null
  %257 = select i1 %255, ptr @__ts_const_undefined_value.17, ptr %254
  %258 = select i1 %256, ptr @__ts_const_undefined_value.17, ptr %252
  %typePtr151 = getelementptr inbounds %TsValue, ptr %257, i32 0, i32 0
  %type152 = load i8, ptr %typePtr151, align 1
  %unionPtr153 = getelementptr inbounds %TsValue, ptr %257, i32 0, i32 2
  %unionVal154 = load i64, ptr %unionPtr153, align 8
  %typePtr155 = getelementptr inbounds %TsValue, ptr %258, i32 0, i32 0
  %type156 = load i8, ptr %typePtr155, align 1
  %unionPtr157 = getelementptr inbounds %TsValue, ptr %258, i32 0, i32 2
  %unionVal158 = load i64, ptr %unionPtr157, align 8
  call void @__ts_map_set_at(ptr %250, i64 %unionVal154, i8 %type152, i64 %unionVal154, i8 %type156, i64 %unionVal158)
  store ptr %250, ptr %target8, align 8
  %target8159 = load ptr, ptr %target8, align 8
  %259 = call ptr @ts_value_make_object(ptr %target8159)
  %260 = call ptr @ts_map_create()
  %261 = call ptr @ts_value_make_object(ptr %260)
  %262 = call ptr @ts_proxy_revocable(ptr %259, ptr %261)
  %263 = call ptr @ts_string_create(ptr @90)
  %264 = call ptr @ts_value_get_property(ptr %262, ptr %263)
  store ptr %264, ptr %proxy8, align 8
  %265 = call ptr @ts_string_create(ptr @91)
  %266 = call ptr @ts_value_get_property(ptr %262, ptr %265)
  store ptr %266, ptr %revoke, align 8
  %267 = call ptr @ts_string_create(ptr @92)
  %proxy8160 = load ptr, ptr %proxy8, align 8
  %268 = call ptr @ts_string_create(ptr @93)
  %269 = call ptr @ts_value_make_string(ptr %268)
  %270 = call ptr @ts_object_get_dynamic(ptr %proxy8160, ptr %269)
  %271 = call ptr @ts_value_make_string(ptr %267)
  %272 = call ptr @ts_value_add(ptr %271, ptr %270)
  call void @ts_console_log_value(ptr %272)
  %revoke161 = load ptr, ptr %revoke, align 8
  %273 = call ptr @ts_call_0(ptr %revoke161)
  %274 = call ptr @ts_string_create(ptr @94)
  call void @ts_console_log(ptr %274)
  %275 = call ptr @ts_string_create(ptr @95)
  call void @ts_console_log(ptr %275)
  ret double 0.000000e+00

dead:                                             ; No predecessors!
  br label %return
}

define ptr @__synthetic_user_main(ptr %context) #0 !type !11 {
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
  %4 = call ptr @ts_string_create(ptr @96)
  %5 = call ptr @ts_value_make_string(ptr %4)
  %6 = icmp eq ptr %5, null
  %7 = icmp eq ptr %3, null
  %8 = select i1 %6, ptr @__ts_const_undefined_value.18, ptr %5
  %9 = select i1 %7, ptr @__ts_const_undefined_value.18, ptr %3
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
  %10 = call ptr @ts_string_create(ptr @97)
  %11 = call ptr @ts_value_make_string(ptr %10)
  %__module_obj_05 = load ptr, ptr %__module_obj_0, align 8
  %12 = call ptr @ts_value_box_any(ptr %__module_obj_05)
  call void @ts_module_register(ptr %11, ptr %12)
  %__module_obj_06 = load ptr, ptr %__module_obj_0, align 8
  %13 = call ptr @__module_init_13086476663298097481_any(ptr %context, ptr %__module_obj_06)
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

declare ptr @ts_value_box_any(ptr)

declare ptr @ts_object_get_dynamic(ptr, ptr)

declare void @ts_console_log(ptr)

declare ptr @ts_map_create()

declare void @__ts_map_set_at(ptr, i64, i8, i64, i8, i64)

define internal ptr @func_expr_0(ptr %context, ptr %obj, ptr %prop, ptr %receiver) #0 !type !12 {
entry:
  %receiver3 = alloca ptr, align 8
  %prop2 = alloca ptr, align 8
  %obj1 = alloca ptr, align 8
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
  store ptr %obj, ptr %obj1, align 8
  store ptr %prop, ptr %prop2, align 8
  store ptr %receiver, ptr %receiver3, align 8
  %1 = call ptr @ts_string_create(ptr @31)
  %prop4 = load ptr, ptr %prop2, align 8
  %2 = call ptr @ts_value_make_string(ptr %1)
  %3 = call ptr @ts_value_add(ptr %2, ptr %prop4)
  call void @ts_console_log_value(ptr %3)
  %obj5 = load ptr, ptr %obj1, align 8
  %prop6 = load ptr, ptr %prop2, align 8
  %receiver7 = load ptr, ptr %receiver3, align 8
  %4 = call ptr @ts_reflect_get(ptr %obj5, ptr %prop6, ptr %receiver7)
  ret ptr %4

return:                                           ; preds = %dead
  %5 = load ptr, ptr %returnValue, align 8
  ret ptr %5

dead:                                             ; No predecessors!
  br label %return
}

declare ptr @ts_value_add(ptr, ptr)

declare void @ts_console_log_value(ptr)

declare ptr @ts_reflect_get(ptr, ptr, ptr)

declare ptr @ts_value_make_function(ptr, ptr)

declare ptr @ts_proxy_create(ptr, ptr)

define internal ptr @func_expr_1(ptr %context, ptr %obj, ptr %prop, ptr %value, ptr %receiver) #0 !type !12 {
entry:
  %receiver4 = alloca ptr, align 8
  %value3 = alloca ptr, align 8
  %prop2 = alloca ptr, align 8
  %obj1 = alloca ptr, align 8
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
  store ptr %obj, ptr %obj1, align 8
  store ptr %prop, ptr %prop2, align 8
  store ptr %value, ptr %value3, align 8
  store ptr %receiver, ptr %receiver4, align 8
  %1 = call ptr @ts_string_create(ptr @39)
  %prop5 = load ptr, ptr %prop2, align 8
  %2 = call ptr @ts_value_make_string(ptr %1)
  %3 = call ptr @ts_value_add(ptr %2, ptr %prop5)
  %4 = call ptr @ts_string_create(ptr @40)
  %5 = call ptr @ts_string_from_value(ptr %3)
  %6 = call ptr @ts_string_concat(ptr %5, ptr %4)
  %value6 = load ptr, ptr %value3, align 8
  %7 = call ptr @ts_value_make_string(ptr %6)
  %8 = call ptr @ts_value_add(ptr %7, ptr %value6)
  call void @ts_console_log_value(ptr %8)
  %obj7 = load ptr, ptr %obj1, align 8
  %prop8 = load ptr, ptr %prop2, align 8
  %value9 = load ptr, ptr %value3, align 8
  %receiver10 = load ptr, ptr %receiver4, align 8
  %9 = call i64 @ts_reflect_set(ptr %obj7, ptr %prop8, ptr %value9, ptr %receiver10)
  %10 = icmp ne i64 %9, 0
  %11 = call ptr @ts_value_make_bool(i1 %10)
  ret ptr %11

return:                                           ; preds = %dead
  %12 = load ptr, ptr %returnValue, align 8
  ret ptr %12

dead:                                             ; No predecessors!
  br label %return
}

declare ptr @ts_string_from_value(ptr)

declare ptr @ts_string_concat(ptr, ptr)

declare i64 @ts_reflect_set(ptr, ptr, ptr, ptr)

declare void @ts_object_set_dynamic(ptr, ptr, ptr)

declare ptr @ts_value_get_object(ptr)

declare void @ts_panic(ptr)

declare i64 @__ts_map_find_bucket(ptr, i64, i8, i64)

declare void @__ts_map_get_value_at(ptr, i64, ptr, ptr)

declare i64 @ts_value_get_int(ptr)

declare ptr @ts_string_from_int(i64)

declare i64 @ts_reflect_has(ptr, ptr)

declare ptr @ts_string_from_bool(i1)

declare ptr @ts_object_keys(ptr)

declare ptr @ts_array_join(ptr, ptr)

declare i64 @ts_reflect_deleteProperty(ptr, ptr)

declare ptr @ts_reflect_ownKeys(ptr)

define internal ptr @func_expr_2(ptr %context, ptr %obj, ptr %prop) #0 !type !12 {
entry:
  %prop2 = alloca ptr, align 8
  %obj1 = alloca ptr, align 8
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
  store ptr %obj, ptr %obj1, align 8
  store ptr %prop, ptr %prop2, align 8
  %prop3 = load ptr, ptr %prop2, align 8
  %1 = call ptr @ts_string_create(ptr @80)
  %2 = call ptr @ts_value_make_string(ptr %1)
  %3 = call ptr @ts_value_strict_eq_wrapper(ptr %prop3, ptr %2)
  %4 = call i1 @ts_value_get_bool(ptr %3)
  br i1 %4, label %then, label %ifcont

return:                                           ; preds = %dead6
  %5 = load ptr, ptr %returnValue, align 8
  ret ptr %5

then:                                             ; preds = %entry
  %6 = call ptr @ts_string_create(ptr @81)
  call void @ts_console_log(ptr %6)
  %7 = call ptr @ts_value_make_bool(i1 false)
  ret ptr %7

dead:                                             ; No predecessors!
  br label %ifcont

ifcont:                                           ; preds = %dead, %entry
  %obj4 = load ptr, ptr %obj1, align 8
  %prop5 = load ptr, ptr %prop2, align 8
  %8 = call i64 @ts_reflect_has(ptr %obj4, ptr %prop5)
  %9 = icmp ne i64 %8, 0
  %10 = call ptr @ts_value_make_bool(i1 %9)
  ret ptr %10

dead6:                                            ; No predecessors!
  br label %return
}

declare ptr @ts_value_strict_eq_wrapper(ptr, ptr)

declare i1 @ts_value_get_bool(ptr)

declare i1 @ts_object_has_prop(ptr, ptr)

declare ptr @ts_proxy_revocable(ptr, ptr)

declare ptr @ts_value_get_property(ptr, ptr)

declare ptr @ts_call_0(ptr)

declare void @ts_module_register(ptr, ptr)

declare ptr @ts_value_make_double(double)

declare i32 @ts_main(i32, ptr, ptr) #0

define i32 @main(i32 %0, ptr %1) #0 {
entry:
  %2 = call i32 @ts_main(i32 %0, ptr %1, ptr @__synthetic_user_main)
  ret i32 0
}

attributes #0 = { "sspstrong" "stack-protector-buffer-size"="8" }

!llvm.linker.options = !{!5, !6, !7, !8}

!0 = !{i64 0, !"RegExpMatchArray"}
!1 = !{i64 0, !"SocketAddress"}
!2 = !{i64 0, !"URLSearchParams"}
!3 = !{i64 0, !"URL"}
!4 = !{i64 0, !"Response"}
!5 = !{!"/FAILIFMISMATCH:\22_ITERATOR_DEBUG_LEVEL=0\22"}
!6 = !{!"/DEFAULTLIB:libcmt.lib"}
!7 = !{!"/NODEFAULTLIB:libcmtd.lib"}
!8 = !{!"/FAILIFMISMATCH:\22RuntimeLibrary=MT_StaticRelease\22"}
!9 = !{i64 0, !"__module_init_13086476663298097481_any"}
!10 = !{i64 0, !"user_main"}
!11 = !{i64 0, !"__synthetic_user_main"}
!12 = !{i64 0, !"TsFunction"}
