; ModuleID = 'ts_module'
source_filename = "ts_module"
target datalayout = "e-m:w-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-windows-msvc"

%ArrayBuffer_VTable = type { ptr, ptr }
%Sign_VTable = type { ptr, ptr, ptr, ptr }
%Zlib_VTable = type { ptr, ptr }
%ServerHttp2Session_VTable = type { ptr, ptr }
%RegExpMatchArray_VTable = type { ptr, ptr }
%Hash_VTable = type { ptr, ptr, ptr, ptr, ptr }
%SocketAddress_VTable = type { ptr, ptr }
%ClientHttp2Stream_VTable = type { ptr, ptr }
%URLSearchParams_VTable = type { ptr, ptr, ptr, ptr, ptr, ptr, ptr, ptr, ptr, ptr, ptr, ptr, ptr, ptr }
%Response_VTable = type { ptr, ptr, ptr, ptr }
%URL_VTable = type { ptr, ptr, ptr, ptr }
%DeflateRaw_VTable = type { ptr, ptr }
%PerformanceObserver_VTable = type { ptr, ptr }
%ClientHttp2Session_VTable = type { ptr, ptr }
%ServerHttp2Stream_VTable = type { ptr, ptr }
%Http2Stream_VTable = type { ptr, ptr }
%Http2Session_VTable = type { ptr, ptr }
%Http2Server_VTable = type { ptr, ptr }
%Http2SecureServer_VTable = type { ptr, ptr }
%Hmac_VTable = type { ptr, ptr, ptr, ptr }
%Cipher_VTable = type { ptr, ptr, ptr, ptr, ptr, ptr }
%Decipher_VTable = type { ptr, ptr, ptr, ptr, ptr, ptr }
%Verify_VTable = type { ptr, ptr, ptr, ptr }
%Script_VTable = type { ptr, ptr, ptr, ptr, ptr, ptr }
%AssertionError_VTable = type { ptr, ptr }
%PerformanceEntry_VTable = type { ptr, ptr }
%TLSSocket_VTable = type { ptr, ptr }
%PerformanceMark_VTable = type { ptr, ptr }
%PerformanceMeasure_VTable = type { ptr, ptr }
%EventLoopUtilization_VTable = type { ptr, ptr }
%PerformanceObserverEntryList_VTable = type { ptr, ptr }
%SecureContext_VTable = type { ptr, ptr }
%Gzip_VTable = type { ptr, ptr }
%Gunzip_VTable = type { ptr, ptr }
%Deflate_VTable = type { ptr, ptr }
%Inflate_VTable = type { ptr, ptr }
%InflateRaw_VTable = type { ptr, ptr }
%Unzip_VTable = type { ptr, ptr }
%BrotliCompress_VTable = type { ptr, ptr }
%BrotliDecompress_VTable = type { ptr, ptr }
%TsValue = type { i8, [7 x i8], i64 }
%ArrayBuffer = type { ptr, i64 }
%Zlib = type { ptr, i64, ptr, ptr, ptr }
%ServerHttp2Session = type { ptr, ptr, ptr, ptr, i1, i1, ptr, i1, i1, ptr, ptr, ptr, ptr, ptr, ptr, ptr, ptr, ptr, i64, ptr }
%RegExpMatchArray = type { ptr, i64, ptr, ptr, i64 }
%SocketAddress = type { ptr, ptr, ptr, i64, i64 }
%URLSearchParams = type { ptr, i64 }
%Response = type { ptr, i1, i64, ptr }
%URL = type { ptr, ptr, ptr, ptr, ptr, ptr, ptr, ptr, ptr, ptr, ptr, ptr, ptr }
%DeflateRaw = type { ptr, i64, ptr, ptr, ptr }
%PerformanceObserver = type { ptr, ptr, ptr, ptr }
%ClientHttp2Session = type { ptr, ptr, ptr, i1, i1, ptr, i1, i1, ptr, ptr, ptr, ptr, ptr, ptr, ptr, ptr, ptr, i64, ptr }
%ServerHttp2Stream = type { ptr, i1, i1 }
%Http2Stream = type { ptr, i1, i64, i1, i1, i1, i64, i1, i64, ptr, ptr, ptr, ptr }
%Http2Session = type { ptr, ptr, ptr, i1, i1, ptr, i1, i1, ptr, ptr, ptr, ptr, ptr, ptr, ptr, ptr, i64, ptr }
%AssertionError = type { ptr, ptr, ptr, ptr, ptr, ptr }
%PerformanceEntry = type { ptr, double, ptr, ptr, double }
%TLSSocket = type { ptr, ptr, ptr, i1, i1, ptr, ptr, ptr, ptr, ptr, ptr, ptr, ptr, ptr, ptr, ptr, ptr, ptr }
%PerformanceMark = type { ptr, double, ptr, ptr, double }
%PerformanceMeasure = type { ptr, double, ptr, ptr, double }
%EventLoopUtilization = type { ptr, double, double, double }
%PerformanceObserverEntryList = type { ptr, ptr, ptr, ptr }
%Gzip = type { ptr, i64, ptr, ptr, ptr }
%Gunzip = type { ptr, i64, ptr, ptr, ptr }
%Deflate = type { ptr, i64, ptr, ptr, ptr, ptr }
%Inflate = type { ptr, i64, ptr, ptr, ptr }
%InflateRaw = type { ptr, i64, ptr, ptr, ptr }
%Unzip = type { ptr, i64, ptr, ptr, ptr }
%BrotliCompress = type { ptr, i64, ptr, ptr, ptr }
%BrotliDecompress = type { ptr, i64, ptr, ptr, ptr }

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
@tls = global ptr null
@Readable = global ptr null
@null = global ptr null
@zlib = global ptr null
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
@tty = global ptr null
@BigInt64Array = global ptr null
@TextDecoder = global ptr null
@String = global ptr null
@DataView = global ptr null
@https = global ptr null
@buffer = global ptr null
@Boolean = global ptr null
@http = global ptr null
@stream = global ptr null
@fs = global ptr null
@path = global ptr null
@net = global ptr null
@http2 = global ptr null
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
@PerformanceObserver = global ptr null
@performance = global ptr null
@perf_hooks = global ptr null
@async_hooks = global ptr null
@child_process = global ptr null
@cluster = global ptr null
@dns = global ptr null
@dgram = global ptr null
@inspector = global ptr null
@readline = global ptr null
@__dirname = global ptr null
@__filename = global ptr null
@__ts_exports = internal global ptr null
@__ts_module = internal global ptr null
@0 = private unnamed_addr constant [11 x i8] c"byteLength\00", align 1
@ArrayBuffer_VTable_Global = constant %ArrayBuffer_VTable { ptr null, ptr @ArrayBuffer_get_property }, !type !0
@Sign_VTable_Global = constant %Sign_VTable { ptr null, ptr @Sign_get_property, ptr @Sign_sign, ptr @Sign_update }, !type !1
@1 = private unnamed_addr constant [13 x i8] c"bytesWritten\00", align 1
@2 = private unnamed_addr constant [6 x i8] c"close\00", align 1
@3 = private unnamed_addr constant [6 x i8] c"flush\00", align 1
@4 = private unnamed_addr constant [6 x i8] c"reset\00", align 1
@Zlib_VTable_Global = constant %Zlib_VTable { ptr null, ptr @Zlib_get_property }, !type !2
@5 = private unnamed_addr constant [13 x i8] c"alpnProtocol\00", align 1
@6 = private unnamed_addr constant [7 x i8] c"altsvc\00", align 1
@7 = private unnamed_addr constant [6 x i8] c"close\00", align 1
@8 = private unnamed_addr constant [7 x i8] c"closed\00", align 1
@9 = private unnamed_addr constant [11 x i8] c"connecting\00", align 1
@10 = private unnamed_addr constant [8 x i8] c"destroy\00", align 1
@11 = private unnamed_addr constant [10 x i8] c"destroyed\00", align 1
@12 = private unnamed_addr constant [10 x i8] c"encrypted\00", align 1
@13 = private unnamed_addr constant [7 x i8] c"goaway\00", align 1
@14 = private unnamed_addr constant [14 x i8] c"localSettings\00", align 1
@15 = private unnamed_addr constant [7 x i8] c"origin\00", align 1
@16 = private unnamed_addr constant [5 x i8] c"ping\00", align 1
@17 = private unnamed_addr constant [4 x i8] c"ref\00", align 1
@18 = private unnamed_addr constant [15 x i8] c"remoteSettings\00", align 1
@19 = private unnamed_addr constant [11 x i8] c"setTimeout\00", align 1
@20 = private unnamed_addr constant [9 x i8] c"settings\00", align 1
@21 = private unnamed_addr constant [7 x i8] c"socket\00", align 1
@22 = private unnamed_addr constant [5 x i8] c"type\00", align 1
@23 = private unnamed_addr constant [6 x i8] c"unref\00", align 1
@ServerHttp2Session_VTable_Global = constant %ServerHttp2Session_VTable { ptr null, ptr @ServerHttp2Session_get_property }, !type !3
@24 = private unnamed_addr constant [6 x i8] c"index\00", align 1
@25 = private unnamed_addr constant [8 x i8] c"indices\00", align 1
@26 = private unnamed_addr constant [6 x i8] c"input\00", align 1
@27 = private unnamed_addr constant [7 x i8] c"length\00", align 1
@RegExpMatchArray_VTable_Global = constant %RegExpMatchArray_VTable { ptr null, ptr @RegExpMatchArray_get_property }, !type !4
@Hash_VTable_Global = constant %Hash_VTable { ptr null, ptr @Hash_get_property, ptr @Hash_copy, ptr @Hash_digest, ptr @Hash_update }, !type !5
@28 = private unnamed_addr constant [8 x i8] c"address\00", align 1
@29 = private unnamed_addr constant [7 x i8] c"family\00", align 1
@30 = private unnamed_addr constant [10 x i8] c"flowlabel\00", align 1
@31 = private unnamed_addr constant [5 x i8] c"port\00", align 1
@SocketAddress_VTable_Global = constant %SocketAddress_VTable { ptr null, ptr @SocketAddress_get_property }, !type !6
@ClientHttp2Stream_VTable_Global = constant %ClientHttp2Stream_VTable { ptr null, ptr @ClientHttp2Stream_get_property }, !type !7
@32 = private unnamed_addr constant [5 x i8] c"size\00", align 1
@URLSearchParams_VTable_Global = constant %URLSearchParams_VTable { ptr null, ptr @URLSearchParams_get_property, ptr @URLSearchParams_append, ptr @URLSearchParams_delete, ptr @URLSearchParams_entries, ptr @URLSearchParams_forEach, ptr @URLSearchParams_get, ptr @URLSearchParams_getAll, ptr @URLSearchParams_has, ptr @URLSearchParams_keys, ptr @URLSearchParams_set, ptr @URLSearchParams_sort, ptr @URLSearchParams_toString, ptr @URLSearchParams_values }, !type !8
@33 = private unnamed_addr constant [3 x i8] c"ok\00", align 1
@34 = private unnamed_addr constant [7 x i8] c"status\00", align 1
@35 = private unnamed_addr constant [11 x i8] c"statusText\00", align 1
@Response_VTable_Global = constant %Response_VTable { ptr null, ptr @Response_get_property, ptr @Response_json, ptr @Response_text }, !type !9
@36 = private unnamed_addr constant [5 x i8] c"hash\00", align 1
@37 = private unnamed_addr constant [5 x i8] c"host\00", align 1
@38 = private unnamed_addr constant [9 x i8] c"hostname\00", align 1
@39 = private unnamed_addr constant [5 x i8] c"href\00", align 1
@40 = private unnamed_addr constant [7 x i8] c"origin\00", align 1
@41 = private unnamed_addr constant [9 x i8] c"password\00", align 1
@42 = private unnamed_addr constant [9 x i8] c"pathname\00", align 1
@43 = private unnamed_addr constant [5 x i8] c"port\00", align 1
@44 = private unnamed_addr constant [9 x i8] c"protocol\00", align 1
@45 = private unnamed_addr constant [7 x i8] c"search\00", align 1
@46 = private unnamed_addr constant [13 x i8] c"searchParams\00", align 1
@47 = private unnamed_addr constant [9 x i8] c"username\00", align 1
@URL_VTable_Global = constant %URL_VTable { ptr null, ptr @URL_get_property, ptr @URL_toJSON, ptr @URL_toString }, !type !10
@48 = private unnamed_addr constant [13 x i8] c"bytesWritten\00", align 1
@49 = private unnamed_addr constant [6 x i8] c"close\00", align 1
@50 = private unnamed_addr constant [6 x i8] c"flush\00", align 1
@51 = private unnamed_addr constant [6 x i8] c"reset\00", align 1
@DeflateRaw_VTable_Global = constant %DeflateRaw_VTable { ptr null, ptr @DeflateRaw_get_property }, !type !11
@52 = private unnamed_addr constant [11 x i8] c"disconnect\00", align 1
@53 = private unnamed_addr constant [8 x i8] c"observe\00", align 1
@54 = private unnamed_addr constant [12 x i8] c"takeRecords\00", align 1
@PerformanceObserver_VTable_Global = constant %PerformanceObserver_VTable { ptr null, ptr @PerformanceObserver_get_property }, !type !12
@55 = private unnamed_addr constant [13 x i8] c"alpnProtocol\00", align 1
@56 = private unnamed_addr constant [6 x i8] c"close\00", align 1
@57 = private unnamed_addr constant [7 x i8] c"closed\00", align 1
@58 = private unnamed_addr constant [11 x i8] c"connecting\00", align 1
@59 = private unnamed_addr constant [8 x i8] c"destroy\00", align 1
@60 = private unnamed_addr constant [10 x i8] c"destroyed\00", align 1
@61 = private unnamed_addr constant [10 x i8] c"encrypted\00", align 1
@62 = private unnamed_addr constant [7 x i8] c"goaway\00", align 1
@63 = private unnamed_addr constant [14 x i8] c"localSettings\00", align 1
@64 = private unnamed_addr constant [5 x i8] c"ping\00", align 1
@65 = private unnamed_addr constant [4 x i8] c"ref\00", align 1
@66 = private unnamed_addr constant [15 x i8] c"remoteSettings\00", align 1
@67 = private unnamed_addr constant [8 x i8] c"request\00", align 1
@68 = private unnamed_addr constant [11 x i8] c"setTimeout\00", align 1
@69 = private unnamed_addr constant [9 x i8] c"settings\00", align 1
@70 = private unnamed_addr constant [7 x i8] c"socket\00", align 1
@71 = private unnamed_addr constant [5 x i8] c"type\00", align 1
@72 = private unnamed_addr constant [6 x i8] c"unref\00", align 1
@ClientHttp2Session_VTable_Global = constant %ClientHttp2Session_VTable { ptr null, ptr @ClientHttp2Session_get_property }, !type !13
@73 = private unnamed_addr constant [12 x i8] c"headersSent\00", align 1
@74 = private unnamed_addr constant [12 x i8] c"pushAllowed\00", align 1
@ServerHttp2Stream_VTable_Global = constant %ServerHttp2Stream_VTable { ptr null, ptr @ServerHttp2Stream_get_property }, !type !14
@75 = private unnamed_addr constant [8 x i8] c"aborted\00", align 1
@76 = private unnamed_addr constant [11 x i8] c"bufferSize\00", align 1
@77 = private unnamed_addr constant [7 x i8] c"closed\00", align 1
@78 = private unnamed_addr constant [10 x i8] c"destroyed\00", align 1
@79 = private unnamed_addr constant [16 x i8] c"endAfterHeaders\00", align 1
@80 = private unnamed_addr constant [3 x i8] c"id\00", align 1
@81 = private unnamed_addr constant [8 x i8] c"pending\00", align 1
@82 = private unnamed_addr constant [8 x i8] c"rstCode\00", align 1
@83 = private unnamed_addr constant [12 x i8] c"sentHeaders\00", align 1
@84 = private unnamed_addr constant [16 x i8] c"sentInfoHeaders\00", align 1
@85 = private unnamed_addr constant [13 x i8] c"sentTrailers\00", align 1
@86 = private unnamed_addr constant [8 x i8] c"session\00", align 1
@Http2Stream_VTable_Global = constant %Http2Stream_VTable { ptr null, ptr @Http2Stream_get_property }, !type !15
@87 = private unnamed_addr constant [13 x i8] c"alpnProtocol\00", align 1
@88 = private unnamed_addr constant [6 x i8] c"close\00", align 1
@89 = private unnamed_addr constant [7 x i8] c"closed\00", align 1
@90 = private unnamed_addr constant [11 x i8] c"connecting\00", align 1
@91 = private unnamed_addr constant [8 x i8] c"destroy\00", align 1
@92 = private unnamed_addr constant [10 x i8] c"destroyed\00", align 1
@93 = private unnamed_addr constant [10 x i8] c"encrypted\00", align 1
@94 = private unnamed_addr constant [7 x i8] c"goaway\00", align 1
@95 = private unnamed_addr constant [14 x i8] c"localSettings\00", align 1
@96 = private unnamed_addr constant [5 x i8] c"ping\00", align 1
@97 = private unnamed_addr constant [4 x i8] c"ref\00", align 1
@98 = private unnamed_addr constant [15 x i8] c"remoteSettings\00", align 1
@99 = private unnamed_addr constant [11 x i8] c"setTimeout\00", align 1
@100 = private unnamed_addr constant [9 x i8] c"settings\00", align 1
@101 = private unnamed_addr constant [7 x i8] c"socket\00", align 1
@102 = private unnamed_addr constant [5 x i8] c"type\00", align 1
@103 = private unnamed_addr constant [6 x i8] c"unref\00", align 1
@Http2Session_VTable_Global = constant %Http2Session_VTable { ptr null, ptr @Http2Session_get_property }, !type !16
@Http2Server_VTable_Global = constant %Http2Server_VTable { ptr null, ptr @Http2Server_get_property }, !type !17
@Http2SecureServer_VTable_Global = constant %Http2SecureServer_VTable { ptr null, ptr @Http2SecureServer_get_property }, !type !18
@Hmac_VTable_Global = constant %Hmac_VTable { ptr null, ptr @Hmac_get_property, ptr @Hmac_digest, ptr @Hmac_update }, !type !19
@Cipher_VTable_Global = constant %Cipher_VTable { ptr null, ptr @Cipher_get_property, ptr @Cipher_final, ptr @Cipher_getAuthTag, ptr @Cipher_setAAD, ptr @Cipher_update }, !type !20
@Decipher_VTable_Global = constant %Decipher_VTable { ptr null, ptr @Decipher_get_property, ptr @Decipher_final, ptr @Decipher_setAAD, ptr @Decipher_setAuthTag, ptr @Decipher_update }, !type !21
@Verify_VTable_Global = constant %Verify_VTable { ptr null, ptr @Verify_get_property, ptr @Verify_update, ptr @Verify_verify }, !type !22
@Script_VTable_Global = constant %Script_VTable { ptr null, ptr @Script_get_property, ptr @Script_createCachedData, ptr @Script_runInContext, ptr @Script_runInNewContext, ptr @Script_runInThisContext }, !type !23
@104 = private unnamed_addr constant [7 x i8] c"actual\00", align 1
@105 = private unnamed_addr constant [5 x i8] c"code\00", align 1
@106 = private unnamed_addr constant [9 x i8] c"expected\00", align 1
@107 = private unnamed_addr constant [8 x i8] c"message\00", align 1
@108 = private unnamed_addr constant [9 x i8] c"operator\00", align 1
@AssertionError_VTable_Global = constant %AssertionError_VTable { ptr null, ptr @AssertionError_get_property }, !type !24
@109 = private unnamed_addr constant [9 x i8] c"duration\00", align 1
@110 = private unnamed_addr constant [10 x i8] c"entryType\00", align 1
@111 = private unnamed_addr constant [5 x i8] c"name\00", align 1
@112 = private unnamed_addr constant [10 x i8] c"startTime\00", align 1
@PerformanceEntry_VTable_Global = constant %PerformanceEntry_VTable { ptr null, ptr @PerformanceEntry_get_property }, !type !25
@113 = private unnamed_addr constant [13 x i8] c"alpnProtocol\00", align 1
@114 = private unnamed_addr constant [19 x i8] c"authorizationError\00", align 1
@115 = private unnamed_addr constant [11 x i8] c"authorized\00", align 1
@116 = private unnamed_addr constant [10 x i8] c"encrypted\00", align 1
@117 = private unnamed_addr constant [15 x i8] c"getCertificate\00", align 1
@118 = private unnamed_addr constant [19 x i8] c"getPeerCertificate\00", align 1
@119 = private unnamed_addr constant [12 x i8] c"getProtocol\00", align 1
@120 = private unnamed_addr constant [14 x i8] c"getServername\00", align 1
@121 = private unnamed_addr constant [11 x i8] c"getSession\00", align 1
@122 = private unnamed_addr constant [16 x i8] c"isSessionReused\00", align 1
@123 = private unnamed_addr constant [12 x i8] c"renegotiate\00", align 1
@124 = private unnamed_addr constant [11 x i8] c"servername\00", align 1
@125 = private unnamed_addr constant [17 x i8] c"setALPNProtocols\00", align 1
@126 = private unnamed_addr constant [21 x i8] c"setClientCertificate\00", align 1
@127 = private unnamed_addr constant [19 x i8] c"setMaxSendFragment\00", align 1
@128 = private unnamed_addr constant [14 x i8] c"setServername\00", align 1
@129 = private unnamed_addr constant [11 x i8] c"setSession\00", align 1
@TLSSocket_VTable_Global = constant %TLSSocket_VTable { ptr null, ptr @TLSSocket_get_property }, !type !26
@130 = private unnamed_addr constant [9 x i8] c"duration\00", align 1
@131 = private unnamed_addr constant [10 x i8] c"entryType\00", align 1
@132 = private unnamed_addr constant [5 x i8] c"name\00", align 1
@133 = private unnamed_addr constant [10 x i8] c"startTime\00", align 1
@PerformanceMark_VTable_Global = constant %PerformanceMark_VTable { ptr null, ptr @PerformanceMark_get_property }, !type !27
@134 = private unnamed_addr constant [9 x i8] c"duration\00", align 1
@135 = private unnamed_addr constant [10 x i8] c"entryType\00", align 1
@136 = private unnamed_addr constant [5 x i8] c"name\00", align 1
@137 = private unnamed_addr constant [10 x i8] c"startTime\00", align 1
@PerformanceMeasure_VTable_Global = constant %PerformanceMeasure_VTable { ptr null, ptr @PerformanceMeasure_get_property }, !type !28
@138 = private unnamed_addr constant [7 x i8] c"active\00", align 1
@139 = private unnamed_addr constant [5 x i8] c"idle\00", align 1
@140 = private unnamed_addr constant [12 x i8] c"utilization\00", align 1
@EventLoopUtilization_VTable_Global = constant %EventLoopUtilization_VTable { ptr null, ptr @EventLoopUtilization_get_property }, !type !29
@141 = private unnamed_addr constant [11 x i8] c"getEntries\00", align 1
@142 = private unnamed_addr constant [17 x i8] c"getEntriesByName\00", align 1
@143 = private unnamed_addr constant [17 x i8] c"getEntriesByType\00", align 1
@PerformanceObserverEntryList_VTable_Global = constant %PerformanceObserverEntryList_VTable { ptr null, ptr @PerformanceObserverEntryList_get_property }, !type !30
@SecureContext_VTable_Global = constant %SecureContext_VTable { ptr null, ptr @SecureContext_get_property }, !type !31
@144 = private unnamed_addr constant [13 x i8] c"bytesWritten\00", align 1
@145 = private unnamed_addr constant [6 x i8] c"close\00", align 1
@146 = private unnamed_addr constant [6 x i8] c"flush\00", align 1
@147 = private unnamed_addr constant [6 x i8] c"reset\00", align 1
@Gzip_VTable_Global = constant %Gzip_VTable { ptr null, ptr @Gzip_get_property }, !type !32
@148 = private unnamed_addr constant [13 x i8] c"bytesWritten\00", align 1
@149 = private unnamed_addr constant [6 x i8] c"close\00", align 1
@150 = private unnamed_addr constant [6 x i8] c"flush\00", align 1
@151 = private unnamed_addr constant [6 x i8] c"reset\00", align 1
@Gunzip_VTable_Global = constant %Gunzip_VTable { ptr null, ptr @Gunzip_get_property }, !type !33
@152 = private unnamed_addr constant [13 x i8] c"bytesWritten\00", align 1
@153 = private unnamed_addr constant [6 x i8] c"close\00", align 1
@154 = private unnamed_addr constant [6 x i8] c"flush\00", align 1
@155 = private unnamed_addr constant [7 x i8] c"params\00", align 1
@156 = private unnamed_addr constant [6 x i8] c"reset\00", align 1
@Deflate_VTable_Global = constant %Deflate_VTable { ptr null, ptr @Deflate_get_property }, !type !34
@157 = private unnamed_addr constant [13 x i8] c"bytesWritten\00", align 1
@158 = private unnamed_addr constant [6 x i8] c"close\00", align 1
@159 = private unnamed_addr constant [6 x i8] c"flush\00", align 1
@160 = private unnamed_addr constant [6 x i8] c"reset\00", align 1
@Inflate_VTable_Global = constant %Inflate_VTable { ptr null, ptr @Inflate_get_property }, !type !35
@161 = private unnamed_addr constant [13 x i8] c"bytesWritten\00", align 1
@162 = private unnamed_addr constant [6 x i8] c"close\00", align 1
@163 = private unnamed_addr constant [6 x i8] c"flush\00", align 1
@164 = private unnamed_addr constant [6 x i8] c"reset\00", align 1
@InflateRaw_VTable_Global = constant %InflateRaw_VTable { ptr null, ptr @InflateRaw_get_property }, !type !36
@165 = private unnamed_addr constant [13 x i8] c"bytesWritten\00", align 1
@166 = private unnamed_addr constant [6 x i8] c"close\00", align 1
@167 = private unnamed_addr constant [6 x i8] c"flush\00", align 1
@168 = private unnamed_addr constant [6 x i8] c"reset\00", align 1
@Unzip_VTable_Global = constant %Unzip_VTable { ptr null, ptr @Unzip_get_property }, !type !37
@169 = private unnamed_addr constant [13 x i8] c"bytesWritten\00", align 1
@170 = private unnamed_addr constant [6 x i8] c"close\00", align 1
@171 = private unnamed_addr constant [6 x i8] c"flush\00", align 1
@172 = private unnamed_addr constant [6 x i8] c"reset\00", align 1
@BrotliCompress_VTable_Global = constant %BrotliCompress_VTable { ptr null, ptr @BrotliCompress_get_property }, !type !38
@173 = private unnamed_addr constant [13 x i8] c"bytesWritten\00", align 1
@174 = private unnamed_addr constant [6 x i8] c"close\00", align 1
@175 = private unnamed_addr constant [6 x i8] c"flush\00", align 1
@176 = private unnamed_addr constant [6 x i8] c"reset\00", align 1
@BrotliDecompress_VTable_Global = constant %BrotliDecompress_VTable { ptr null, ptr @BrotliDecompress_get_property }, !type !39
@177 = private unnamed_addr constant [8 x i8] c"exports\00", align 1
@178 = private unnamed_addr constant [8 x i8] c"exports\00", align 1
@179 = private unnamed_addr constant [6 x i8] c"hello\00", align 1
@180 = private unnamed_addr constant [6 x i8] c"value\00", align 1
@__ts_const_undefined_value = private constant %TsValue zeroinitializer
@181 = private unnamed_addr constant [30 x i8] c"Null or undefined dereference\00", align 1
@182 = private unnamed_addr constant [6 x i8] c"value\00", align 1
@__ts_const_undefined_value.1 = private constant %TsValue zeroinitializer
@183 = private unnamed_addr constant [5 x i8] c"test\00", align 1
@184 = private unnamed_addr constant [5 x i8] c"data\00", align 1
@__ts_const_undefined_value.2 = private constant %TsValue zeroinitializer
@185 = private unnamed_addr constant [6 x i8] c"inner\00", align 1
@__ts_const_undefined_value.3 = private constant %TsValue zeroinitializer
@186 = private unnamed_addr constant [30 x i8] c"Null or undefined dereference\00", align 1
@187 = private unnamed_addr constant [6 x i8] c"inner\00", align 1
@__ts_const_undefined_value.4 = private constant %TsValue zeroinitializer
@188 = private unnamed_addr constant [30 x i8] c"Null or undefined dereference\00", align 1
@189 = private unnamed_addr constant [5 x i8] c"data\00", align 1
@__ts_const_undefined_value.5 = private constant %TsValue zeroinitializer
@190 = private unnamed_addr constant [8 x i8] c"exports\00", align 1
@__ts_const_undefined_value.6 = private constant %TsValue zeroinitializer
@191 = private unnamed_addr constant [93 x i8] c"E:\\src\\github.com\\cgrinker\\ts-aoc\\tests\\golden_ir\\typescript\\assertions\\nonnull_assertion.ts\00", align 1
@192 = private unnamed_addr constant [8 x i8] c"exports\00", align 1

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

