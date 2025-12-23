; ModuleID = 'ts_module'
source_filename = "ts_module"
target datalayout = "e-m:w-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-windows-msvc"

%DataView_VTable = type { ptr, ptr, ptr, ptr }
%Uint8Array_VTable = type { ptr, ptr }
%Uint32Array_VTable = type { ptr, ptr }
%Float64Array_VTable = type { ptr, ptr }
%Buffer_VTable = type { ptr, ptr, ptr }
%Response_VTable = type { ptr, ptr, ptr, ptr }
%URL_VTable = type { ptr, ptr }
%IncomingMessage_VTable = type { ptr, ptr }
%ServerResponse_VTable = type { ptr, ptr, ptr, ptr, ptr }
%Server_VTable = type { ptr, ptr, ptr }
%Uint8Array = type { ptr, ptr, i64 }
%Uint32Array = type { ptr, ptr, i64 }
%Float64Array = type { ptr, ptr, i64 }
%Buffer = type { ptr, i64 }
%Response = type { ptr, i1, i64, ptr }
%URL = type { ptr, ptr, ptr, ptr, ptr, ptr, ptr, ptr, ptr }
%IncomingMessage = type { ptr, ptr, ptr, ptr }
%TsString = type { i32, i32 }
%TsBuffer = type { ptr, i32, ptr, i64 }

@undefined = local_unnamed_addr global i8 0
@JSON = local_unnamed_addr global ptr null
@null = local_unnamed_addr global i8 0
@console = local_unnamed_addr global ptr null
@path = local_unnamed_addr global ptr null
@fs = local_unnamed_addr global ptr null
@process = local_unnamed_addr global ptr null
@Math = local_unnamed_addr global ptr null
@Buffer = local_unnamed_addr global ptr null
@http = local_unnamed_addr global ptr null
@DataView_VTable_Global = local_unnamed_addr constant %DataView_VTable { ptr null, ptr @DataView_get_property, ptr @DataView_getUint32, ptr @DataView_setUint32 }, !type !0
@Uint8Array_VTable_Global = local_unnamed_addr constant %Uint8Array_VTable { ptr null, ptr @Uint8Array_get_property }, !type !1
@Uint32Array_VTable_Global = local_unnamed_addr constant %Uint32Array_VTable { ptr null, ptr @Uint32Array_get_property }, !type !2
@0 = private unnamed_addr constant [7 x i8] c"buffer\00", align 1
@Float64Array_VTable_Global = local_unnamed_addr constant %Float64Array_VTable { ptr null, ptr @Float64Array_get_property }, !type !3
@1 = private unnamed_addr constant [7 x i8] c"length\00", align 1
@Buffer_VTable_Global = local_unnamed_addr constant %Buffer_VTable { ptr null, ptr @Buffer_get_property, ptr @Buffer_toString }, !type !4
@2 = private unnamed_addr constant [3 x i8] c"ok\00", align 1
@3 = private unnamed_addr constant [7 x i8] c"status\00", align 1
@4 = private unnamed_addr constant [11 x i8] c"statusText\00", align 1
@Response_VTable_Global = local_unnamed_addr constant %Response_VTable { ptr null, ptr @Response_get_property, ptr @Response_json, ptr @Response_text }, !type !5
@5 = private unnamed_addr constant [5 x i8] c"hash\00", align 1
@6 = private unnamed_addr constant [5 x i8] c"host\00", align 1
@7 = private unnamed_addr constant [9 x i8] c"hostname\00", align 1
@8 = private unnamed_addr constant [5 x i8] c"href\00", align 1
@9 = private unnamed_addr constant [9 x i8] c"pathname\00", align 1
@10 = private unnamed_addr constant [5 x i8] c"port\00", align 1
@11 = private unnamed_addr constant [9 x i8] c"protocol\00", align 1
@12 = private unnamed_addr constant [7 x i8] c"search\00", align 1
@URL_VTable_Global = local_unnamed_addr constant %URL_VTable { ptr null, ptr @URL_get_property }, !type !6
@13 = private unnamed_addr constant [8 x i8] c"headers\00", align 1
@14 = private unnamed_addr constant [7 x i8] c"method\00", align 1
@15 = private unnamed_addr constant [4 x i8] c"url\00", align 1
@IncomingMessage_VTable_Global = local_unnamed_addr constant %IncomingMessage_VTable { ptr null, ptr @IncomingMessage_get_property }, !type !7
@ServerResponse_VTable_Global = local_unnamed_addr constant %ServerResponse_VTable { ptr null, ptr @ServerResponse_get_property, ptr @ServerResponse_end, ptr @ServerResponse_write, ptr @ServerResponse_writeHead }, !type !8
@Server_VTable_Global = local_unnamed_addr constant %Server_VTable { ptr null, ptr @Server_get_property, ptr @Server_listen }, !type !9
@16 = private unnamed_addr constant [30 x i8] c"Null or undefined dereference\00", align 1
@17 = private unnamed_addr constant [2 x i8] c"0\00", align 1
@18 = private unnamed_addr constant [44 x i8] c"The quick brown fox jumps over the lazy dog\00", align 1
@19 = private unnamed_addr constant [29 x i8] c"Starting SHA-256 Benchmark (\00", align 1
@20 = private unnamed_addr constant [16 x i8] c" iterations)...\00", align 1
@21 = private unnamed_addr constant [1 x i8] zeroinitializer, align 1
@22 = private unnamed_addr constant [20 x i8] c"Benchmark Complete.\00", align 1
@23 = private unnamed_addr constant [12 x i8] c"Last Hash: \00", align 1
@24 = private unnamed_addr constant [13 x i8] c"Total Time: \00", align 1
@25 = private unnamed_addr constant [15 x i8] c"Average Time: \00", align 1
@26 = private unnamed_addr constant [3 x i8] c"ms\00", align 1
@27 = private unnamed_addr constant [20 x i8] c"Index out of bounds\00", align 1

declare ptr @ts_typed_array_create_u8(i64) local_unnamed_addr

declare ptr @ts_typed_array_create_u32(i64) local_unnamed_addr

declare ptr @ts_typed_array_from_array_u32(ptr) local_unnamed_addr

declare ptr @ts_data_view_create(ptr) local_unnamed_addr

declare i64 @DataView_getUint32(ptr, ptr, i64, i1)

declare void @DataView_setUint32(ptr, ptr, i64, i64, i1)

define ptr @DataView_get_property(ptr nocapture readnone %0, ptr nocapture readnone %1) #0 {
entry:
  %2 = tail call ptr @ts_value_make_undefined()
  ret ptr %2
}

declare ptr @ts_string_create(ptr) local_unnamed_addr

declare i1 @ts_string_eq(ptr, ptr) local_unnamed_addr

declare ptr @ts_value_make_undefined() local_unnamed_addr

define ptr @Uint8Array_get_property(ptr nocapture readonly %0, ptr %1) #0 {
entry:
  %2 = tail call ptr @ts_string_create(ptr nonnull @0)
  %3 = tail call i1 @ts_string_eq(ptr %1, ptr %2)
  br i1 %3, label %match_buffer, label %next_buffer

common.ret:                                       ; preds = %next_length, %match_length, %match_buffer
  %common.ret.op = phi ptr [ %6, %match_buffer ], [ %11, %match_length ], [ %12, %next_length ]
  ret ptr %common.ret.op

match_buffer:                                     ; preds = %entry
  %4 = getelementptr inbounds %Uint8Array, ptr %0, i64 0, i32 1
  %5 = load ptr, ptr %4, align 8
  %6 = tail call ptr @ts_value_make_object(ptr %5)
  br label %common.ret

next_buffer:                                      ; preds = %entry
  %7 = tail call ptr @ts_string_create(ptr nonnull @1)
  %8 = tail call i1 @ts_string_eq(ptr %1, ptr %7)
  br i1 %8, label %match_length, label %next_length

match_length:                                     ; preds = %next_buffer
  %9 = getelementptr inbounds %Uint8Array, ptr %0, i64 0, i32 2
  %10 = load i64, ptr %9, align 8
  %11 = tail call ptr @ts_value_make_int(i64 %10)
  br label %common.ret

next_length:                                      ; preds = %next_buffer
  %12 = tail call ptr @ts_value_make_undefined()
  br label %common.ret
}

declare ptr @ts_value_make_object(ptr) local_unnamed_addr

declare ptr @ts_value_make_int(i64) local_unnamed_addr

define ptr @Uint32Array_get_property(ptr nocapture readonly %0, ptr %1) #0 {
entry:
  %2 = tail call ptr @ts_string_create(ptr nonnull @0)
  %3 = tail call i1 @ts_string_eq(ptr %1, ptr %2)
  br i1 %3, label %match_buffer, label %next_buffer

common.ret:                                       ; preds = %next_length, %match_length, %match_buffer
  %common.ret.op = phi ptr [ %6, %match_buffer ], [ %11, %match_length ], [ %12, %next_length ]
  ret ptr %common.ret.op

match_buffer:                                     ; preds = %entry
  %4 = getelementptr inbounds %Uint32Array, ptr %0, i64 0, i32 1
  %5 = load ptr, ptr %4, align 8
  %6 = tail call ptr @ts_value_make_object(ptr %5)
  br label %common.ret

next_buffer:                                      ; preds = %entry
  %7 = tail call ptr @ts_string_create(ptr nonnull @1)
  %8 = tail call i1 @ts_string_eq(ptr %1, ptr %7)
  br i1 %8, label %match_length, label %next_length

match_length:                                     ; preds = %next_buffer
  %9 = getelementptr inbounds %Uint32Array, ptr %0, i64 0, i32 2
  %10 = load i64, ptr %9, align 8
  %11 = tail call ptr @ts_value_make_int(i64 %10)
  br label %common.ret

next_length:                                      ; preds = %next_buffer
  %12 = tail call ptr @ts_value_make_undefined()
  br label %common.ret
}

define ptr @Float64Array_get_property(ptr nocapture readonly %0, ptr %1) #0 {
entry:
  %2 = tail call ptr @ts_string_create(ptr nonnull @0)
  %3 = tail call i1 @ts_string_eq(ptr %1, ptr %2)
  br i1 %3, label %match_buffer, label %next_buffer

common.ret:                                       ; preds = %next_length, %match_length, %match_buffer
  %common.ret.op = phi ptr [ %6, %match_buffer ], [ %11, %match_length ], [ %12, %next_length ]
  ret ptr %common.ret.op

