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
%URL_VTable = type { ptr, ptr }
%Response_VTable = type { ptr, ptr, ptr, ptr }
%Uint8Array = type { ptr, ptr, i64 }
%Uint32Array = type { ptr, ptr, i64 }
%Float64Array = type { ptr, ptr, i64 }
%URL = type { ptr, ptr, ptr, ptr, ptr, ptr, ptr, ptr, ptr }
%Response = type { ptr, i1, i64, ptr }

@undefined = global i8 0
@JSON = global ptr null
@console = global ptr null
@null = global i8 0
@Symbol = global ptr null
@Math = global ptr null
@https = global ptr null
@process = global ptr null
@events = global ptr null
@stream = global ptr null
@http = global ptr null
@options = global ptr null
@Buffer = global ptr null
@buffer = global ptr null
@fs = global ptr null
@path = global ptr null
@net = global ptr null
@server = global ptr null
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
@6 = private unnamed_addr constant [5 x i8] c"hash\00", align 1
@7 = private unnamed_addr constant [5 x i8] c"host\00", align 1
@8 = private unnamed_addr constant [9 x i8] c"hostname\00", align 1
@9 = private unnamed_addr constant [5 x i8] c"href\00", align 1
@10 = private unnamed_addr constant [9 x i8] c"pathname\00", align 1
@11 = private unnamed_addr constant [5 x i8] c"port\00", align 1
@12 = private unnamed_addr constant [9 x i8] c"protocol\00", align 1
@13 = private unnamed_addr constant [7 x i8] c"search\00", align 1
@URL_VTable_Global = constant %URL_VTable { ptr null, ptr @URL_get_property }, !type !6
@14 = private unnamed_addr constant [3 x i8] c"ok\00", align 1
@15 = private unnamed_addr constant [7 x i8] c"status\00", align 1
@16 = private unnamed_addr constant [11 x i8] c"statusText\00", align 1
@Response_VTable_Global = constant %Response_VTable { ptr null, ptr @Response_get_property, ptr @Response_json, ptr @Response_text }, !type !7
@17 = private unnamed_addr constant [8 x i8] c"key.pem\00", align 1
@18 = private unnamed_addr constant [4 x i8] c"key\00", align 1
@19 = private unnamed_addr constant [9 x i8] c"cert.pem\00", align 1
@20 = private unnamed_addr constant [5 x i8] c"cert\00", align 1
@21 = private unnamed_addr constant [18 x i8] c"Request received:\00", align 1
@22 = private unnamed_addr constant [30 x i8] c"Null or undefined dereference\00", align 1
@23 = private unnamed_addr constant [7 x i8] c"method\00", align 1
@24 = private unnamed_addr constant [4 x i8] c"url\00", align 1
@25 = private unnamed_addr constant [11 x i8] c"text/plain\00", align 1
@26 = private unnamed_addr constant [15 x i8] c"'Content-Type'\00", align 1
@27 = private unnamed_addr constant [26 x i8] c"Hello from HTTPS server!\0A\00", align 1
@28 = private unnamed_addr constant [6 x i8] c"error\00", align 1
@29 = private unnamed_addr constant [30 x i8] c"Null or undefined dereference\00", align 1
@30 = private unnamed_addr constant [6 x i8] c"error\00", align 1
@31 = private unnamed_addr constant [14 x i8] c"Server error:\00", align 1
@32 = private unnamed_addr constant [36 x i8] c"HTTPS server listening on port 8443\00", align 1
@33 = private unnamed_addr constant [35 x i8] c"Sending request to HTTPS server...\00", align 1
@34 = private unnamed_addr constant [10 x i8] c"localhost\00", align 1
@35 = private unnamed_addr constant [9 x i8] c"hostname\00", align 1
@36 = private unnamed_addr constant [5 x i8] c"port\00", align 1
@37 = private unnamed_addr constant [2 x i8] c"/\00", align 1
@38 = private unnamed_addr constant [5 x i8] c"path\00", align 1
@39 = private unnamed_addr constant [4 x i8] c"GET\00", align 1
@40 = private unnamed_addr constant [7 x i8] c"method\00", align 1
@41 = private unnamed_addr constant [19 x i8] c"rejectUnauthorized\00", align 1
@42 = private unnamed_addr constant [17 x i8] c"Response status:\00", align 1
@43 = private unnamed_addr constant [30 x i8] c"Null or undefined dereference\00", align 1
@44 = private unnamed_addr constant [11 x i8] c"statusCode\00", align 1
@45 = private unnamed_addr constant [5 x i8] c"data\00", align 1
@46 = private unnamed_addr constant [15 x i8] c"Response data:\00", align 1
@47 = private unnamed_addr constant [6 x i8] c"error\00", align 1
@48 = private unnamed_addr constant [6 x i8] c"error\00", align 1
@49 = private unnamed_addr constant [15 x i8] c"Request error:\00", align 1

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
  ret ptr %5