define ptr @ArrayBuffer_get_property(ptr %0, ptr %1) #0 {
entry:
  %2 = call ptr @ts_string_create(ptr @0)
  %3 = call i1 @ts_string_eq(ptr %1, ptr %2)
  br i1 %3, label %match_byteLength, label %next_byteLength

match_byteLength:                                 ; preds = %entry
  %4 = getelementptr inbounds %ArrayBuffer, ptr %0, i32 0, i32 1
  %5 = load i64, ptr %4, align 8
  %6 = call ptr @ts_value_make_int(i64 %5)
  ret ptr %6

next_byteLength:                                  ; preds = %entry
  %7 = call ptr @ts_value_make_undefined()
  ret ptr %7
}

declare ptr @ts_string_create(ptr)

declare i1 @ts_string_eq(ptr, ptr)

declare ptr @ts_value_make_undefined()

declare ptr @ts_value_make_int(i64)

define ptr @Sign_get_property(ptr %0, ptr %1) #0 {
entry:
  %2 = call ptr @ts_value_make_undefined()
  ret ptr %2
}

declare ptr @Sign_sign(ptr, ptr, ptr, ptr) #0

declare ptr @Sign_update(ptr, ptr, ptr) #0

define ptr @Zlib_get_property(ptr %0, ptr %1) #0 {
entry:
  %2 = call ptr @ts_string_create(ptr @1)
  %3 = call i1 @ts_string_eq(ptr %1, ptr %2)
  br i1 %3, label %match_bytesWritten, label %next_bytesWritten

match_bytesWritten:                               ; preds = %entry
  %4 = getelementptr inbounds %Zlib, ptr %0, i32 0, i32 1
  %5 = load i64, ptr %4, align 8
  %6 = call ptr @ts_value_make_int(i64 %5)
  ret ptr %6

next_bytesWritten:                                ; preds = %entry
  %7 = call ptr @ts_string_create(ptr @2)
  %8 = call i1 @ts_string_eq(ptr %1, ptr %7)
  br i1 %8, label %match_close, label %next_close

match_close:                                      ; preds = %next_bytesWritten
  %9 = getelementptr inbounds %Zlib, ptr %0, i32 0, i32 2
  %10 = load ptr, ptr %9, align 8
  %11 = call ptr @ts_value_box_any(ptr %10)
  ret ptr %11

next_close:                                       ; preds = %next_bytesWritten
  %12 = call ptr @ts_string_create(ptr @3)
  %13 = call i1 @ts_string_eq(ptr %1, ptr %12)
  br i1 %13, label %match_flush, label %next_flush

match_flush:                                      ; preds = %next_close
  %14 = getelementptr inbounds %Zlib, ptr %0, i32 0, i32 3
  %15 = load ptr, ptr %14, align 8
  %16 = call ptr @ts_value_box_any(ptr %15)
  ret ptr %16

next_flush:                                       ; preds = %next_close
  %17 = call ptr @ts_string_create(ptr @4)
  %18 = call i1 @ts_string_eq(ptr %1, ptr %17)
  br i1 %18, label %match_reset, label %next_reset

match_reset:                                      ; preds = %next_flush
  %19 = getelementptr inbounds %Zlib, ptr %0, i32 0, i32 4
  %20 = load ptr, ptr %19, align 8
  %21 = call ptr @ts_value_box_any(ptr %20)
  ret ptr %21

next_reset:                                       ; preds = %next_flush
  %22 = call ptr @ts_value_make_undefined()
  ret ptr %22
}

declare ptr @ts_value_box_any(ptr)

define ptr @ServerHttp2Session_get_property(ptr %0, ptr %1) #0 {
entry:
  %2 = call ptr @ts_string_create(ptr @5)
  %3 = call i1 @ts_string_eq(ptr %1, ptr %2)
  br i1 %3, label %match_alpnProtocol, label %next_alpnProtocol

match_alpnProtocol:                               ; preds = %entry
  %4 = getelementptr inbounds %ServerHttp2Session, ptr %0, i32 0, i32 1
  %5 = load ptr, ptr %4, align 8
  %6 = call ptr @ts_value_make_string(ptr %5)
  ret ptr %6

next_alpnProtocol:                                ; preds = %entry
  %7 = call ptr @ts_string_create(ptr @6)
  %8 = call i1 @ts_string_eq(ptr %1, ptr %7)
  br i1 %8, label %match_altsvc, label %next_altsvc

match_altsvc:                                     ; preds = %next_alpnProtocol
  %9 = getelementptr inbounds %ServerHttp2Session, ptr %0, i32 0, i32 2
  %10 = load ptr, ptr %9, align 8
  %11 = call ptr @ts_value_box_any(ptr %10)
  ret ptr %11

next_altsvc:                                      ; preds = %next_alpnProtocol
  %12 = call ptr @ts_string_create(ptr @7)
  %13 = call i1 @ts_string_eq(ptr %1, ptr %12)
  br i1 %13, label %match_close, label %next_close

match_close:                                      ; preds = %next_altsvc
  %14 = getelementptr inbounds %ServerHttp2Session, ptr %0, i32 0, i32 3
  %15 = load ptr, ptr %14, align 8
  %16 = call ptr @ts_value_box_any(ptr %15)
  ret ptr %16

next_close:                                       ; preds = %next_altsvc
  %17 = call ptr @ts_string_create(ptr @8)
  %18 = call i1 @ts_string_eq(ptr %1, ptr %17)
  br i1 %18, label %match_closed, label %next_closed

match_closed:                                     ; preds = %next_close
  %19 = getelementptr inbounds %ServerHttp2Session, ptr %0, i32 0, i32 4
  %20 = load i1, ptr %19, align 1
  %21 = call ptr @ts_value_make_bool(i1 %20)
  ret ptr %21

next_closed:                                      ; preds = %next_close
  %22 = call ptr @ts_string_create(ptr @9)
  %23 = call i1 @ts_string_eq(ptr %1, ptr %22)
  br i1 %23, label %match_connecting, label %next_connecting

match_connecting:                                 ; preds = %next_closed
  %24 = getelementptr inbounds %ServerHttp2Session, ptr %0, i32 0, i32 5
  %25 = load i1, ptr %24, align 1
  %26 = call ptr @ts_value_make_bool(i1 %25)
  ret ptr %26

next_connecting:                                  ; preds = %next_closed
  %27 = call ptr @ts_string_create(ptr @10)
  %28 = call i1 @ts_string_eq(ptr %1, ptr %27)
  br i1 %28, label %match_destroy, label %next_destroy

match_destroy:                                    ; preds = %next_connecting
  %29 = getelementptr inbounds %ServerHttp2Session, ptr %0, i32 0, i32 6
  %30 = load ptr, ptr %29, align 8
  %31 = call ptr @ts_value_box_any(ptr %30)
  ret ptr %31

next_destroy:                                     ; preds = %next_connecting
  %32 = call ptr @ts_string_create(ptr @11)
  %33 = call i1 @ts_string_eq(ptr %1, ptr %32)
  br i1 %33, label %match_destroyed, label %next_destroyed

match_destroyed:                                  ; preds = %next_destroy
  %34 = getelementptr inbounds %ServerHttp2Session, ptr %0, i32 0, i32 7
  %35 = load i1, ptr %34, align 1
  %36 = call ptr @ts_value_make_bool(i1 %35)
  ret ptr %36

next_destroyed:                                   ; preds = %next_destroy
  %37 = call ptr @ts_string_create(ptr @12)
  %38 = call i1 @ts_string_eq(ptr %1, ptr %37)
  br i1 %38, label %match_encrypted, label %next_encrypted