match_buffer:                                     ; preds = %entry
  %4 = getelementptr inbounds %Float64Array, ptr %0, i64 0, i32 1
  %5 = load ptr, ptr %4, align 8
  %6 = tail call ptr @ts_value_make_object(ptr %5)
  br label %common.ret

next_buffer:                                      ; preds = %entry
  %7 = tail call ptr @ts_string_create(ptr nonnull @1)
  %8 = tail call i1 @ts_string_eq(ptr %1, ptr %7)
  br i1 %8, label %match_length, label %next_length

match_length:                                     ; preds = %next_buffer
  %9 = getelementptr inbounds %Float64Array, ptr %0, i64 0, i32 2
  %10 = load i64, ptr %9, align 8
  %11 = tail call ptr @ts_value_make_int(i64 %10)
  br label %common.ret

next_length:                                      ; preds = %next_buffer
  %12 = tail call ptr @ts_value_make_undefined()
  br label %common.ret
}

define ptr @Buffer_get_property(ptr nocapture readonly %0, ptr %1) #0 {
entry:
  %2 = tail call ptr @ts_string_create(ptr nonnull @1)
  %3 = tail call i1 @ts_string_eq(ptr %1, ptr %2)
  br i1 %3, label %match_length, label %next_length

common.ret:                                       ; preds = %next_length, %match_length
  %common.ret.op = phi ptr [ %6, %match_length ], [ %7, %next_length ]
  ret ptr %common.ret.op

match_length:                                     ; preds = %entry
  %4 = getelementptr inbounds %Buffer, ptr %0, i64 0, i32 1
  %5 = load i64, ptr %4, align 8
  %6 = tail call ptr @ts_value_make_int(i64 %5)
  br label %common.ret

next_length:                                      ; preds = %entry
  %7 = tail call ptr @ts_value_make_undefined()
  br label %common.ret
}

declare ptr @Buffer_toString(ptr, ptr) #0

define ptr @Response_get_property(ptr nocapture readonly %0, ptr %1) #0 {
entry:
  %2 = tail call ptr @ts_string_create(ptr nonnull @2)
  %3 = tail call i1 @ts_string_eq(ptr %1, ptr %2)
  br i1 %3, label %match_ok, label %next_ok

common.ret:                                       ; preds = %next_statusText, %match_statusText, %match_status, %match_ok
  %common.ret.op = phi ptr [ %6, %match_ok ], [ %11, %match_status ], [ %16, %match_statusText ], [ %17, %next_statusText ]
  ret ptr %common.ret.op

match_ok:                                         ; preds = %entry
  %4 = getelementptr inbounds %Response, ptr %0, i64 0, i32 1
  %5 = load i1, ptr %4, align 1
  %6 = tail call ptr @ts_value_make_bool(i1 %5)
  br label %common.ret

next_ok:                                          ; preds = %entry
  %7 = tail call ptr @ts_string_create(ptr nonnull @3)
  %8 = tail call i1 @ts_string_eq(ptr %1, ptr %7)
  br i1 %8, label %match_status, label %next_status

match_status:                                     ; preds = %next_ok
  %9 = getelementptr inbounds %Response, ptr %0, i64 0, i32 2
  %10 = load i64, ptr %9, align 8
  %11 = tail call ptr @ts_value_make_int(i64 %10)
  br label %common.ret

next_status:                                      ; preds = %next_ok
  %12 = tail call ptr @ts_string_create(ptr nonnull @4)
  %13 = tail call i1 @ts_string_eq(ptr %1, ptr %12)
  br i1 %13, label %match_statusText, label %next_statusText

match_statusText:                                 ; preds = %next_status
  %14 = getelementptr inbounds %Response, ptr %0, i64 0, i32 3
  %15 = load ptr, ptr %14, align 8
  %16 = tail call ptr @ts_value_make_string(ptr %15)
  br label %common.ret

next_statusText:                                  ; preds = %next_status
  %17 = tail call ptr @ts_value_make_undefined()
  br label %common.ret
}

declare ptr @ts_value_make_bool(i1) local_unnamed_addr

declare ptr @ts_value_make_string(ptr) local_unnamed_addr

declare ptr @Response_json(ptr, ptr) #0

declare ptr @Response_text(ptr, ptr) #0

define ptr @URL_get_property(ptr nocapture readonly %0, ptr %1) #0 {
entry:
  %2 = tail call ptr @ts_string_create(ptr nonnull @5)
  %3 = tail call i1 @ts_string_eq(ptr %1, ptr %2)
  br i1 %3, label %match_hash, label %next_hash

common.ret:                                       ; preds = %next_search, %match_search, %match_protocol, %match_port, %match_pathname, %match_href, %match_hostname, %match_host, %match_hash
  %common.ret.op = phi ptr [ %6, %match_hash ], [ %11, %match_host ], [ %16, %match_hostname ], [ %21, %match_href ], [ %26, %match_pathname ], [ %31, %match_port ], [ %36, %match_protocol ], [ %41, %match_search ], [ %42, %next_search ]
  ret ptr %common.ret.op

match_hash:                                       ; preds = %entry
  %4 = getelementptr inbounds %URL, ptr %0, i64 0, i32 1
  %5 = load ptr, ptr %4, align 8
  %6 = tail call ptr @ts_value_make_string(ptr %5)
  br label %common.ret

next_hash:                                        ; preds = %entry
  %7 = tail call ptr @ts_string_create(ptr nonnull @6)
  %8 = tail call i1 @ts_string_eq(ptr %1, ptr %7)
  br i1 %8, label %match_host, label %next_host

match_host:                                       ; preds = %next_hash
  %9 = getelementptr inbounds %URL, ptr %0, i64 0, i32 2
  %10 = load ptr, ptr %9, align 8
  %11 = tail call ptr @ts_value_make_string(ptr %10)
  br label %common.ret

next_host:                                        ; preds = %next_hash
  %12 = tail call ptr @ts_string_create(ptr nonnull @7)
  %13 = tail call i1 @ts_string_eq(ptr %1, ptr %12)
  br i1 %13, label %match_hostname, label %next_hostname

match_hostname:                                   ; preds = %next_host
  %14 = getelementptr inbounds %URL, ptr %0, i64 0, i32 3
  %15 = load ptr, ptr %14, align 8
  %16 = tail call ptr @ts_value_make_string(ptr %15)
  br label %common.ret

next_hostname:                                    ; preds = %next_host
  %17 = tail call ptr @ts_string_create(ptr nonnull @8)
  %18 = tail call i1 @ts_string_eq(ptr %1, ptr %17)
  br i1 %18, label %match_href, label %next_href

match_href:                                       ; preds = %next_hostname
  %19 = getelementptr inbounds %URL, ptr %0, i64 0, i32 4
  %20 = load ptr, ptr %19, align 8
  %21 = tail call ptr @ts_value_make_string(ptr %20)
  br label %common.ret

next_href:                                        ; preds = %next_hostname
  %22 = tail call ptr @ts_string_create(ptr nonnull @9)
  %23 = tail call i1 @ts_string_eq(ptr %1, ptr %22)
  br i1 %23, label %match_pathname, label %next_pathname

match_pathname:                                   ; preds = %next_href
  %24 = getelementptr inbounds %URL, ptr %0, i64 0, i32 5
  %25 = load ptr, ptr %24, align 8
  %26 = tail call ptr @ts_value_make_string(ptr %25)
  br label %common.ret

next_pathname:                                    ; preds = %next_href
  %27 = tail call ptr @ts_string_create(ptr nonnull @10)
  %28 = tail call i1 @ts_string_eq(ptr %1, ptr %27)
  br i1 %28, label %match_port, label %next_port

match_port:                                       ; preds = %next_pathname
  %29 = getelementptr inbounds %URL, ptr %0, i64 0, i32 6
  %30 = load ptr, ptr %29, align 8
  %31 = tail call ptr @ts_value_make_string(ptr %30)
  br label %common.ret

next_port:                                        ; preds = %next_pathname
  %32 = tail call ptr @ts_string_create(ptr nonnull @11)
  %33 = tail call i1 @ts_string_eq(ptr %1, ptr %32)
  br i1 %33, label %match_protocol, label %next_protocol

match_protocol:                                   ; preds = %next_port
  %34 = getelementptr inbounds %URL, ptr %0, i64 0, i32 7
  %35 = load ptr, ptr %34, align 8
  %36 = tail call ptr @ts_value_make_string(ptr %35)
  br label %common.ret

next_protocol:                                    ; preds = %next_port
  %37 = tail call ptr @ts_string_create(ptr nonnull @12)
  %38 = tail call i1 @ts_string_eq(ptr %1, ptr %37)
  br i1 %38, label %match_search, label %next_search

match_search:                                     ; preds = %next_protocol
  %39 = getelementptr inbounds %URL, ptr %0, i64 0, i32 8
  %40 = load ptr, ptr %39, align 8
  %41 = tail call ptr @ts_value_make_string(ptr %40)
  br label %common.ret

next_search:                                      ; preds = %next_protocol
  %42 = tail call ptr @ts_value_make_undefined()
  br label %common.ret
}

define ptr @IncomingMessage_get_property(ptr nocapture readonly %0, ptr %1) #0 {
entry:
  %2 = tail call ptr @ts_string_create(ptr nonnull @13)
  %3 = tail call i1 @ts_string_eq(ptr %1, ptr %2)
  br i1 %3, label %match_headers, label %next_headers

common.ret:                                       ; preds = %next_url, %match_url, %match_method, %match_headers
  %common.ret.op = phi ptr [ %6, %match_headers ], [ %11, %match_method ], [ %16, %match_url ], [ %17, %next_url ]
  ret ptr %common.ret.op

match_headers:                                    ; preds = %entry
  %4 = getelementptr inbounds %IncomingMessage, ptr %0, i64 0, i32 1
  %5 = load ptr, ptr %4, align 8
  %6 = tail call ptr @ts_value_make_object(ptr %5)
  br label %common.ret

next_headers:                                     ; preds = %entry
  %7 = tail call ptr @ts_string_create(ptr nonnull @14)
  %8 = tail call i1 @ts_string_eq(ptr %1, ptr %7)
  br i1 %8, label %match_method, label %next_method

match_method:                                     ; preds = %next_headers
  %9 = getelementptr inbounds %IncomingMessage, ptr %0, i64 0, i32 2
  %10 = load ptr, ptr %9, align 8
  %11 = tail call ptr @ts_value_make_string(ptr %10)
  br label %common.ret

