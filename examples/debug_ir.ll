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
%inner2_decl_1_closure = type { ptr }

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
@result_12425448642228079428 = global ptr null
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
@27 = private unnamed_addr constant [2 x i8] c"a\00", align 1
@__ts_const_undefined_value = private constant %TsValue zeroinitializer
@28 = private unnamed_addr constant [2 x i8] c"b\00", align 1
@__ts_const_undefined_value.1 = private constant %TsValue zeroinitializer
@29 = private unnamed_addr constant [17 x i8] c"typeof result.a:\00", align 1
@30 = private unnamed_addr constant [30 x i8] c"Null or undefined dereference\00", align 1
@31 = private unnamed_addr constant [2 x i8] c"a\00", align 1
@__ts_const_undefined_value.2 = private constant %TsValue zeroinitializer
@32 = private unnamed_addr constant [9 x i8] c"function\00", align 1
@33 = private unnamed_addr constant [17 x i8] c"typeof result.b:\00", align 1
@34 = private unnamed_addr constant [30 x i8] c"Null or undefined dereference\00", align 1
@35 = private unnamed_addr constant [2 x i8] c"b\00", align 1
@__ts_const_undefined_value.3 = private constant %TsValue zeroinitializer
@36 = private unnamed_addr constant [9 x i8] c"function\00", align 1
@37 = private unnamed_addr constant [12 x i8] c"result.a():\00", align 1
@38 = private unnamed_addr constant [30 x i8] c"Null or undefined dereference\00", align 1
@39 = private unnamed_addr constant [2 x i8] c"a\00", align 1
@40 = private unnamed_addr constant [12 x i8] c"result.b():\00", align 1
@41 = private unnamed_addr constant [2 x i8] c"b\00", align 1
@42 = private unnamed_addr constant [8 x i8] c"exports\00", align 1
@43 = private unnamed_addr constant [8 x i8] c"exports\00", align 1
@__ts_const_undefined_value.4 = private constant %TsValue zeroinitializer
@44 = private unnamed_addr constant [64 x i8] c"E:\\src\\github.com\\cgrinker\\ts-aoc\\examples\\test_iife_minimal.ts\00", align 1

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

define ptr @__module_init_12425448642228079428_any(ptr %context, ptr %module) #0 !type !14 {
entry:
  %mapGetResult6 = alloca %TsValue, align 8
  %mapGetResult = alloca %TsValue, align 8
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
  %5 = call ptr @ts_value_make_function(ptr @func_expr_0, ptr null)
  %6 = call ptr @ts_call_0(ptr %5)
  store ptr %6, ptr @result_12425448642228079428, align 8
  %7 = call ptr @ts_string_create(ptr @29)
  call void @ts_console_log(ptr %7)
  %result = load ptr, ptr @result_12425448642228079428, align 8
  %8 = call ptr @ts_value_get_object(ptr %result)
  %9 = icmp eq ptr %8, null
  br i1 %9, label %null_fail, label %null_cont

return:                                           ; preds = %dead
  %10 = load ptr, ptr %returnValue, align 8
  ret ptr %10

null_fail:                                        ; preds = %entry
  call void @ts_panic(ptr @30)
  unreachable

null_cont:                                        ; preds = %entry
  %11 = call ptr @ts_string_create(ptr @31)
  %12 = call ptr @ts_value_make_string(ptr %11)
  %13 = icmp eq ptr %12, null
  %14 = select i1 %13, ptr @__ts_const_undefined_value.2, ptr %12
  %typePtr = getelementptr inbounds %TsValue, ptr %14, i32 0, i32 0
  %type = load i8, ptr %typePtr, align 1
  %unionPtr = getelementptr inbounds %TsValue, ptr %14, i32 0, i32 2
  %unionVal = load i64, ptr %unionPtr, align 8
  %15 = call i64 @__ts_map_find_bucket(ptr %8, i64 %unionVal, i8 %type, i64 %unionVal)
  %notFound = icmp slt i64 %15, 0
  br i1 %notFound, label %map.notfound, label %map.found

map.found:                                        ; preds = %null_cont
  %16 = alloca i8, align 1
  %17 = alloca i64, align 8
  call void @__ts_map_get_value_at(ptr %8, i64 %15, ptr %16, ptr %17)
  %18 = load i64, ptr %17, align 8
  %19 = load i8, ptr %16, align 1
  %20 = getelementptr inbounds %TsValue, ptr %mapGetResult, i32 0, i32 0
  store i8 %19, ptr %20, align 1
  %21 = getelementptr inbounds %TsValue, ptr %mapGetResult, i32 0, i32 2
  store i64 %18, ptr %21, align 8
  br label %map.done

