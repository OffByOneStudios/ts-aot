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
%URL_VTable = type { ptr, ptr, ptr, ptr }
%Response_VTable = type { ptr, ptr, ptr, ptr }
%Uint8Array = type { ptr, ptr, i64 }
%Uint32Array = type { ptr, ptr, i64 }
%Float64Array = type { ptr, ptr, i64 }
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
@parseFloat = external global ptr
@parseInt = external global ptr
@process = external global ptr
@_ = global ptr null
@TextEncoder = global ptr null
@null = global ptr null
@undefined = global ptr null
@Symbol = global ptr null
@TextDecoder = global ptr null
@https = global ptr null
@events = global ptr null
@stream = global ptr null
@http = global ptr null
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
@11 = private unnamed_addr constant [5 x i8] c"hash\00", align 1
@12 = private unnamed_addr constant [5 x i8] c"host\00", align 1
@13 = private unnamed_addr constant [9 x i8] c"hostname\00", align 1
@14 = private unnamed_addr constant [5 x i8] c"href\00", align 1
@15 = private unnamed_addr constant [7 x i8] c"origin\00", align 1
@16 = private unnamed_addr constant [9 x i8] c"password\00", align 1
@17 = private unnamed_addr constant [9 x i8] c"pathname\00", align 1
@18 = private unnamed_addr constant [5 x i8] c"port\00", align 1
@19 = private unnamed_addr constant [9 x i8] c"protocol\00", align 1
@20 = private unnamed_addr constant [7 x i8] c"search\00", align 1
@21 = private unnamed_addr constant [13 x i8] c"searchParams\00", align 1
@22 = private unnamed_addr constant [9 x i8] c"username\00", align 1
@URL_VTable_Global = constant %URL_VTable { ptr null, ptr @URL_get_property, ptr @URL_toJSON, ptr @URL_toString }, !type !8
@23 = private unnamed_addr constant [3 x i8] c"ok\00", align 1
@24 = private unnamed_addr constant [7 x i8] c"status\00", align 1
@25 = private unnamed_addr constant [11 x i8] c"statusText\00", align 1
@Response_VTable_Global = constant %Response_VTable { ptr null, ptr @Response_get_property, ptr @Response_json, ptr @Response_text }, !type !9
@26 = private unnamed_addr constant [8 x i8] c"exports\00", align 1
@27 = private unnamed_addr constant [7 x i8] c"lodash\00", align 1
@28 = private unnamed_addr constant [62 x i8] c"e:\\src\\github.com\\cgrinker\\ts-aoc\\examples\\lodash_npm_test.ts\00", align 1
@29 = private unnamed_addr constant [8 x i8] c"exports\00", align 1
@30 = private unnamed_addr constant [8 x i8] c"exports\00", align 1
@31 = private unnamed_addr constant [62 x i8] c"e:\\src\\github.com\\cgrinker\\ts-aoc\\examples\\lodash_npm_test.ts\00", align 1
@32 = private unnamed_addr constant [81 x i8] c"module_init_begin: e:\\src\\github.com\\cgrinker\\ts-aoc\\examples\\lodash_npm_test.ts\00", align 1
@33 = private unnamed_addr constant [79 x i8] c"module_init_end: e:\\src\\github.com\\cgrinker\\ts-aoc\\examples\\lodash_npm_test.ts\00", align 1
@34 = private unnamed_addr constant [6 x i8] c"chunk\00", align 1
@35 = private unnamed_addr constant [7 x i8] c"chunk:\00", align 1
@36 = private unnamed_addr constant [6 x i8] c"merge\00", align 1
@37 = private unnamed_addr constant [2 x i8] c"a\00", align 1
@38 = private unnamed_addr constant [2 x i8] c"x\00", align 1
@39 = private unnamed_addr constant [7 x i8] c"nested\00", align 1
@40 = private unnamed_addr constant [2 x i8] c"b\00", align 1
@41 = private unnamed_addr constant [2 x i8] c"y\00", align 1
@42 = private unnamed_addr constant [7 x i8] c"nested\00", align 1
@43 = private unnamed_addr constant [7 x i8] c"merge:\00", align 1
@44 = private unnamed_addr constant [8 x i8] c"shuffle\00", align 1
@45 = private unnamed_addr constant [16 x i8] c"shuffle length:\00", align 1
@46 = private unnamed_addr constant [7 x i8] c"length\00", align 1

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