match_encrypted:                                  ; preds = %next_destroyed
  %39 = getelementptr inbounds %ServerHttp2Session, ptr %0, i32 0, i32 8
  %40 = load i1, ptr %39, align 1
  %41 = call ptr @ts_value_make_bool(i1 %40)
  ret ptr %41

next_encrypted:                                   ; preds = %next_destroyed
  %42 = call ptr @ts_string_create(ptr @13)
  %43 = call i1 @ts_string_eq(ptr %1, ptr %42)
  br i1 %43, label %match_goaway, label %next_goaway

match_goaway:                                     ; preds = %next_encrypted
  %44 = getelementptr inbounds %ServerHttp2Session, ptr %0, i32 0, i32 9
  %45 = load ptr, ptr %44, align 8
  %46 = call ptr @ts_value_box_any(ptr %45)
  ret ptr %46

next_goaway:                                      ; preds = %next_encrypted
  %47 = call ptr @ts_string_create(ptr @14)
  %48 = call i1 @ts_string_eq(ptr %1, ptr %47)
  br i1 %48, label %match_localSettings, label %next_localSettings

match_localSettings:                              ; preds = %next_goaway
  %49 = getelementptr inbounds %ServerHttp2Session, ptr %0, i32 0, i32 10
  %50 = load ptr, ptr %49, align 8
  %51 = call ptr @ts_value_make_object(ptr %50)
  ret ptr %51

next_localSettings:                               ; preds = %next_goaway
  %52 = call ptr @ts_string_create(ptr @15)
  %53 = call i1 @ts_string_eq(ptr %1, ptr %52)
  br i1 %53, label %match_origin, label %next_origin

match_origin:                                     ; preds = %next_localSettings
  %54 = getelementptr inbounds %ServerHttp2Session, ptr %0, i32 0, i32 11
  %55 = load ptr, ptr %54, align 8
  %56 = call ptr @ts_value_box_any(ptr %55)
  ret ptr %56

next_origin:                                      ; preds = %next_localSettings
  %57 = call ptr @ts_string_create(ptr @16)
  %58 = call i1 @ts_string_eq(ptr %1, ptr %57)
  br i1 %58, label %match_ping, label %next_ping

match_ping:                                       ; preds = %next_origin
  %59 = getelementptr inbounds %ServerHttp2Session, ptr %0, i32 0, i32 12
  %60 = load ptr, ptr %59, align 8
  %61 = call ptr @ts_value_box_any(ptr %60)
  ret ptr %61

next_ping:                                        ; preds = %next_origin
  %62 = call ptr @ts_string_create(ptr @17)
  %63 = call i1 @ts_string_eq(ptr %1, ptr %62)
  br i1 %63, label %match_ref, label %next_ref

match_ref:                                        ; preds = %next_ping
  %64 = getelementptr inbounds %ServerHttp2Session, ptr %0, i32 0, i32 13
  %65 = load ptr, ptr %64, align 8
  %66 = call ptr @ts_value_box_any(ptr %65)
  ret ptr %66

next_ref:                                         ; preds = %next_ping
  %67 = call ptr @ts_string_create(ptr @18)
  %68 = call i1 @ts_string_eq(ptr %1, ptr %67)
  br i1 %68, label %match_remoteSettings, label %next_remoteSettings

match_remoteSettings:                             ; preds = %next_ref
  %69 = getelementptr inbounds %ServerHttp2Session, ptr %0, i32 0, i32 14
  %70 = load ptr, ptr %69, align 8
  %71 = call ptr @ts_value_make_object(ptr %70)
  ret ptr %71

next_remoteSettings:                              ; preds = %next_ref
  %72 = call ptr @ts_string_create(ptr @19)
  %73 = call i1 @ts_string_eq(ptr %1, ptr %72)
  br i1 %73, label %match_setTimeout, label %next_setTimeout

match_setTimeout:                                 ; preds = %next_remoteSettings
  %74 = getelementptr inbounds %ServerHttp2Session, ptr %0, i32 0, i32 15
  %75 = load ptr, ptr %74, align 8
  %76 = call ptr @ts_value_box_any(ptr %75)
  ret ptr %76

next_setTimeout:                                  ; preds = %next_remoteSettings
  %77 = call ptr @ts_string_create(ptr @20)
  %78 = call i1 @ts_string_eq(ptr %1, ptr %77)
  br i1 %78, label %match_settings, label %next_settings

match_settings:                                   ; preds = %next_setTimeout
  %79 = getelementptr inbounds %ServerHttp2Session, ptr %0, i32 0, i32 16
  %80 = load ptr, ptr %79, align 8
  %81 = call ptr @ts_value_box_any(ptr %80)
  ret ptr %81

next_settings:                                    ; preds = %next_setTimeout
  %82 = call ptr @ts_string_create(ptr @21)
  %83 = call i1 @ts_string_eq(ptr %1, ptr %82)
  br i1 %83, label %match_socket, label %next_socket

match_socket:                                     ; preds = %next_settings
  %84 = getelementptr inbounds %ServerHttp2Session, ptr %0, i32 0, i32 17
  %85 = load ptr, ptr %84, align 8
  %86 = call ptr @ts_value_make_object(ptr %85)
  ret ptr %86

next_socket:                                      ; preds = %next_settings
  %87 = call ptr @ts_string_create(ptr @22)
  %88 = call i1 @ts_string_eq(ptr %1, ptr %87)
  br i1 %88, label %match_type, label %next_type

match_type:                                       ; preds = %next_socket
  %89 = getelementptr inbounds %ServerHttp2Session, ptr %0, i32 0, i32 18
  %90 = load i64, ptr %89, align 8
  %91 = call ptr @ts_value_make_int(i64 %90)
  ret ptr %91

next_type:                                        ; preds = %next_socket
  %92 = call ptr @ts_string_create(ptr @23)
  %93 = call i1 @ts_string_eq(ptr %1, ptr %92)
  br i1 %93, label %match_unref, label %next_unref

match_unref:                                      ; preds = %next_type
  %94 = getelementptr inbounds %ServerHttp2Session, ptr %0, i32 0, i32 19
  %95 = load ptr, ptr %94, align 8
  %96 = call ptr @ts_value_box_any(ptr %95)
  ret ptr %96

next_unref:                                       ; preds = %next_type
  %97 = call ptr @ts_value_make_undefined()
  ret ptr %97
}

declare ptr @ts_value_make_string(ptr)

declare ptr @ts_value_make_bool(i1)

declare ptr @ts_value_make_object(ptr)

define ptr @RegExpMatchArray_get_property(ptr %0, ptr %1) #0 {
entry:
  %2 = call ptr @ts_string_create(ptr @24)
  %3 = call i1 @ts_string_eq(ptr %1, ptr %2)
  br i1 %3, label %match_index, label %next_index

match_index:                                      ; preds = %entry
  %4 = getelementptr inbounds %RegExpMatchArray, ptr %0, i32 0, i32 1
  %5 = load i64, ptr %4, align 8
  %6 = call ptr @ts_value_make_int(i64 %5)
  ret ptr %6

next_index:                                       ; preds = %entry
  %7 = call ptr @ts_string_create(ptr @25)
  %8 = call i1 @ts_string_eq(ptr %1, ptr %7)
  br i1 %8, label %match_indices, label %next_indices

match_indices:                                    ; preds = %next_index
  %9 = getelementptr inbounds %RegExpMatchArray, ptr %0, i32 0, i32 2
  %10 = load ptr, ptr %9, align 8
  %11 = call ptr @ts_value_make_array(ptr %10)
  ret ptr %11

next_indices:                                     ; preds = %next_index
  %12 = call ptr @ts_string_create(ptr @26)
  %13 = call i1 @ts_string_eq(ptr %1, ptr %12)
  br i1 %13, label %match_input, label %next_input

match_input:                                      ; preds = %next_indices
  %14 = getelementptr inbounds %RegExpMatchArray, ptr %0, i32 0, i32 3
  %15 = load ptr, ptr %14, align 8
  %16 = call ptr @ts_value_make_string(ptr %15)
  ret ptr %16

next_input:                                       ; preds = %next_indices
  %17 = call ptr @ts_string_create(ptr @27)
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

declare ptr @ts_value_make_array(ptr)

define ptr @Hash_get_property(ptr %0, ptr %1) #0 {
entry:
  %2 = call ptr @ts_value_make_undefined()
  ret ptr %2
}

declare ptr @Hash_copy(ptr, ptr) #0

declare ptr @Hash_digest(ptr, ptr, ptr) #0

declare ptr @Hash_update(ptr, ptr, ptr) #0

define ptr @SocketAddress_get_property(ptr %0, ptr %1) #0 {
entry:
  %2 = call ptr @ts_string_create(ptr @28)
  %3 = call i1 @ts_string_eq(ptr %1, ptr %2)
  br i1 %3, label %match_address, label %next_address

match_address:                                    ; preds = %entry
  %4 = getelementptr inbounds %SocketAddress, ptr %0, i32 0, i32 1
  %5 = load ptr, ptr %4, align 8
  %6 = call ptr @ts_value_make_string(ptr %5)
  ret ptr %6

next_address:                                     ; preds = %entry
  %7 = call ptr @ts_string_create(ptr @29)
  %8 = call i1 @ts_string_eq(ptr %1, ptr %7)
  br i1 %8, label %match_family, label %next_family

match_family:                                     ; preds = %next_address
  %9 = getelementptr inbounds %SocketAddress, ptr %0, i32 0, i32 2
  %10 = load ptr, ptr %9, align 8
  %11 = call ptr @ts_value_make_string(ptr %10)
  ret ptr %11

next_family:                                      ; preds = %next_address
  %12 = call ptr @ts_string_create(ptr @30)
  %13 = call i1 @ts_string_eq(ptr %1, ptr %12)
  br i1 %13, label %match_flowlabel, label %next_flowlabel

match_flowlabel:                                  ; preds = %next_family
  %14 = getelementptr inbounds %SocketAddress, ptr %0, i32 0, i32 3
  %15 = load i64, ptr %14, align 8
  %16 = call ptr @ts_value_make_int(i64 %15)
  ret ptr %16

next_flowlabel:                                   ; preds = %next_family
  %17 = call ptr @ts_string_create(ptr @31)
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

define ptr @ClientHttp2Stream_get_property(ptr %0, ptr %1) #0 {
entry:
  %2 = call ptr @ts_value_make_undefined()
  ret ptr %2
}

define ptr @URLSearchParams_get_property(ptr %0, ptr %1) #0 {
entry:
  %2 = call ptr @ts_string_create(ptr @32)
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
  %2 = call ptr @ts_string_create(ptr @33)
  %3 = call i1 @ts_string_eq(ptr %1, ptr %2)
  br i1 %3, label %match_ok, label %next_ok

match_ok:                                         ; preds = %entry
  %4 = getelementptr inbounds %Response, ptr %0, i32 0, i32 1
  %5 = load i1, ptr %4, align 1
  %6 = call ptr @ts_value_make_bool(i1 %5)
  ret ptr %6

next_ok:                                          ; preds = %entry
  %7 = call ptr @ts_string_create(ptr @34)
  %8 = call i1 @ts_string_eq(ptr %1, ptr %7)
  br i1 %8, label %match_status, label %next_status

match_status:                                     ; preds = %next_ok
  %9 = getelementptr inbounds %Response, ptr %0, i32 0, i32 2
  %10 = load i64, ptr %9, align 8
  %11 = call ptr @ts_value_make_int(i64 %10)
  ret ptr %11

next_status:                                      ; preds = %next_ok
  %12 = call ptr @ts_string_create(ptr @35)
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

declare ptr @Response_json(ptr, ptr) #0

declare ptr @Response_text(ptr, ptr) #0

define ptr @URL_get_property(ptr %0, ptr %1) #0 {
entry:
  %2 = call ptr @ts_string_create(ptr @36)
  %3 = call i1 @ts_string_eq(ptr %1, ptr %2)
  br i1 %3, label %match_hash, label %next_hash

match_hash:                                       ; preds = %entry
  %4 = getelementptr inbounds %URL, ptr %0, i32 0, i32 1
  %5 = load ptr, ptr %4, align 8
  %6 = call ptr @ts_value_make_string(ptr %5)
  ret ptr %6

next_hash:                                        ; preds = %entry
  %7 = call ptr @ts_string_create(ptr @37)
  %8 = call i1 @ts_string_eq(ptr %1, ptr %7)
  br i1 %8, label %match_host, label %next_host

match_host:                                       ; preds = %next_hash
  %9 = getelementptr inbounds %URL, ptr %0, i32 0, i32 2
  %10 = load ptr, ptr %9, align 8
  %11 = call ptr @ts_value_make_string(ptr %10)
  ret ptr %11

next_host:                                        ; preds = %next_hash
  %12 = call ptr @ts_string_create(ptr @38)
  %13 = call i1 @ts_string_eq(ptr %1, ptr %12)
  br i1 %13, label %match_hostname, label %next_hostname

match_hostname:                                   ; preds = %next_host
  %14 = getelementptr inbounds %URL, ptr %0, i32 0, i32 3
  %15 = load ptr, ptr %14, align 8
  %16 = call ptr @ts_value_make_string(ptr %15)
  ret ptr %16

next_hostname:                                    ; preds = %next_host
  %17 = call ptr @ts_string_create(ptr @39)
  %18 = call i1 @ts_string_eq(ptr %1, ptr %17)
  br i1 %18, label %match_href, label %next_href

match_href:                                       ; preds = %next_hostname
  %19 = getelementptr inbounds %URL, ptr %0, i32 0, i32 4
  %20 = load ptr, ptr %19, align 8
  %21 = call ptr @ts_value_make_string(ptr %20)
  ret ptr %21

next_href:                                        ; preds = %next_hostname
  %22 = call ptr @ts_string_create(ptr @40)
  %23 = call i1 @ts_string_eq(ptr %1, ptr %22)
  br i1 %23, label %match_origin, label %next_origin

match_origin:                                     ; preds = %next_href
  %24 = getelementptr inbounds %URL, ptr %0, i32 0, i32 5
  %25 = load ptr, ptr %24, align 8
  %26 = call ptr @ts_value_make_string(ptr %25)
  ret ptr %26

next_origin:                                      ; preds = %next_href
  %27 = call ptr @ts_string_create(ptr @41)
  %28 = call i1 @ts_string_eq(ptr %1, ptr %27)
  br i1 %28, label %match_password, label %next_password

match_password:                                   ; preds = %next_origin
  %29 = getelementptr inbounds %URL, ptr %0, i32 0, i32 6
  %30 = load ptr, ptr %29, align 8
  %31 = call ptr @ts_value_make_string(ptr %30)
  ret ptr %31

next_password:                                    ; preds = %next_origin
  %32 = call ptr @ts_string_create(ptr @42)
  %33 = call i1 @ts_string_eq(ptr %1, ptr %32)
  br i1 %33, label %match_pathname, label %next_pathname

match_pathname:                                   ; preds = %next_password
  %34 = getelementptr inbounds %URL, ptr %0, i32 0, i32 7
  %35 = load ptr, ptr %34, align 8
  %36 = call ptr @ts_value_make_string(ptr %35)
  ret ptr %36

next_pathname:                                    ; preds = %next_password
  %37 = call ptr @ts_string_create(ptr @43)
  %38 = call i1 @ts_string_eq(ptr %1, ptr %37)
  br i1 %38, label %match_port, label %next_port

match_port:                                       ; preds = %next_pathname
  %39 = getelementptr inbounds %URL, ptr %0, i32 0, i32 8
  %40 = load ptr, ptr %39, align 8
  %41 = call ptr @ts_value_make_string(ptr %40)
  ret ptr %41

next_port:                                        ; preds = %next_pathname
  %42 = call ptr @ts_string_create(ptr @44)
  %43 = call i1 @ts_string_eq(ptr %1, ptr %42)
  br i1 %43, label %match_protocol, label %next_protocol

match_protocol:                                   ; preds = %next_port
  %44 = getelementptr inbounds %URL, ptr %0, i32 0, i32 9
  %45 = load ptr, ptr %44, align 8
  %46 = call ptr @ts_value_make_string(ptr %45)
  ret ptr %46

next_protocol:                                    ; preds = %next_port
  %47 = call ptr @ts_string_create(ptr @45)
  %48 = call i1 @ts_string_eq(ptr %1, ptr %47)
  br i1 %48, label %match_search, label %next_search

match_search:                                     ; preds = %next_protocol
  %49 = getelementptr inbounds %URL, ptr %0, i32 0, i32 10
  %50 = load ptr, ptr %49, align 8
  %51 = call ptr @ts_value_make_string(ptr %50)
  ret ptr %51

next_search:                                      ; preds = %next_protocol
  %52 = call ptr @ts_string_create(ptr @46)
  %53 = call i1 @ts_string_eq(ptr %1, ptr %52)
  br i1 %53, label %match_searchParams, label %next_searchParams

match_searchParams:                               ; preds = %next_search
  %54 = getelementptr inbounds %URL, ptr %0, i32 0, i32 11
  %55 = load ptr, ptr %54, align 8
  %56 = call ptr @ts_value_make_object(ptr %55)
  ret ptr %56

next_searchParams:                                ; preds = %next_search
  %57 = call ptr @ts_string_create(ptr @47)
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

declare ptr @URL_toJSON(ptr, ptr) #0

declare ptr @URL_toString(ptr, ptr) #0

define ptr @DeflateRaw_get_property(ptr %0, ptr %1) #0 {
entry:
  %2 = call ptr @ts_string_create(ptr @48)
  %3 = call i1 @ts_string_eq(ptr %1, ptr %2)
  br i1 %3, label %match_bytesWritten, label %next_bytesWritten

match_bytesWritten:                               ; preds = %entry
  %4 = getelementptr inbounds %DeflateRaw, ptr %0, i32 0, i32 1
  %5 = load i64, ptr %4, align 8
  %6 = call ptr @ts_value_make_int(i64 %5)
  ret ptr %6

next_bytesWritten:                                ; preds = %entry
  %7 = call ptr @ts_string_create(ptr @49)
  %8 = call i1 @ts_string_eq(ptr %1, ptr %7)
  br i1 %8, label %match_close, label %next_close