next_buffer:                                      ; preds = %entry
  %6 = call ptr @ts_string_create(ptr @1)
  %7 = call i1 @ts_string_eq(ptr %1, ptr %6)
  br i1 %7, label %match_length, label %next_length

match_length:                                     ; preds = %next_buffer
  %8 = getelementptr inbounds %Uint8Array, ptr %0, i32 0, i32 2
  %9 = load i64, ptr %8, align 8
  %10 = call ptr @ts_value_make_int(i64 %9)
  ret ptr %10

next_length:                                      ; preds = %next_buffer
  %11 = call ptr @ts_value_make_undefined()
  ret ptr %11
}

declare ptr @ts_value_make_int(i64)

define ptr @Uint32Array_get_property(ptr %0, ptr %1) #0 {
entry:
  %2 = call ptr @ts_string_create(ptr @2)
  %3 = call i1 @ts_string_eq(ptr %1, ptr %2)
  br i1 %3, label %match_buffer, label %next_buffer

match_buffer:                                     ; preds = %entry
  %4 = getelementptr inbounds %Uint32Array, ptr %0, i32 0, i32 1
  %5 = load ptr, ptr %4, align 8
  ret ptr %5

next_buffer:                                      ; preds = %entry
  %6 = call ptr @ts_string_create(ptr @3)
  %7 = call i1 @ts_string_eq(ptr %1, ptr %6)
  br i1 %7, label %match_length, label %next_length

match_length:                                     ; preds = %next_buffer
  %8 = getelementptr inbounds %Uint32Array, ptr %0, i32 0, i32 2
  %9 = load i64, ptr %8, align 8
  %10 = call ptr @ts_value_make_int(i64 %9)
  ret ptr %10

next_length:                                      ; preds = %next_buffer
  %11 = call ptr @ts_value_make_undefined()
  ret ptr %11
}

define ptr @Float64Array_get_property(ptr %0, ptr %1) #0 {
entry:
  %2 = call ptr @ts_string_create(ptr @4)
  %3 = call i1 @ts_string_eq(ptr %1, ptr %2)
  br i1 %3, label %match_buffer, label %next_buffer

match_buffer:                                     ; preds = %entry
  %4 = getelementptr inbounds %Float64Array, ptr %0, i32 0, i32 1
  %5 = load ptr, ptr %4, align 8
  ret ptr %5

next_buffer:                                      ; preds = %entry
  %6 = call ptr @ts_string_create(ptr @5)
  %7 = call i1 @ts_string_eq(ptr %1, ptr %6)
  br i1 %7, label %match_length, label %next_length

match_length:                                     ; preds = %next_buffer
  %8 = getelementptr inbounds %Float64Array, ptr %0, i32 0, i32 2
  %9 = load i64, ptr %8, align 8
  %10 = call ptr @ts_value_make_int(i64 %9)
  ret ptr %10

next_length:                                      ; preds = %next_buffer
  %11 = call ptr @ts_value_make_undefined()
  ret ptr %11
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

define ptr @URL_get_property(ptr %0, ptr %1) #0 {
entry:
  %2 = call ptr @ts_string_create(ptr @6)
  %3 = call i1 @ts_string_eq(ptr %1, ptr %2)
  br i1 %3, label %match_hash, label %next_hash