map.notfound:                                     ; preds = %null_cont
  %22 = getelementptr inbounds %TsValue, ptr %mapGetResult, i32 0, i32 0
  store i8 0, ptr %22, align 1
  %23 = getelementptr inbounds %TsValue, ptr %mapGetResult, i32 0, i32 2
  store i64 0, ptr %23, align 8
  br label %map.done

map.done:                                         ; preds = %map.notfound, %map.found
  %24 = call ptr @ts_string_create(ptr @32)
  call void @ts_console_log(ptr %24)
  %25 = call ptr @ts_string_create(ptr @33)
  call void @ts_console_log(ptr %25)
  %result3 = load ptr, ptr @result_12425448642228079428, align 8
  %26 = call ptr @ts_value_get_object(ptr %result3)
  %27 = icmp eq ptr %26, null
  br i1 %27, label %null_fail4, label %null_cont5

null_fail4:                                       ; preds = %map.done
  call void @ts_panic(ptr @34)
  unreachable

null_cont5:                                       ; preds = %map.done
  %28 = call ptr @ts_string_create(ptr @35)
  %29 = call ptr @ts_value_make_string(ptr %28)
  %30 = icmp eq ptr %29, null
  %31 = select i1 %30, ptr @__ts_const_undefined_value.3, ptr %29
  %typePtr7 = getelementptr inbounds %TsValue, ptr %31, i32 0, i32 0
  %type8 = load i8, ptr %typePtr7, align 1
  %unionPtr9 = getelementptr inbounds %TsValue, ptr %31, i32 0, i32 2
  %unionVal10 = load i64, ptr %unionPtr9, align 8
  %32 = call i64 @__ts_map_find_bucket(ptr %26, i64 %unionVal10, i8 %type8, i64 %unionVal10)
  %notFound11 = icmp slt i64 %32, 0
  br i1 %notFound11, label %map.notfound13, label %map.found12

map.found12:                                      ; preds = %null_cont5
  %33 = alloca i8, align 1
  %34 = alloca i64, align 8
  call void @__ts_map_get_value_at(ptr %26, i64 %32, ptr %33, ptr %34)
  %35 = load i64, ptr %34, align 8
  %36 = load i8, ptr %33, align 1
  %37 = getelementptr inbounds %TsValue, ptr %mapGetResult6, i32 0, i32 0
  store i8 %36, ptr %37, align 1
  %38 = getelementptr inbounds %TsValue, ptr %mapGetResult6, i32 0, i32 2
  store i64 %35, ptr %38, align 8
  br label %map.done14

map.notfound13:                                   ; preds = %null_cont5
  %39 = getelementptr inbounds %TsValue, ptr %mapGetResult6, i32 0, i32 0
  store i8 0, ptr %39, align 1
  %40 = getelementptr inbounds %TsValue, ptr %mapGetResult6, i32 0, i32 2
  store i64 0, ptr %40, align 8
  br label %map.done14

map.done14:                                       ; preds = %map.notfound13, %map.found12
  %41 = call ptr @ts_string_create(ptr @36)
  call void @ts_console_log(ptr %41)
  %42 = call ptr @ts_string_create(ptr @37)
  call void @ts_console_log(ptr %42)
  %result15 = load ptr, ptr @result_12425448642228079428, align 8
  %43 = icmp eq ptr %result15, null
  br i1 %43, label %null_fail16, label %null_cont17

null_fail16:                                      ; preds = %map.done14
  call void @ts_panic(ptr @38)
  unreachable

null_cont17:                                      ; preds = %map.done14
  %44 = call ptr @ts_value_get_object(ptr %result15)
  %45 = call ptr @ts_object_get_property(ptr %44, ptr @39)
  %46 = call ptr @ts_call_0(ptr %45)
  %47 = call i64 @ts_value_get_int(ptr %46)
  call void @ts_console_log_int(i64 %47)
  %48 = call ptr @ts_string_create(ptr @40)
  call void @ts_console_log(ptr %48)
  %result18 = load ptr, ptr @result_12425448642228079428, align 8
  %49 = call ptr @ts_value_get_object(ptr %result18)
  %50 = call ptr @ts_object_get_property(ptr %49, ptr @41)
  %51 = call ptr @ts_call_0(ptr %50)
  %52 = call i64 @ts_value_get_int(ptr %51)
  call void @ts_console_log_int(i64 %52)
  %module19 = load ptr, ptr %module1, align 8
  %53 = call ptr @ts_value_box_any(ptr %module19)
  %54 = call ptr @ts_string_create(ptr @42)
  %55 = call ptr @ts_value_make_string(ptr %54)
  %56 = call ptr @ts_object_get_dynamic(ptr %53, ptr %55)
  ret ptr %56