match_close:                                      ; preds = %next_bytesWritten
  %9 = getelementptr inbounds %DeflateRaw, ptr %0, i32 0, i32 2
  %10 = load ptr, ptr %9, align 8
  %11 = call ptr @ts_value_box_any(ptr %10)
  ret ptr %11

next_close:                                       ; preds = %next_bytesWritten
  %12 = call ptr @ts_string_create(ptr @50)
  %13 = call i1 @ts_string_eq(ptr %1, ptr %12)
  br i1 %13, label %match_flush, label %next_flush

match_flush:                                      ; preds = %next_close
  %14 = getelementptr inbounds %DeflateRaw, ptr %0, i32 0, i32 3
  %15 = load ptr, ptr %14, align 8
  %16 = call ptr @ts_value_box_any(ptr %15)
  ret ptr %16

next_flush:                                       ; preds = %next_close
  %17 = call ptr @ts_string_create(ptr @51)
  %18 = call i1 @ts_string_eq(ptr %1, ptr %17)
  br i1 %18, label %match_reset, label %next_reset

match_reset:                                      ; preds = %next_flush
  %19 = getelementptr inbounds %DeflateRaw, ptr %0, i32 0, i32 4
  %20 = load ptr, ptr %19, align 8
  %21 = call ptr @ts_value_box_any(ptr %20)
  ret ptr %21

next_reset:                                       ; preds = %next_flush
  %22 = call ptr @ts_value_make_undefined()
  ret ptr %22
}

define ptr @PerformanceObserver_get_property(ptr %0, ptr %1) #0 {
entry:
  %2 = call ptr @ts_string_create(ptr @52)
  %3 = call i1 @ts_string_eq(ptr %1, ptr %2)
  br i1 %3, label %match_disconnect, label %next_disconnect

match_disconnect:                                 ; preds = %entry
  %4 = getelementptr inbounds %PerformanceObserver, ptr %0, i32 0, i32 1
  %5 = load ptr, ptr %4, align 8
  %6 = call ptr @ts_value_box_any(ptr %5)
  ret ptr %6

next_disconnect:                                  ; preds = %entry
  %7 = call ptr @ts_string_create(ptr @53)
  %8 = call i1 @ts_string_eq(ptr %1, ptr %7)
  br i1 %8, label %match_observe, label %next_observe

match_observe:                                    ; preds = %next_disconnect
  %9 = getelementptr inbounds %PerformanceObserver, ptr %0, i32 0, i32 2
  %10 = load ptr, ptr %9, align 8
  %11 = call ptr @ts_value_box_any(ptr %10)
  ret ptr %11

next_observe:                                     ; preds = %next_disconnect
  %12 = call ptr @ts_string_create(ptr @54)
  %13 = call i1 @ts_string_eq(ptr %1, ptr %12)
  br i1 %13, label %match_takeRecords, label %next_takeRecords

match_takeRecords:                                ; preds = %next_observe
  %14 = getelementptr inbounds %PerformanceObserver, ptr %0, i32 0, i32 3
  %15 = load ptr, ptr %14, align 8
  %16 = call ptr @ts_value_box_any(ptr %15)
  ret ptr %16

next_takeRecords:                                 ; preds = %next_observe
  %17 = call ptr @ts_value_make_undefined()
  ret ptr %17
}

define ptr @ClientHttp2Session_get_property(ptr %0, ptr %1) #0 {
entry:
  %2 = call ptr @ts_string_create(ptr @55)
  %3 = call i1 @ts_string_eq(ptr %1, ptr %2)
  br i1 %3, label %match_alpnProtocol, label %next_alpnProtocol

match_alpnProtocol:                               ; preds = %entry
  %4 = getelementptr inbounds %ClientHttp2Session, ptr %0, i32 0, i32 1
  %5 = load ptr, ptr %4, align 8
  %6 = call ptr @ts_value_make_string(ptr %5)
  ret ptr %6

next_alpnProtocol:                                ; preds = %entry
  %7 = call ptr @ts_string_create(ptr @56)
  %8 = call i1 @ts_string_eq(ptr %1, ptr %7)
  br i1 %8, label %match_close, label %next_close

match_close:                                      ; preds = %next_alpnProtocol
  %9 = getelementptr inbounds %ClientHttp2Session, ptr %0, i32 0, i32 2
  %10 = load ptr, ptr %9, align 8
  %11 = call ptr @ts_value_box_any(ptr %10)
  ret ptr %11

next_close:                                       ; preds = %next_alpnProtocol
  %12 = call ptr @ts_string_create(ptr @57)
  %13 = call i1 @ts_string_eq(ptr %1, ptr %12)
  br i1 %13, label %match_closed, label %next_closed

match_closed:                                     ; preds = %next_close
  %14 = getelementptr inbounds %ClientHttp2Session, ptr %0, i32 0, i32 3
  %15 = load i1, ptr %14, align 1
  %16 = call ptr @ts_value_make_bool(i1 %15)
  ret ptr %16

next_closed:                                      ; preds = %next_close
  %17 = call ptr @ts_string_create(ptr @58)
  %18 = call i1 @ts_string_eq(ptr %1, ptr %17)
  br i1 %18, label %match_connecting, label %next_connecting

match_connecting:                                 ; preds = %next_closed
  %19 = getelementptr inbounds %ClientHttp2Session, ptr %0, i32 0, i32 4
  %20 = load i1, ptr %19, align 1
  %21 = call ptr @ts_value_make_bool(i1 %20)
  ret ptr %21

next_connecting:                                  ; preds = %next_closed
  %22 = call ptr @ts_string_create(ptr @59)
  %23 = call i1 @ts_string_eq(ptr %1, ptr %22)
  br i1 %23, label %match_destroy, label %next_destroy

match_destroy:                                    ; preds = %next_connecting
  %24 = getelementptr inbounds %ClientHttp2Session, ptr %0, i32 0, i32 5
  %25 = load ptr, ptr %24, align 8
  %26 = call ptr @ts_value_box_any(ptr %25)
  ret ptr %26

next_destroy:                                     ; preds = %next_connecting
  %27 = call ptr @ts_string_create(ptr @60)
  %28 = call i1 @ts_string_eq(ptr %1, ptr %27)
  br i1 %28, label %match_destroyed, label %next_destroyed

match_destroyed:                                  ; preds = %next_destroy
  %29 = getelementptr inbounds %ClientHttp2Session, ptr %0, i32 0, i32 6
  %30 = load i1, ptr %29, align 1
  %31 = call ptr @ts_value_make_bool(i1 %30)
  ret ptr %31

next_destroyed:                                   ; preds = %next_destroy
  %32 = call ptr @ts_string_create(ptr @61)
  %33 = call i1 @ts_string_eq(ptr %1, ptr %32)
  br i1 %33, label %match_encrypted, label %next_encrypted

match_encrypted:                                  ; preds = %next_destroyed
  %34 = getelementptr inbounds %ClientHttp2Session, ptr %0, i32 0, i32 7
  %35 = load i1, ptr %34, align 1
  %36 = call ptr @ts_value_make_bool(i1 %35)
  ret ptr %36

next_encrypted:                                   ; preds = %next_destroyed
  %37 = call ptr @ts_string_create(ptr @62)
  %38 = call i1 @ts_string_eq(ptr %1, ptr %37)
  br i1 %38, label %match_goaway, label %next_goaway

match_goaway:                                     ; preds = %next_encrypted
  %39 = getelementptr inbounds %ClientHttp2Session, ptr %0, i32 0, i32 8
  %40 = load ptr, ptr %39, align 8
  %41 = call ptr @ts_value_box_any(ptr %40)
  ret ptr %41

next_goaway:                                      ; preds = %next_encrypted
  %42 = call ptr @ts_string_create(ptr @63)
  %43 = call i1 @ts_string_eq(ptr %1, ptr %42)
  br i1 %43, label %match_localSettings, label %next_localSettings

match_localSettings:                              ; preds = %next_goaway
  %44 = getelementptr inbounds %ClientHttp2Session, ptr %0, i32 0, i32 9
  %45 = load ptr, ptr %44, align 8
  %46 = call ptr @ts_value_make_object(ptr %45)
  ret ptr %46

next_localSettings:                               ; preds = %next_goaway
  %47 = call ptr @ts_string_create(ptr @64)
  %48 = call i1 @ts_string_eq(ptr %1, ptr %47)
  br i1 %48, label %match_ping, label %next_ping

match_ping:                                       ; preds = %next_localSettings
  %49 = getelementptr inbounds %ClientHttp2Session, ptr %0, i32 0, i32 10
  %50 = load ptr, ptr %49, align 8
  %51 = call ptr @ts_value_box_any(ptr %50)
  ret ptr %51

next_ping:                                        ; preds = %next_localSettings
  %52 = call ptr @ts_string_create(ptr @65)
  %53 = call i1 @ts_string_eq(ptr %1, ptr %52)
  br i1 %53, label %match_ref, label %next_ref

match_ref:                                        ; preds = %next_ping
  %54 = getelementptr inbounds %ClientHttp2Session, ptr %0, i32 0, i32 11
  %55 = load ptr, ptr %54, align 8
  %56 = call ptr @ts_value_box_any(ptr %55)
  ret ptr %56

next_ref:                                         ; preds = %next_ping
  %57 = call ptr @ts_string_create(ptr @66)
  %58 = call i1 @ts_string_eq(ptr %1, ptr %57)
  br i1 %58, label %match_remoteSettings, label %next_remoteSettings

match_remoteSettings:                             ; preds = %next_ref
  %59 = getelementptr inbounds %ClientHttp2Session, ptr %0, i32 0, i32 12
  %60 = load ptr, ptr %59, align 8
  %61 = call ptr @ts_value_make_object(ptr %60)
  ret ptr %61

next_remoteSettings:                              ; preds = %next_ref
  %62 = call ptr @ts_string_create(ptr @67)
  %63 = call i1 @ts_string_eq(ptr %1, ptr %62)
  br i1 %63, label %match_request, label %next_request

match_request:                                    ; preds = %next_remoteSettings
  %64 = getelementptr inbounds %ClientHttp2Session, ptr %0, i32 0, i32 13
  %65 = load ptr, ptr %64, align 8
  %66 = call ptr @ts_value_box_any(ptr %65)
  ret ptr %66

next_request:                                     ; preds = %next_remoteSettings
  %67 = call ptr @ts_string_create(ptr @68)
  %68 = call i1 @ts_string_eq(ptr %1, ptr %67)
  br i1 %68, label %match_setTimeout, label %next_setTimeout

match_setTimeout:                                 ; preds = %next_request
  %69 = getelementptr inbounds %ClientHttp2Session, ptr %0, i32 0, i32 14
  %70 = load ptr, ptr %69, align 8
  %71 = call ptr @ts_value_box_any(ptr %70)
  ret ptr %71

next_setTimeout:                                  ; preds = %next_request
  %72 = call ptr @ts_string_create(ptr @69)
  %73 = call i1 @ts_string_eq(ptr %1, ptr %72)
  br i1 %73, label %match_settings, label %next_settings

match_settings:                                   ; preds = %next_setTimeout
  %74 = getelementptr inbounds %ClientHttp2Session, ptr %0, i32 0, i32 15
  %75 = load ptr, ptr %74, align 8
  %76 = call ptr @ts_value_box_any(ptr %75)
  ret ptr %76

next_settings:                                    ; preds = %next_setTimeout
  %77 = call ptr @ts_string_create(ptr @70)
  %78 = call i1 @ts_string_eq(ptr %1, ptr %77)
  br i1 %78, label %match_socket, label %next_socket

match_socket:                                     ; preds = %next_settings
  %79 = getelementptr inbounds %ClientHttp2Session, ptr %0, i32 0, i32 16
  %80 = load ptr, ptr %79, align 8
  %81 = call ptr @ts_value_make_object(ptr %80)
  ret ptr %81

next_socket:                                      ; preds = %next_settings
  %82 = call ptr @ts_string_create(ptr @71)
  %83 = call i1 @ts_string_eq(ptr %1, ptr %82)
  br i1 %83, label %match_type, label %next_type

match_type:                                       ; preds = %next_socket
  %84 = getelementptr inbounds %ClientHttp2Session, ptr %0, i32 0, i32 17
  %85 = load i64, ptr %84, align 8
  %86 = call ptr @ts_value_make_int(i64 %85)
  ret ptr %86

next_type:                                        ; preds = %next_socket
  %87 = call ptr @ts_string_create(ptr @72)
  %88 = call i1 @ts_string_eq(ptr %1, ptr %87)
  br i1 %88, label %match_unref, label %next_unref

match_unref:                                      ; preds = %next_type
  %89 = getelementptr inbounds %ClientHttp2Session, ptr %0, i32 0, i32 18
  %90 = load ptr, ptr %89, align 8
  %91 = call ptr @ts_value_box_any(ptr %90)
  ret ptr %91

next_unref:                                       ; preds = %next_type
  %92 = call ptr @ts_value_make_undefined()
  ret ptr %92
}

define ptr @ServerHttp2Stream_get_property(ptr %0, ptr %1) #0 {
entry:
  %2 = call ptr @ts_string_create(ptr @73)
  %3 = call i1 @ts_string_eq(ptr %1, ptr %2)
  br i1 %3, label %match_headersSent, label %next_headersSent

match_headersSent:                                ; preds = %entry
  %4 = getelementptr inbounds %ServerHttp2Stream, ptr %0, i32 0, i32 1
  %5 = load i1, ptr %4, align 1
  %6 = call ptr @ts_value_make_bool(i1 %5)
  ret ptr %6

next_headersSent:                                 ; preds = %entry
  %7 = call ptr @ts_string_create(ptr @74)
  %8 = call i1 @ts_string_eq(ptr %1, ptr %7)
  br i1 %8, label %match_pushAllowed, label %next_pushAllowed

match_pushAllowed:                                ; preds = %next_headersSent
  %9 = getelementptr inbounds %ServerHttp2Stream, ptr %0, i32 0, i32 2
  %10 = load i1, ptr %9, align 1
  %11 = call ptr @ts_value_make_bool(i1 %10)
  ret ptr %11

next_pushAllowed:                                 ; preds = %next_headersSent
  %12 = call ptr @ts_value_make_undefined()
  ret ptr %12
}

define ptr @Http2Stream_get_property(ptr %0, ptr %1) #0 {
entry:
  %2 = call ptr @ts_string_create(ptr @75)
  %3 = call i1 @ts_string_eq(ptr %1, ptr %2)
  br i1 %3, label %match_aborted, label %next_aborted

match_aborted:                                    ; preds = %entry
  %4 = getelementptr inbounds %Http2Stream, ptr %0, i32 0, i32 1
  %5 = load i1, ptr %4, align 1
  %6 = call ptr @ts_value_make_bool(i1 %5)
  ret ptr %6

next_aborted:                                     ; preds = %entry
  %7 = call ptr @ts_string_create(ptr @76)
  %8 = call i1 @ts_string_eq(ptr %1, ptr %7)
  br i1 %8, label %match_bufferSize, label %next_bufferSize

match_bufferSize:                                 ; preds = %next_aborted
  %9 = getelementptr inbounds %Http2Stream, ptr %0, i32 0, i32 2
  %10 = load i64, ptr %9, align 8
  %11 = call ptr @ts_value_make_int(i64 %10)
  ret ptr %11

next_bufferSize:                                  ; preds = %next_aborted
  %12 = call ptr @ts_string_create(ptr @77)
  %13 = call i1 @ts_string_eq(ptr %1, ptr %12)
  br i1 %13, label %match_closed, label %next_closed

match_closed:                                     ; preds = %next_bufferSize
  %14 = getelementptr inbounds %Http2Stream, ptr %0, i32 0, i32 3
  %15 = load i1, ptr %14, align 1
  %16 = call ptr @ts_value_make_bool(i1 %15)
  ret ptr %16

next_closed:                                      ; preds = %next_bufferSize
  %17 = call ptr @ts_string_create(ptr @78)
  %18 = call i1 @ts_string_eq(ptr %1, ptr %17)
  br i1 %18, label %match_destroyed, label %next_destroyed

match_destroyed:                                  ; preds = %next_closed
  %19 = getelementptr inbounds %Http2Stream, ptr %0, i32 0, i32 4
  %20 = load i1, ptr %19, align 1
  %21 = call ptr @ts_value_make_bool(i1 %20)
  ret ptr %21

next_destroyed:                                   ; preds = %next_closed
  %22 = call ptr @ts_string_create(ptr @79)
  %23 = call i1 @ts_string_eq(ptr %1, ptr %22)
  br i1 %23, label %match_endAfterHeaders, label %next_endAfterHeaders

match_endAfterHeaders:                            ; preds = %next_destroyed
  %24 = getelementptr inbounds %Http2Stream, ptr %0, i32 0, i32 5
  %25 = load i1, ptr %24, align 1
  %26 = call ptr @ts_value_make_bool(i1 %25)
  ret ptr %26

next_endAfterHeaders:                             ; preds = %next_destroyed
  %27 = call ptr @ts_string_create(ptr @80)
  %28 = call i1 @ts_string_eq(ptr %1, ptr %27)
  br i1 %28, label %match_id, label %next_id

match_id:                                         ; preds = %next_endAfterHeaders
  %29 = getelementptr inbounds %Http2Stream, ptr %0, i32 0, i32 6
  %30 = load i64, ptr %29, align 8
  %31 = call ptr @ts_value_make_int(i64 %30)
  ret ptr %31

next_id:                                          ; preds = %next_endAfterHeaders
  %32 = call ptr @ts_string_create(ptr @81)
  %33 = call i1 @ts_string_eq(ptr %1, ptr %32)
  br i1 %33, label %match_pending, label %next_pending

match_pending:                                    ; preds = %next_id
  %34 = getelementptr inbounds %Http2Stream, ptr %0, i32 0, i32 7
  %35 = load i1, ptr %34, align 1
  %36 = call ptr @ts_value_make_bool(i1 %35)
  ret ptr %36

next_pending:                                     ; preds = %next_id
  %37 = call ptr @ts_string_create(ptr @82)
  %38 = call i1 @ts_string_eq(ptr %1, ptr %37)
  br i1 %38, label %match_rstCode, label %next_rstCode

match_rstCode:                                    ; preds = %next_pending
  %39 = getelementptr inbounds %Http2Stream, ptr %0, i32 0, i32 8
  %40 = load i64, ptr %39, align 8
  %41 = call ptr @ts_value_make_int(i64 %40)
  ret ptr %41