match_hash:                                       ; preds = %entry
  %4 = getelementptr inbounds %URL, ptr %0, i32 0, i32 1
  %5 = load ptr, ptr %4, align 8
  %6 = call ptr @ts_value_make_string(ptr %5)
  ret ptr %6

next_hash:                                        ; preds = %entry
  %7 = call ptr @ts_string_create(ptr @7)
  %8 = call i1 @ts_string_eq(ptr %1, ptr %7)
  br i1 %8, label %match_host, label %next_host

match_host:                                       ; preds = %next_hash
  %9 = getelementptr inbounds %URL, ptr %0, i32 0, i32 2
  %10 = load ptr, ptr %9, align 8
  %11 = call ptr @ts_value_make_string(ptr %10)
  ret ptr %11

next_host:                                        ; preds = %next_hash
  %12 = call ptr @ts_string_create(ptr @8)
  %13 = call i1 @ts_string_eq(ptr %1, ptr %12)
  br i1 %13, label %match_hostname, label %next_hostname

match_hostname:                                   ; preds = %next_host
  %14 = getelementptr inbounds %URL, ptr %0, i32 0, i32 3
  %15 = load ptr, ptr %14, align 8
  %16 = call ptr @ts_value_make_string(ptr %15)
  ret ptr %16

next_hostname:                                    ; preds = %next_host
  %17 = call ptr @ts_string_create(ptr @9)
  %18 = call i1 @ts_string_eq(ptr %1, ptr %17)
  br i1 %18, label %match_href, label %next_href

match_href:                                       ; preds = %next_hostname
  %19 = getelementptr inbounds %URL, ptr %0, i32 0, i32 4
  %20 = load ptr, ptr %19, align 8
  %21 = call ptr @ts_value_make_string(ptr %20)
  ret ptr %21

next_href:                                        ; preds = %next_hostname
  %22 = call ptr @ts_string_create(ptr @10)
  %23 = call i1 @ts_string_eq(ptr %1, ptr %22)
  br i1 %23, label %match_pathname, label %next_pathname

match_pathname:                                   ; preds = %next_href
  %24 = getelementptr inbounds %URL, ptr %0, i32 0, i32 5
  %25 = load ptr, ptr %24, align 8
  %26 = call ptr @ts_value_make_string(ptr %25)
  ret ptr %26

next_pathname:                                    ; preds = %next_href
  %27 = call ptr @ts_string_create(ptr @11)
  %28 = call i1 @ts_string_eq(ptr %1, ptr %27)
  br i1 %28, label %match_port, label %next_port

match_port:                                       ; preds = %next_pathname
  %29 = getelementptr inbounds %URL, ptr %0, i32 0, i32 6
  %30 = load ptr, ptr %29, align 8
  %31 = call ptr @ts_value_make_string(ptr %30)
  ret ptr %31

next_port:                                        ; preds = %next_pathname
  %32 = call ptr @ts_string_create(ptr @12)
  %33 = call i1 @ts_string_eq(ptr %1, ptr %32)
  br i1 %33, label %match_protocol, label %next_protocol

match_protocol:                                   ; preds = %next_port
  %34 = getelementptr inbounds %URL, ptr %0, i32 0, i32 7
  %35 = load ptr, ptr %34, align 8
  %36 = call ptr @ts_value_make_string(ptr %35)
  ret ptr %36

next_protocol:                                    ; preds = %next_port
  %37 = call ptr @ts_string_create(ptr @13)
  %38 = call i1 @ts_string_eq(ptr %1, ptr %37)
  br i1 %38, label %match_search, label %next_search

match_search:                                     ; preds = %next_protocol
  %39 = getelementptr inbounds %URL, ptr %0, i32 0, i32 8
  %40 = load ptr, ptr %39, align 8
  %41 = call ptr @ts_value_make_string(ptr %40)
  ret ptr %41

next_search:                                      ; preds = %next_protocol
  %42 = call ptr @ts_value_make_undefined()
  ret ptr %42
}

declare ptr @ts_value_make_string(ptr)

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