define ptr @URL_get_property(ptr %0, ptr %1) #0 {
entry:
  %2 = call ptr @ts_string_create(ptr @11)
  %3 = call i1 @ts_string_eq(ptr %1, ptr %2)
  br i1 %3, label %match_hash, label %next_hash

match_hash:                                       ; preds = %entry
  %4 = getelementptr inbounds %URL, ptr %0, i32 0, i32 1
  %5 = load ptr, ptr %4, align 8
  %6 = call ptr @ts_value_make_string(ptr %5)
  ret ptr %6

next_hash:                                        ; preds = %entry
  %7 = call ptr @ts_string_create(ptr @12)
  %8 = call i1 @ts_string_eq(ptr %1, ptr %7)
  br i1 %8, label %match_host, label %next_host

match_host:                                       ; preds = %next_hash
  %9 = getelementptr inbounds %URL, ptr %0, i32 0, i32 2
  %10 = load ptr, ptr %9, align 8
  %11 = call ptr @ts_value_make_string(ptr %10)
  ret ptr %11

next_host:                                        ; preds = %next_hash
  %12 = call ptr @ts_string_create(ptr @13)
  %13 = call i1 @ts_string_eq(ptr %1, ptr %12)
  br i1 %13, label %match_hostname, label %next_hostname

match_hostname:                                   ; preds = %next_host
  %14 = getelementptr inbounds %URL, ptr %0, i32 0, i32 3
  %15 = load ptr, ptr %14, align 8
  %16 = call ptr @ts_value_make_string(ptr %15)
  ret ptr %16

next_hostname:                                    ; preds = %next_host
  %17 = call ptr @ts_string_create(ptr @14)
  %18 = call i1 @ts_string_eq(ptr %1, ptr %17)
  br i1 %18, label %match_href, label %next_href

match_href:                                       ; preds = %next_hostname
  %19 = getelementptr inbounds %URL, ptr %0, i32 0, i32 4
  %20 = load ptr, ptr %19, align 8
  %21 = call ptr @ts_value_make_string(ptr %20)
  ret ptr %21

next_href:                                        ; preds = %next_hostname
  %22 = call ptr @ts_string_create(ptr @15)
  %23 = call i1 @ts_string_eq(ptr %1, ptr %22)
  br i1 %23, label %match_origin, label %next_origin

match_origin:                                     ; preds = %next_href
  %24 = getelementptr inbounds %URL, ptr %0, i32 0, i32 5
  %25 = load ptr, ptr %24, align 8
  %26 = call ptr @ts_value_make_string(ptr %25)
  ret ptr %26

next_origin:                                      ; preds = %next_href
  %27 = call ptr @ts_string_create(ptr @16)
  %28 = call i1 @ts_string_eq(ptr %1, ptr %27)
  br i1 %28, label %match_password, label %next_password

match_password:                                   ; preds = %next_origin
  %29 = getelementptr inbounds %URL, ptr %0, i32 0, i32 6
  %30 = load ptr, ptr %29, align 8
  %31 = call ptr @ts_value_make_string(ptr %30)
  ret ptr %31

next_password:                                    ; preds = %next_origin
  %32 = call ptr @ts_string_create(ptr @17)
  %33 = call i1 @ts_string_eq(ptr %1, ptr %32)
  br i1 %33, label %match_pathname, label %next_pathname

match_pathname:                                   ; preds = %next_password
  %34 = getelementptr inbounds %URL, ptr %0, i32 0, i32 7
  %35 = load ptr, ptr %34, align 8
  %36 = call ptr @ts_value_make_string(ptr %35)
  ret ptr %36

next_pathname:                                    ; preds = %next_password
  %37 = call ptr @ts_string_create(ptr @18)
  %38 = call i1 @ts_string_eq(ptr %1, ptr %37)
  br i1 %38, label %match_port, label %next_port

match_port:                                       ; preds = %next_pathname
  %39 = getelementptr inbounds %URL, ptr %0, i32 0, i32 8
  %40 = load ptr, ptr %39, align 8
  %41 = call ptr @ts_value_make_string(ptr %40)
  ret ptr %41

next_port:                                        ; preds = %next_pathname
  %42 = call ptr @ts_string_create(ptr @19)
  %43 = call i1 @ts_string_eq(ptr %1, ptr %42)
  br i1 %43, label %match_protocol, label %next_protocol