next_rstCode:                                     ; preds = %next_pending
  %42 = call ptr @ts_string_create(ptr @83)
  %43 = call i1 @ts_string_eq(ptr %1, ptr %42)
  br i1 %43, label %match_sentHeaders, label %next_sentHeaders

match_sentHeaders:                                ; preds = %next_rstCode
  %44 = getelementptr inbounds %Http2Stream, ptr %0, i32 0, i32 9
  %45 = load ptr, ptr %44, align 8
  %46 = call ptr @ts_value_box_any(ptr %45)
  ret ptr %46

next_sentHeaders:                                 ; preds = %next_rstCode
  %47 = call ptr @ts_string_create(ptr @84)
  %48 = call i1 @ts_string_eq(ptr %1, ptr %47)
  br i1 %48, label %match_sentInfoHeaders, label %next_sentInfoHeaders

match_sentInfoHeaders:                            ; preds = %next_sentHeaders
  %49 = getelementptr inbounds %Http2Stream, ptr %0, i32 0, i32 10
  %50 = load ptr, ptr %49, align 8
  %51 = call ptr @ts_value_make_array(ptr %50)
  ret ptr %51

next_sentInfoHeaders:                             ; preds = %next_sentHeaders
  %52 = call ptr @ts_string_create(ptr @85)
  %53 = call i1 @ts_string_eq(ptr %1, ptr %52)
  br i1 %53, label %match_sentTrailers, label %next_sentTrailers

match_sentTrailers:                               ; preds = %next_sentInfoHeaders
  %54 = getelementptr inbounds %Http2Stream, ptr %0, i32 0, i32 11
  %55 = load ptr, ptr %54, align 8
  %56 = call ptr @ts_value_box_any(ptr %55)
  ret ptr %56

next_sentTrailers:                                ; preds = %next_sentInfoHeaders
  %57 = call ptr @ts_string_create(ptr @86)
  %58 = call i1 @ts_string_eq(ptr %1, ptr %57)
  br i1 %58, label %match_session, label %next_session

match_session:                                    ; preds = %next_sentTrailers
  %59 = getelementptr inbounds %Http2Stream, ptr %0, i32 0, i32 12
  %60 = load ptr, ptr %59, align 8
  %61 = call ptr @ts_value_make_object(ptr %60)
  ret ptr %61

next_session:                                     ; preds = %next_sentTrailers
  %62 = call ptr @ts_value_make_undefined()
  ret ptr %62
}

define ptr @Http2Session_get_property(ptr %0, ptr %1) #0 {
entry:
  %2 = call ptr @ts_string_create(ptr @87)
  %3 = call i1 @ts_string_eq(ptr %1, ptr %2)
  br i1 %3, label %match_alpnProtocol, label %next_alpnProtocol

match_alpnProtocol:                               ; preds = %entry
  %4 = getelementptr inbounds %Http2Session, ptr %0, i32 0, i32 1
  %5 = load ptr, ptr %4, align 8
  %6 = call ptr @ts_value_make_string(ptr %5)
  ret ptr %6

next_alpnProtocol:                                ; preds = %entry
  %7 = call ptr @ts_string_create(ptr @88)
  %8 = call i1 @ts_string_eq(ptr %1, ptr %7)
  br i1 %8, label %match_close, label %next_close

match_close:                                      ; preds = %next_alpnProtocol
  %9 = getelementptr inbounds %Http2Session, ptr %0, i32 0, i32 2
  %10 = load ptr, ptr %9, align 8
  %11 = call ptr @ts_value_box_any(ptr %10)
  ret ptr %11

next_close:                                       ; preds = %next_alpnProtocol
  %12 = call ptr @ts_string_create(ptr @89)
  %13 = call i1 @ts_string_eq(ptr %1, ptr %12)
  br i1 %13, label %match_closed, label %next_closed

match_closed:                                     ; preds = %next_close
  %14 = getelementptr inbounds %Http2Session, ptr %0, i32 0, i32 3
  %15 = load i1, ptr %14, align 1
  %16 = call ptr @ts_value_make_bool(i1 %15)
  ret ptr %16

next_closed:                                      ; preds = %next_close
  %17 = call ptr @ts_string_create(ptr @90)
  %18 = call i1 @ts_string_eq(ptr %1, ptr %17)
  br i1 %18, label %match_connecting, label %next_connecting

match_connecting:                                 ; preds = %next_closed
  %19 = getelementptr inbounds %Http2Session, ptr %0, i32 0, i32 4
  %20 = load i1, ptr %19, align 1
  %21 = call ptr @ts_value_make_bool(i1 %20)
  ret ptr %21

next_connecting:                                  ; preds = %next_closed
  %22 = call ptr @ts_string_create(ptr @91)
  %23 = call i1 @ts_string_eq(ptr %1, ptr %22)
  br i1 %23, label %match_destroy, label %next_destroy

match_destroy:                                    ; preds = %next_connecting
  %24 = getelementptr inbounds %Http2Session, ptr %0, i32 0, i32 5
  %25 = load ptr, ptr %24, align 8
  %26 = call ptr @ts_value_box_any(ptr %25)
  ret ptr %26

next_destroy:                                     ; preds = %next_connecting
  %27 = call ptr @ts_string_create(ptr @92)
  %28 = call i1 @ts_string_eq(ptr %1, ptr %27)
  br i1 %28, label %match_destroyed, label %next_destroyed

match_destroyed:                                  ; preds = %next_destroy
  %29 = getelementptr inbounds %Http2Session, ptr %0, i32 0, i32 6
  %30 = load i1, ptr %29, align 1
  %31 = call ptr @ts_value_make_bool(i1 %30)
  ret ptr %31

next_destroyed:                                   ; preds = %next_destroy
  %32 = call ptr @ts_string_create(ptr @93)
  %33 = call i1 @ts_string_eq(ptr %1, ptr %32)
  br i1 %33, label %match_encrypted, label %next_encrypted

match_encrypted:                                  ; preds = %next_destroyed
  %34 = getelementptr inbounds %Http2Session, ptr %0, i32 0, i32 7
  %35 = load i1, ptr %34, align 1
  %36 = call ptr @ts_value_make_bool(i1 %35)
  ret ptr %36

next_encrypted:                                   ; preds = %next_destroyed
  %37 = call ptr @ts_string_create(ptr @94)
  %38 = call i1 @ts_string_eq(ptr %1, ptr %37)
  br i1 %38, label %match_goaway, label %next_goaway

match_goaway:                                     ; preds = %next_encrypted
  %39 = getelementptr inbounds %Http2Session, ptr %0, i32 0, i32 8
  %40 = load ptr, ptr %39, align 8
  %41 = call ptr @ts_value_box_any(ptr %40)
  ret ptr %41

next_goaway:                                      ; preds = %next_encrypted
  %42 = call ptr @ts_string_create(ptr @95)
  %43 = call i1 @ts_string_eq(ptr %1, ptr %42)
  br i1 %43, label %match_localSettings, label %next_localSettings

match_localSettings:                              ; preds = %next_goaway
  %44 = getelementptr inbounds %Http2Session, ptr %0, i32 0, i32 9
  %45 = load ptr, ptr %44, align 8
  %46 = call ptr @ts_value_make_object(ptr %45)
  ret ptr %46

next_localSettings:                               ; preds = %next_goaway
  %47 = call ptr @ts_string_create(ptr @96)
  %48 = call i1 @ts_string_eq(ptr %1, ptr %47)
  br i1 %48, label %match_ping, label %next_ping

match_ping:                                       ; preds = %next_localSettings
  %49 = getelementptr inbounds %Http2Session, ptr %0, i32 0, i32 10
  %50 = load ptr, ptr %49, align 8
  %51 = call ptr @ts_value_box_any(ptr %50)
  ret ptr %51

next_ping:                                        ; preds = %next_localSettings
  %52 = call ptr @ts_string_create(ptr @97)
  %53 = call i1 @ts_string_eq(ptr %1, ptr %52)
  br i1 %53, label %match_ref, label %next_ref

match_ref:                                        ; preds = %next_ping
  %54 = getelementptr inbounds %Http2Session, ptr %0, i32 0, i32 11
  %55 = load ptr, ptr %54, align 8
  %56 = call ptr @ts_value_box_any(ptr %55)
  ret ptr %56

next_ref:                                         ; preds = %next_ping
  %57 = call ptr @ts_string_create(ptr @98)
  %58 = call i1 @ts_string_eq(ptr %1, ptr %57)
  br i1 %58, label %match_remoteSettings, label %next_remoteSettings

match_remoteSettings:                             ; preds = %next_ref
  %59 = getelementptr inbounds %Http2Session, ptr %0, i32 0, i32 12
  %60 = load ptr, ptr %59, align 8
  %61 = call ptr @ts_value_make_object(ptr %60)
  ret ptr %61

next_remoteSettings:                              ; preds = %next_ref
  %62 = call ptr @ts_string_create(ptr @99)
  %63 = call i1 @ts_string_eq(ptr %1, ptr %62)
  br i1 %63, label %match_setTimeout, label %next_setTimeout

match_setTimeout:                                 ; preds = %next_remoteSettings
  %64 = getelementptr inbounds %Http2Session, ptr %0, i32 0, i32 13
  %65 = load ptr, ptr %64, align 8
  %66 = call ptr @ts_value_box_any(ptr %65)
  ret ptr %66

next_setTimeout:                                  ; preds = %next_remoteSettings
  %67 = call ptr @ts_string_create(ptr @100)
  %68 = call i1 @ts_string_eq(ptr %1, ptr %67)
  br i1 %68, label %match_settings, label %next_settings

match_settings:                                   ; preds = %next_setTimeout
  %69 = getelementptr inbounds %Http2Session, ptr %0, i32 0, i32 14
  %70 = load ptr, ptr %69, align 8
  %71 = call ptr @ts_value_box_any(ptr %70)
  ret ptr %71

next_settings:                                    ; preds = %next_setTimeout
  %72 = call ptr @ts_string_create(ptr @101)
  %73 = call i1 @ts_string_eq(ptr %1, ptr %72)
  br i1 %73, label %match_socket, label %next_socket

match_socket:                                     ; preds = %next_settings
  %74 = getelementptr inbounds %Http2Session, ptr %0, i32 0, i32 15
  %75 = load ptr, ptr %74, align 8
  %76 = call ptr @ts_value_make_object(ptr %75)
  ret ptr %76

next_socket:                                      ; preds = %next_settings
  %77 = call ptr @ts_string_create(ptr @102)
  %78 = call i1 @ts_string_eq(ptr %1, ptr %77)
  br i1 %78, label %match_type, label %next_type

match_type:                                       ; preds = %next_socket
  %79 = getelementptr inbounds %Http2Session, ptr %0, i32 0, i32 16
  %80 = load i64, ptr %79, align 8
  %81 = call ptr @ts_value_make_int(i64 %80)
  ret ptr %81

next_type:                                        ; preds = %next_socket
  %82 = call ptr @ts_string_create(ptr @103)
  %83 = call i1 @ts_string_eq(ptr %1, ptr %82)
  br i1 %83, label %match_unref, label %next_unref

match_unref:                                      ; preds = %next_type
  %84 = getelementptr inbounds %Http2Session, ptr %0, i32 0, i32 17
  %85 = load ptr, ptr %84, align 8
  %86 = call ptr @ts_value_box_any(ptr %85)
  ret ptr %86

next_unref:                                       ; preds = %next_type
  %87 = call ptr @ts_value_make_undefined()
  ret ptr %87
}

define ptr @Http2Server_get_property(ptr %0, ptr %1) #0 {
entry:
  %2 = call ptr @ts_value_make_undefined()
  ret ptr %2
}

define ptr @Http2SecureServer_get_property(ptr %0, ptr %1) #0 {
entry:
  %2 = call ptr @ts_value_make_undefined()
  ret ptr %2
}

define ptr @Hmac_get_property(ptr %0, ptr %1) #0 {
entry:
  %2 = call ptr @ts_value_make_undefined()
  ret ptr %2
}

declare ptr @Hmac_digest(ptr, ptr, ptr) #0

declare ptr @Hmac_update(ptr, ptr, ptr) #0

define ptr @Cipher_get_property(ptr %0, ptr %1) #0 {
entry:
  %2 = call ptr @ts_value_make_undefined()
  ret ptr %2
}

declare ptr @Cipher_final(ptr, ptr) #0

declare ptr @Cipher_getAuthTag(ptr, ptr) #0

declare ptr @Cipher_setAAD(ptr, ptr, ptr) #0

declare ptr @Cipher_update(ptr, ptr, ptr) #0

define ptr @Decipher_get_property(ptr %0, ptr %1) #0 {
entry:
  %2 = call ptr @ts_value_make_undefined()
  ret ptr %2
}

declare ptr @Decipher_final(ptr, ptr) #0

declare ptr @Decipher_setAAD(ptr, ptr, ptr) #0

declare ptr @Decipher_setAuthTag(ptr, ptr, ptr) #0

declare ptr @Decipher_update(ptr, ptr, ptr) #0

define ptr @Verify_get_property(ptr %0, ptr %1) #0 {
entry:
  %2 = call ptr @ts_value_make_undefined()
  ret ptr %2
}

declare ptr @Verify_update(ptr, ptr, ptr) #0

declare i1 @Verify_verify(ptr, ptr, ptr, ptr, ptr) #0

define ptr @Script_get_property(ptr %0, ptr %1) #0 {
entry:
  %2 = call ptr @ts_value_make_undefined()
  ret ptr %2
}

declare ptr @Script_createCachedData(ptr, ptr) #0

declare ptr @Script_runInContext(ptr, ptr, ptr, ptr) #0

declare ptr @Script_runInNewContext(ptr, ptr, ptr, ptr) #0

declare ptr @Script_runInThisContext(ptr, ptr, ptr) #0

define ptr @AssertionError_get_property(ptr %0, ptr %1) #0 {
entry:
  %2 = call ptr @ts_string_create(ptr @104)
  %3 = call i1 @ts_string_eq(ptr %1, ptr %2)
  br i1 %3, label %match_actual, label %next_actual

match_actual:                                     ; preds = %entry
  %4 = getelementptr inbounds %AssertionError, ptr %0, i32 0, i32 1
  %5 = load ptr, ptr %4, align 8
  %6 = call ptr @ts_value_box_any(ptr %5)
  ret ptr %6

next_actual:                                      ; preds = %entry
  %7 = call ptr @ts_string_create(ptr @105)
  %8 = call i1 @ts_string_eq(ptr %1, ptr %7)
  br i1 %8, label %match_code, label %next_code

match_code:                                       ; preds = %next_actual
  %9 = getelementptr inbounds %AssertionError, ptr %0, i32 0, i32 2
  %10 = load ptr, ptr %9, align 8
  %11 = call ptr @ts_value_make_string(ptr %10)
  ret ptr %11

next_code:                                        ; preds = %next_actual
  %12 = call ptr @ts_string_create(ptr @106)
  %13 = call i1 @ts_string_eq(ptr %1, ptr %12)
  br i1 %13, label %match_expected, label %next_expected

match_expected:                                   ; preds = %next_code
  %14 = getelementptr inbounds %AssertionError, ptr %0, i32 0, i32 3
  %15 = load ptr, ptr %14, align 8
  %16 = call ptr @ts_value_box_any(ptr %15)
  ret ptr %16

next_expected:                                    ; preds = %next_code
  %17 = call ptr @ts_string_create(ptr @107)
  %18 = call i1 @ts_string_eq(ptr %1, ptr %17)
  br i1 %18, label %match_message, label %next_message

match_message:                                    ; preds = %next_expected
  %19 = getelementptr inbounds %AssertionError, ptr %0, i32 0, i32 4
  %20 = load ptr, ptr %19, align 8
  %21 = call ptr @ts_value_make_string(ptr %20)
  ret ptr %21

next_message:                                     ; preds = %next_expected
  %22 = call ptr @ts_string_create(ptr @108)
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

define ptr @PerformanceEntry_get_property(ptr %0, ptr %1) #0 {
entry:
  %2 = call ptr @ts_string_create(ptr @109)
  %3 = call i1 @ts_string_eq(ptr %1, ptr %2)
  br i1 %3, label %match_duration, label %next_duration

match_duration:                                   ; preds = %entry
  %4 = getelementptr inbounds %PerformanceEntry, ptr %0, i32 0, i32 1
  %5 = load double, ptr %4, align 8
  %6 = call ptr @ts_value_make_double(double %5)
  ret ptr %6

next_duration:                                    ; preds = %entry
  %7 = call ptr @ts_string_create(ptr @110)
  %8 = call i1 @ts_string_eq(ptr %1, ptr %7)
  br i1 %8, label %match_entryType, label %next_entryType

match_entryType:                                  ; preds = %next_duration
  %9 = getelementptr inbounds %PerformanceEntry, ptr %0, i32 0, i32 2
  %10 = load ptr, ptr %9, align 8
  %11 = call ptr @ts_value_make_string(ptr %10)
  ret ptr %11

next_entryType:                                   ; preds = %next_duration
  %12 = call ptr @ts_string_create(ptr @111)
  %13 = call i1 @ts_string_eq(ptr %1, ptr %12)
  br i1 %13, label %match_name, label %next_name

match_name:                                       ; preds = %next_entryType
  %14 = getelementptr inbounds %PerformanceEntry, ptr %0, i32 0, i32 3
  %15 = load ptr, ptr %14, align 8
  %16 = call ptr @ts_value_make_string(ptr %15)
  ret ptr %16

next_name:                                        ; preds = %next_entryType
  %17 = call ptr @ts_string_create(ptr @112)
  %18 = call i1 @ts_string_eq(ptr %1, ptr %17)
  br i1 %18, label %match_startTime, label %next_startTime

match_startTime:                                  ; preds = %next_name
  %19 = getelementptr inbounds %PerformanceEntry, ptr %0, i32 0, i32 4
  %20 = load double, ptr %19, align 8
  %21 = call ptr @ts_value_make_double(double %20)
  ret ptr %21

next_startTime:                                   ; preds = %next_name
  %22 = call ptr @ts_value_make_undefined()
  ret ptr %22
}

declare ptr @ts_value_make_double(double)