dead:                                             ; No predecessors!
  br label %return
}

define ptr @user_main(ptr %context) #0 !type !15 {
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
  %4 = call ptr @ts_string_create(ptr @43)
  %5 = call ptr @ts_value_make_string(ptr %4)
  %6 = icmp eq ptr %5, null
  %7 = icmp eq ptr %3, null
  %8 = select i1 %6, ptr @__ts_const_undefined_value.4, ptr %5
  %9 = select i1 %7, ptr @__ts_const_undefined_value.4, ptr %3
  %typePtr = getelementptr inbounds %TsValue, ptr %8, i32 0, i32 0
  %type = load i8, ptr %typePtr, align 1
  %unionPtr = getelementptr inbounds %TsValue, ptr %8, i32 0, i32 2
  %unionVal = load i64, ptr %unionPtr, align 8
  %typePtr1 = getelementptr inbounds %TsValue, ptr %9, i32 0, i32 0
  %type2 = load i8, ptr %typePtr1, align 1
  %unionPtr3 = getelementptr inbounds %TsValue, ptr %9, i32 0, i32 2
  %unionVal4 = load i64, ptr %unionPtr3, align 8
  call void @__ts_map_set_at(ptr %1, i64 %unionVal, i8 %type, i64 %unionVal, i8 %type2, i64 %unionVal4)
  %10 = call ptr @ts_value_make_object(ptr %1)
  store ptr %10, ptr %__module_obj_0, align 8
  %11 = call ptr @ts_string_create(ptr @44)
  %12 = call ptr @ts_value_make_string(ptr %11)
  %__module_obj_05 = load ptr, ptr %__module_obj_0, align 8
  call void @ts_module_register(ptr %12, ptr %__module_obj_05)
  %__module_obj_06 = load ptr, ptr %__module_obj_0, align 8
  %13 = call ptr @__module_init_12425448642228079428_any(ptr null, ptr %__module_obj_06)
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

define internal ptr @func_expr_0(ptr %context) #0 !type !16 {
entry:
  %inner2 = alloca ptr, align 8
  %inner1 = alloca ptr, align 8
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
  %1 = call ptr @ts_cell_create(ptr null)
  store ptr %1, ptr %inner1, align 8
  %2 = call ptr @ts_cell_create(ptr null)
  store ptr %2, ptr %inner2, align 8
  %3 = call ptr @ts_value_make_function(ptr @inner1_decl_0, ptr %context)
  call void @ts_cell_set(ptr %1, ptr %3)
  %4 = call ptr @ts_pool_alloc(i64 8)
  %5 = getelementptr inbounds %inner2_decl_1_closure, ptr %4, i32 0, i32 0
  %6 = load ptr, ptr %inner1, align 8
  store ptr %6, ptr %5, align 8
  %7 = call ptr @ts_value_make_function(ptr @inner2_decl_1, ptr %4)
  call void @ts_cell_set(ptr %2, ptr %7)
  %8 = call ptr @ts_map_create()
  %9 = load ptr, ptr %inner1, align 8
  %10 = call ptr @ts_cell_get(ptr %9)
  %11 = call ptr @ts_string_create(ptr @27)
  %12 = call ptr @ts_value_make_string(ptr %11)
  %13 = icmp eq ptr %12, null
  %14 = icmp eq ptr %10, null
  %15 = select i1 %13, ptr @__ts_const_undefined_value, ptr %12
  %16 = select i1 %14, ptr @__ts_const_undefined_value, ptr %10
  %typePtr = getelementptr inbounds %TsValue, ptr %15, i32 0, i32 0
  %type = load i8, ptr %typePtr, align 1
  %unionPtr = getelementptr inbounds %TsValue, ptr %15, i32 0, i32 2
  %unionVal = load i64, ptr %unionPtr, align 8
  %typePtr1 = getelementptr inbounds %TsValue, ptr %16, i32 0, i32 0
  %type2 = load i8, ptr %typePtr1, align 1
  %unionPtr3 = getelementptr inbounds %TsValue, ptr %16, i32 0, i32 2
  %unionVal4 = load i64, ptr %unionPtr3, align 8
  call void @__ts_map_set_at(ptr %8, i64 %unionVal, i8 %type, i64 %unionVal, i8 %type2, i64 %unionVal4)
  %17 = load ptr, ptr %inner2, align 8
  %18 = call ptr @ts_cell_get(ptr %17)
  %19 = call ptr @ts_string_create(ptr @28)
  %20 = call ptr @ts_value_make_string(ptr %19)
  %21 = icmp eq ptr %20, null
  %22 = icmp eq ptr %18, null
  %23 = select i1 %21, ptr @__ts_const_undefined_value.1, ptr %20
  %24 = select i1 %22, ptr @__ts_const_undefined_value.1, ptr %18
  %typePtr5 = getelementptr inbounds %TsValue, ptr %23, i32 0, i32 0
  %type6 = load i8, ptr %typePtr5, align 1
  %unionPtr7 = getelementptr inbounds %TsValue, ptr %23, i32 0, i32 2
  %unionVal8 = load i64, ptr %unionPtr7, align 8
  %typePtr9 = getelementptr inbounds %TsValue, ptr %24, i32 0, i32 0
  %type10 = load i8, ptr %typePtr9, align 1
  %unionPtr11 = getelementptr inbounds %TsValue, ptr %24, i32 0, i32 2
  %unionVal12 = load i64, ptr %unionPtr11, align 8
  call void @__ts_map_set_at(ptr %8, i64 %unionVal8, i8 %type6, i64 %unionVal8, i8 %type10, i64 %unionVal12)
  %25 = call ptr @ts_value_make_object(ptr %8)
  ret ptr %25