next_method:                                      ; preds = %next_headers
  %12 = tail call ptr @ts_string_create(ptr nonnull @15)
  %13 = tail call i1 @ts_string_eq(ptr %1, ptr %12)
  br i1 %13, label %match_url, label %next_url

match_url:                                        ; preds = %next_method
  %14 = getelementptr inbounds %IncomingMessage, ptr %0, i64 0, i32 3
  %15 = load ptr, ptr %14, align 8
  %16 = tail call ptr @ts_value_make_string(ptr %15)
  br label %common.ret

next_url:                                         ; preds = %next_method
  %17 = tail call ptr @ts_value_make_undefined()
  br label %common.ret
}

define ptr @ServerResponse_get_property(ptr nocapture readnone %0, ptr nocapture readnone %1) #0 {
entry:
  %2 = tail call ptr @ts_value_make_undefined()
  ret ptr %2
}

declare ptr @ServerResponse_end(ptr, ptr, ptr) #0

declare ptr @ServerResponse_write(ptr, ptr, ptr) #0

declare ptr @ServerResponse_writeHead(ptr, ptr, i64, ptr) #0

define ptr @Server_get_property(ptr nocapture readnone %0, ptr nocapture readnone %1) #0 {
entry:
  %2 = tail call ptr @ts_value_make_undefined()
  ret ptr %2
}

declare ptr @Server_listen(ptr, ptr, i64, ptr) #0

define ptr @__module_init_13540839489060876406(ptr nocapture readnone %context) local_unnamed_addr #0 !type !10 {
entry:
  %0 = tail call ptr @__ts_main(ptr poison)
  %1 = tail call ptr @ts_value_make_undefined()
  ret ptr %1
}

define ptr @user_main(ptr nocapture readnone %context) #0 !type !11 {
entry:
  %0 = tail call ptr @__ts_main(ptr poison)
  %1 = tail call ptr @ts_value_make_undefined()
  %2 = tail call ptr @ts_value_make_object(ptr %1)
  ret ptr %2
}

define nonnull ptr @hex_int(ptr nocapture readnone %context, i64 %n) local_unnamed_addr #0 !type !12 {
entry:
  %0 = and i64 %n, 4294967295
  %1 = tail call ptr @ts_int_to_string(i64 %0, i64 16)
  %2 = icmp eq ptr %1, null
  br i1 %2, label %null_fail, label %null_cont

null_fail:                                        ; preds = %whileloop, %entry
  tail call void @ts_panic(ptr nonnull @16)
  unreachable

null_cont:                                        ; preds = %entry, %whileloop
  %s.07 = phi ptr [ %6, %whileloop ], [ %1, %entry ]
  %3 = getelementptr inbounds %TsString, ptr %s.07, i64 0, i32 1
  %4 = load i32, ptr %3, align 4, !range !13
  %cmptmp = icmp ult i32 %4, 8
  br i1 %cmptmp, label %whileloop, label %whileafter

whileloop:                                        ; preds = %null_cont
  %5 = tail call ptr @ts_string_create(ptr nonnull @17)
  %6 = tail call ptr @ts_string_concat(ptr %5, ptr nonnull %s.07)
  %7 = icmp eq ptr %6, null
  br i1 %7, label %null_fail, label %null_cont

whileafter:                                       ; preds = %null_cont
  ret ptr %s.07
}

define noalias noundef ptr @__ts_main(ptr nocapture readnone %context) local_unnamed_addr #0 !type !14 {
entry:
  %0 = tail call ptr @ts_string_create(ptr nonnull @18)
  %1 = tail call ptr @ts_string_create(ptr nonnull @19)
  %2 = tail call ptr @ts_string_from_int(i64 1000)
  %3 = tail call ptr @ts_string_concat(ptr %1, ptr %2)
  %4 = tail call ptr @ts_string_create(ptr nonnull @20)
  %5 = tail call ptr @ts_string_concat(ptr %3, ptr %4)
  tail call void @ts_console_log(ptr %5)
  %6 = tail call i64 @Date_static_now()
  %7 = tail call ptr @ts_string_create(ptr nonnull @21)
  br label %forloop

forloop:                                          ; preds = %entry, %forloop
  %i.018 = phi i64 [ 0, %entry ], [ %incdec, %forloop ]
  %8 = tail call ptr @sha256_str(ptr poison, ptr %0)
  %incdec = add nuw nsw i64 %i.018, 1
  %exitcond.not = icmp eq i64 %incdec, 1000
  br i1 %exitcond.not, label %forafter, label %forloop

forafter:                                         ; preds = %forloop
  %9 = tail call i64 @Date_static_now()
  %10 = tail call ptr @ts_string_create(ptr nonnull @22)
  tail call void @ts_console_log(ptr %10)
  %11 = tail call ptr @ts_string_create(ptr nonnull @23)
  %12 = tail call ptr @ts_string_concat(ptr %11, ptr %8)
  tail call void @ts_console_log(ptr %12)
  %13 = tail call ptr @ts_string_create(ptr nonnull @24)
  %subtmp = sub i64 %9, %6
  %14 = tail call ptr @ts_string_from_int(i64 %subtmp)
  %15 = tail call ptr @ts_string_concat(ptr %13, ptr %14)
  %16 = tail call ptr @ts_string_create(ptr nonnull @26)
  %17 = tail call ptr @ts_string_concat(ptr %15, ptr %16)
  tail call void @ts_console_log(ptr %17)
  %18 = tail call ptr @ts_string_create(ptr nonnull @25)
  %19 = sitofp i64 %subtmp to double
  %divtmp = fmul fast double %19, 1.000000e-03
  %20 = tail call ptr @ts_double_to_fixed(double %divtmp, i64 4)
  %21 = tail call ptr @ts_string_concat(ptr %18, ptr %20)
  %22 = tail call ptr @ts_string_create(ptr nonnull @26)
  %23 = tail call ptr @ts_string_concat(ptr %21, ptr %22)
  tail call void @ts_console_log(ptr %23)
  ret ptr null
}

; Function Attrs: mustprogress nofree norecurse nosync nounwind willreturn memory(none)
define i64 @rotr_int_int(ptr nocapture readnone %context, i64 %n, i64 %x) local_unnamed_addr #1 !type !15 {
entry:
  %0 = trunc i64 %x to i32
  %1 = trunc i64 %n to i32
  %lshrtmp = lshr i32 %0, %1
  %2 = sub i32 32, %1
  %shltmp = shl i32 %0, %2
  %ortmp = or i32 %lshrtmp, %shltmp
  %3 = sext i32 %ortmp to i64
  ret i64 %3
}