define ptr @__module_init_5266667812657221616(ptr %context) #0 !type !8 {
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
  %0 = call ptr @ts_map_create()
  %1 = call ptr @ts_string_create(ptr @17)
  %2 = call ptr @ts_fs_readFileSync(ptr %1)
  %3 = call ptr @ts_value_make_object(ptr %2)
  %4 = call ptr @ts_string_create(ptr @18)
  %5 = call ptr @ts_value_make_string(ptr %4)
  call void @ts_map_set(ptr %0, ptr %5, ptr %3)
  %6 = call ptr @ts_string_create(ptr @19)
  %7 = call ptr @ts_fs_readFileSync(ptr %6)
  %8 = call ptr @ts_value_make_object(ptr %7)
  %9 = call ptr @ts_string_create(ptr @20)
  %10 = call ptr @ts_value_make_string(ptr %9)
  call void @ts_map_set(ptr %0, ptr %10, ptr %8)
  %11 = call ptr @ts_value_make_object(ptr %0)
  store ptr %11, ptr @options, align 8
  %12 = call ptr @ts_value_make_undefined()
  %13 = call ptr @ts_value_make_undefined()
  %options = load ptr, ptr @options, align 8
  %14 = call ptr @ts_value_make_function(ptr @lambda_0, ptr %context)
  %15 = call ptr @ts_https_create_server(ptr %options, ptr %14)
  store ptr %15, ptr @server, align 8
  %server = load ptr, ptr @server, align 8
  %16 = call ptr @ts_string_create(ptr @28)
  %17 = call ptr @ts_value_make_function(ptr @lambda_1, ptr %context)
  call void @ts_event_emitter_on(ptr %server, ptr %16, ptr %17)
  %server1 = load ptr, ptr @server, align 8
  %18 = call ptr @ts_value_make_int(i64 8443)
  %19 = call ptr @ts_value_make_function(ptr @lambda_2, ptr %context)
  call void @ts_http_server_listen(ptr %server1, ptr %18, ptr %19)
  ret ptr %server1

return:                                           ; preds = %dead
  %20 = load ptr, ptr %returnValue, align 8
  ret ptr %20

dead:                                             ; No predecessors!
  br label %return
}

define ptr @user_main(ptr %context) #0 !type !9 {
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
  %0 = call ptr @__module_init_5266667812657221616(ptr null)
  ret ptr %0

return:                                           ; preds = %dead
  %1 = load ptr, ptr %returnValue, align 8
  ret ptr %1

dead:                                             ; No predecessors!
  br label %return
}

declare ptr @ts_map_create()

declare void @ts_map_set(ptr, ptr, ptr)

declare ptr @ts_fs_readFileSync(ptr)

declare ptr @ts_value_make_object(ptr)

define internal ptr @lambda_0(ptr %context, ptr %req, ptr %res) #0 !type !10 {
entry:
  %res2 = alloca ptr, align 8
  %req1 = alloca ptr, align 8
  store ptr %req, ptr %req1, align 8
  store ptr %res, ptr %res2, align 8
  %0 = call ptr @ts_string_create(ptr @21)
  call void @ts_console_log(ptr %0)
  %req3 = load ptr, ptr %req1, align 8
  %1 = icmp eq ptr %req3, null
  br i1 %1, label %null_fail, label %null_cont

null_fail:                                        ; preds = %entry
  call void @ts_panic(ptr @22)
  unreachable

null_cont:                                        ; preds = %entry
  %2 = call ptr @ts_string_create(ptr @23)
  %3 = call ptr @ts_value_get_property(ptr %req3, ptr %2)
  call void @ts_console_log_value(ptr %3)
  %req4 = load ptr, ptr %req1, align 8
  %4 = call ptr @ts_string_create(ptr @24)
  %5 = call ptr @ts_value_get_property(ptr %req4, ptr %4)
  call void @ts_console_log_value(ptr %5)
  %res5 = load ptr, ptr %res2, align 8
  %6 = call ptr @ts_value_get_object(ptr %res5)
  %7 = call ptr @ts_map_create()
  %8 = call ptr @ts_string_create(ptr @25)
  %9 = call ptr @ts_value_make_string(ptr %8)
  %10 = call ptr @ts_string_create(ptr @26)
  %11 = call ptr @ts_value_make_string(ptr %10)
  call void @ts_map_set(ptr %7, ptr %11, ptr %9)
  %12 = call ptr @ts_value_make_object(ptr %7)
  call void @ts_http_server_response_write_head(ptr %6, i64 200, ptr %12)
  %res6 = load ptr, ptr %res2, align 8
  %13 = call ptr @ts_value_get_object(ptr %res6)
  %14 = call ptr @ts_string_create(ptr @27)
  %15 = call ptr @ts_value_make_string(ptr %14)
  call void @ts_writable_end(ptr %13, ptr %15)
  ret ptr null
}