return:                                           ; preds = %dead
  %26 = load ptr, ptr %returnValue, align 8
  ret ptr %26

dead:                                             ; No predecessors!
  br label %return
}

declare ptr @ts_cell_create(ptr)

define internal ptr @inner1_decl_0(ptr %context) {
entry:
  %returnValue = alloca ptr, align 8
  %continueTarget = alloca ptr, align 8
  %breakTarget = alloca ptr, align 8
  %shouldContinue = alloca i1, align 1
  %shouldBreak = alloca i1, align 1
  %shouldReturn = alloca i1, align 1
  store i1 false, ptr %shouldReturn, align 1
  store i1 false, ptr %shouldBreak, align 1
  store i1 false, ptr %shouldContinue, align 1
  store ptr null, ptr %returnValue, align 8
  %0 = call ptr @ts_value_make_int(i64 1)
  ret ptr %0

return:                                           ; preds = %dead
  %1 = load ptr, ptr %returnValue, align 8
  ret ptr %1

dead:                                             ; No predecessors!
  br label %return
}

declare ptr @ts_value_make_function(ptr, ptr)

declare void @ts_cell_set(ptr, ptr)

define internal ptr @inner2_decl_1(ptr %context) {
entry:
  %inner1 = alloca ptr, align 8
  %returnValue = alloca ptr, align 8
  %continueTarget = alloca ptr, align 8
  %breakTarget = alloca ptr, align 8
  %shouldContinue = alloca i1, align 1
  %shouldBreak = alloca i1, align 1
  %shouldReturn = alloca i1, align 1
  store i1 false, ptr %shouldReturn, align 1
  store i1 false, ptr %shouldBreak, align 1
  store i1 false, ptr %shouldContinue, align 1
  store ptr null, ptr %returnValue, align 8
  %0 = getelementptr inbounds %inner2_decl_1_closure, ptr %context, i32 0, i32 0
  %1 = load ptr, ptr %0, align 8
  store ptr %1, ptr %inner1, align 8
  %2 = load ptr, ptr %inner1, align 8
  %3 = call ptr @ts_cell_get(ptr %2)
  %4 = call ptr @ts_call_0(ptr %3)
  %5 = call i64 @ts_value_get_int(ptr %4)
  %addtmp = add i64 %5, 1
  %6 = call ptr @ts_value_make_int(i64 %addtmp)
  ret ptr %6

return:                                           ; preds = %dead
  %7 = load ptr, ptr %returnValue, align 8
  ret ptr %7

dead:                                             ; No predecessors!
  br label %return
}

declare ptr @ts_pool_alloc(i64)

declare ptr @ts_cell_get(ptr)

declare ptr @ts_call_0(ptr)

declare i64 @ts_value_get_int(ptr)

declare ptr @ts_map_create()

declare void @__ts_map_set_at(ptr, i64, i8, i64, i8, i64)

declare void @ts_console_log(ptr)

declare ptr @ts_value_get_object(ptr)

declare void @ts_panic(ptr)

declare i64 @__ts_map_find_bucket(ptr, i64, i8, i64)

declare void @__ts_map_get_value_at(ptr, i64, ptr, ptr)

declare ptr @ts_object_get_property(ptr, ptr)

declare void @ts_console_log_int(i64)

declare void @ts_module_register(ptr, ptr)

declare i32 @ts_main(i32, ptr, ptr) #0

define i32 @main(i32 %0, ptr %1) #0 {
entry:
  %2 = call i32 @ts_main(i32 %0, ptr %1, ptr @user_main)
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
!14 = !{i64 0, !"__module_init_12425448642228079428_any"}
!15 = !{i64 0, !"user_main"}
!16 = !{i64 0, !"TsFunction"}