define ptr @TLSSocket_get_property(ptr %0, ptr %1) #0 {
entry:
  %2 = call ptr @ts_string_create(ptr @113)
  %3 = call i1 @ts_string_eq(ptr %1, ptr %2)
  br i1 %3, label %match_alpnProtocol, label %next_alpnProtocol

match_alpnProtocol:                               ; preds = %entry
  %4 = getelementptr inbounds %TLSSocket, ptr %0, i32 0, i32 1
  %5 = load ptr, ptr %4, align 8
  %6 = call ptr @ts_value_make_string(ptr %5)
  ret ptr %6

next_alpnProtocol:                                ; preds = %entry
  %7 = call ptr @ts_string_create(ptr @114)
  %8 = call i1 @ts_string_eq(ptr %1, ptr %7)
  br i1 %8, label %match_authorizationError, label %next_authorizationError

match_authorizationError:                         ; preds = %next_alpnProtocol
  %9 = getelementptr inbounds %TLSSocket, ptr %0, i32 0, i32 2
  %10 = load ptr, ptr %9, align 8
  %11 = call ptr @ts_value_make_string(ptr %10)
  ret ptr %11

next_authorizationError:                          ; preds = %next_alpnProtocol
  %12 = call ptr @ts_string_create(ptr @115)
  %13 = call i1 @ts_string_eq(ptr %1, ptr %12)
  br i1 %13, label %match_authorized, label %next_authorized

match_authorized:                                 ; preds = %next_authorizationError
  %14 = getelementptr inbounds %TLSSocket, ptr %0, i32 0, i32 3
  %15 = load i1, ptr %14, align 1
  %16 = call ptr @ts_value_make_bool(i1 %15)
  ret ptr %16

next_authorized:                                  ; preds = %next_authorizationError
  %17 = call ptr @ts_string_create(ptr @116)
  %18 = call i1 @ts_string_eq(ptr %1, ptr %17)
  br i1 %18, label %match_encrypted, label %next_encrypted

match_encrypted:                                  ; preds = %next_authorized
  %19 = getelementptr inbounds %TLSSocket, ptr %0, i32 0, i32 4
  %20 = load i1, ptr %19, align 1
  %21 = call ptr @ts_value_make_bool(i1 %20)
  ret ptr %21

next_encrypted:                                   ; preds = %next_authorized
  %22 = call ptr @ts_string_create(ptr @117)
  %23 = call i1 @ts_string_eq(ptr %1, ptr %22)
  br i1 %23, label %match_getCertificate, label %next_getCertificate

match_getCertificate:                             ; preds = %next_encrypted
  %24 = getelementptr inbounds %TLSSocket, ptr %0, i32 0, i32 5
  %25 = load ptr, ptr %24, align 8
  %26 = call ptr @ts_value_box_any(ptr %25)
  ret ptr %26

next_getCertificate:                              ; preds = %next_encrypted
  %27 = call ptr @ts_string_create(ptr @118)
  %28 = call i1 @ts_string_eq(ptr %1, ptr %27)
  br i1 %28, label %match_getPeerCertificate, label %next_getPeerCertificate

match_getPeerCertificate:                         ; preds = %next_getCertificate
  %29 = getelementptr inbounds %TLSSocket, ptr %0, i32 0, i32 6
  %30 = load ptr, ptr %29, align 8
  %31 = call ptr @ts_value_box_any(ptr %30)
  ret ptr %31

next_getPeerCertificate:                          ; preds = %next_getCertificate
  %32 = call ptr @ts_string_create(ptr @119)
  %33 = call i1 @ts_string_eq(ptr %1, ptr %32)
  br i1 %33, label %match_getProtocol, label %next_getProtocol

match_getProtocol:                                ; preds = %next_getPeerCertificate
  %34 = getelementptr inbounds %TLSSocket, ptr %0, i32 0, i32 7
  %35 = load ptr, ptr %34, align 8
  %36 = call ptr @ts_value_box_any(ptr %35)
  ret ptr %36

next_getProtocol:                                 ; preds = %next_getPeerCertificate
  %37 = call ptr @ts_string_create(ptr @120)
  %38 = call i1 @ts_string_eq(ptr %1, ptr %37)
  br i1 %38, label %match_getServername, label %next_getServername

match_getServername:                              ; preds = %next_getProtocol
  %39 = getelementptr inbounds %TLSSocket, ptr %0, i32 0, i32 8
  %40 = load ptr, ptr %39, align 8
  %41 = call ptr @ts_value_box_any(ptr %40)
  ret ptr %41

next_getServername:                               ; preds = %next_getProtocol
  %42 = call ptr @ts_string_create(ptr @121)
  %43 = call i1 @ts_string_eq(ptr %1, ptr %42)
  br i1 %43, label %match_getSession, label %next_getSession

match_getSession:                                 ; preds = %next_getServername
  %44 = getelementptr inbounds %TLSSocket, ptr %0, i32 0, i32 9
  %45 = load ptr, ptr %44, align 8
  %46 = call ptr @ts_value_box_any(ptr %45)
  ret ptr %46

next_getSession:                                  ; preds = %next_getServername
  %47 = call ptr @ts_string_create(ptr @122)
  %48 = call i1 @ts_string_eq(ptr %1, ptr %47)
  br i1 %48, label %match_isSessionReused, label %next_isSessionReused

match_isSessionReused:                            ; preds = %next_getSession
  %49 = getelementptr inbounds %TLSSocket, ptr %0, i32 0, i32 10
  %50 = load ptr, ptr %49, align 8
  %51 = call ptr @ts_value_box_any(ptr %50)
  ret ptr %51

next_isSessionReused:                             ; preds = %next_getSession
  %52 = call ptr @ts_string_create(ptr @123)
  %53 = call i1 @ts_string_eq(ptr %1, ptr %52)
  br i1 %53, label %match_renegotiate, label %next_renegotiate

match_renegotiate:                                ; preds = %next_isSessionReused
  %54 = getelementptr inbounds %TLSSocket, ptr %0, i32 0, i32 11
  %55 = load ptr, ptr %54, align 8
  %56 = call ptr @ts_value_box_any(ptr %55)
  ret ptr %56

next_renegotiate:                                 ; preds = %next_isSessionReused
  %57 = call ptr @ts_string_create(ptr @124)
  %58 = call i1 @ts_string_eq(ptr %1, ptr %57)
  br i1 %58, label %match_servername, label %next_servername

match_servername:                                 ; preds = %next_renegotiate
  %59 = getelementptr inbounds %TLSSocket, ptr %0, i32 0, i32 12
  %60 = load ptr, ptr %59, align 8
  %61 = call ptr @ts_value_make_string(ptr %60)
  ret ptr %61

next_servername:                                  ; preds = %next_renegotiate
  %62 = call ptr @ts_string_create(ptr @125)
  %63 = call i1 @ts_string_eq(ptr %1, ptr %62)
  br i1 %63, label %match_setALPNProtocols, label %next_setALPNProtocols

match_setALPNProtocols:                           ; preds = %next_servername
  %64 = getelementptr inbounds %TLSSocket, ptr %0, i32 0, i32 13
  %65 = load ptr, ptr %64, align 8
  %66 = call ptr @ts_value_box_any(ptr %65)
  ret ptr %66

next_setALPNProtocols:                            ; preds = %next_servername
  %67 = call ptr @ts_string_create(ptr @126)
  %68 = call i1 @ts_string_eq(ptr %1, ptr %67)
  br i1 %68, label %match_setClientCertificate, label %next_setClientCertificate

match_setClientCertificate:                       ; preds = %next_setALPNProtocols
  %69 = getelementptr inbounds %TLSSocket, ptr %0, i32 0, i32 14
  %70 = load ptr, ptr %69, align 8
  %71 = call ptr @ts_value_box_any(ptr %70)
  ret ptr %71

next_setClientCertificate:                        ; preds = %next_setALPNProtocols
  %72 = call ptr @ts_string_create(ptr @127)
  %73 = call i1 @ts_string_eq(ptr %1, ptr %72)
  br i1 %73, label %match_setMaxSendFragment, label %next_setMaxSendFragment

match_setMaxSendFragment:                         ; preds = %next_setClientCertificate
  %74 = getelementptr inbounds %TLSSocket, ptr %0, i32 0, i32 15
  %75 = load ptr, ptr %74, align 8
  %76 = call ptr @ts_value_box_any(ptr %75)
  ret ptr %76

next_setMaxSendFragment:                          ; preds = %next_setClientCertificate
  %77 = call ptr @ts_string_create(ptr @128)
  %78 = call i1 @ts_string_eq(ptr %1, ptr %77)
  br i1 %78, label %match_setServername, label %next_setServername

match_setServername:                              ; preds = %next_setMaxSendFragment
  %79 = getelementptr inbounds %TLSSocket, ptr %0, i32 0, i32 16
  %80 = load ptr, ptr %79, align 8
  %81 = call ptr @ts_value_box_any(ptr %80)
  ret ptr %81

next_setServername:                               ; preds = %next_setMaxSendFragment
  %82 = call ptr @ts_string_create(ptr @129)
  %83 = call i1 @ts_string_eq(ptr %1, ptr %82)
  br i1 %83, label %match_setSession, label %next_setSession

match_setSession:                                 ; preds = %next_setServername
  %84 = getelementptr inbounds %TLSSocket, ptr %0, i32 0, i32 17
  %85 = load ptr, ptr %84, align 8
  %86 = call ptr @ts_value_box_any(ptr %85)
  ret ptr %86

next_setSession:                                  ; preds = %next_setServername
  %87 = call ptr @ts_value_make_undefined()
  ret ptr %87
}

define ptr @PerformanceMark_get_property(ptr %0, ptr %1) #0 {
entry:
  %2 = call ptr @ts_string_create(ptr @130)
  %3 = call i1 @ts_string_eq(ptr %1, ptr %2)
  br i1 %3, label %match_duration, label %next_duration

match_duration:                                   ; preds = %entry
  %4 = getelementptr inbounds %PerformanceMark, ptr %0, i32 0, i32 1
  %5 = load double, ptr %4, align 8
  %6 = call ptr @ts_value_make_double(double %5)
  ret ptr %6

next_duration:                                    ; preds = %entry
  %7 = call ptr @ts_string_create(ptr @131)
  %8 = call i1 @ts_string_eq(ptr %1, ptr %7)
  br i1 %8, label %match_entryType, label %next_entryType

match_entryType:                                  ; preds = %next_duration
  %9 = getelementptr inbounds %PerformanceMark, ptr %0, i32 0, i32 2
  %10 = load ptr, ptr %9, align 8
  %11 = call ptr @ts_value_make_string(ptr %10)
  ret ptr %11

next_entryType:                                   ; preds = %next_duration
  %12 = call ptr @ts_string_create(ptr @132)
  %13 = call i1 @ts_string_eq(ptr %1, ptr %12)
  br i1 %13, label %match_name, label %next_name

match_name:                                       ; preds = %next_entryType
  %14 = getelementptr inbounds %PerformanceMark, ptr %0, i32 0, i32 3
  %15 = load ptr, ptr %14, align 8
  %16 = call ptr @ts_value_make_string(ptr %15)
  ret ptr %16

next_name:                                        ; preds = %next_entryType
  %17 = call ptr @ts_string_create(ptr @133)
  %18 = call i1 @ts_string_eq(ptr %1, ptr %17)
  br i1 %18, label %match_startTime, label %next_startTime

match_startTime:                                  ; preds = %next_name
  %19 = getelementptr inbounds %PerformanceMark, ptr %0, i32 0, i32 4
  %20 = load double, ptr %19, align 8
  %21 = call ptr @ts_value_make_double(double %20)
  ret ptr %21

next_startTime:                                   ; preds = %next_name
  %22 = call ptr @ts_value_make_undefined()
  ret ptr %22
}

define ptr @PerformanceMeasure_get_property(ptr %0, ptr %1) #0 {
entry:
  %2 = call ptr @ts_string_create(ptr @134)
  %3 = call i1 @ts_string_eq(ptr %1, ptr %2)
  br i1 %3, label %match_duration, label %next_duration

match_duration:                                   ; preds = %entry
  %4 = getelementptr inbounds %PerformanceMeasure, ptr %0, i32 0, i32 1
  %5 = load double, ptr %4, align 8
  %6 = call ptr @ts_value_make_double(double %5)
  ret ptr %6

next_duration:                                    ; preds = %entry
  %7 = call ptr @ts_string_create(ptr @135)
  %8 = call i1 @ts_string_eq(ptr %1, ptr %7)
  br i1 %8, label %match_entryType, label %next_entryType

match_entryType:                                  ; preds = %next_duration
  %9 = getelementptr inbounds %PerformanceMeasure, ptr %0, i32 0, i32 2
  %10 = load ptr, ptr %9, align 8
  %11 = call ptr @ts_value_make_string(ptr %10)
  ret ptr %11

next_entryType:                                   ; preds = %next_duration
  %12 = call ptr @ts_string_create(ptr @136)
  %13 = call i1 @ts_string_eq(ptr %1, ptr %12)
  br i1 %13, label %match_name, label %next_name

match_name:                                       ; preds = %next_entryType
  %14 = getelementptr inbounds %PerformanceMeasure, ptr %0, i32 0, i32 3
  %15 = load ptr, ptr %14, align 8
  %16 = call ptr @ts_value_make_string(ptr %15)
  ret ptr %16

next_name:                                        ; preds = %next_entryType
  %17 = call ptr @ts_string_create(ptr @137)
  %18 = call i1 @ts_string_eq(ptr %1, ptr %17)
  br i1 %18, label %match_startTime, label %next_startTime

match_startTime:                                  ; preds = %next_name
  %19 = getelementptr inbounds %PerformanceMeasure, ptr %0, i32 0, i32 4
  %20 = load double, ptr %19, align 8
  %21 = call ptr @ts_value_make_double(double %20)
  ret ptr %21

next_startTime:                                   ; preds = %next_name
  %22 = call ptr @ts_value_make_undefined()
  ret ptr %22
}

define ptr @EventLoopUtilization_get_property(ptr %0, ptr %1) #0 {
entry:
  %2 = call ptr @ts_string_create(ptr @138)
  %3 = call i1 @ts_string_eq(ptr %1, ptr %2)
  br i1 %3, label %match_active, label %next_active

match_active:                                     ; preds = %entry
  %4 = getelementptr inbounds %EventLoopUtilization, ptr %0, i32 0, i32 1
  %5 = load double, ptr %4, align 8
  %6 = call ptr @ts_value_make_double(double %5)
  ret ptr %6

next_active:                                      ; preds = %entry
  %7 = call ptr @ts_string_create(ptr @139)
  %8 = call i1 @ts_string_eq(ptr %1, ptr %7)
  br i1 %8, label %match_idle, label %next_idle

match_idle:                                       ; preds = %next_active
  %9 = getelementptr inbounds %EventLoopUtilization, ptr %0, i32 0, i32 2
  %10 = load double, ptr %9, align 8
  %11 = call ptr @ts_value_make_double(double %10)
  ret ptr %11

next_idle:                                        ; preds = %next_active
  %12 = call ptr @ts_string_create(ptr @140)
  %13 = call i1 @ts_string_eq(ptr %1, ptr %12)
  br i1 %13, label %match_utilization, label %next_utilization

match_utilization:                                ; preds = %next_idle
  %14 = getelementptr inbounds %EventLoopUtilization, ptr %0, i32 0, i32 3
  %15 = load double, ptr %14, align 8
  %16 = call ptr @ts_value_make_double(double %15)
  ret ptr %16

next_utilization:                                 ; preds = %next_idle
  %17 = call ptr @ts_value_make_undefined()
  ret ptr %17
}

define ptr @PerformanceObserverEntryList_get_property(ptr %0, ptr %1) #0 {
entry:
  %2 = call ptr @ts_string_create(ptr @141)
  %3 = call i1 @ts_string_eq(ptr %1, ptr %2)
  br i1 %3, label %match_getEntries, label %next_getEntries

match_getEntries:                                 ; preds = %entry
  %4 = getelementptr inbounds %PerformanceObserverEntryList, ptr %0, i32 0, i32 1
  %5 = load ptr, ptr %4, align 8
  %6 = call ptr @ts_value_box_any(ptr %5)
  ret ptr %6

next_getEntries:                                  ; preds = %entry
  %7 = call ptr @ts_string_create(ptr @142)
  %8 = call i1 @ts_string_eq(ptr %1, ptr %7)
  br i1 %8, label %match_getEntriesByName, label %next_getEntriesByName

match_getEntriesByName:                           ; preds = %next_getEntries
  %9 = getelementptr inbounds %PerformanceObserverEntryList, ptr %0, i32 0, i32 2
  %10 = load ptr, ptr %9, align 8
  %11 = call ptr @ts_value_box_any(ptr %10)
  ret ptr %11

next_getEntriesByName:                            ; preds = %next_getEntries
  %12 = call ptr @ts_string_create(ptr @143)
  %13 = call i1 @ts_string_eq(ptr %1, ptr %12)
  br i1 %13, label %match_getEntriesByType, label %next_getEntriesByType

match_getEntriesByType:                           ; preds = %next_getEntriesByName
  %14 = getelementptr inbounds %PerformanceObserverEntryList, ptr %0, i32 0, i32 3
  %15 = load ptr, ptr %14, align 8
  %16 = call ptr @ts_value_box_any(ptr %15)
  ret ptr %16

next_getEntriesByType:                            ; preds = %next_getEntriesByName
  %17 = call ptr @ts_value_make_undefined()
  ret ptr %17
}

define ptr @SecureContext_get_property(ptr %0, ptr %1) #0 {
entry:
  %2 = call ptr @ts_value_make_undefined()
  ret ptr %2
}

define ptr @Gzip_get_property(ptr %0, ptr %1) #0 {
entry:
  %2 = call ptr @ts_string_create(ptr @144)
  %3 = call i1 @ts_string_eq(ptr %1, ptr %2)
  br i1 %3, label %match_bytesWritten, label %next_bytesWritten

match_bytesWritten:                               ; preds = %entry
  %4 = getelementptr inbounds %Gzip, ptr %0, i32 0, i32 1
  %5 = load i64, ptr %4, align 8
  %6 = call ptr @ts_value_make_int(i64 %5)
  ret ptr %6

next_bytesWritten:                                ; preds = %entry
  %7 = call ptr @ts_string_create(ptr @145)
  %8 = call i1 @ts_string_eq(ptr %1, ptr %7)
  br i1 %8, label %match_close, label %next_close