define ptr @sha256_str(ptr nocapture readnone %context, ptr %message) local_unnamed_addr #0 !type !16 {
entry:
  %0 = tail call ptr @ts_array_create()
  %1 = tail call ptr @ts_value_make_int(i64 1116352408)
  tail call void @ts_array_push(ptr %0, ptr %1)
  %2 = tail call ptr @ts_value_make_int(i64 1899447441)
  tail call void @ts_array_push(ptr %0, ptr %2)
  %3 = tail call ptr @ts_value_make_int(i64 3049323471)
  tail call void @ts_array_push(ptr %0, ptr %3)
  %4 = tail call ptr @ts_value_make_int(i64 3921009573)
  tail call void @ts_array_push(ptr %0, ptr %4)
  %5 = tail call ptr @ts_value_make_int(i64 961987163)
  tail call void @ts_array_push(ptr %0, ptr %5)
  %6 = tail call ptr @ts_value_make_int(i64 1508970993)
  tail call void @ts_array_push(ptr %0, ptr %6)
  %7 = tail call ptr @ts_value_make_int(i64 2453635748)
  tail call void @ts_array_push(ptr %0, ptr %7)
  %8 = tail call ptr @ts_value_make_int(i64 2870763221)
  tail call void @ts_array_push(ptr %0, ptr %8)
  %9 = tail call ptr @ts_value_make_int(i64 3624381080)
  tail call void @ts_array_push(ptr %0, ptr %9)
  %10 = tail call ptr @ts_value_make_int(i64 310598401)
  tail call void @ts_array_push(ptr %0, ptr %10)
  %11 = tail call ptr @ts_value_make_int(i64 607225278)
  tail call void @ts_array_push(ptr %0, ptr %11)
  %12 = tail call ptr @ts_value_make_int(i64 1426881987)
  tail call void @ts_array_push(ptr %0, ptr %12)
  %13 = tail call ptr @ts_value_make_int(i64 1925078388)
  tail call void @ts_array_push(ptr %0, ptr %13)
  %14 = tail call ptr @ts_value_make_int(i64 2162078206)
  tail call void @ts_array_push(ptr %0, ptr %14)
  %15 = tail call ptr @ts_value_make_int(i64 2614888103)
  tail call void @ts_array_push(ptr %0, ptr %15)
  %16 = tail call ptr @ts_value_make_int(i64 3248222580)
  tail call void @ts_array_push(ptr %0, ptr %16)
  %17 = tail call ptr @ts_value_make_int(i64 3835390401)
  tail call void @ts_array_push(ptr %0, ptr %17)
  %18 = tail call ptr @ts_value_make_int(i64 4022224774)
  tail call void @ts_array_push(ptr %0, ptr %18)
  %19 = tail call ptr @ts_value_make_int(i64 264347078)
  tail call void @ts_array_push(ptr %0, ptr %19)
  %20 = tail call ptr @ts_value_make_int(i64 604807628)
  tail call void @ts_array_push(ptr %0, ptr %20)
  %21 = tail call ptr @ts_value_make_int(i64 770255983)
  tail call void @ts_array_push(ptr %0, ptr %21)
  %22 = tail call ptr @ts_value_make_int(i64 1249150122)
  tail call void @ts_array_push(ptr %0, ptr %22)
  %23 = tail call ptr @ts_value_make_int(i64 1555081692)
  tail call void @ts_array_push(ptr %0, ptr %23)
  %24 = tail call ptr @ts_value_make_int(i64 1996064986)
  tail call void @ts_array_push(ptr %0, ptr %24)
  %25 = tail call ptr @ts_value_make_int(i64 2554220882)
  tail call void @ts_array_push(ptr %0, ptr %25)
  %26 = tail call ptr @ts_value_make_int(i64 2821834349)
  tail call void @ts_array_push(ptr %0, ptr %26)
  %27 = tail call ptr @ts_value_make_int(i64 2952996808)
  tail call void @ts_array_push(ptr %0, ptr %27)
  %28 = tail call ptr @ts_value_make_int(i64 3210313671)
  tail call void @ts_array_push(ptr %0, ptr %28)
  %29 = tail call ptr @ts_value_make_int(i64 3336571891)
  tail call void @ts_array_push(ptr %0, ptr %29)
  %30 = tail call ptr @ts_value_make_int(i64 3584528711)
  tail call void @ts_array_push(ptr %0, ptr %30)
  %31 = tail call ptr @ts_value_make_int(i64 113926993)
  tail call void @ts_array_push(ptr %0, ptr %31)
  %32 = tail call ptr @ts_value_make_int(i64 338241895)
  tail call void @ts_array_push(ptr %0, ptr %32)
  %33 = tail call ptr @ts_value_make_int(i64 666307205)
  tail call void @ts_array_push(ptr %0, ptr %33)
  %34 = tail call ptr @ts_value_make_int(i64 773529912)
  tail call void @ts_array_push(ptr %0, ptr %34)
  %35 = tail call ptr @ts_value_make_int(i64 1294757372)
  tail call void @ts_array_push(ptr %0, ptr %35)
  %36 = tail call ptr @ts_value_make_int(i64 1396182291)
  tail call void @ts_array_push(ptr %0, ptr %36)
  %37 = tail call ptr @ts_value_make_int(i64 1695183700)
  tail call void @ts_array_push(ptr %0, ptr %37)
  %38 = tail call ptr @ts_value_make_int(i64 1986661051)
  tail call void @ts_array_push(ptr %0, ptr %38)
  %39 = tail call ptr @ts_value_make_int(i64 2177026350)
  tail call void @ts_array_push(ptr %0, ptr %39)
  %40 = tail call ptr @ts_value_make_int(i64 2456956037)
  tail call void @ts_array_push(ptr %0, ptr %40)
  %41 = tail call ptr @ts_value_make_int(i64 2730485921)
  tail call void @ts_array_push(ptr %0, ptr %41)
  %42 = tail call ptr @ts_value_make_int(i64 2820302411)
  tail call void @ts_array_push(ptr %0, ptr %42)
  %43 = tail call ptr @ts_value_make_int(i64 3259730800)
  tail call void @ts_array_push(ptr %0, ptr %43)
  %44 = tail call ptr @ts_value_make_int(i64 3345764771)
  tail call void @ts_array_push(ptr %0, ptr %44)
  %45 = tail call ptr @ts_value_make_int(i64 3516065817)
  tail call void @ts_array_push(ptr %0, ptr %45)
  %46 = tail call ptr @ts_value_make_int(i64 3600352804)
  tail call void @ts_array_push(ptr %0, ptr %46)
  %47 = tail call ptr @ts_value_make_int(i64 4094571909)
  tail call void @ts_array_push(ptr %0, ptr %47)
  %48 = tail call ptr @ts_value_make_int(i64 275423344)
  tail call void @ts_array_push(ptr %0, ptr %48)
  %49 = tail call ptr @ts_value_make_int(i64 430227734)
  tail call void @ts_array_push(ptr %0, ptr %49)
  %50 = tail call ptr @ts_value_make_int(i64 506948616)
  tail call void @ts_array_push(ptr %0, ptr %50)
  %51 = tail call ptr @ts_value_make_int(i64 659060556)
  tail call void @ts_array_push(ptr %0, ptr %51)
  %52 = tail call ptr @ts_value_make_int(i64 883997877)
  tail call void @ts_array_push(ptr %0, ptr %52)
  %53 = tail call ptr @ts_value_make_int(i64 958139571)
  tail call void @ts_array_push(ptr %0, ptr %53)
  %54 = tail call ptr @ts_value_make_int(i64 1322822218)
  tail call void @ts_array_push(ptr %0, ptr %54)
  %55 = tail call ptr @ts_value_make_int(i64 1537002063)
  tail call void @ts_array_push(ptr %0, ptr %55)
  %56 = tail call ptr @ts_value_make_int(i64 1747873779)
  tail call void @ts_array_push(ptr %0, ptr %56)
  %57 = tail call ptr @ts_value_make_int(i64 1955562222)
  tail call void @ts_array_push(ptr %0, ptr %57)
  %58 = tail call ptr @ts_value_make_int(i64 2024104815)
  tail call void @ts_array_push(ptr %0, ptr %58)
  %59 = tail call ptr @ts_value_make_int(i64 2227730452)
  tail call void @ts_array_push(ptr %0, ptr %59)
  %60 = tail call ptr @ts_value_make_int(i64 2361852424)
  tail call void @ts_array_push(ptr %0, ptr %60)
  %61 = tail call ptr @ts_value_make_int(i64 2428436474)
  tail call void @ts_array_push(ptr %0, ptr %61)
  %62 = tail call ptr @ts_value_make_int(i64 2756734187)
  tail call void @ts_array_push(ptr %0, ptr %62)
  %63 = tail call ptr @ts_value_make_int(i64 3204031479)
  tail call void @ts_array_push(ptr %0, ptr %63)
  %64 = tail call ptr @ts_value_make_int(i64 3329325298)
  tail call void @ts_array_push(ptr %0, ptr %64)
  %65 = tail call ptr @ts_typed_array_from_array_u32(ptr %0)
  %66 = getelementptr inbounds %TsString, ptr %message, i64 0, i32 1
  %67 = load i32, ptr %66, align 4, !range !13
  %68 = zext nneg i32 %67 to i64
  %addtmp = add nuw i32 %67, 8
  %ashrtmp = ashr i32 %addtmp, 6
  %narrow = add nsw i32 %ashrtmp, 1
  %addtmp4 = sext i32 %narrow to i64
  %shltmp = shl i32 %narrow, 6
  %69 = sext i32 %shltmp to i64
  %70 = tail call ptr @ts_typed_array_create_u8(i64 %69)
  %cmptmp600.not = icmp eq i32 %67, 0
  br i1 %cmptmp600.not, label %forafter, label %forloop.lr.ph

forloop.lr.ph:                                    ; preds = %entry
  %71 = getelementptr inbounds %Uint8Array, ptr %70, i64 0, i32 2
  %72 = getelementptr inbounds %Uint8Array, ptr %70, i64 0, i32 1
  br label %forloop

forloop:                                          ; preds = %forloop.lr.ph, %bounds_cont
  %i.0601 = phi i64 [ 0, %forloop.lr.ph ], [ %incdec, %bounds_cont ]
  %73 = tail call i64 @ts_string_charCodeAt(ptr %message, i64 %i.0601)
  %74 = load i64, ptr %71, align 8
  %.not511 = icmp slt i64 %i.0601, %74
  br i1 %.not511, label %bounds_cont, label %bounds_fail

bounds_fail:                                      ; preds = %forloop
  tail call void @ts_panic(ptr nonnull @27)
  unreachable

bounds_cont:                                      ; preds = %forloop
  %75 = load ptr, ptr %72, align 8
  %76 = getelementptr inbounds %TsBuffer, ptr %75, i64 0, i32 2
  %77 = load ptr, ptr %76, align 8
  %78 = getelementptr i8, ptr %77, i64 %i.0601
  %79 = trunc i64 %73 to i8
  store i8 %79, ptr %78, align 1
  %incdec = add nuw nsw i64 %i.0601, 1
  %exitcond.not = icmp eq i64 %incdec, %68
  br i1 %exitcond.not, label %forafter, label %forloop

forafter:                                         ; preds = %bounds_cont, %entry
  %80 = getelementptr inbounds %Uint8Array, ptr %70, i64 0, i32 2
  %81 = load i64, ptr %80, align 8
  %.not = icmp sgt i64 %81, %68
  br i1 %.not, label %bounds_cont16, label %bounds_fail15

bounds_fail15:                                    ; preds = %forafter
  tail call void @ts_panic(ptr nonnull @27)
  unreachable

bounds_cont16:                                    ; preds = %forafter
  %82 = getelementptr inbounds %Uint8Array, ptr %70, i64 0, i32 1
  %83 = load ptr, ptr %82, align 8
  %84 = getelementptr inbounds %TsBuffer, ptr %83, i64 0, i32 2
  %85 = load ptr, ptr %84, align 8
  %86 = getelementptr i8, ptr %85, i64 %68
  store i8 -128, ptr %86, align 1
  %87 = load ptr, ptr %82, align 8
  %88 = tail call ptr @ts_data_view_create(ptr %87)
  %subtmp = add nsw i64 %69, -4
  %multmp = shl i32 %67, 3
  %89 = sext i32 %multmp to i64
  tail call void @DataView_setUint32(ptr null, ptr %88, i64 %subtmp, i64 %89, i1 false)
  %90 = tail call ptr @ts_typed_array_create_u32(i64 64)
  %cmptmp25613 = icmp sgt i32 %ashrtmp, -1
  br i1 %cmptmp25613, label %forcond27.preheader.lr.ph, label %forafter222

forcond27.preheader.lr.ph:                        ; preds = %bounds_cont16
  %91 = getelementptr inbounds %Uint32Array, ptr %90, i64 0, i32 2
  %92 = getelementptr inbounds %Uint32Array, ptr %90, i64 0, i32 1
  %93 = getelementptr inbounds %Uint32Array, ptr %65, i64 0, i32 2
  %94 = getelementptr inbounds %Uint32Array, ptr %65, i64 0, i32 1
  br label %forcond27.preheader

forcond27.preheader:                              ; preds = %forcond27.preheader.lr.ph, %forafter186
  %h0.0622 = phi i64 [ 1779033703, %forcond27.preheader.lr.ph ], [ %259, %forafter186 ]
  %h1.0621 = phi i64 [ 3144134277, %forcond27.preheader.lr.ph ], [ %260, %forafter186 ]
  %h2.0620 = phi i64 [ 1013904242, %forcond27.preheader.lr.ph ], [ %261, %forafter186 ]
  %h3.0619 = phi i64 [ 2773480762, %forcond27.preheader.lr.ph ], [ %262, %forafter186 ]
  %h4.0618 = phi i64 [ 1359893119, %forcond27.preheader.lr.ph ], [ %263, %forafter186 ]
  %h5.0617 = phi i64 [ 2600822924, %forcond27.preheader.lr.ph ], [ %264, %forafter186 ]
  %h6.0616 = phi i64 [ 528734635, %forcond27.preheader.lr.ph ], [ %265, %forafter186 ]
  %h7.0615 = phi i64 [ 1541459225, %forcond27.preheader.lr.ph ], [ %266, %forafter186 ]
  %b.0614 = phi i64 [ 0, %forcond27.preheader.lr.ph ], [ %incdec221, %forafter186 ]
  %95 = trunc i64 %b.0614 to i32
  %shltmp33 = shl i32 %95, 6
  %96 = sext i32 %shltmp33 to i64
  %97 = tail call i64 @DataView_getUint32(ptr null, ptr %88, i64 %96, i1 false)
  %98 = load i64, ptr %91, align 8
  %.not510 = icmp sgt i64 %98, 0
  br i1 %.not510, label %bounds_cont40, label %bounds_fail39