match_protocol:                                   ; preds = %next_port
  %44 = getelementptr inbounds %URL, ptr %0, i32 0, i32 9
  %45 = load ptr, ptr %44, align 8
  %46 = call ptr @ts_value_make_string(ptr %45)
  ret ptr %46

next_protocol:                                    ; preds = %next_port
  %47 = call ptr @ts_string_create(ptr @20)
  %48 = call i1 @ts_string_eq(ptr %1, ptr %47)
  br i1 %48, label %match_search, label %next_search

match_search:                                     ; preds = %next_protocol
  %49 = getelementptr inbounds %URL, ptr %0, i32 0, i32 10
  %50 = load ptr, ptr %49, align 8
  %51 = call ptr @ts_value_make_string(ptr %50)
  ret ptr %51

next_search:                                      ; preds = %next_protocol
  %52 = call ptr @ts_string_create(ptr @21)
  %53 = call i1 @ts_string_eq(ptr %1, ptr %52)
  br i1 %53, label %match_searchParams, label %next_searchParams

match_searchParams:                               ; preds = %next_search
  %54 = getelementptr inbounds %URL, ptr %0, i32 0, i32 11
  %55 = load ptr, ptr %54, align 8
  %56 = call ptr @ts_value_make_object(ptr %55)
  ret ptr %56

next_searchParams:                                ; preds = %next_search
  %57 = call ptr @ts_string_create(ptr @22)
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
  %2 = call ptr @ts_string_create(ptr @23)
  %3 = call i1 @ts_string_eq(ptr %1, ptr %2)
  br i1 %3, label %match_ok, label %next_ok

match_ok:                                         ; preds = %entry
  %4 = getelementptr inbounds %Response, ptr %0, i32 0, i32 1
  %5 = load i1, ptr %4, align 1
  %6 = call ptr @ts_value_make_bool(i1 %5)
  ret ptr %6

next_ok:                                          ; preds = %entry
  %7 = call ptr @ts_string_create(ptr @24)
  %8 = call i1 @ts_string_eq(ptr %1, ptr %7)
  br i1 %8, label %match_status, label %next_status

match_status:                                     ; preds = %next_ok
  %9 = getelementptr inbounds %Response, ptr %0, i32 0, i32 2
  %10 = load i64, ptr %9, align 8
  %11 = call ptr @ts_value_make_int(i64 %10)
  ret ptr %11

next_status:                                      ; preds = %next_ok
  %12 = call ptr @ts_string_create(ptr @25)
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

define ptr @__module_init_1841422227439969958_any(ptr %context, ptr %module) #0 !type !14 {
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
  store ptr null, ptr %returnValue, align 8
  store ptr %module, ptr %module1, align 8
  %module2 = load ptr, ptr %module1, align 8
  %0 = call ptr @ts_value_box_any(ptr %module2)
  %1 = call ptr @ts_string_create(ptr @26)
  %2 = call ptr @ts_value_make_string(ptr %1)
  %3 = call ptr @ts_object_get_prop(ptr %0, ptr %2)
  store ptr %3, ptr %exports, align 8
  %4 = call ptr @ts_string_create(ptr @27)
  %5 = call ptr @ts_value_make_string(ptr %4)
  %6 = call ptr @ts_require(ptr %5, ptr @28)
  store ptr %6, ptr @_, align 8
  call void @__ts_main(ptr null)
  %module3 = load ptr, ptr %module1, align 8
  %7 = call ptr @ts_value_box_any(ptr %module3)
  %8 = call ptr @ts_string_create(ptr @29)
  %9 = call ptr @ts_value_make_string(ptr %8)
  %10 = call ptr @ts_object_get_prop(ptr %7, ptr %9)
  ret ptr %10

return:                                           ; preds = %dead
  %11 = load ptr, ptr %returnValue, align 8
  ret ptr %11

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
  store ptr null, ptr %returnValue, align 8
  %0 = call ptr @ts_map_create()
  %1 = call ptr @ts_map_create()
  %2 = call ptr @ts_value_make_object(ptr %1)
  %3 = call ptr @ts_string_create(ptr @30)
  %4 = call ptr @ts_value_make_string(ptr %3)
  call void @ts_map_set(ptr %0, ptr %4, ptr %2)
  %5 = call ptr @ts_value_make_object(ptr %0)
  store ptr %5, ptr %__module_obj_0, align 8
  %6 = call ptr @ts_string_create(ptr @31)
  %7 = call ptr @ts_value_make_string(ptr %6)
  %__module_obj_01 = load ptr, ptr %__module_obj_0, align 8
  call void @ts_module_register(ptr %7, ptr %__module_obj_01)
  %8 = call ptr @ts_string_create(ptr @32)
  call void @ts_console_log(ptr %8)
  %__module_obj_02 = load ptr, ptr %__module_obj_0, align 8
  %9 = call ptr @__module_init_1841422227439969958_any(ptr null, ptr %__module_obj_02)
  store ptr %9, ptr %__module_res_0, align 8
  %10 = call ptr @ts_string_create(ptr @33)
  call void @ts_console_log(ptr %10)
  %__module_res_03 = load ptr, ptr %__module_res_0, align 8
  %11 = call ptr @ts_value_box_any(ptr %__module_res_03)
  ret ptr %11