match_close:                                      ; preds = %next_bytesWritten
  %9 = getelementptr inbounds %Gzip, ptr %0, i32 0, i32 2
  %10 = load ptr, ptr %9, align 8
  %11 = call ptr @ts_value_box_any(ptr %10)
  ret ptr %11

next_close:                                       ; preds = %next_bytesWritten
  %12 = call ptr @ts_string_create(ptr @146)
  %13 = call i1 @ts_string_eq(ptr %1, ptr %12)
  br i1 %13, label %match_flush, label %next_flush

match_flush:                                      ; preds = %next_close
  %14 = getelementptr inbounds %Gzip, ptr %0, i32 0, i32 3
  %15 = load ptr, ptr %14, align 8
  %16 = call ptr @ts_value_box_any(ptr %15)
  ret ptr %16

next_flush:                                       ; preds = %next_close
  %17 = call ptr @ts_string_create(ptr @147)
  %18 = call i1 @ts_string_eq(ptr %1, ptr %17)
  br i1 %18, label %match_reset, label %next_reset

match_reset:                                      ; preds = %next_flush
  %19 = getelementptr inbounds %Gzip, ptr %0, i32 0, i32 4
  %20 = load ptr, ptr %19, align 8
  %21 = call ptr @ts_value_box_any(ptr %20)
  ret ptr %21

next_reset:                                       ; preds = %next_flush
  %22 = call ptr @ts_value_make_undefined()
  ret ptr %22
}

define ptr @Gunzip_get_property(ptr %0, ptr %1) #0 {
entry:
  %2 = call ptr @ts_string_create(ptr @148)
  %3 = call i1 @ts_string_eq(ptr %1, ptr %2)
  br i1 %3, label %match_bytesWritten, label %next_bytesWritten

match_bytesWritten:                               ; preds = %entry
  %4 = getelementptr inbounds %Gunzip, ptr %0, i32 0, i32 1
  %5 = load i64, ptr %4, align 8
  %6 = call ptr @ts_value_make_int(i64 %5)
  ret ptr %6

next_bytesWritten:                                ; preds = %entry
  %7 = call ptr @ts_string_create(ptr @149)
  %8 = call i1 @ts_string_eq(ptr %1, ptr %7)
  br i1 %8, label %match_close, label %next_close

match_close:                                      ; preds = %next_bytesWritten
  %9 = getelementptr inbounds %Gunzip, ptr %0, i32 0, i32 2
  %10 = load ptr, ptr %9, align 8
  %11 = call ptr @ts_value_box_any(ptr %10)
  ret ptr %11

next_close:                                       ; preds = %next_bytesWritten
  %12 = call ptr @ts_string_create(ptr @150)
  %13 = call i1 @ts_string_eq(ptr %1, ptr %12)
  br i1 %13, label %match_flush, label %next_flush

match_flush:                                      ; preds = %next_close
  %14 = getelementptr inbounds %Gunzip, ptr %0, i32 0, i32 3
  %15 = load ptr, ptr %14, align 8
  %16 = call ptr @ts_value_box_any(ptr %15)
  ret ptr %16

next_flush:                                       ; preds = %next_close
  %17 = call ptr @ts_string_create(ptr @151)
  %18 = call i1 @ts_string_eq(ptr %1, ptr %17)
  br i1 %18, label %match_reset, label %next_reset

match_reset:                                      ; preds = %next_flush
  %19 = getelementptr inbounds %Gunzip, ptr %0, i32 0, i32 4
  %20 = load ptr, ptr %19, align 8
  %21 = call ptr @ts_value_box_any(ptr %20)
  ret ptr %21

next_reset:                                       ; preds = %next_flush
  %22 = call ptr @ts_value_make_undefined()
  ret ptr %22
}

define ptr @Deflate_get_property(ptr %0, ptr %1) #0 {
entry:
  %2 = call ptr @ts_string_create(ptr @152)
  %3 = call i1 @ts_string_eq(ptr %1, ptr %2)
  br i1 %3, label %match_bytesWritten, label %next_bytesWritten

match_bytesWritten:                               ; preds = %entry
  %4 = getelementptr inbounds %Deflate, ptr %0, i32 0, i32 1
  %5 = load i64, ptr %4, align 8
  %6 = call ptr @ts_value_make_int(i64 %5)
  ret ptr %6

next_bytesWritten:                                ; preds = %entry
  %7 = call ptr @ts_string_create(ptr @153)
  %8 = call i1 @ts_string_eq(ptr %1, ptr %7)
  br i1 %8, label %match_close, label %next_close

match_close:                                      ; preds = %next_bytesWritten
  %9 = getelementptr inbounds %Deflate, ptr %0, i32 0, i32 2
  %10 = load ptr, ptr %9, align 8
  %11 = call ptr @ts_value_box_any(ptr %10)
  ret ptr %11

next_close:                                       ; preds = %next_bytesWritten
  %12 = call ptr @ts_string_create(ptr @154)
  %13 = call i1 @ts_string_eq(ptr %1, ptr %12)
  br i1 %13, label %match_flush, label %next_flush

match_flush:                                      ; preds = %next_close
  %14 = getelementptr inbounds %Deflate, ptr %0, i32 0, i32 3
  %15 = load ptr, ptr %14, align 8
  %16 = call ptr @ts_value_box_any(ptr %15)
  ret ptr %16

next_flush:                                       ; preds = %next_close
  %17 = call ptr @ts_string_create(ptr @155)
  %18 = call i1 @ts_string_eq(ptr %1, ptr %17)
  br i1 %18, label %match_params, label %next_params

match_params:                                     ; preds = %next_flush
  %19 = getelementptr inbounds %Deflate, ptr %0, i32 0, i32 4
  %20 = load ptr, ptr %19, align 8
  %21 = call ptr @ts_value_box_any(ptr %20)
  ret ptr %21

next_params:                                      ; preds = %next_flush
  %22 = call ptr @ts_string_create(ptr @156)
  %23 = call i1 @ts_string_eq(ptr %1, ptr %22)
  br i1 %23, label %match_reset, label %next_reset

match_reset:                                      ; preds = %next_params
  %24 = getelementptr inbounds %Deflate, ptr %0, i32 0, i32 5
  %25 = load ptr, ptr %24, align 8
  %26 = call ptr @ts_value_box_any(ptr %25)
  ret ptr %26

next_reset:                                       ; preds = %next_params
  %27 = call ptr @ts_value_make_undefined()
  ret ptr %27
}

define ptr @Inflate_get_property(ptr %0, ptr %1) #0 {
entry:
  %2 = call ptr @ts_string_create(ptr @157)
  %3 = call i1 @ts_string_eq(ptr %1, ptr %2)
  br i1 %3, label %match_bytesWritten, label %next_bytesWritten

match_bytesWritten:                               ; preds = %entry
  %4 = getelementptr inbounds %Inflate, ptr %0, i32 0, i32 1
  %5 = load i64, ptr %4, align 8
  %6 = call ptr @ts_value_make_int(i64 %5)
  ret ptr %6

next_bytesWritten:                                ; preds = %entry
  %7 = call ptr @ts_string_create(ptr @158)
  %8 = call i1 @ts_string_eq(ptr %1, ptr %7)
  br i1 %8, label %match_close, label %next_close

match_close:                                      ; preds = %next_bytesWritten
  %9 = getelementptr inbounds %Inflate, ptr %0, i32 0, i32 2
  %10 = load ptr, ptr %9, align 8
  %11 = call ptr @ts_value_box_any(ptr %10)
  ret ptr %11

next_close:                                       ; preds = %next_bytesWritten
  %12 = call ptr @ts_string_create(ptr @159)
  %13 = call i1 @ts_string_eq(ptr %1, ptr %12)
  br i1 %13, label %match_flush, label %next_flush

match_flush:                                      ; preds = %next_close
  %14 = getelementptr inbounds %Inflate, ptr %0, i32 0, i32 3
  %15 = load ptr, ptr %14, align 8
  %16 = call ptr @ts_value_box_any(ptr %15)
  ret ptr %16

next_flush:                                       ; preds = %next_close
  %17 = call ptr @ts_string_create(ptr @160)
  %18 = call i1 @ts_string_eq(ptr %1, ptr %17)
  br i1 %18, label %match_reset, label %next_reset

match_reset:                                      ; preds = %next_flush
  %19 = getelementptr inbounds %Inflate, ptr %0, i32 0, i32 4
  %20 = load ptr, ptr %19, align 8
  %21 = call ptr @ts_value_box_any(ptr %20)
  ret ptr %21

next_reset:                                       ; preds = %next_flush
  %22 = call ptr @ts_value_make_undefined()
  ret ptr %22
}

define ptr @InflateRaw_get_property(ptr %0, ptr %1) #0 {
entry:
  %2 = call ptr @ts_string_create(ptr @161)
  %3 = call i1 @ts_string_eq(ptr %1, ptr %2)
  br i1 %3, label %match_bytesWritten, label %next_bytesWritten

match_bytesWritten:                               ; preds = %entry
  %4 = getelementptr inbounds %InflateRaw, ptr %0, i32 0, i32 1
  %5 = load i64, ptr %4, align 8
  %6 = call ptr @ts_value_make_int(i64 %5)
  ret ptr %6

next_bytesWritten:                                ; preds = %entry
  %7 = call ptr @ts_string_create(ptr @162)
  %8 = call i1 @ts_string_eq(ptr %1, ptr %7)
  br i1 %8, label %match_close, label %next_close

match_close:                                      ; preds = %next_bytesWritten
  %9 = getelementptr inbounds %InflateRaw, ptr %0, i32 0, i32 2
  %10 = load ptr, ptr %9, align 8
  %11 = call ptr @ts_value_box_any(ptr %10)
  ret ptr %11

next_close:                                       ; preds = %next_bytesWritten
  %12 = call ptr @ts_string_create(ptr @163)
  %13 = call i1 @ts_string_eq(ptr %1, ptr %12)
  br i1 %13, label %match_flush, label %next_flush

match_flush:                                      ; preds = %next_close
  %14 = getelementptr inbounds %InflateRaw, ptr %0, i32 0, i32 3
  %15 = load ptr, ptr %14, align 8
  %16 = call ptr @ts_value_box_any(ptr %15)
  ret ptr %16

next_flush:                                       ; preds = %next_close
  %17 = call ptr @ts_string_create(ptr @164)
  %18 = call i1 @ts_string_eq(ptr %1, ptr %17)
  br i1 %18, label %match_reset, label %next_reset

match_reset:                                      ; preds = %next_flush
  %19 = getelementptr inbounds %InflateRaw, ptr %0, i32 0, i32 4
  %20 = load ptr, ptr %19, align 8
  %21 = call ptr @ts_value_box_any(ptr %20)
  ret ptr %21

next_reset:                                       ; preds = %next_flush
  %22 = call ptr @ts_value_make_undefined()
  ret ptr %22
}

define ptr @Unzip_get_property(ptr %0, ptr %1) #0 {
entry:
  %2 = call ptr @ts_string_create(ptr @165)
  %3 = call i1 @ts_string_eq(ptr %1, ptr %2)
  br i1 %3, label %match_bytesWritten, label %next_bytesWritten

match_bytesWritten:                               ; preds = %entry
  %4 = getelementptr inbounds %Unzip, ptr %0, i32 0, i32 1
  %5 = load i64, ptr %4, align 8
  %6 = call ptr @ts_value_make_int(i64 %5)
  ret ptr %6

next_bytesWritten:                                ; preds = %entry
  %7 = call ptr @ts_string_create(ptr @166)
  %8 = call i1 @ts_string_eq(ptr %1, ptr %7)
  br i1 %8, label %match_close, label %next_close

match_close:                                      ; preds = %next_bytesWritten
  %9 = getelementptr inbounds %Unzip, ptr %0, i32 0, i32 2
  %10 = load ptr, ptr %9, align 8
  %11 = call ptr @ts_value_box_any(ptr %10)
  ret ptr %11

next_close:                                       ; preds = %next_bytesWritten
  %12 = call ptr @ts_string_create(ptr @167)
  %13 = call i1 @ts_string_eq(ptr %1, ptr %12)
  br i1 %13, label %match_flush, label %next_flush

match_flush:                                      ; preds = %next_close
  %14 = getelementptr inbounds %Unzip, ptr %0, i32 0, i32 3
  %15 = load ptr, ptr %14, align 8
  %16 = call ptr @ts_value_box_any(ptr %15)
  ret ptr %16

next_flush:                                       ; preds = %next_close
  %17 = call ptr @ts_string_create(ptr @168)
  %18 = call i1 @ts_string_eq(ptr %1, ptr %17)
  br i1 %18, label %match_reset, label %next_reset

match_reset:                                      ; preds = %next_flush
  %19 = getelementptr inbounds %Unzip, ptr %0, i32 0, i32 4
  %20 = load ptr, ptr %19, align 8
  %21 = call ptr @ts_value_box_any(ptr %20)
  ret ptr %21

next_reset:                                       ; preds = %next_flush
  %22 = call ptr @ts_value_make_undefined()
  ret ptr %22
}

define ptr @BrotliCompress_get_property(ptr %0, ptr %1) #0 {
entry:
  %2 = call ptr @ts_string_create(ptr @169)
  %3 = call i1 @ts_string_eq(ptr %1, ptr %2)
  br i1 %3, label %match_bytesWritten, label %next_bytesWritten

match_bytesWritten:                               ; preds = %entry
  %4 = getelementptr inbounds %BrotliCompress, ptr %0, i32 0, i32 1
  %5 = load i64, ptr %4, align 8
  %6 = call ptr @ts_value_make_int(i64 %5)
  ret ptr %6

next_bytesWritten:                                ; preds = %entry
  %7 = call ptr @ts_string_create(ptr @170)
  %8 = call i1 @ts_string_eq(ptr %1, ptr %7)
  br i1 %8, label %match_close, label %next_close

match_close:                                      ; preds = %next_bytesWritten
  %9 = getelementptr inbounds %BrotliCompress, ptr %0, i32 0, i32 2
  %10 = load ptr, ptr %9, align 8
  %11 = call ptr @ts_value_box_any(ptr %10)
  ret ptr %11

next_close:                                       ; preds = %next_bytesWritten
  %12 = call ptr @ts_string_create(ptr @171)
  %13 = call i1 @ts_string_eq(ptr %1, ptr %12)
  br i1 %13, label %match_flush, label %next_flush

match_flush:                                      ; preds = %next_close
  %14 = getelementptr inbounds %BrotliCompress, ptr %0, i32 0, i32 3
  %15 = load ptr, ptr %14, align 8
  %16 = call ptr @ts_value_box_any(ptr %15)
  ret ptr %16

next_flush:                                       ; preds = %next_close
  %17 = call ptr @ts_string_create(ptr @172)
  %18 = call i1 @ts_string_eq(ptr %1, ptr %17)
  br i1 %18, label %match_reset, label %next_reset

match_reset:                                      ; preds = %next_flush
  %19 = getelementptr inbounds %BrotliCompress, ptr %0, i32 0, i32 4
  %20 = load ptr, ptr %19, align 8
  %21 = call ptr @ts_value_box_any(ptr %20)
  ret ptr %21

next_reset:                                       ; preds = %next_flush
  %22 = call ptr @ts_value_make_undefined()
  ret ptr %22
}

define ptr @BrotliDecompress_get_property(ptr %0, ptr %1) #0 {
entry:
  %2 = call ptr @ts_string_create(ptr @173)
  %3 = call i1 @ts_string_eq(ptr %1, ptr %2)
  br i1 %3, label %match_bytesWritten, label %next_bytesWritten

match_bytesWritten:                               ; preds = %entry
  %4 = getelementptr inbounds %BrotliDecompress, ptr %0, i32 0, i32 1
  %5 = load i64, ptr %4, align 8
  %6 = call ptr @ts_value_make_int(i64 %5)
  ret ptr %6

next_bytesWritten:                                ; preds = %entry
  %7 = call ptr @ts_string_create(ptr @174)
  %8 = call i1 @ts_string_eq(ptr %1, ptr %7)
  br i1 %8, label %match_close, label %next_close

match_close:                                      ; preds = %next_bytesWritten
  %9 = getelementptr inbounds %BrotliDecompress, ptr %0, i32 0, i32 2
  %10 = load ptr, ptr %9, align 8
  %11 = call ptr @ts_value_box_any(ptr %10)
  ret ptr %11

next_close:                                       ; preds = %next_bytesWritten
  %12 = call ptr @ts_string_create(ptr @175)
  %13 = call i1 @ts_string_eq(ptr %1, ptr %12)
  br i1 %13, label %match_flush, label %next_flush

match_flush:                                      ; preds = %next_close
  %14 = getelementptr inbounds %BrotliDecompress, ptr %0, i32 0, i32 3
  %15 = load ptr, ptr %14, align 8
  %16 = call ptr @ts_value_box_any(ptr %15)
  ret ptr %16

next_flush:                                       ; preds = %next_close
  %17 = call ptr @ts_string_create(ptr @176)
  %18 = call i1 @ts_string_eq(ptr %1, ptr %17)
  br i1 %18, label %match_reset, label %next_reset

match_reset:                                      ; preds = %next_flush
  %19 = getelementptr inbounds %BrotliDecompress, ptr %0, i32 0, i32 4
  %20 = load ptr, ptr %19, align 8
  %21 = call ptr @ts_value_box_any(ptr %20)
  ret ptr %21

next_reset:                                       ; preds = %next_flush
  %22 = call ptr @ts_value_make_undefined()
  ret ptr %22
}

define ptr @__module_init_853456782593715192_any(ptr %context, ptr %module) #0 !type !44 {
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
  %2 = call ptr @ts_string_create(ptr @177)
  %3 = call ptr @ts_value_make_string(ptr %2)
  %4 = call ptr @ts_object_get_dynamic(ptr %1, ptr %3)
  store ptr %4, ptr %exports, align 8
  %module3 = load ptr, ptr %module1, align 8
  %5 = call ptr @ts_value_box_any(ptr %module3)
  %6 = call ptr @ts_string_create(ptr @178)
  %7 = call ptr @ts_value_make_string(ptr %6)
  %8 = call ptr @ts_object_get_dynamic(ptr %5, ptr %7)
  ret ptr %8