bounds_fail39:                                    ; preds = %bounds_cont40.14, %bounds_cont40.13, %bounds_cont40.12, %bounds_cont40.11, %bounds_cont40.10, %bounds_cont40.9, %bounds_cont40.8, %bounds_cont40.7, %bounds_cont40.6, %bounds_cont40.5, %bounds_cont40.4, %bounds_cont40.3, %bounds_cont40.2, %bounds_cont40.1, %bounds_cont40, %forcond27.preheader
  tail call void @ts_panic(ptr nonnull @27)
  unreachable

bounds_cont40:                                    ; preds = %forcond27.preheader
  %99 = load ptr, ptr %92, align 8
  %100 = getelementptr inbounds %TsBuffer, ptr %99, i64 0, i32 2
  %101 = load ptr, ptr %100, align 8
  %102 = trunc i64 %97 to i32
  store i32 %102, ptr %101, align 4
  %addtmp36.1 = or disjoint i64 %96, 4
  %103 = tail call i64 @DataView_getUint32(ptr null, ptr %88, i64 %addtmp36.1, i1 false)
  %104 = load i64, ptr %91, align 8
  %.not510.1 = icmp sgt i64 %104, 1
  br i1 %.not510.1, label %bounds_cont40.1, label %bounds_fail39

bounds_cont40.1:                                  ; preds = %bounds_cont40
  %105 = load ptr, ptr %92, align 8
  %106 = getelementptr inbounds %TsBuffer, ptr %105, i64 0, i32 2
  %107 = load ptr, ptr %106, align 8
  %108 = getelementptr i32, ptr %107, i64 1
  %109 = trunc i64 %103 to i32
  store i32 %109, ptr %108, align 4
  %addtmp36.2 = or disjoint i64 %96, 8
  %110 = tail call i64 @DataView_getUint32(ptr null, ptr %88, i64 %addtmp36.2, i1 false)
  %111 = load i64, ptr %91, align 8
  %.not510.2 = icmp sgt i64 %111, 2
  br i1 %.not510.2, label %bounds_cont40.2, label %bounds_fail39

bounds_cont40.2:                                  ; preds = %bounds_cont40.1
  %112 = load ptr, ptr %92, align 8
  %113 = getelementptr inbounds %TsBuffer, ptr %112, i64 0, i32 2
  %114 = load ptr, ptr %113, align 8
  %115 = getelementptr i32, ptr %114, i64 2
  %116 = trunc i64 %110 to i32
  store i32 %116, ptr %115, align 4
  %addtmp36.3 = or disjoint i64 %96, 12
  %117 = tail call i64 @DataView_getUint32(ptr null, ptr %88, i64 %addtmp36.3, i1 false)
  %118 = load i64, ptr %91, align 8
  %.not510.3 = icmp sgt i64 %118, 3
  br i1 %.not510.3, label %bounds_cont40.3, label %bounds_fail39

bounds_cont40.3:                                  ; preds = %bounds_cont40.2
  %119 = load ptr, ptr %92, align 8
  %120 = getelementptr inbounds %TsBuffer, ptr %119, i64 0, i32 2
  %121 = load ptr, ptr %120, align 8
  %122 = getelementptr i32, ptr %121, i64 3
  %123 = trunc i64 %117 to i32
  store i32 %123, ptr %122, align 4
  %addtmp36.4 = or disjoint i64 %96, 16
  %124 = tail call i64 @DataView_getUint32(ptr null, ptr %88, i64 %addtmp36.4, i1 false)
  %125 = load i64, ptr %91, align 8
  %.not510.4 = icmp sgt i64 %125, 4
  br i1 %.not510.4, label %bounds_cont40.4, label %bounds_fail39

bounds_cont40.4:                                  ; preds = %bounds_cont40.3
  %126 = load ptr, ptr %92, align 8
  %127 = getelementptr inbounds %TsBuffer, ptr %126, i64 0, i32 2
  %128 = load ptr, ptr %127, align 8
  %129 = getelementptr i32, ptr %128, i64 4
  %130 = trunc i64 %124 to i32
  store i32 %130, ptr %129, align 4
  %addtmp36.5 = or disjoint i64 %96, 20
  %131 = tail call i64 @DataView_getUint32(ptr null, ptr %88, i64 %addtmp36.5, i1 false)
  %132 = load i64, ptr %91, align 8
  %.not510.5 = icmp sgt i64 %132, 5
  br i1 %.not510.5, label %bounds_cont40.5, label %bounds_fail39

bounds_cont40.5:                                  ; preds = %bounds_cont40.4
  %133 = load ptr, ptr %92, align 8
  %134 = getelementptr inbounds %TsBuffer, ptr %133, i64 0, i32 2
  %135 = load ptr, ptr %134, align 8
  %136 = getelementptr i32, ptr %135, i64 5
  %137 = trunc i64 %131 to i32
  store i32 %137, ptr %136, align 4
  %addtmp36.6 = or disjoint i64 %96, 24
  %138 = tail call i64 @DataView_getUint32(ptr null, ptr %88, i64 %addtmp36.6, i1 false)
  %139 = load i64, ptr %91, align 8
  %.not510.6 = icmp sgt i64 %139, 6
  br i1 %.not510.6, label %bounds_cont40.6, label %bounds_fail39

bounds_cont40.6:                                  ; preds = %bounds_cont40.5
  %140 = load ptr, ptr %92, align 8
  %141 = getelementptr inbounds %TsBuffer, ptr %140, i64 0, i32 2
  %142 = load ptr, ptr %141, align 8
  %143 = getelementptr i32, ptr %142, i64 6
  %144 = trunc i64 %138 to i32
  store i32 %144, ptr %143, align 4
  %addtmp36.7 = or disjoint i64 %96, 28
  %145 = tail call i64 @DataView_getUint32(ptr null, ptr %88, i64 %addtmp36.7, i1 false)
  %146 = load i64, ptr %91, align 8
  %.not510.7 = icmp sgt i64 %146, 7
  br i1 %.not510.7, label %bounds_cont40.7, label %bounds_fail39

bounds_cont40.7:                                  ; preds = %bounds_cont40.6
  %147 = load ptr, ptr %92, align 8
  %148 = getelementptr inbounds %TsBuffer, ptr %147, i64 0, i32 2
  %149 = load ptr, ptr %148, align 8
  %150 = getelementptr i32, ptr %149, i64 7
  %151 = trunc i64 %145 to i32
  store i32 %151, ptr %150, align 4
  %addtmp36.8 = or disjoint i64 %96, 32
  %152 = tail call i64 @DataView_getUint32(ptr null, ptr %88, i64 %addtmp36.8, i1 false)
  %153 = load i64, ptr %91, align 8
  %.not510.8 = icmp sgt i64 %153, 8
  br i1 %.not510.8, label %bounds_cont40.8, label %bounds_fail39

bounds_cont40.8:                                  ; preds = %bounds_cont40.7
  %154 = load ptr, ptr %92, align 8
  %155 = getelementptr inbounds %TsBuffer, ptr %154, i64 0, i32 2
  %156 = load ptr, ptr %155, align 8
  %157 = getelementptr i32, ptr %156, i64 8
  %158 = trunc i64 %152 to i32
  store i32 %158, ptr %157, align 4
  %addtmp36.9 = or disjoint i64 %96, 36
  %159 = tail call i64 @DataView_getUint32(ptr null, ptr %88, i64 %addtmp36.9, i1 false)
  %160 = load i64, ptr %91, align 8
  %.not510.9 = icmp sgt i64 %160, 9
  br i1 %.not510.9, label %bounds_cont40.9, label %bounds_fail39

bounds_cont40.9:                                  ; preds = %bounds_cont40.8
  %161 = load ptr, ptr %92, align 8
  %162 = getelementptr inbounds %TsBuffer, ptr %161, i64 0, i32 2
  %163 = load ptr, ptr %162, align 8
  %164 = getelementptr i32, ptr %163, i64 9
  %165 = trunc i64 %159 to i32
  store i32 %165, ptr %164, align 4
  %addtmp36.10 = or disjoint i64 %96, 40
  %166 = tail call i64 @DataView_getUint32(ptr null, ptr %88, i64 %addtmp36.10, i1 false)
  %167 = load i64, ptr %91, align 8
  %.not510.10 = icmp sgt i64 %167, 10
  br i1 %.not510.10, label %bounds_cont40.10, label %bounds_fail39

bounds_cont40.10:                                 ; preds = %bounds_cont40.9
  %168 = load ptr, ptr %92, align 8
  %169 = getelementptr inbounds %TsBuffer, ptr %168, i64 0, i32 2
  %170 = load ptr, ptr %169, align 8
  %171 = getelementptr i32, ptr %170, i64 10
  %172 = trunc i64 %166 to i32
  store i32 %172, ptr %171, align 4
  %addtmp36.11 = or disjoint i64 %96, 44
  %173 = tail call i64 @DataView_getUint32(ptr null, ptr %88, i64 %addtmp36.11, i1 false)
  %174 = load i64, ptr %91, align 8
  %.not510.11 = icmp sgt i64 %174, 11
  br i1 %.not510.11, label %bounds_cont40.11, label %bounds_fail39

bounds_cont40.11:                                 ; preds = %bounds_cont40.10
  %175 = load ptr, ptr %92, align 8
  %176 = getelementptr inbounds %TsBuffer, ptr %175, i64 0, i32 2
  %177 = load ptr, ptr %176, align 8
  %178 = getelementptr i32, ptr %177, i64 11
  %179 = trunc i64 %173 to i32
  store i32 %179, ptr %178, align 4
  %addtmp36.12 = or disjoint i64 %96, 48
  %180 = tail call i64 @DataView_getUint32(ptr null, ptr %88, i64 %addtmp36.12, i1 false)
  %181 = load i64, ptr %91, align 8
  %.not510.12 = icmp sgt i64 %181, 12
  br i1 %.not510.12, label %bounds_cont40.12, label %bounds_fail39