return:                                           ; preds = %dead
  %12 = load ptr, ptr %returnValue, align 8
  ret ptr %12

dead:                                             ; No predecessors!
  br label %return
}

define void @__ts_main(ptr %context) #0 !type !16 {
entry:
  %shuffled = alloca ptr, align 8
  %merged = alloca ptr, align 8
  %chunked = alloca ptr, align 8
  %continueTarget = alloca ptr, align 8
  %breakTarget = alloca ptr, align 8
  %shouldContinue = alloca i1, align 1
  %shouldBreak = alloca i1, align 1
  %shouldReturn = alloca i1, align 1
  store i1 false, ptr %shouldReturn, align 1
  store i1 false, ptr %shouldBreak, align 1
  store i1 false, ptr %shouldContinue, align 1
  %_ = load ptr, ptr @_, align 8
  %0 = call ptr @ts_value_box_any(ptr %_)
  %1 = call ptr @ts_string_create(ptr @34)
  %2 = call ptr @ts_value_make_string(ptr %1)
  %3 = call ptr @ts_object_get_prop(ptr %0, ptr %2)
  %4 = call ptr @ts_array_create_specialized(i64 5, i64 8, i1 false)
  %5 = call ptr @ts_array_get_elements_ptr(ptr %4)
  %6 = getelementptr i64, ptr %5, i64 0
  store i64 1, ptr %6, align 8
  %7 = getelementptr i64, ptr %5, i64 1
  store i64 2, ptr %7, align 8
  %8 = getelementptr i64, ptr %5, i64 2
  store i64 3, ptr %8, align 8
  %9 = getelementptr i64, ptr %5, i64 3
  store i64 4, ptr %9, align 8
  %10 = getelementptr i64, ptr %5, i64 4
  store i64 5, ptr %10, align 8
  %11 = call ptr @ts_value_make_array(ptr %4)
  %12 = call ptr @ts_value_make_int(i64 2)
  %13 = call ptr @ts_call_2(ptr %3, ptr %11, ptr %12)
  store ptr %13, ptr %chunked, align 8
  %14 = call ptr @ts_string_create(ptr @35)
  call void @ts_console_log(ptr %14)
  %chunked1 = load ptr, ptr %chunked, align 8
  %15 = call ptr @ts_json_stringify(ptr %chunked1, ptr null, ptr null)
  call void @ts_console_log(ptr %15)
  %_2 = load ptr, ptr @_, align 8
  %16 = call ptr @ts_value_box_any(ptr %_2)
  %17 = call ptr @ts_string_create(ptr @36)
  %18 = call ptr @ts_value_make_string(ptr %17)
  %19 = call ptr @ts_object_get_prop(ptr %16, ptr %18)
  %20 = call ptr @ts_map_create()
  %21 = call ptr @ts_value_make_int(i64 1)
  %22 = call ptr @ts_string_create(ptr @37)
  %23 = call ptr @ts_value_make_string(ptr %22)
  call void @ts_map_set(ptr %20, ptr %23, ptr %21)
  %24 = call ptr @ts_map_create()
  %25 = call ptr @ts_value_make_int(i64 1)
  %26 = call ptr @ts_string_create(ptr @38)
  %27 = call ptr @ts_value_make_string(ptr %26)
  call void @ts_map_set(ptr %24, ptr %27, ptr %25)
  %28 = call ptr @ts_value_make_object(ptr %24)
  %29 = call ptr @ts_string_create(ptr @39)
  %30 = call ptr @ts_value_make_string(ptr %29)
  call void @ts_map_set(ptr %20, ptr %30, ptr %28)
  %31 = call ptr @ts_value_make_object(ptr %20)
  %32 = call ptr @ts_map_create()
  %33 = call ptr @ts_value_make_int(i64 2)
  %34 = call ptr @ts_string_create(ptr @40)
  %35 = call ptr @ts_value_make_string(ptr %34)
  call void @ts_map_set(ptr %32, ptr %35, ptr %33)
  %36 = call ptr @ts_map_create()
  %37 = call ptr @ts_value_make_int(i64 3)
  %38 = call ptr @ts_string_create(ptr @41)
  %39 = call ptr @ts_value_make_string(ptr %38)
  call void @ts_map_set(ptr %36, ptr %39, ptr %37)
  %40 = call ptr @ts_value_make_object(ptr %36)
  %41 = call ptr @ts_string_create(ptr @42)
  %42 = call ptr @ts_value_make_string(ptr %41)
  call void @ts_map_set(ptr %32, ptr %42, ptr %40)
  %43 = call ptr @ts_value_make_object(ptr %32)
  %44 = call ptr @ts_call_2(ptr %19, ptr %31, ptr %43)
  store ptr %44, ptr %merged, align 8
  %45 = call ptr @ts_string_create(ptr @43)
  call void @ts_console_log(ptr %45)
  %merged3 = load ptr, ptr %merged, align 8
  %46 = call ptr @ts_json_stringify(ptr %merged3, ptr null, ptr null)
  call void @ts_console_log(ptr %46)
  %_4 = load ptr, ptr @_, align 8
  %47 = call ptr @ts_value_box_any(ptr %_4)
  %48 = call ptr @ts_string_create(ptr @44)
  %49 = call ptr @ts_value_make_string(ptr %48)
  %50 = call ptr @ts_object_get_prop(ptr %47, ptr %49)
  %51 = call ptr @ts_array_create_specialized(i64 5, i64 8, i1 false)
  %52 = call ptr @ts_array_get_elements_ptr(ptr %51)
  %53 = getelementptr i64, ptr %52, i64 0
  store i64 10, ptr %53, align 8
  %54 = getelementptr i64, ptr %52, i64 1
  store i64 20, ptr %54, align 8
  %55 = getelementptr i64, ptr %52, i64 2
  store i64 30, ptr %55, align 8
  %56 = getelementptr i64, ptr %52, i64 3
  store i64 40, ptr %56, align 8
  %57 = getelementptr i64, ptr %52, i64 4
  store i64 50, ptr %57, align 8
  %58 = call ptr @ts_value_make_array(ptr %51)
  %59 = call ptr @ts_call_1(ptr %50, ptr %58)
  store ptr %59, ptr %shuffled, align 8
  %60 = call ptr @ts_string_create(ptr @45)
  call void @ts_console_log(ptr %60)
  %shuffled5 = load ptr, ptr %shuffled, align 8
  %61 = call ptr @ts_string_create(ptr @46)
  %62 = call ptr @ts_value_make_string(ptr %61)
  %63 = call ptr @ts_object_get_prop(ptr %shuffled5, ptr %62)
  call void @ts_console_log_value(ptr %63)
  br label %return