declare void @ts_console_log(ptr)

declare void @ts_panic(ptr)

declare ptr @ts_value_get_property(ptr, ptr)

declare void @ts_console_log_value(ptr)

declare ptr @ts_value_get_object(ptr)

declare void @ts_http_server_response_write_head(ptr, i64, ptr)

declare void @ts_writable_end(ptr, ptr)

declare ptr @ts_value_make_function(ptr, ptr)

declare ptr @ts_https_create_server(ptr, ptr)

define internal ptr @lambda_1(ptr %context, ptr %err) #0 !type !10 {
entry:
  %err1 = alloca ptr, align 8
  store ptr %err, ptr %err1, align 8
  %console = load ptr, ptr @console, align 8
  %0 = icmp eq ptr %console, null
  br i1 %0, label %null_fail, label %null_cont

null_fail:                                        ; preds = %entry
  call void @ts_panic(ptr @29)
  unreachable

null_cont:                                        ; preds = %entry
  %1 = call ptr @ts_value_make_object(ptr %console)
  %2 = call ptr @ts_string_create(ptr @30)
  %3 = call ptr @ts_value_get_property(ptr %1, ptr %2)
  %4 = call ptr @ts_string_create(ptr @31)
  %5 = call ptr @ts_value_make_string(ptr %4)
  %err2 = load ptr, ptr %err1, align 8
  %6 = call ptr @ts_call_2(ptr %3, ptr %5, ptr %err2)
  ret ptr null
}

declare ptr @ts_call_2(ptr, ptr, ptr)

declare void @ts_event_emitter_on(ptr, ptr, ptr)

define internal ptr @lambda_2(ptr %context) #0 !type !10 {
entry:
  %0 = call ptr @ts_string_create(ptr @32)
  call void @ts_console_log(ptr %0)
  %1 = call ptr @ts_value_make_function(ptr @lambda_3, ptr %context)
  %2 = call ptr @ts_set_timeout(ptr %1, i64 1000)
  %3 = call i64 @ts_value_get_int(ptr %2)
  ret ptr null
}

define internal ptr @lambda_3(ptr %context) #0 !type !10 {
entry:
  %req = alloca ptr, align 8
  %0 = call ptr @ts_string_create(ptr @33)
  call void @ts_console_log(ptr %0)
  %1 = call ptr @ts_map_create()
  %2 = call ptr @ts_string_create(ptr @34)
  %3 = call ptr @ts_value_make_string(ptr %2)
  %4 = call ptr @ts_string_create(ptr @35)
  %5 = call ptr @ts_value_make_string(ptr %4)
  call void @ts_map_set(ptr %1, ptr %5, ptr %3)
  %6 = call ptr @ts_value_make_int(i64 8443)
  %7 = call ptr @ts_string_create(ptr @36)
  %8 = call ptr @ts_value_make_string(ptr %7)
  call void @ts_map_set(ptr %1, ptr %8, ptr %6)
  %9 = call ptr @ts_string_create(ptr @37)
  %10 = call ptr @ts_value_make_string(ptr %9)
  %11 = call ptr @ts_string_create(ptr @38)
  %12 = call ptr @ts_value_make_string(ptr %11)
  call void @ts_map_set(ptr %1, ptr %12, ptr %10)
  %13 = call ptr @ts_string_create(ptr @39)
  %14 = call ptr @ts_value_make_string(ptr %13)
  %15 = call ptr @ts_string_create(ptr @40)
  %16 = call ptr @ts_value_make_string(ptr %15)
  call void @ts_map_set(ptr %1, ptr %16, ptr %14)
  %17 = call ptr @ts_value_make_bool(i1 false)
  %18 = call ptr @ts_string_create(ptr @41)
  %19 = call ptr @ts_value_make_string(ptr %18)
  call void @ts_map_set(ptr %1, ptr %19, ptr %17)
  %20 = call ptr @ts_value_make_object(ptr %1)
  %21 = call ptr @ts_value_make_function(ptr @lambda_4, ptr %context)
  %22 = call ptr @ts_https_request(ptr %20, ptr %21)
  store ptr %22, ptr %req, align 8
  %req1 = load ptr, ptr %req, align 8
  %23 = call ptr @ts_string_create(ptr @47)
  %24 = call ptr @ts_value_make_function(ptr @lambda_6, ptr %context)
  call void @ts_event_emitter_on(ptr %req1, ptr %23, ptr %24)
  %req2 = load ptr, ptr %req, align 8
  call void @ts_writable_end(ptr %req2, ptr null)
  ret ptr null
}