bounds_cont40.12:                                 ; preds = %bounds_cont40.11
  %182 = load ptr, ptr %92, align 8
  %183 = getelementptr inbounds %TsBuffer, ptr %182, i64 0, i32 2
  %184 = load ptr, ptr %183, align 8
  %185 = getelementptr i32, ptr %184, i64 12
  %186 = trunc i64 %180 to i32
  store i32 %186, ptr %185, align 4
  %addtmp36.13 = or disjoint i64 %96, 52
  %187 = tail call i64 @DataView_getUint32(ptr null, ptr %88, i64 %addtmp36.13, i1 false)
  %188 = load i64, ptr %91, align 8
  %.not510.13 = icmp sgt i64 %188, 13
  br i1 %.not510.13, label %bounds_cont40.13, label %bounds_fail39

bounds_cont40.13:                                 ; preds = %bounds_cont40.12
  %189 = load ptr, ptr %92, align 8
  %190 = getelementptr inbounds %TsBuffer, ptr %189, i64 0, i32 2
  %191 = load ptr, ptr %190, align 8
  %192 = getelementptr i32, ptr %191, i64 13
  %193 = trunc i64 %187 to i32
  store i32 %193, ptr %192, align 4
  %addtmp36.14 = or disjoint i64 %96, 56
  %194 = tail call i64 @DataView_getUint32(ptr null, ptr %88, i64 %addtmp36.14, i1 false)
  %195 = load i64, ptr %91, align 8
  %.not510.14 = icmp sgt i64 %195, 14
  br i1 %.not510.14, label %bounds_cont40.14, label %bounds_fail39

bounds_cont40.14:                                 ; preds = %bounds_cont40.13
  %196 = load ptr, ptr %92, align 8
  %197 = getelementptr inbounds %TsBuffer, ptr %196, i64 0, i32 2
  %198 = load ptr, ptr %197, align 8
  %199 = getelementptr i32, ptr %198, i64 14
  %200 = trunc i64 %194 to i32
  store i32 %200, ptr %199, align 4
  %addtmp36.15 = or disjoint i64 %96, 60
  %201 = tail call i64 @DataView_getUint32(ptr null, ptr %88, i64 %addtmp36.15, i1 false)
  %202 = load i64, ptr %91, align 8
  %.not510.15 = icmp sgt i64 %202, 15
  br i1 %.not510.15, label %bounds_cont40.15, label %bounds_fail39

bounds_cont40.15:                                 ; preds = %bounds_cont40.14
  %203 = load ptr, ptr %92, align 8
  %204 = getelementptr inbounds %TsBuffer, ptr %203, i64 0, i32 2
  %205 = load ptr, ptr %204, align 8
  %206 = getelementptr i32, ptr %205, i64 15
  %207 = trunc i64 %201 to i32
  store i32 %207, ptr %206, align 4
  br label %forloop49

forcond115.preheader:                             ; preds = %bounds_cont102
  %208 = load i64, ptr %93, align 8
  %exitcond669.peel.not = icmp slt i64 %208, 1
  br i1 %exitcond669.peel.not, label %bounds_fail139, label %bounds_cont140.peel

bounds_cont140.peel:                              ; preds = %forcond115.preheader
  %209 = load i64, ptr %91, align 8
  %.not504.peel = icmp sgt i64 %209, 0
  br i1 %.not504.peel, label %forloop119.peel.next, label %bounds_fail144

forloop119.peel.next:                             ; preds = %bounds_cont140.peel
  %210 = load ptr, ptr %94, align 8
  %211 = getelementptr inbounds %TsBuffer, ptr %210, i64 0, i32 2
  %212 = load ptr, ptr %211, align 8
  %213 = load ptr, ptr %92, align 8
  %214 = getelementptr inbounds %TsBuffer, ptr %213, i64 0, i32 2
  %215 = load ptr, ptr %214, align 8
  %216 = trunc i64 %h0.0622 to i32
  %ortmp.i300.peel = tail call i32 @llvm.fshl.i32(i32 %216, i32 %216, i32 30)
  %ortmp.i310.peel = tail call i32 @llvm.fshl.i32(i32 %216, i32 %216, i32 19)
  %xortmp150.peel = xor i32 %ortmp.i300.peel, %ortmp.i310.peel
  %ortmp.i320.peel = tail call i32 @llvm.fshl.i32(i32 %216, i32 %216, i32 10)
  %xortmp152.peel = xor i32 %xortmp150.peel, %ortmp.i320.peel
  %andtmp159505.peel = xor i64 %h1.0621, %h2.0620
  %xortmp160.peel = and i64 %andtmp159505.peel, %h0.0622
  %andtmp163.peel = and i64 %h1.0621, %h2.0620
  %xortmp164.peel = xor i64 %xortmp160.peel, %andtmp163.peel
  %217 = trunc i64 %xortmp164.peel to i32
  %218 = add i32 %xortmp152.peel, %217
  %219 = load i32, ptr %212, align 4
  %220 = trunc i64 %h4.0618 to i32
  %ortmp.i270.peel = tail call i32 @llvm.fshl.i32(i32 %220, i32 %220, i32 26)
  %ortmp.i280.peel = tail call i32 @llvm.fshl.i32(i32 %220, i32 %220, i32 21)
  %xortmp122.peel = xor i32 %ortmp.i270.peel, %ortmp.i280.peel
  %ortmp.i290.peel = tail call i32 @llvm.fshl.i32(i32 %220, i32 %220, i32 7)
  %xortmp124.peel = xor i32 %xortmp122.peel, %ortmp.i290.peel
  %andtmp501.peel = and i64 %h4.0618, %h5.0617
  %addtmp134.peel = add nsw i64 %andtmp501.peel, %h7.0615
  %bitnot.peel = xor i64 %h4.0618, -1
  %andtmp130.peel = and i64 %h6.0616, %bitnot.peel
  %xortmp131.peel = add nsw i64 %addtmp134.peel, %andtmp130.peel
  %221 = trunc i64 %xortmp131.peel to i32
  %222 = add i32 %xortmp124.peel, %221
  %223 = add i32 %219, %222
  %224 = load i32, ptr %215, align 4
  %225 = add i32 %223, %224
  %addtmp181.peel = add i32 %218, %225
  %226 = zext i32 %addtmp181.peel to i64
  %227 = trunc i64 %h3.0619 to i32
  %228 = add i32 %225, %227
  %229 = zext i32 %228 to i64
  br label %forloop119

forloop49:                                        ; preds = %bounds_cont40.15, %bounds_cont102
  %t46.0603 = phi i64 [ %incdec105, %bounds_cont102 ], [ 16, %bounds_cont40.15 ]
  %subtmp52 = add nsw i64 %t46.0603, -15
  %230 = load i64, ptr %91, align 8
  %.not675 = icmp slt i64 %subtmp52, %230
  br i1 %.not675, label %bounds_cont54, label %bounds_fail53

bounds_fail53:                                    ; preds = %forloop49
  tail call void @ts_panic(ptr nonnull @27)
  unreachable

bounds_cont54:                                    ; preds = %forloop49
  %231 = load ptr, ptr %92, align 8
  %232 = getelementptr inbounds %TsBuffer, ptr %231, i64 0, i32 2
  %233 = load ptr, ptr %232, align 8
  %234 = getelementptr i32, ptr %233, i64 %subtmp52
  %235 = load i32, ptr %234, align 4
  %ortmp.i = tail call i32 @llvm.fshl.i32(i32 %235, i32 %235, i32 25)
  %ortmp.i240 = tail call i32 @llvm.fshl.i32(i32 %235, i32 %235, i32 14)
  %xortmp = xor i32 %ortmp.i, %ortmp.i240
  %lshrtmp = lshr i32 %235, 3
  %xortmp65 = xor i32 %xortmp, %lshrtmp
  %subtmp68 = add nsw i64 %t46.0603, -2
  %.not507 = icmp slt i64 %subtmp68, %230
  br i1 %.not507, label %bounds_cont88, label %bounds_fail69

bounds_fail69:                                    ; preds = %bounds_cont54
  tail call void @ts_panic(ptr nonnull @27)
  unreachable

bounds_cont88:                                    ; preds = %bounds_cont54
  %236 = getelementptr i32, ptr %233, i64 %subtmp68
  %237 = load i32, ptr %236, align 4
  %ortmp.i250 = tail call i32 @llvm.fshl.i32(i32 %237, i32 %237, i32 15)
  %ortmp.i260 = tail call i32 @llvm.fshl.i32(i32 %237, i32 %237, i32 13)
  %xortmp76 = xor i32 %ortmp.i250, %ortmp.i260
  %lshrtmp82 = lshr i32 %237, 10
  %xortmp83 = xor i32 %xortmp76, %lshrtmp82
  %subtmp93 = add nsw i64 %t46.0603, -7
  %.not508 = icmp ult i64 %subtmp93, %230
  br i1 %.not508, label %bounds_cont95, label %bounds_fail94

bounds_fail94:                                    ; preds = %bounds_cont88
  tail call void @ts_panic(ptr nonnull @27)
  unreachable

bounds_cont95:                                    ; preds = %bounds_cont88
  %.not509 = icmp ult i64 %t46.0603, %230
  br i1 %.not509, label %bounds_cont102, label %bounds_fail101

bounds_fail101:                                   ; preds = %bounds_cont95
  tail call void @ts_panic(ptr nonnull @27)
  unreachable

bounds_cont102:                                   ; preds = %bounds_cont95
  %238 = getelementptr i32, ptr %233, i64 %subtmp93
  %239 = load i32, ptr %238, align 4
  %240 = getelementptr i32, ptr %233, i64 %t46.0603
  %241 = getelementptr i32, ptr %240, i64 -16
  %242 = load i32, ptr %241, align 4
  %addtmp90 = add i32 %xortmp83, %xortmp65
  %addtmp96 = add i32 %addtmp90, %239
  %addtmp98 = add i32 %addtmp96, %242
  store i32 %addtmp98, ptr %240, align 4
  %incdec105 = add nuw nsw i64 %t46.0603, 1
  %exitcond668.not = icmp eq i64 %incdec105, 64
  br i1 %exitcond668.not, label %forcond115.preheader, label %forloop49