return:                                           ; preds = %entry
  ret void
}

declare ptr @ts_object_get_prop(ptr, ptr)

declare ptr @ts_require(ptr, ptr)

declare ptr @ts_map_create()

declare void @ts_map_set(ptr, ptr, ptr)

declare void @ts_module_register(ptr, ptr)

declare void @ts_console_log(ptr)

declare ptr @ts_array_create_specialized(i64, i64, i1)

declare ptr @ts_array_get_elements_ptr(ptr)

declare ptr @ts_value_make_array(ptr)

declare ptr @ts_call_2(ptr, ptr, ptr)

declare ptr @ts_json_stringify(ptr, ptr, ptr)

declare ptr @ts_call_1(ptr, ptr)

declare void @ts_console_log_value(ptr)

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
!8 = !{i64 0, !"URL"}
!9 = !{i64 0, !"Response"}
!10 = !{!"/FAILIFMISMATCH:\22_ITERATOR_DEBUG_LEVEL=0\22"}
!11 = !{!"/DEFAULTLIB:libcmt.lib"}
!12 = !{!"/NODEFAULTLIB:libcmtd.lib"}
!13 = !{!"/FAILIFMISMATCH:\22RuntimeLibrary=MT_StaticRelease\22"}
!14 = !{i64 0, !"__module_init_1841422227439969958_any"}
!15 = !{i64 0, !"user_main"}
!16 = !{i64 0, !"__ts_main"}