return:                                           ; preds = %dead
  %9 = load ptr, ptr %returnValue, align 8
  ret ptr %9

dead:                                             ; No predecessors!
  br label %return
}

define double @user_main(ptr %context) #0 !type !45 {
entry:
  %data = alloca ptr, align 8
  %mapGetResult43 = alloca %TsValue, align 8
  %0 = alloca i8, align 1
  %1 = alloca i64, align 8
  %mapGetResult32 = alloca %TsValue, align 8
  %2 = alloca i8, align 1
  %3 = alloca i64, align 8
  %nested = alloca ptr, align 8
  %val = alloca double, align 8
  %mapGetResult = alloca %TsValue, align 8
  %4 = alloca i8, align 1
  %5 = alloca i64, align 8
  %obj = alloca ptr, align 8
  %str = alloca ptr, align 8
  %maybeStr = alloca ptr, align 8
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
  %6 = call ptr @ts_string_create(ptr @179)
  store ptr %6, ptr %maybeStr, align 8
  %maybeStr1 = load ptr, ptr %maybeStr, align 8
  store ptr %maybeStr1, ptr %str, align 8
  %str2 = load ptr, ptr %str, align 8
  call void @ts_console_log(ptr %str2)
  %7 = call ptr @ts_map_create()
  %8 = call ptr @ts_value_make_int(i64 42)
  %9 = call ptr @ts_string_create(ptr @180)
  %10 = call ptr @ts_value_make_string(ptr %9)
  %11 = icmp eq ptr %10, null
  %12 = icmp eq ptr %8, null
  %13 = select i1 %11, ptr @__ts_const_undefined_value, ptr %10
  %14 = select i1 %12, ptr @__ts_const_undefined_value, ptr %8
  %typePtr = getelementptr inbounds %TsValue, ptr %13, i32 0, i32 0
  %type = load i8, ptr %typePtr, align 1
  %unionPtr = getelementptr inbounds %TsValue, ptr %13, i32 0, i32 2
  %unionVal = load i64, ptr %unionPtr, align 8
  %typePtr3 = getelementptr inbounds %TsValue, ptr %14, i32 0, i32 0
  %type4 = load i8, ptr %typePtr3, align 1
  %unionPtr5 = getelementptr inbounds %TsValue, ptr %14, i32 0, i32 2
  %unionVal6 = load i64, ptr %unionPtr5, align 8
  call void @__ts_map_set_at(ptr %7, i64 %unionVal, i8 %type, i64 %unionVal, i8 %type4, i64 %unionVal6)
  store ptr %7, ptr %obj, align 8
  %obj7 = load ptr, ptr %obj, align 8
  %15 = call ptr @ts_value_get_object(ptr %obj7)
  %16 = icmp eq ptr %15, null
  br i1 %16, label %null_fail, label %null_cont

return:                                           ; preds = %dead
  %17 = load double, ptr %returnValue, align 8
  ret double %17

null_fail:                                        ; preds = %entry
  call void @ts_panic(ptr @181)
  unreachable

null_cont:                                        ; preds = %entry
  %18 = call ptr @ts_string_create(ptr @182)
  %19 = call ptr @ts_value_make_string(ptr %18)
  %20 = icmp eq ptr %19, null
  %21 = select i1 %20, ptr @__ts_const_undefined_value.1, ptr %19
  %typePtr8 = getelementptr inbounds %TsValue, ptr %21, i32 0, i32 0
  %type9 = load i8, ptr %typePtr8, align 1
  %unionPtr10 = getelementptr inbounds %TsValue, ptr %21, i32 0, i32 2
  %unionVal11 = load i64, ptr %unionPtr10, align 8
  %22 = call i64 @__ts_map_find_bucket(ptr %15, i64 %unionVal11, i8 %type9, i64 %unionVal11)
  %notFound = icmp slt i64 %22, 0
  br i1 %notFound, label %map.notfound, label %map.found

map.found:                                        ; preds = %null_cont
  call void @__ts_map_get_value_at(ptr %15, i64 %22, ptr %4, ptr %5)
  %23 = load i64, ptr %5, align 8
  %24 = load i8, ptr %4, align 1
  %25 = getelementptr inbounds %TsValue, ptr %mapGetResult, i32 0, i32 0
  store i8 %24, ptr %25, align 1
  %26 = getelementptr inbounds %TsValue, ptr %mapGetResult, i32 0, i32 1
  store [7 x i8] zeroinitializer, ptr %26, align 1
  %27 = getelementptr inbounds %TsValue, ptr %mapGetResult, i32 0, i32 2
  store i64 %23, ptr %27, align 8
  br label %map.done

map.notfound:                                     ; preds = %null_cont
  %28 = getelementptr inbounds %TsValue, ptr %mapGetResult, i32 0, i32 0
  store i8 0, ptr %28, align 1
  %29 = getelementptr inbounds %TsValue, ptr %mapGetResult, i32 0, i32 1
  store [7 x i8] zeroinitializer, ptr %29, align 1
  %30 = getelementptr inbounds %TsValue, ptr %mapGetResult, i32 0, i32 2
  store i64 0, ptr %30, align 8
  br label %map.done

map.done:                                         ; preds = %map.notfound, %map.found
  %31 = call fast double @ts_value_get_double(ptr %mapGetResult)
  store double %31, ptr %val, align 8
  %val12 = load double, ptr %val, align 8
  call void @ts_console_log_double(double %val12)
  %32 = call ptr @ts_map_create()
  %33 = call ptr @ts_map_create()
  %34 = call ptr @ts_string_create(ptr @183)
  %35 = call ptr @ts_value_make_string(ptr %34)
  %36 = call ptr @ts_string_create(ptr @184)
  %37 = call ptr @ts_value_make_string(ptr %36)
  %38 = icmp eq ptr %37, null
  %39 = icmp eq ptr %35, null
  %40 = select i1 %38, ptr @__ts_const_undefined_value.2, ptr %37
  %41 = select i1 %39, ptr @__ts_const_undefined_value.2, ptr %35
  %typePtr13 = getelementptr inbounds %TsValue, ptr %40, i32 0, i32 0
  %type14 = load i8, ptr %typePtr13, align 1
  %unionPtr15 = getelementptr inbounds %TsValue, ptr %40, i32 0, i32 2
  %unionVal16 = load i64, ptr %unionPtr15, align 8
  %typePtr17 = getelementptr inbounds %TsValue, ptr %41, i32 0, i32 0
  %type18 = load i8, ptr %typePtr17, align 1
  %unionPtr19 = getelementptr inbounds %TsValue, ptr %41, i32 0, i32 2
  %unionVal20 = load i64, ptr %unionPtr19, align 8
  call void @__ts_map_set_at(ptr %33, i64 %unionVal16, i8 %type14, i64 %unionVal16, i8 %type18, i64 %unionVal20)
  %42 = call ptr @ts_value_make_object(ptr %33)
  %43 = call ptr @ts_string_create(ptr @185)
  %44 = call ptr @ts_value_make_string(ptr %43)
  %45 = icmp eq ptr %44, null
  %46 = icmp eq ptr %42, null
  %47 = select i1 %45, ptr @__ts_const_undefined_value.3, ptr %44
  %48 = select i1 %46, ptr @__ts_const_undefined_value.3, ptr %42
  %typePtr21 = getelementptr inbounds %TsValue, ptr %47, i32 0, i32 0
  %type22 = load i8, ptr %typePtr21, align 1
  %unionPtr23 = getelementptr inbounds %TsValue, ptr %47, i32 0, i32 2
  %unionVal24 = load i64, ptr %unionPtr23, align 8
  %typePtr25 = getelementptr inbounds %TsValue, ptr %48, i32 0, i32 0
  %type26 = load i8, ptr %typePtr25, align 1
  %unionPtr27 = getelementptr inbounds %TsValue, ptr %48, i32 0, i32 2
  %unionVal28 = load i64, ptr %unionPtr27, align 8
  call void @__ts_map_set_at(ptr %32, i64 %unionVal24, i8 %type22, i64 %unionVal24, i8 %type26, i64 %unionVal28)
  store ptr %32, ptr %nested, align 8
  %nested29 = load ptr, ptr %nested, align 8
  %49 = call ptr @ts_value_get_object(ptr %nested29)
  %50 = icmp eq ptr %49, null
  br i1 %50, label %null_fail30, label %null_cont31

null_fail30:                                      ; preds = %map.done
  call void @ts_panic(ptr @186)
  unreachable

null_cont31:                                      ; preds = %map.done
  %51 = call ptr @ts_string_create(ptr @187)
  %52 = call ptr @ts_value_make_string(ptr %51)
  %53 = icmp eq ptr %52, null
  %54 = select i1 %53, ptr @__ts_const_undefined_value.4, ptr %52
  %typePtr33 = getelementptr inbounds %TsValue, ptr %54, i32 0, i32 0
  %type34 = load i8, ptr %typePtr33, align 1
  %unionPtr35 = getelementptr inbounds %TsValue, ptr %54, i32 0, i32 2
  %unionVal36 = load i64, ptr %unionPtr35, align 8
  %55 = call i64 @__ts_map_find_bucket(ptr %49, i64 %unionVal36, i8 %type34, i64 %unionVal36)
  %notFound37 = icmp slt i64 %55, 0
  br i1 %notFound37, label %map.notfound39, label %map.found38

map.found38:                                      ; preds = %null_cont31
  call void @__ts_map_get_value_at(ptr %49, i64 %55, ptr %2, ptr %3)
  %56 = load i64, ptr %3, align 8
  %57 = load i8, ptr %2, align 1
  %58 = getelementptr inbounds %TsValue, ptr %mapGetResult32, i32 0, i32 0
  store i8 %57, ptr %58, align 1
  %59 = getelementptr inbounds %TsValue, ptr %mapGetResult32, i32 0, i32 1
  store [7 x i8] zeroinitializer, ptr %59, align 1
  %60 = getelementptr inbounds %TsValue, ptr %mapGetResult32, i32 0, i32 2
  store i64 %56, ptr %60, align 8
  br label %map.done40

map.notfound39:                                   ; preds = %null_cont31
  %61 = getelementptr inbounds %TsValue, ptr %mapGetResult32, i32 0, i32 0
  store i8 0, ptr %61, align 1
  %62 = getelementptr inbounds %TsValue, ptr %mapGetResult32, i32 0, i32 1
  store [7 x i8] zeroinitializer, ptr %62, align 1
  %63 = getelementptr inbounds %TsValue, ptr %mapGetResult32, i32 0, i32 2
  store i64 0, ptr %63, align 8
  br label %map.done40

map.done40:                                       ; preds = %map.notfound39, %map.found38
  %64 = call ptr @ts_value_get_object(ptr %mapGetResult32)
  %65 = icmp eq ptr %64, null
  br i1 %65, label %null_fail41, label %null_cont42

null_fail41:                                      ; preds = %map.done40
  call void @ts_panic(ptr @188)
  unreachable

null_cont42:                                      ; preds = %map.done40
  %66 = call ptr @ts_string_create(ptr @189)
  %67 = call ptr @ts_value_make_string(ptr %66)
  %68 = icmp eq ptr %67, null
  %69 = select i1 %68, ptr @__ts_const_undefined_value.5, ptr %67
  %typePtr44 = getelementptr inbounds %TsValue, ptr %69, i32 0, i32 0
  %type45 = load i8, ptr %typePtr44, align 1
  %unionPtr46 = getelementptr inbounds %TsValue, ptr %69, i32 0, i32 2
  %unionVal47 = load i64, ptr %unionPtr46, align 8
  %70 = call i64 @__ts_map_find_bucket(ptr %64, i64 %unionVal47, i8 %type45, i64 %unionVal47)
  %notFound48 = icmp slt i64 %70, 0
  br i1 %notFound48, label %map.notfound50, label %map.found49

map.found49:                                      ; preds = %null_cont42
  call void @__ts_map_get_value_at(ptr %64, i64 %70, ptr %0, ptr %1)
  %71 = load i64, ptr %1, align 8
  %72 = load i8, ptr %0, align 1
  %73 = getelementptr inbounds %TsValue, ptr %mapGetResult43, i32 0, i32 0
  store i8 %72, ptr %73, align 1
  %74 = getelementptr inbounds %TsValue, ptr %mapGetResult43, i32 0, i32 1
  store [7 x i8] zeroinitializer, ptr %74, align 1
  %75 = getelementptr inbounds %TsValue, ptr %mapGetResult43, i32 0, i32 2
  store i64 %71, ptr %75, align 8
  br label %map.done51

map.notfound50:                                   ; preds = %null_cont42
  %76 = getelementptr inbounds %TsValue, ptr %mapGetResult43, i32 0, i32 0
  store i8 0, ptr %76, align 1
  %77 = getelementptr inbounds %TsValue, ptr %mapGetResult43, i32 0, i32 1
  store [7 x i8] zeroinitializer, ptr %77, align 1
  %78 = getelementptr inbounds %TsValue, ptr %mapGetResult43, i32 0, i32 2
  store i64 0, ptr %78, align 8
  br label %map.done51

map.done51:                                       ; preds = %map.notfound50, %map.found49
  %79 = call ptr @ts_value_get_string(ptr %mapGetResult43)
  store ptr %79, ptr %data, align 8
  %data52 = load ptr, ptr %data, align 8
  call void @ts_console_log(ptr %data52)
  ret double 0.000000e+00

dead:                                             ; No predecessors!
  br label %return
}

define ptr @__synthetic_user_main(ptr %context) #0 !type !46 {
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
  %4 = call ptr @ts_string_create(ptr @190)
  %5 = call ptr @ts_value_make_string(ptr %4)
  %6 = icmp eq ptr %5, null
  %7 = icmp eq ptr %3, null
  %8 = select i1 %6, ptr @__ts_const_undefined_value.6, ptr %5
  %9 = select i1 %7, ptr @__ts_const_undefined_value.6, ptr %3
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
  %10 = call ptr @ts_string_create(ptr @191)
  %11 = call ptr @ts_value_make_string(ptr %10)
  %__module_obj_05 = load ptr, ptr %__module_obj_0, align 8
  %12 = call ptr @ts_value_box_any(ptr %__module_obj_05)
  call void @ts_module_register(ptr %11, ptr %12)
  %__module_obj_06 = load ptr, ptr %__module_obj_0, align 8
  %13 = call ptr @__module_init_853456782593715192_any(ptr %context, ptr %__module_obj_06)
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

declare ptr @ts_map_create()

declare void @__ts_map_set_at(ptr, i64, i8, i64, i8, i64)

declare ptr @ts_value_get_object(ptr)

declare void @ts_panic(ptr)

declare i64 @__ts_map_find_bucket(ptr, i64, i8, i64)

declare void @__ts_map_get_value_at(ptr, i64, ptr, ptr)

declare double @ts_value_get_double(ptr)

declare void @ts_console_log_double(double)

declare ptr @ts_value_get_string(ptr)

declare void @ts_module_register(ptr, ptr)

declare i32 @ts_main(i32, ptr, ptr) #0

define i32 @main(i32 %0, ptr %1) #0 {
entry:
  %2 = call ptr @ts_map_create()
  store ptr %2, ptr @__ts_exports, align 8
  %3 = call ptr @ts_map_create()
  call void @ts_map_set_cstr(ptr %3, ptr @192, ptr %2)
  store ptr %3, ptr @__ts_module, align 8
  %4 = call i32 @ts_main(i32 %0, ptr %1, ptr @__synthetic_user_main)
  ret i32 0
}

declare void @ts_map_set_cstr(ptr, ptr, ptr)

attributes #0 = { "sspstrong" "stack-protector-buffer-size"="8" }

!llvm.linker.options = !{!40, !41, !42, !43}

!0 = !{i64 0, !"ArrayBuffer"}
!1 = !{i64 0, !"Sign"}
!2 = !{i64 0, !"Zlib"}
!3 = !{i64 0, !"ServerHttp2Session"}
!4 = !{i64 0, !"RegExpMatchArray"}
!5 = !{i64 0, !"Hash"}
!6 = !{i64 0, !"SocketAddress"}
!7 = !{i64 0, !"ClientHttp2Stream"}
!8 = !{i64 0, !"URLSearchParams"}
!9 = !{i64 0, !"Response"}
!10 = !{i64 0, !"URL"}
!11 = !{i64 0, !"DeflateRaw"}
!12 = !{i64 0, !"PerformanceObserver"}
!13 = !{i64 0, !"ClientHttp2Session"}
!14 = !{i64 0, !"ServerHttp2Stream"}
!15 = !{i64 0, !"Http2Stream"}
!16 = !{i64 0, !"Http2Session"}
!17 = !{i64 0, !"Http2Server"}
!18 = !{i64 0, !"Http2SecureServer"}
!19 = !{i64 0, !"Hmac"}
!20 = !{i64 0, !"Cipher"}
!21 = !{i64 0, !"Decipher"}
!22 = !{i64 0, !"Verify"}
!23 = !{i64 0, !"Script"}
!24 = !{i64 0, !"AssertionError"}
!25 = !{i64 0, !"PerformanceEntry"}
!26 = !{i64 0, !"TLSSocket"}
!27 = !{i64 0, !"PerformanceMark"}
!28 = !{i64 0, !"PerformanceMeasure"}
!29 = !{i64 0, !"EventLoopUtilization"}
!30 = !{i64 0, !"PerformanceObserverEntryList"}
!31 = !{i64 0, !"SecureContext"}
!32 = !{i64 0, !"Gzip"}
!33 = !{i64 0, !"Gunzip"}
!34 = !{i64 0, !"Deflate"}
!35 = !{i64 0, !"Inflate"}
!36 = !{i64 0, !"InflateRaw"}
!37 = !{i64 0, !"Unzip"}
!38 = !{i64 0, !"BrotliCompress"}
!39 = !{i64 0, !"BrotliDecompress"}
!40 = !{!"/FAILIFMISMATCH:\22_ITERATOR_DEBUG_LEVEL=0\22"}
!41 = !{!"/DEFAULTLIB:libcmt.lib"}
!42 = !{!"/NODEFAULTLIB:libcmtd.lib"}
!43 = !{!"/FAILIFMISMATCH:\22RuntimeLibrary=MT_StaticRelease\22"}
!44 = !{i64 0, !"__module_init_853456782593715192_any"}
!45 = !{i64 0, !"user_main"}
!46 = !{i64 0, !"__synthetic_user_main"}