forloop119:                                       ; preds = %forloop119.peel.next, %bounds_cont145
  %t116.0612 = phi i64 [ 1, %forloop119.peel.next ], [ %incdec185, %bounds_cont145 ]
  %a.0611 = phi i64 [ %226, %forloop119.peel.next ], [ %258, %bounds_cont145 ]
  %b_.0610 = phi i64 [ %h0.0622, %forloop119.peel.next ], [ %a.0611, %bounds_cont145 ]
  %c.0609 = phi i64 [ %h1.0621, %forloop119.peel.next ], [ %b_.0610, %bounds_cont145 ]
  %d.0608 = phi i64 [ %h2.0620, %forloop119.peel.next ], [ %c.0609, %bounds_cont145 ]
  %e.0607 = phi i64 [ %229, %forloop119.peel.next ], [ %257, %bounds_cont145 ]
  %f.0606 = phi i64 [ %h4.0618, %forloop119.peel.next ], [ %e.0607, %bounds_cont145 ]
  %g.0605 = phi i64 [ %h5.0617, %forloop119.peel.next ], [ %f.0606, %bounds_cont145 ]
  %h.0604 = phi i64 [ %h6.0616, %forloop119.peel.next ], [ %g.0605, %bounds_cont145 ]
  %exitcond669.not = icmp eq i64 %208, %t116.0612
  br i1 %exitcond669.not, label %bounds_fail139, label %bounds_cont140

bounds_fail139:                                   ; preds = %forcond115.preheader, %forloop119
  tail call void @ts_panic(ptr nonnull @27)
  unreachable

bounds_cont140:                                   ; preds = %forloop119
  %.not504 = icmp slt i64 %t116.0612, %209
  br i1 %.not504, label %bounds_cont145, label %bounds_fail144

bounds_fail144:                                   ; preds = %bounds_cont140.peel, %bounds_cont140
  tail call void @ts_panic(ptr nonnull @27)
  unreachable

bounds_cont145:                                   ; preds = %bounds_cont140
  %243 = trunc i64 %e.0607 to i32
  %ortmp.i270 = tail call i32 @llvm.fshl.i32(i32 %243, i32 %243, i32 26)
  %ortmp.i280 = tail call i32 @llvm.fshl.i32(i32 %243, i32 %243, i32 21)
  %xortmp122 = xor i32 %ortmp.i270, %ortmp.i280
  %ortmp.i290 = tail call i32 @llvm.fshl.i32(i32 %243, i32 %243, i32 7)
  %xortmp124 = xor i32 %xortmp122, %ortmp.i290
  %bitnot = xor i64 %e.0607, -1
  %andtmp130 = and i64 %g.0605, %bitnot
  %andtmp501 = and i64 %e.0607, %f.0606
  %addtmp134 = add nsw i64 %andtmp501, %h.0604
  %xortmp131 = add nsw i64 %addtmp134, %andtmp130
  %244 = getelementptr i32, ptr %212, i64 %t116.0612
  %245 = load i32, ptr %244, align 4
  %246 = getelementptr i32, ptr %215, i64 %t116.0612
  %247 = load i32, ptr %246, align 4
  %248 = trunc i64 %xortmp131 to i32
  %249 = add i32 %xortmp124, %248
  %250 = add i32 %245, %249
  %251 = add i32 %250, %247
  %252 = trunc i64 %a.0611 to i32
  %ortmp.i300 = tail call i32 @llvm.fshl.i32(i32 %252, i32 %252, i32 30)
  %ortmp.i310 = tail call i32 @llvm.fshl.i32(i32 %252, i32 %252, i32 19)
  %xortmp150 = xor i32 %ortmp.i300, %ortmp.i310
  %ortmp.i320 = tail call i32 @llvm.fshl.i32(i32 %252, i32 %252, i32 10)
  %xortmp152 = xor i32 %xortmp150, %ortmp.i320
  %andtmp159505 = xor i64 %b_.0610, %c.0609
  %xortmp160 = and i64 %andtmp159505, %a.0611
  %andtmp163 = and i64 %b_.0610, %c.0609
  %xortmp164 = xor i64 %xortmp160, %andtmp163
  %253 = trunc i64 %xortmp164 to i32
  %254 = add i32 %xortmp152, %253
  %255 = trunc i64 %d.0608 to i32
  %256 = add i32 %251, %255
  %257 = zext i32 %256 to i64
  %addtmp181 = add i32 %254, %251
  %258 = zext i32 %addtmp181 to i64
  %incdec185 = add nuw nsw i64 %t116.0612, 1
  %exitcond670.not = icmp eq i64 %incdec185, 64
  br i1 %exitcond670.not, label %forafter186, label %forloop119, !llvm.loop !17

forafter186:                                      ; preds = %bounds_cont145
  %addtmp189 = add nsw i64 %h0.0622, %258
  %sext = shl i64 %addtmp189, 32
  %259 = ashr exact i64 %sext, 32
  %addtmp193 = add nsw i64 %a.0611, %h1.0621
  %sext494 = shl i64 %addtmp193, 32
  %260 = ashr exact i64 %sext494, 32
  %addtmp197 = add nsw i64 %b_.0610, %h2.0620
  %sext495 = shl i64 %addtmp197, 32
  %261 = ashr exact i64 %sext495, 32
  %addtmp201 = add nsw i64 %c.0609, %h3.0619
  %sext496 = shl i64 %addtmp201, 32
  %262 = ashr exact i64 %sext496, 32
  %addtmp205 = add nsw i64 %h4.0618, %257
  %sext497 = shl i64 %addtmp205, 32
  %263 = ashr exact i64 %sext497, 32
  %addtmp209 = add nsw i64 %e.0607, %h5.0617
  %sext498 = shl i64 %addtmp209, 32
  %264 = ashr exact i64 %sext498, 32
  %addtmp213 = add nsw i64 %f.0606, %h6.0616
  %sext499 = shl i64 %addtmp213, 32
  %265 = ashr exact i64 %sext499, 32
  %addtmp217 = add nsw i64 %g.0605, %h7.0615
  %sext500 = shl i64 %addtmp217, 32
  %266 = ashr exact i64 %sext500, 32
  %incdec221 = add nuw nsw i64 %b.0614, 1
  %exitcond674.not = icmp eq i64 %incdec221, %addtmp4
  br i1 %exitcond674.not, label %forafter222, label %forcond27.preheader

forafter222:                                      ; preds = %forafter186, %bounds_cont16
  %h7.0.lcssa = phi i64 [ 1541459225, %bounds_cont16 ], [ %266, %forafter186 ]
  %h6.0.lcssa = phi i64 [ 528734635, %bounds_cont16 ], [ %265, %forafter186 ]
  %h5.0.lcssa = phi i64 [ 2600822924, %bounds_cont16 ], [ %264, %forafter186 ]
  %h4.0.lcssa = phi i64 [ 1359893119, %bounds_cont16 ], [ %263, %forafter186 ]
  %h3.0.lcssa = phi i64 [ 2773480762, %bounds_cont16 ], [ %262, %forafter186 ]
  %h2.0.lcssa = phi i64 [ 1013904242, %bounds_cont16 ], [ %261, %forafter186 ]
  %h1.0.lcssa = phi i64 [ 3144134277, %bounds_cont16 ], [ %260, %forafter186 ]
  %h0.0.lcssa = phi i64 [ 1779033703, %bounds_cont16 ], [ %259, %forafter186 ]
  %267 = and i64 %h0.0.lcssa, 4294967295
  %268 = tail call ptr @ts_int_to_string(i64 %267, i64 16)
  %269 = icmp eq ptr %268, null
  br i1 %269, label %null_fail.i, label %null_cont.i

null_fail.i:                                      ; preds = %whileloop.i, %forafter222
  tail call void @ts_panic(ptr nonnull @16)
  unreachable

null_cont.i:                                      ; preds = %forafter222, %whileloop.i
  %s.i.0630 = phi ptr [ %273, %whileloop.i ], [ %268, %forafter222 ]
  %270 = getelementptr inbounds %TsString, ptr %s.i.0630, i64 0, i32 1
  %271 = load i32, ptr %270, align 4, !range !13
  %cmptmp.i = icmp ult i32 %271, 8
  br i1 %cmptmp.i, label %whileloop.i, label %hex_int.exit

whileloop.i:                                      ; preds = %null_cont.i
  %272 = tail call ptr @ts_string_create(ptr nonnull @17)
  %273 = tail call ptr @ts_string_concat(ptr %272, ptr nonnull %s.i.0630)
  %274 = icmp eq ptr %273, null
  br i1 %274, label %null_fail.i, label %null_cont.i

hex_int.exit:                                     ; preds = %null_cont.i
  %275 = and i64 %h1.0.lcssa, 4294967295
  %276 = tail call ptr @ts_int_to_string(i64 %275, i64 16)
  %277 = icmp eq ptr %276, null
  br i1 %277, label %null_fail.i332, label %null_cont.i327

null_fail.i332:                                   ; preds = %whileloop.i330, %hex_int.exit
  tail call void @ts_panic(ptr nonnull @16)
  unreachable

null_cont.i327:                                   ; preds = %hex_int.exit, %whileloop.i330
  %s.i322.0631 = phi ptr [ %281, %whileloop.i330 ], [ %276, %hex_int.exit ]
  %278 = getelementptr inbounds %TsString, ptr %s.i322.0631, i64 0, i32 1
  %279 = load i32, ptr %278, align 4, !range !13
  %cmptmp.i328 = icmp ult i32 %279, 8
  br i1 %cmptmp.i328, label %whileloop.i330, label %hex_int.exit333

whileloop.i330:                                   ; preds = %null_cont.i327
  %280 = tail call ptr @ts_string_create(ptr nonnull @17)
  %281 = tail call ptr @ts_string_concat(ptr %280, ptr nonnull %s.i322.0631)
  %282 = icmp eq ptr %281, null
  br i1 %282, label %null_fail.i332, label %null_cont.i327

hex_int.exit333:                                  ; preds = %null_cont.i327
  %283 = tail call ptr @ts_string_concat(ptr nonnull %s.i.0630, ptr nonnull %s.i322.0631)
  %284 = and i64 %h2.0.lcssa, 4294967295
  %285 = tail call ptr @ts_int_to_string(i64 %284, i64 16)
  %286 = icmp eq ptr %285, null
  br i1 %286, label %null_fail.i344, label %null_cont.i339

null_fail.i344:                                   ; preds = %whileloop.i342, %hex_int.exit333
  tail call void @ts_panic(ptr nonnull @16)
  unreachable