define internal ptr @lambda_4(ptr %context, ptr %res) #0 !type !10 {
entry:
  %res1 = alloca ptr, align 8
  store ptr %res, ptr %res1, align 8
  %0 = call ptr @ts_string_create(ptr @42)
  call void @ts_console_log(ptr %0)
  %res2 = load ptr, ptr %res1, align 8
  %1 = icmp eq ptr %res2, null
  br i1 %1, label %null_fail, label %null_cont

null_fail:                                        ; preds = %entry
  call void @ts_panic(ptr @43)
  unreachable

null_cont:                                        ; preds = %entry
  %2 = call ptr @ts_string_create(ptr @44)
  %3 = call ptr @ts_value_get_property(ptr %res2, ptr %2)
  call void @ts_console_log_value(ptr %3)
  %res3 = load ptr, ptr %res1, align 8
  %4 = call ptr @ts_string_create(ptr @45)
  %5 = call ptr @ts_value_make_function(ptr @lambda_5, ptr %context)
  call void @ts_event_emitter_on(ptr %res3, ptr %4, ptr %5)
  ret ptr null
}

define internal ptr @lambda_5(ptr %context, ptr %d) #0 !type !10 {
entry:
  %d1 = alloca ptr, align 8
  store ptr %d, ptr %d1, align 8
  %0 = call ptr @ts_string_create(ptr @46)
  call void @ts_console_log(ptr %0)
  %d2 = load ptr, ptr %d1, align 8
  %1 = call ptr @ts_buffer_to_string(ptr %d2)
  call void @ts_console_log_value(ptr %1)
  call void @ts_process_exit(i64 0)
  ret ptr null
}

declare ptr @ts_buffer_to_string(ptr)

declare void @ts_process_exit(i64)

declare ptr @ts_https_request(ptr, ptr)

define internal ptr @lambda_6(ptr %context, ptr %e) #0 !type !10 {
entry:
  %e1 = alloca ptr, align 8
  store ptr %e, ptr %e1, align 8
  %console = load ptr, ptr @console, align 8
  %0 = call ptr @ts_value_make_object(ptr %console)
  %1 = call ptr @ts_string_create(ptr @48)
  %2 = call ptr @ts_value_get_property(ptr %0, ptr %1)
  %3 = call ptr @ts_string_create(ptr @49)
  %4 = call ptr @ts_value_make_string(ptr %3)
  %e2 = load ptr, ptr %e1, align 8
  %5 = call ptr @ts_call_2(ptr %2, ptr %4, ptr %e2)
  call void @ts_process_exit(i64 1)
  ret ptr null
}

declare ptr @ts_set_timeout(ptr, i64)

declare i64 @ts_value_get_int(ptr)

declare void @ts_http_server_listen(ptr, ptr, ptr)

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
!4 = !{i64 0, !"Generator"}
!5 = !{i64 0, !"AsyncGenerator"}
!6 = !{i64 0, !"URL"}
!7 = !{i64 0, !"Response"}
!8 = !{i64 0, !"__module_init_5266667812657221616"}
!9 = !{i64 0, !"user_main"}
!10 = !{i64 0, !"TsFunction"}