null_cont.i339:                                   ; preds = %hex_int.exit333, %whileloop.i342
  %s.i334.0632 = phi ptr [ %290, %whileloop.i342 ], [ %285, %hex_int.exit333 ]
  %287 = getelementptr inbounds %TsString, ptr %s.i334.0632, i64 0, i32 1
  %288 = load i32, ptr %287, align 4, !range !13
  %cmptmp.i340 = icmp ult i32 %288, 8
  br i1 %cmptmp.i340, label %whileloop.i342, label %hex_int.exit345

whileloop.i342:                                   ; preds = %null_cont.i339
  %289 = tail call ptr @ts_string_create(ptr nonnull @17)
  %290 = tail call ptr @ts_string_concat(ptr %289, ptr nonnull %s.i334.0632)
  %291 = icmp eq ptr %290, null
  br i1 %291, label %null_fail.i344, label %null_cont.i339

hex_int.exit345:                                  ; preds = %null_cont.i339
  %292 = tail call ptr @ts_string_concat(ptr %283, ptr nonnull %s.i334.0632)
  %293 = and i64 %h3.0.lcssa, 4294967295
  %294 = tail call ptr @ts_int_to_string(i64 %293, i64 16)
  %295 = icmp eq ptr %294, null
  br i1 %295, label %null_fail.i356, label %null_cont.i351

null_fail.i356:                                   ; preds = %whileloop.i354, %hex_int.exit345
  tail call void @ts_panic(ptr nonnull @16)
  unreachable

null_cont.i351:                                   ; preds = %hex_int.exit345, %whileloop.i354
  %s.i346.0633 = phi ptr [ %299, %whileloop.i354 ], [ %294, %hex_int.exit345 ]
  %296 = getelementptr inbounds %TsString, ptr %s.i346.0633, i64 0, i32 1
  %297 = load i32, ptr %296, align 4, !range !13
  %cmptmp.i352 = icmp ult i32 %297, 8
  br i1 %cmptmp.i352, label %whileloop.i354, label %hex_int.exit357

whileloop.i354:                                   ; preds = %null_cont.i351
  %298 = tail call ptr @ts_string_create(ptr nonnull @17)
  %299 = tail call ptr @ts_string_concat(ptr %298, ptr nonnull %s.i346.0633)
  %300 = icmp eq ptr %299, null
  br i1 %300, label %null_fail.i356, label %null_cont.i351

hex_int.exit357:                                  ; preds = %null_cont.i351
  %301 = tail call ptr @ts_string_concat(ptr %292, ptr nonnull %s.i346.0633)
  %302 = and i64 %h4.0.lcssa, 4294967295
  %303 = tail call ptr @ts_int_to_string(i64 %302, i64 16)
  %304 = icmp eq ptr %303, null
  br i1 %304, label %null_fail.i368, label %null_cont.i363

null_fail.i368:                                   ; preds = %whileloop.i366, %hex_int.exit357
  tail call void @ts_panic(ptr nonnull @16)
  unreachable

null_cont.i363:                                   ; preds = %hex_int.exit357, %whileloop.i366
  %s.i358.0634 = phi ptr [ %308, %whileloop.i366 ], [ %303, %hex_int.exit357 ]
  %305 = getelementptr inbounds %TsString, ptr %s.i358.0634, i64 0, i32 1
  %306 = load i32, ptr %305, align 4, !range !13
  %cmptmp.i364 = icmp ult i32 %306, 8
  br i1 %cmptmp.i364, label %whileloop.i366, label %hex_int.exit369

whileloop.i366:                                   ; preds = %null_cont.i363
  %307 = tail call ptr @ts_string_create(ptr nonnull @17)
  %308 = tail call ptr @ts_string_concat(ptr %307, ptr nonnull %s.i358.0634)
  %309 = icmp eq ptr %308, null
  br i1 %309, label %null_fail.i368, label %null_cont.i363

hex_int.exit369:                                  ; preds = %null_cont.i363
  %310 = tail call ptr @ts_string_concat(ptr %301, ptr nonnull %s.i358.0634)
  %311 = and i64 %h5.0.lcssa, 4294967295
  %312 = tail call ptr @ts_int_to_string(i64 %311, i64 16)
  %313 = icmp eq ptr %312, null
  br i1 %313, label %null_fail.i380, label %null_cont.i375

null_fail.i380:                                   ; preds = %whileloop.i378, %hex_int.exit369
  tail call void @ts_panic(ptr nonnull @16)
  unreachable

null_cont.i375:                                   ; preds = %hex_int.exit369, %whileloop.i378
  %s.i370.0635 = phi ptr [ %317, %whileloop.i378 ], [ %312, %hex_int.exit369 ]
  %314 = getelementptr inbounds %TsString, ptr %s.i370.0635, i64 0, i32 1
  %315 = load i32, ptr %314, align 4, !range !13
  %cmptmp.i376 = icmp ult i32 %315, 8
  br i1 %cmptmp.i376, label %whileloop.i378, label %hex_int.exit381

whileloop.i378:                                   ; preds = %null_cont.i375
  %316 = tail call ptr @ts_string_create(ptr nonnull @17)
  %317 = tail call ptr @ts_string_concat(ptr %316, ptr nonnull %s.i370.0635)
  %318 = icmp eq ptr %317, null
  br i1 %318, label %null_fail.i380, label %null_cont.i375

hex_int.exit381:                                  ; preds = %null_cont.i375
  %319 = tail call ptr @ts_string_concat(ptr %310, ptr nonnull %s.i370.0635)
  %320 = and i64 %h6.0.lcssa, 4294967295
  %321 = tail call ptr @ts_int_to_string(i64 %320, i64 16)
  %322 = icmp eq ptr %321, null
  br i1 %322, label %null_fail.i392, label %null_cont.i387

null_fail.i392:                                   ; preds = %whileloop.i390, %hex_int.exit381
  tail call void @ts_panic(ptr nonnull @16)
  unreachable

null_cont.i387:                                   ; preds = %hex_int.exit381, %whileloop.i390
  %s.i382.0636 = phi ptr [ %326, %whileloop.i390 ], [ %321, %hex_int.exit381 ]
  %323 = getelementptr inbounds %TsString, ptr %s.i382.0636, i64 0, i32 1
  %324 = load i32, ptr %323, align 4, !range !13
  %cmptmp.i388 = icmp ult i32 %324, 8
  br i1 %cmptmp.i388, label %whileloop.i390, label %hex_int.exit393

whileloop.i390:                                   ; preds = %null_cont.i387
  %325 = tail call ptr @ts_string_create(ptr nonnull @17)
  %326 = tail call ptr @ts_string_concat(ptr %325, ptr nonnull %s.i382.0636)
  %327 = icmp eq ptr %326, null
  br i1 %327, label %null_fail.i392, label %null_cont.i387

hex_int.exit393:                                  ; preds = %null_cont.i387
  %328 = tail call ptr @ts_string_concat(ptr %319, ptr nonnull %s.i382.0636)
  %329 = and i64 %h7.0.lcssa, 4294967295
  %330 = tail call ptr @ts_int_to_string(i64 %329, i64 16)
  %331 = icmp eq ptr %330, null
  br i1 %331, label %null_fail.i404, label %null_cont.i399

null_fail.i404:                                   ; preds = %whileloop.i402, %hex_int.exit393
  tail call void @ts_panic(ptr nonnull @16)
  unreachable

null_cont.i399:                                   ; preds = %hex_int.exit393, %whileloop.i402
  %s.i394.0637 = phi ptr [ %335, %whileloop.i402 ], [ %330, %hex_int.exit393 ]
  %332 = getelementptr inbounds %TsString, ptr %s.i394.0637, i64 0, i32 1
  %333 = load i32, ptr %332, align 4, !range !13
  %cmptmp.i400 = icmp ult i32 %333, 8
  br i1 %cmptmp.i400, label %whileloop.i402, label %hex_int.exit405

whileloop.i402:                                   ; preds = %null_cont.i399
  %334 = tail call ptr @ts_string_create(ptr nonnull @17)
  %335 = tail call ptr @ts_string_concat(ptr %334, ptr nonnull %s.i394.0637)
  %336 = icmp eq ptr %335, null
  br i1 %336, label %null_fail.i404, label %null_cont.i399

hex_int.exit405:                                  ; preds = %null_cont.i399
  %337 = tail call ptr @ts_string_concat(ptr %328, ptr nonnull %s.i394.0637)
  ret ptr %337
}

declare ptr @ts_int_to_string(i64, i64) local_unnamed_addr

declare void @ts_panic(ptr) local_unnamed_addr

declare ptr @ts_string_concat(ptr, ptr) local_unnamed_addr

declare ptr @ts_string_from_int(i64) local_unnamed_addr

declare void @ts_console_log(ptr) local_unnamed_addr

declare i64 @Date_static_now() local_unnamed_addr

declare ptr @ts_double_to_fixed(double, i64) local_unnamed_addr

declare ptr @ts_array_create() local_unnamed_addr

declare void @ts_array_push(ptr, ptr) local_unnamed_addr

declare i64 @ts_string_charCodeAt(ptr, i64) local_unnamed_addr

declare i32 @ts_main(i32, ptr, ptr) local_unnamed_addr #0

define noundef i32 @ts_aot_main(i32 %0, ptr %1) local_unnamed_addr #0 {
entry:
  %2 = tail call i32 @ts_main(i32 %0, ptr %1, ptr nonnull @user_main)
  ret i32 0
}

; Function Attrs: nocallback nofree nosync nounwind speculatable willreturn memory(none)
declare i32 @llvm.fshl.i32(i32, i32, i32) #2

attributes #0 = { "sspstrong" "stack-protector-buffer-size"="8" }
attributes #1 = { mustprogress nofree norecurse nosync nounwind willreturn memory(none) "sspstrong" "stack-protector-buffer-size"="8" }
attributes #2 = { nocallback nofree nosync nounwind speculatable willreturn memory(none) }

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
!10 = !{i64 0, !"__module_init_13540839489060876406"}
!11 = !{i64 0, !"user_main"}
!12 = !{i64 0, !"hex_int"}
!13 = !{i32 0, i32 2147483647}
!14 = !{i64 0, !"__ts_main"}
!15 = !{i64 0, !"rotr_int_int"}
!16 = !{i64 0, !"sha256_str"}
!17 = distinct !{!17, !18}
!18 = !{!"llvm.loop.peeled.count", i32 1}
