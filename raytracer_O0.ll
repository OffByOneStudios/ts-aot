; ModuleID = 'ts_module'
source_filename = "ts_module"
target datalayout = "e-m:w-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-windows-msvc"

%IncomingMessage_VTable = type { ptr }
%Buffer_VTable = type { ptr, ptr }
%URL_VTable = type { ptr }
%Response_VTable = type { ptr, ptr, ptr }
%ServerResponse_VTable = type { ptr, ptr, ptr, ptr }
%Server_VTable = type { ptr, ptr }
%Vector3_VTable = type { ptr, ptr, ptr, ptr, ptr, ptr, ptr, ptr }
%Ray_VTable = type { ptr, ptr }
%Sphere_VTable = type { ptr, ptr, ptr }
%Vector3 = type { ptr, double, double, double }
%Ray = type { ptr, ptr, ptr }
%Sphere = type { ptr, ptr, ptr, double }

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
@IncomingMessage_VTable_Global = constant %IncomingMessage_VTable zeroinitializer
@Buffer_VTable_Global = constant %Buffer_VTable { ptr null, ptr @Buffer_toString }
@URL_VTable_Global = constant %URL_VTable zeroinitializer
@Response_VTable_Global = constant %Response_VTable { ptr null, ptr @Response_json, ptr @Response_text }
@ServerResponse_VTable_Global = constant %ServerResponse_VTable { ptr null, ptr @ServerResponse_end, ptr @ServerResponse_write, ptr @ServerResponse_writeHead }
@Server_VTable_Global = constant %Server_VTable { ptr null, ptr @Server_listen }
@Vector3_VTable_Global = constant %Vector3_VTable { ptr null, ptr @Vector3_add, ptr @Vector3_constructor, ptr @Vector3_cross, ptr @Vector3_dot, ptr @Vector3_mul, ptr @Vector3_normalize, ptr @Vector3_sub }
@Ray_VTable_Global = constant %Ray_VTable { ptr null, ptr @Ray_constructor }
@Sphere_VTable_Global = constant %Sphere_VTable { ptr null, ptr @Sphere_constructor, ptr @Sphere_intersect }
@0 = private unnamed_addr constant [33 x i8] c"Starting Ray Tracer Benchmark...\00", align 1
@1 = private unnamed_addr constant [20 x i8] c"Benchmark Complete.\00", align 1
@2 = private unnamed_addr constant [17 x i8] c"Total Checksum: \00", align 1
@3 = private unnamed_addr constant [15 x i8] c"Average Time: \00", align 1
@4 = private unnamed_addr constant [3 x i8] c"ms\00", align 1

declare ptr @Buffer_toString(ptr, ptr)

declare ptr @Response_json(ptr, ptr)

declare ptr @Response_text(ptr, ptr)

declare ptr @ServerResponse_end(ptr, ptr, ptr)

declare ptr @ServerResponse_write(ptr, ptr, ptr)

declare ptr @ServerResponse_writeHead(ptr, ptr, i64, ptr)

declare ptr @Server_listen(ptr, ptr, i64, ptr)

define ptr @Vector3_add(ptr %context, ptr %a, ptr %b, ptr %0) {
entry:
  %b2 = alloca ptr, align 8
  %a1 = alloca ptr, align 8
  store ptr %a, ptr %a1, align 8
  store ptr %b, ptr %b2, align 8
  %1 = call ptr @ts_alloc(i64 32)
  %2 = getelementptr inbounds %Vector3, ptr %1, i32 0, i32 0
  store ptr @Vector3_VTable_Global, ptr %2, align 8
  %a3 = load ptr, ptr %a1, align 8
  %3 = getelementptr inbounds %Vector3, ptr %a3, i32 0, i32 1
  %4 = load double, ptr %3, align 8
  %b4 = load ptr, ptr %b2, align 8
  %5 = getelementptr inbounds %Vector3, ptr %b4, i32 0, i32 1
  %6 = load double, ptr %5, align 8
  %addtmp = fadd double %4, %6
  %a5 = load ptr, ptr %a1, align 8
  %7 = getelementptr inbounds %Vector3, ptr %a5, i32 0, i32 2
  %8 = load double, ptr %7, align 8
  %b6 = load ptr, ptr %b2, align 8
  %9 = getelementptr inbounds %Vector3, ptr %b6, i32 0, i32 2
  %10 = load double, ptr %9, align 8
  %addtmp7 = fadd double %8, %10
  %a8 = load ptr, ptr %a1, align 8
  %11 = getelementptr inbounds %Vector3, ptr %a8, i32 0, i32 3
  %12 = load double, ptr %11, align 8
  %b9 = load ptr, ptr %b2, align 8
  %13 = getelementptr inbounds %Vector3, ptr %b9, i32 0, i32 3
  %14 = load double, ptr %13, align 8
  %addtmp10 = fadd double %12, %14
  %15 = call ptr @Vector3_constructor(ptr null, ptr %1, double %addtmp, double %addtmp7, double %addtmp10)
  ret ptr %1
}

define ptr @Vector3_constructor(ptr %context, ptr %this, double %x, double %y, double %z) {
entry:
  %z4 = alloca double, align 8
  %y3 = alloca double, align 8
  %x2 = alloca double, align 8
  %this1 = alloca ptr, align 8
  store ptr %this, ptr %this1, align 8
  store double %x, ptr %x2, align 8
  store double %y, ptr %y3, align 8
  store double %z, ptr %z4, align 8
  %x5 = load double, ptr %x2, align 8
  %this6 = load ptr, ptr %this1, align 8
  %0 = getelementptr inbounds %Vector3, ptr %this6, i32 0, i32 1
  store double %x5, ptr %0, align 8
  %y7 = load double, ptr %y3, align 8
  %this8 = load ptr, ptr %this1, align 8
  %1 = getelementptr inbounds %Vector3, ptr %this8, i32 0, i32 2
  store double %y7, ptr %1, align 8
  %z9 = load double, ptr %z4, align 8
  %this10 = load ptr, ptr %this1, align 8
  %2 = getelementptr inbounds %Vector3, ptr %this10, i32 0, i32 3
  store double %z9, ptr %2, align 8
  ret ptr null
}

define ptr @Vector3_cross(ptr %context, ptr %a, ptr %b, ptr %0) {
entry:
  %b2 = alloca ptr, align 8
  %a1 = alloca ptr, align 8
  store ptr %a, ptr %a1, align 8
  store ptr %b, ptr %b2, align 8
  %1 = call ptr @ts_alloc(i64 32)
  %2 = getelementptr inbounds %Vector3, ptr %1, i32 0, i32 0
  store ptr @Vector3_VTable_Global, ptr %2, align 8
  %a3 = load ptr, ptr %a1, align 8
  %3 = getelementptr inbounds %Vector3, ptr %a3, i32 0, i32 2
  %4 = load double, ptr %3, align 8
  %b4 = load ptr, ptr %b2, align 8
  %5 = getelementptr inbounds %Vector3, ptr %b4, i32 0, i32 3
  %6 = load double, ptr %5, align 8
  %multmp = fmul double %4, %6
  %a5 = load ptr, ptr %a1, align 8
  %7 = getelementptr inbounds %Vector3, ptr %a5, i32 0, i32 3
  %8 = load double, ptr %7, align 8
  %b6 = load ptr, ptr %b2, align 8
  %9 = getelementptr inbounds %Vector3, ptr %b6, i32 0, i32 2
  %10 = load double, ptr %9, align 8
  %multmp7 = fmul double %8, %10
  %subtmp = fsub double %multmp, %multmp7
  %a8 = load ptr, ptr %a1, align 8
  %11 = getelementptr inbounds %Vector3, ptr %a8, i32 0, i32 3
  %12 = load double, ptr %11, align 8
  %b9 = load ptr, ptr %b2, align 8
  %13 = getelementptr inbounds %Vector3, ptr %b9, i32 0, i32 1
  %14 = load double, ptr %13, align 8
  %multmp10 = fmul double %12, %14
  %a11 = load ptr, ptr %a1, align 8
  %15 = getelementptr inbounds %Vector3, ptr %a11, i32 0, i32 1
  %16 = load double, ptr %15, align 8
  %b12 = load ptr, ptr %b2, align 8
  %17 = getelementptr inbounds %Vector3, ptr %b12, i32 0, i32 3
  %18 = load double, ptr %17, align 8
  %multmp13 = fmul double %16, %18
  %subtmp14 = fsub double %multmp10, %multmp13
  %a15 = load ptr, ptr %a1, align 8
  %19 = getelementptr inbounds %Vector3, ptr %a15, i32 0, i32 1
  %20 = load double, ptr %19, align 8
  %b16 = load ptr, ptr %b2, align 8
  %21 = getelementptr inbounds %Vector3, ptr %b16, i32 0, i32 2
  %22 = load double, ptr %21, align 8
  %multmp17 = fmul double %20, %22
  %a18 = load ptr, ptr %a1, align 8
  %23 = getelementptr inbounds %Vector3, ptr %a18, i32 0, i32 2
  %24 = load double, ptr %23, align 8
  %b19 = load ptr, ptr %b2, align 8
  %25 = getelementptr inbounds %Vector3, ptr %b19, i32 0, i32 1
  %26 = load double, ptr %25, align 8
  %multmp20 = fmul double %24, %26
  %subtmp21 = fsub double %multmp17, %multmp20
  %27 = call ptr @Vector3_constructor(ptr null, ptr %1, double %subtmp, double %subtmp14, double %subtmp21)
  ret ptr %1
}

define double @Vector3_dot(ptr %context, ptr %a, ptr %b, ptr %0) {
entry:
  %b2 = alloca ptr, align 8
  %a1 = alloca ptr, align 8
  store ptr %a, ptr %a1, align 8
  store ptr %b, ptr %b2, align 8
  %a3 = load ptr, ptr %a1, align 8
  %1 = getelementptr inbounds %Vector3, ptr %a3, i32 0, i32 1
  %2 = load double, ptr %1, align 8
  %b4 = load ptr, ptr %b2, align 8
  %3 = getelementptr inbounds %Vector3, ptr %b4, i32 0, i32 1
  %4 = load double, ptr %3, align 8
  %multmp = fmul double %2, %4
  %a5 = load ptr, ptr %a1, align 8
  %5 = getelementptr inbounds %Vector3, ptr %a5, i32 0, i32 2
  %6 = load double, ptr %5, align 8
  %b6 = load ptr, ptr %b2, align 8
  %7 = getelementptr inbounds %Vector3, ptr %b6, i32 0, i32 2
  %8 = load double, ptr %7, align 8
  %multmp7 = fmul double %6, %8
  %addtmp = fadd double %multmp, %multmp7
  %a8 = load ptr, ptr %a1, align 8
  %9 = getelementptr inbounds %Vector3, ptr %a8, i32 0, i32 3
  %10 = load double, ptr %9, align 8
  %b9 = load ptr, ptr %b2, align 8
  %11 = getelementptr inbounds %Vector3, ptr %b9, i32 0, i32 3
  %12 = load double, ptr %11, align 8
  %multmp10 = fmul double %10, %12
  %addtmp11 = fadd double %addtmp, %multmp10
  ret double %addtmp11
}

define ptr @Vector3_mul(ptr %context, ptr %a, ptr %b, double %0) {
entry:
  %b2 = alloca ptr, align 8
  %a1 = alloca ptr, align 8
  store ptr %a, ptr %a1, align 8
  store ptr %b, ptr %b2, align 8
  %1 = call ptr @ts_alloc(i64 32)
  %2 = getelementptr inbounds %Vector3, ptr %1, i32 0, i32 0
  store ptr @Vector3_VTable_Global, ptr %2, align 8
  %a3 = load ptr, ptr %a1, align 8
  %3 = getelementptr inbounds %Vector3, ptr %a3, i32 0, i32 1
  %4 = load double, ptr %3, align 8
  %b4 = load ptr, ptr %b2, align 8
  %5 = call double @ts_value_get_double(ptr %b4)
  %multmp = fmul double %4, %5
  %a5 = load ptr, ptr %a1, align 8
  %6 = getelementptr inbounds %Vector3, ptr %a5, i32 0, i32 2
  %7 = load double, ptr %6, align 8
  %b6 = load ptr, ptr %b2, align 8
  %8 = call double @ts_value_get_double(ptr %b6)
  %multmp7 = fmul double %7, %8
  %a8 = load ptr, ptr %a1, align 8
  %9 = getelementptr inbounds %Vector3, ptr %a8, i32 0, i32 3
  %10 = load double, ptr %9, align 8
  %b9 = load ptr, ptr %b2, align 8
  %11 = call double @ts_value_get_double(ptr %b9)
  %multmp10 = fmul double %10, %11
  %12 = call ptr @Vector3_constructor(ptr null, ptr %1, double %multmp, double %multmp7, double %multmp10)
  ret ptr %1
}

define ptr @Vector3_normalize(ptr %context, ptr %a, ptr %0) {
entry:
  %len = alloca double, align 8
  %a1 = alloca ptr, align 8
  store ptr %a, ptr %a1, align 8
  %a2 = load ptr, ptr %a1, align 8
  %a3 = load ptr, ptr %a1, align 8
  %1 = call double @Vector3_static_dot(ptr null, ptr %a2, ptr %a3)
  %2 = call double @ts_math_sqrt(double %1)
  store double %2, ptr %len, align 8
  %a4 = load ptr, ptr %a1, align 8
  %len5 = load double, ptr %len, align 8
  %divtmp = fdiv double 1.000000e+00, %len5
  %3 = call ptr @Vector3_static_mul(ptr null, ptr %a4, double %divtmp)
  ret ptr %3
}

define ptr @Vector3_sub(ptr %context, ptr %a, ptr %b, ptr %0) {
entry:
  %b2 = alloca ptr, align 8
  %a1 = alloca ptr, align 8
  store ptr %a, ptr %a1, align 8
  store ptr %b, ptr %b2, align 8
  %1 = call ptr @ts_alloc(i64 32)
  %2 = getelementptr inbounds %Vector3, ptr %1, i32 0, i32 0
  store ptr @Vector3_VTable_Global, ptr %2, align 8
  %a3 = load ptr, ptr %a1, align 8
  %3 = getelementptr inbounds %Vector3, ptr %a3, i32 0, i32 1
  %4 = load double, ptr %3, align 8
  %b4 = load ptr, ptr %b2, align 8
  %5 = getelementptr inbounds %Vector3, ptr %b4, i32 0, i32 1
  %6 = load double, ptr %5, align 8
  %subtmp = fsub double %4, %6
  %a5 = load ptr, ptr %a1, align 8
  %7 = getelementptr inbounds %Vector3, ptr %a5, i32 0, i32 2
  %8 = load double, ptr %7, align 8
  %b6 = load ptr, ptr %b2, align 8
  %9 = getelementptr inbounds %Vector3, ptr %b6, i32 0, i32 2
  %10 = load double, ptr %9, align 8
  %subtmp7 = fsub double %8, %10
  %a8 = load ptr, ptr %a1, align 8
  %11 = getelementptr inbounds %Vector3, ptr %a8, i32 0, i32 3
  %12 = load double, ptr %11, align 8
  %b9 = load ptr, ptr %b2, align 8
  %13 = getelementptr inbounds %Vector3, ptr %b9, i32 0, i32 3
  %14 = load double, ptr %13, align 8
  %subtmp10 = fsub double %12, %14
  %15 = call ptr @Vector3_constructor(ptr null, ptr %1, double %subtmp, double %subtmp7, double %subtmp10)
  ret ptr %1
}

define ptr @Ray_constructor(ptr %context, ptr %this, ptr %origin, ptr %direction) {
entry:
  %direction3 = alloca ptr, align 8
  %origin2 = alloca ptr, align 8
  %this1 = alloca ptr, align 8
  store ptr %this, ptr %this1, align 8
  store ptr %origin, ptr %origin2, align 8
  store ptr %direction, ptr %direction3, align 8
  %origin4 = load ptr, ptr %origin2, align 8
  %this5 = load ptr, ptr %this1, align 8
  %0 = getelementptr inbounds %Ray, ptr %this5, i32 0, i32 2
  store ptr %origin4, ptr %0, align 8
  %direction6 = load ptr, ptr %direction3, align 8
  %this7 = load ptr, ptr %this1, align 8
  %1 = getelementptr inbounds %Ray, ptr %this7, i32 0, i32 1
  store ptr %direction6, ptr %1, align 8
  ret ptr null
}

define ptr @Sphere_constructor(ptr %context, ptr %this, ptr %center, double %radius, ptr %color) {
entry:
  %color4 = alloca ptr, align 8
  %radius3 = alloca double, align 8
  %center2 = alloca ptr, align 8
  %this1 = alloca ptr, align 8
  store ptr %this, ptr %this1, align 8
  store ptr %center, ptr %center2, align 8
  store double %radius, ptr %radius3, align 8
  store ptr %color, ptr %color4, align 8
  %center5 = load ptr, ptr %center2, align 8
  %this6 = load ptr, ptr %this1, align 8
  %0 = getelementptr inbounds %Sphere, ptr %this6, i32 0, i32 1
  store ptr %center5, ptr %0, align 8
  %radius7 = load double, ptr %radius3, align 8
  %this8 = load ptr, ptr %this1, align 8
  %1 = getelementptr inbounds %Sphere, ptr %this8, i32 0, i32 3
  store double %radius7, ptr %1, align 8
  %color9 = load ptr, ptr %color4, align 8
  %this10 = load ptr, ptr %this1, align 8
  %2 = getelementptr inbounds %Sphere, ptr %this10, i32 0, i32 2
  store ptr %color9, ptr %2, align 8
  ret ptr null
}

define double @Sphere_intersect(ptr %context, ptr %this, ptr %ray) {
entry:
  %discriminant = alloca double, align 8
  %c = alloca double, align 8
  %b = alloca double, align 8
  %a = alloca double, align 8
  %oc = alloca ptr, align 8
  %ray2 = alloca ptr, align 8
  %this1 = alloca ptr, align 8
  store ptr %this, ptr %this1, align 8
  store ptr %ray, ptr %ray2, align 8
  %ray3 = load ptr, ptr %ray2, align 8
  %0 = getelementptr inbounds %Ray, ptr %ray3, i32 0, i32 2
  %1 = load ptr, ptr %0, align 8
  %this4 = load ptr, ptr %this1, align 8
  %2 = getelementptr inbounds %Sphere, ptr %this4, i32 0, i32 1
  %3 = load ptr, ptr %2, align 8
  %4 = call ptr @Vector3_static_sub(ptr null, ptr %1, ptr %3)
  store ptr %4, ptr %oc, align 8
  %ray5 = load ptr, ptr %ray2, align 8
  %5 = getelementptr inbounds %Ray, ptr %ray5, i32 0, i32 1
  %6 = load ptr, ptr %5, align 8
  %ray6 = load ptr, ptr %ray2, align 8
  %7 = getelementptr inbounds %Ray, ptr %ray6, i32 0, i32 1
  %8 = load ptr, ptr %7, align 8
  %9 = call double @Vector3_static_dot(ptr null, ptr %6, ptr %8)
  store double %9, ptr %a, align 8
  %oc7 = load ptr, ptr %oc, align 8
  %ray8 = load ptr, ptr %ray2, align 8
  %10 = getelementptr inbounds %Ray, ptr %ray8, i32 0, i32 1
  %11 = load ptr, ptr %10, align 8
  %12 = call double @Vector3_static_dot(ptr null, ptr %oc7, ptr %11)
  %multmp = fmul double 2.000000e+00, %12
  store double %multmp, ptr %b, align 8
  %oc9 = load ptr, ptr %oc, align 8
  %oc10 = load ptr, ptr %oc, align 8
  %13 = call double @Vector3_static_dot(ptr null, ptr %oc9, ptr %oc10)
  %this11 = load ptr, ptr %this1, align 8
  %14 = getelementptr inbounds %Sphere, ptr %this11, i32 0, i32 3
  %15 = load double, ptr %14, align 8
  %this12 = load ptr, ptr %this1, align 8
  %16 = getelementptr inbounds %Sphere, ptr %this12, i32 0, i32 3
  %17 = load double, ptr %16, align 8
  %multmp13 = fmul double %15, %17
  %subtmp = fsub double %13, %multmp13
  store double %subtmp, ptr %c, align 8
  %b14 = load double, ptr %b, align 8
  %b15 = load double, ptr %b, align 8
  %multmp16 = fmul double %b14, %b15
  %a17 = load double, ptr %a, align 8
  %multmp18 = fmul double 4.000000e+00, %a17
  %c19 = load double, ptr %c, align 8
  %multmp20 = fmul double %multmp18, %c19
  %subtmp21 = fsub double %multmp16, %multmp20
  store double %subtmp21, ptr %discriminant, align 8
  %discriminant22 = load double, ptr %discriminant, align 8
  %cmptmp = fcmp olt double %discriminant22, 0.000000e+00
  br i1 %cmptmp, label %then, label %else

then:                                             ; preds = %entry
  ret double -1.000000e+00

else:                                             ; preds = %entry
  %b23 = load double, ptr %b, align 8
  %negtmp = fneg double %b23
  %discriminant24 = load double, ptr %discriminant, align 8
  %18 = call double @ts_math_sqrt(double %discriminant24)
  %subtmp25 = fsub double %negtmp, %18
  %a26 = load double, ptr %a, align 8
  %multmp27 = fmul double 2.000000e+00, %a26
  %divtmp = fdiv double %subtmp25, %multmp27
  ret double %divtmp

ifcont:                                           ; No predecessors!
  ret double 0.000000e+00
}

define ptr @__module_init_8747018653979234134(ptr %context) {
entry:
  %0 = call ptr @__ts_main(ptr null)
  %1 = call ptr @ts_value_make_object(ptr %0)
  ret ptr %1
}

define ptr @user_main(ptr %context) {
entry:
  %0 = call ptr @__module_init_8747018653979234134(ptr null)
  %1 = call ptr @ts_value_make_object(ptr %0)
  ret ptr %1
}

define ptr @__ts_main(ptr %context) {
entry:
  %end = alloca i64, align 8
  %i4 = alloca double, align 8
  %totalChecksum = alloca double, align 8
  %start = alloca i64, align 8
  %i = alloca double, align 8
  %iterations = alloca double, align 8
  store double 5.000000e+00, ptr %iterations, align 8
  %0 = call ptr @ts_string_create(ptr @0)
  call void @ts_console_log(ptr %0)
  store double 0.000000e+00, ptr %i, align 8
  br label %forcond

forcond:                                          ; preds = %forinc, %entry
  %i1 = load double, ptr %i, align 8
  %cmptmp = fcmp olt double %i1, 2.000000e+00
  br i1 %cmptmp, label %forloop, label %forafter

forloop:                                          ; preds = %forcond
  %1 = call double @render(ptr null)
  br label %forinc

forinc:                                           ; preds = %forloop
  %i2 = load double, ptr %i, align 8
  %incdec = fadd double %i2, 1.000000e+00
  store double %incdec, ptr %i, align 8
  br label %forcond

forafter:                                         ; preds = %forcond
  %2 = call i64 @Date_static_now()
  store i64 %2, ptr %start, align 8
  store double 0.000000e+00, ptr %totalChecksum, align 8
  store double 0.000000e+00, ptr %i4, align 8
  br label %forcond3

forcond3:                                         ; preds = %forinc10, %forafter
  %i5 = load double, ptr %i4, align 8
  %iterations6 = load double, ptr %iterations, align 8
  %cmptmp7 = fcmp olt double %i5, %iterations6
  br i1 %cmptmp7, label %forloop8, label %forafter13

forloop8:                                         ; preds = %forcond3
  %totalChecksum9 = load double, ptr %totalChecksum, align 8
  %3 = call double @render(ptr null)
  %addtmp = fadd double %totalChecksum9, %3
  store double %addtmp, ptr %totalChecksum, align 8
  br label %forinc10

forinc10:                                         ; preds = %forloop8
  %i11 = load double, ptr %i4, align 8
  %incdec12 = fadd double %i11, 1.000000e+00
  store double %incdec12, ptr %i4, align 8
  br label %forcond3

forafter13:                                       ; preds = %forcond3
  %4 = call i64 @Date_static_now()
  store i64 %4, ptr %end, align 8
  %5 = call ptr @ts_string_create(ptr @1)
  call void @ts_console_log(ptr %5)
  %6 = call ptr @ts_string_create(ptr @2)
  %totalChecksum14 = load double, ptr %totalChecksum, align 8
  %7 = call ptr @ts_string_from_double(double %totalChecksum14)
  %8 = call ptr @ts_string_concat(ptr %6, ptr %7)
  call void @ts_console_log(ptr %8)
  %9 = call ptr @ts_string_create(ptr @3)
  %end15 = load i64, ptr %end, align 8
  %start16 = load i64, ptr %start, align 8
  %subtmp = sub i64 %end15, %start16
  %iterations17 = load double, ptr %iterations, align 8
  %casttmp = sitofp i64 %subtmp to double
  %divtmp = fdiv double %casttmp, %iterations17
  %10 = call ptr @ts_string_from_double(double %divtmp)
  %11 = call ptr @ts_string_concat(ptr %9, ptr %10)
  %12 = call ptr @ts_string_create(ptr @4)
  %13 = call ptr @ts_string_concat(ptr %11, ptr %12)
  call void @ts_console_log(ptr %13)
  ret ptr null
}

define double @render(ptr %context) {
entry:
  %t = alloca double, align 8
  %i = alloca double, align 8
  %hitSphere = alloca ptr, align 8
  %closestT = alloca double, align 8
  %ray = alloca ptr, align 8
  %dir = alloca ptr, align 8
  %v = alloca double, align 8
  %u = alloca double, align 8
  %x = alloca double, align 8
  %y = alloca double, align 8
  %checksum = alloca double, align 8
  %origin = alloca ptr, align 8
  %spheres = alloca ptr, align 8
  %height = alloca double, align 8
  %width = alloca double, align 8
  store double 2.000000e+02, ptr %width, align 8
  store double 1.000000e+02, ptr %height, align 8
  %0 = call ptr @ts_array_create()
  %1 = call ptr @ts_alloc(i64 32)
  %2 = getelementptr inbounds %Sphere, ptr %1, i32 0, i32 0
  store ptr @Sphere_VTable_Global, ptr %2, align 8
  %3 = call ptr @ts_alloc(i64 32)
  %4 = getelementptr inbounds %Vector3, ptr %3, i32 0, i32 0
  store ptr @Vector3_VTable_Global, ptr %4, align 8
  %5 = call ptr @Vector3_constructor(ptr null, ptr %3, double 0.000000e+00, double 0.000000e+00, double -5.000000e+00)
  %6 = call ptr @ts_alloc(i64 32)
  %7 = getelementptr inbounds %Vector3, ptr %6, i32 0, i32 0
  store ptr @Vector3_VTable_Global, ptr %7, align 8
  %8 = call ptr @Vector3_constructor(ptr null, ptr %6, double 1.000000e+00, double 0.000000e+00, double 0.000000e+00)
  %9 = call ptr @Sphere_constructor(ptr null, ptr %1, ptr %3, double 1.000000e+00, ptr %6)
  %10 = call ptr @ts_value_make_object(ptr %1)
  call void @ts_array_push(ptr %0, ptr %10)
  %11 = call ptr @ts_alloc(i64 32)
  %12 = getelementptr inbounds %Sphere, ptr %11, i32 0, i32 0
  store ptr @Sphere_VTable_Global, ptr %12, align 8
  %13 = call ptr @ts_alloc(i64 32)
  %14 = getelementptr inbounds %Vector3, ptr %13, i32 0, i32 0
  store ptr @Vector3_VTable_Global, ptr %14, align 8
  %15 = call ptr @Vector3_constructor(ptr null, ptr %13, double 2.000000e+00, double 0.000000e+00, double -5.000000e+00)
  %16 = call ptr @ts_alloc(i64 32)
  %17 = getelementptr inbounds %Vector3, ptr %16, i32 0, i32 0
  store ptr @Vector3_VTable_Global, ptr %17, align 8
  %18 = call ptr @Vector3_constructor(ptr null, ptr %16, double 0.000000e+00, double 1.000000e+00, double 0.000000e+00)
  %19 = call ptr @Sphere_constructor(ptr null, ptr %11, ptr %13, double 1.000000e+00, ptr %16)
  %20 = call ptr @ts_value_make_object(ptr %11)
  call void @ts_array_push(ptr %0, ptr %20)
  %21 = call ptr @ts_alloc(i64 32)
  %22 = getelementptr inbounds %Sphere, ptr %21, i32 0, i32 0
  store ptr @Sphere_VTable_Global, ptr %22, align 8
  %23 = call ptr @ts_alloc(i64 32)
  %24 = getelementptr inbounds %Vector3, ptr %23, i32 0, i32 0
  store ptr @Vector3_VTable_Global, ptr %24, align 8
  %25 = call ptr @Vector3_constructor(ptr null, ptr %23, double -2.000000e+00, double 0.000000e+00, double -5.000000e+00)
  %26 = call ptr @ts_alloc(i64 32)
  %27 = getelementptr inbounds %Vector3, ptr %26, i32 0, i32 0
  store ptr @Vector3_VTable_Global, ptr %27, align 8
  %28 = call ptr @Vector3_constructor(ptr null, ptr %26, double 0.000000e+00, double 0.000000e+00, double 1.000000e+00)
  %29 = call ptr @Sphere_constructor(ptr null, ptr %21, ptr %23, double 1.000000e+00, ptr %26)
  %30 = call ptr @ts_value_make_object(ptr %21)
  call void @ts_array_push(ptr %0, ptr %30)
  %31 = call ptr @ts_alloc(i64 32)
  %32 = getelementptr inbounds %Sphere, ptr %31, i32 0, i32 0
  store ptr @Sphere_VTable_Global, ptr %32, align 8
  %33 = call ptr @ts_alloc(i64 32)
  %34 = getelementptr inbounds %Vector3, ptr %33, i32 0, i32 0
  store ptr @Vector3_VTable_Global, ptr %34, align 8
  %35 = call ptr @Vector3_constructor(ptr null, ptr %33, double 0.000000e+00, double -1.001000e+03, double -5.000000e+00)
  %36 = call ptr @ts_alloc(i64 32)
  %37 = getelementptr inbounds %Vector3, ptr %36, i32 0, i32 0
  store ptr @Vector3_VTable_Global, ptr %37, align 8
  %38 = call ptr @Vector3_constructor(ptr null, ptr %36, double 5.000000e-01, double 5.000000e-01, double 5.000000e-01)
  %39 = call ptr @Sphere_constructor(ptr null, ptr %31, ptr %33, double 1.000000e+03, ptr %36)
  %40 = call ptr @ts_value_make_object(ptr %31)
  call void @ts_array_push(ptr %0, ptr %40)
  store ptr %0, ptr %spheres, align 8
  %41 = call ptr @ts_alloc(i64 32)
  %42 = getelementptr inbounds %Vector3, ptr %41, i32 0, i32 0
  store ptr @Vector3_VTable_Global, ptr %42, align 8
  %43 = call ptr @Vector3_constructor(ptr null, ptr %41, double 0.000000e+00, double 0.000000e+00, double 0.000000e+00)
  store ptr %41, ptr %origin, align 8
  store double 0.000000e+00, ptr %checksum, align 8
  store double 0.000000e+00, ptr %y, align 8
  br label %forcond

forcond:                                          ; preds = %forinc52, %entry
  %y1 = load double, ptr %y, align 8
  %height2 = load double, ptr %height, align 8
  %cmptmp = fcmp olt double %y1, %height2
  br i1 %cmptmp, label %forloop, label %forafter55

forloop:                                          ; preds = %forcond
  store double 0.000000e+00, ptr %x, align 8
  br label %forcond3

forcond3:                                         ; preds = %forinc48, %forloop
  %x4 = load double, ptr %x, align 8
  %width5 = load double, ptr %width, align 8
  %cmptmp6 = fcmp olt double %x4, %width5
  br i1 %cmptmp6, label %forloop7, label %forafter51

forloop7:                                         ; preds = %forcond3
  %x8 = load double, ptr %x, align 8
  %width9 = load double, ptr %width, align 8
  %divtmp = fdiv double %width9, 2.000000e+00
  %subtmp = fsub double %x8, %divtmp
  %width10 = load double, ptr %width, align 8
  %divtmp11 = fdiv double %subtmp, %width10
  store double %divtmp11, ptr %u, align 8
  %y12 = load double, ptr %y, align 8
  %height13 = load double, ptr %height, align 8
  %divtmp14 = fdiv double %height13, 2.000000e+00
  %subtmp15 = fsub double %y12, %divtmp14
  %height16 = load double, ptr %height, align 8
  %divtmp17 = fdiv double %subtmp15, %height16
  store double %divtmp17, ptr %v, align 8
  %44 = call ptr @ts_alloc(i64 32)
  %45 = getelementptr inbounds %Vector3, ptr %44, i32 0, i32 0
  store ptr @Vector3_VTable_Global, ptr %45, align 8
  %u18 = load double, ptr %u, align 8
  %v19 = load double, ptr %v, align 8
  %46 = call ptr @Vector3_constructor(ptr null, ptr %44, double %u18, double %v19, double -1.000000e+00)
  %47 = call ptr @Vector3_static_normalize(ptr null, ptr %44)
  store ptr %47, ptr %dir, align 8
  %48 = call ptr @ts_alloc(i64 24)
  %49 = getelementptr inbounds %Ray, ptr %48, i32 0, i32 0
  store ptr @Ray_VTable_Global, ptr %49, align 8
  %origin20 = load ptr, ptr %origin, align 8
  %dir21 = load ptr, ptr %dir, align 8
  %50 = call ptr @Ray_constructor(ptr null, ptr %48, ptr %origin20, ptr %dir21)
  store ptr %48, ptr %ray, align 8
  store double 1.000000e+06, ptr %closestT, align 8
  store ptr null, ptr %hitSphere, align 8
  store double 0.000000e+00, ptr %i, align 8
  br label %forcond22

forcond22:                                        ; preds = %forinc, %forloop7
  %i23 = load double, ptr %i, align 8
  %spheres24 = load ptr, ptr %spheres, align 8
  %51 = call i64 @ts_array_length(ptr %spheres24)
  %casttmp = sitofp i64 %51 to double
  %cmptmp25 = fcmp olt double %i23, %casttmp
  br i1 %cmptmp25, label %forloop26, label %forafter

forloop26:                                        ; preds = %forcond22
  %spheres27 = load ptr, ptr %spheres, align 8
  %i28 = load double, ptr %i, align 8
  %52 = fptosi double %i28 to i64
  %53 = call ptr @ts_array_get(ptr %spheres27, i64 %52)
  %54 = call ptr @ts_value_get_object(ptr %53)
  %ray29 = load ptr, ptr %ray, align 8
  %55 = call double @Sphere_intersect(ptr null, ptr %54, ptr %ray29)
  store double %55, ptr %t, align 8
  %t30 = load double, ptr %t, align 8
  %cmptmp31 = fcmp ogt double %t30, 0.000000e+00
  %t32 = load double, ptr %t, align 8
  %closestT33 = load double, ptr %closestT, align 8
  %cmptmp34 = fcmp olt double %t32, %closestT33
  %andtmp = and i1 %cmptmp31, %cmptmp34
  br i1 %andtmp, label %then, label %ifcont

then:                                             ; preds = %forloop26
  %t35 = load double, ptr %t, align 8
  store double %t35, ptr %closestT, align 8
  %spheres36 = load ptr, ptr %spheres, align 8
  %i37 = load double, ptr %i, align 8
  %56 = fptosi double %i37 to i64
  %57 = call ptr @ts_array_get(ptr %spheres36, i64 %56)
  %58 = call ptr @ts_value_get_object(ptr %57)
  store ptr %58, ptr %hitSphere, align 8
  br label %ifcont

ifcont:                                           ; preds = %then, %forloop26
  br label %forinc

forinc:                                           ; preds = %ifcont
  %i38 = load double, ptr %i, align 8
  %incdec = fadd double %i38, 1.000000e+00
  store double %incdec, ptr %i, align 8
  br label %forcond22

forafter:                                         ; preds = %forcond22
  %hitSphere39 = load ptr, ptr %hitSphere, align 8
  %ifcond = icmp ne ptr %hitSphere39, null
  br i1 %ifcond, label %then40, label %ifcont47

then40:                                           ; preds = %forafter
  %checksum41 = load double, ptr %checksum, align 8
  %hitSphere42 = load ptr, ptr %hitSphere, align 8
  %59 = getelementptr inbounds %Sphere, ptr %hitSphere42, i32 0, i32 2
  %60 = load ptr, ptr %59, align 8
  %61 = getelementptr inbounds %Vector3, ptr %60, i32 0, i32 1
  %62 = load double, ptr %61, align 8
  %hitSphere43 = load ptr, ptr %hitSphere, align 8
  %63 = getelementptr inbounds %Sphere, ptr %hitSphere43, i32 0, i32 2
  %64 = load ptr, ptr %63, align 8
  %65 = getelementptr inbounds %Vector3, ptr %64, i32 0, i32 2
  %66 = load double, ptr %65, align 8
  %addtmp = fadd double %62, %66
  %hitSphere44 = load ptr, ptr %hitSphere, align 8
  %67 = getelementptr inbounds %Sphere, ptr %hitSphere44, i32 0, i32 2
  %68 = load ptr, ptr %67, align 8
  %69 = getelementptr inbounds %Vector3, ptr %68, i32 0, i32 3
  %70 = load double, ptr %69, align 8
  %addtmp45 = fadd double %addtmp, %70
  %addtmp46 = fadd double %checksum41, %addtmp45
  store double %addtmp46, ptr %checksum, align 8
  br label %ifcont47

ifcont47:                                         ; preds = %then40, %forafter
  br label %forinc48

forinc48:                                         ; preds = %ifcont47
  %x49 = load double, ptr %x, align 8
  %incdec50 = fadd double %x49, 1.000000e+00
  store double %incdec50, ptr %x, align 8
  br label %forcond3

forafter51:                                       ; preds = %forcond3
  br label %forinc52

forinc52:                                         ; preds = %forafter51
  %y53 = load double, ptr %y, align 8
  %incdec54 = fadd double %y53, 1.000000e+00
  store double %incdec54, ptr %y, align 8
  br label %forcond

forafter55:                                       ; preds = %forcond
  %checksum56 = load double, ptr %checksum, align 8
  ret double %checksum56
}

; Function Attrs: alwaysinline
define ptr @Vector3_static_add(ptr %context, ptr %a, ptr %b) #0 {
entry:
  %b2 = alloca ptr, align 8
  %a1 = alloca ptr, align 8
  store ptr %a, ptr %a1, align 8
  store ptr %b, ptr %b2, align 8
  %0 = call ptr @ts_alloc(i64 32)
  %1 = getelementptr inbounds %Vector3, ptr %0, i32 0, i32 0
  store ptr @Vector3_VTable_Global, ptr %1, align 8
  %a3 = load ptr, ptr %a1, align 8
  %2 = getelementptr inbounds %Vector3, ptr %a3, i32 0, i32 1
  %3 = load double, ptr %2, align 8
  %b4 = load ptr, ptr %b2, align 8
  %4 = getelementptr inbounds %Vector3, ptr %b4, i32 0, i32 1
  %5 = load double, ptr %4, align 8
  %addtmp = fadd double %3, %5
  %a5 = load ptr, ptr %a1, align 8
  %6 = getelementptr inbounds %Vector3, ptr %a5, i32 0, i32 2
  %7 = load double, ptr %6, align 8
  %b6 = load ptr, ptr %b2, align 8
  %8 = getelementptr inbounds %Vector3, ptr %b6, i32 0, i32 2
  %9 = load double, ptr %8, align 8
  %addtmp7 = fadd double %7, %9
  %a8 = load ptr, ptr %a1, align 8
  %10 = getelementptr inbounds %Vector3, ptr %a8, i32 0, i32 3
  %11 = load double, ptr %10, align 8
  %b9 = load ptr, ptr %b2, align 8
  %12 = getelementptr inbounds %Vector3, ptr %b9, i32 0, i32 3
  %13 = load double, ptr %12, align 8
  %addtmp10 = fadd double %11, %13
  %14 = call ptr @Vector3_constructor(ptr null, ptr %0, double %addtmp, double %addtmp7, double %addtmp10)
  ret ptr %0
}

; Function Attrs: alwaysinline
define ptr @Vector3_static_sub(ptr %context, ptr %a, ptr %b) #0 {
entry:
  %b2 = alloca ptr, align 8
  %a1 = alloca ptr, align 8
  store ptr %a, ptr %a1, align 8
  store ptr %b, ptr %b2, align 8
  %0 = call ptr @ts_alloc(i64 32)
  %1 = getelementptr inbounds %Vector3, ptr %0, i32 0, i32 0
  store ptr @Vector3_VTable_Global, ptr %1, align 8
  %a3 = load ptr, ptr %a1, align 8
  %2 = getelementptr inbounds %Vector3, ptr %a3, i32 0, i32 1
  %3 = load double, ptr %2, align 8
  %b4 = load ptr, ptr %b2, align 8
  %4 = getelementptr inbounds %Vector3, ptr %b4, i32 0, i32 1
  %5 = load double, ptr %4, align 8
  %subtmp = fsub double %3, %5
  %a5 = load ptr, ptr %a1, align 8
  %6 = getelementptr inbounds %Vector3, ptr %a5, i32 0, i32 2
  %7 = load double, ptr %6, align 8
  %b6 = load ptr, ptr %b2, align 8
  %8 = getelementptr inbounds %Vector3, ptr %b6, i32 0, i32 2
  %9 = load double, ptr %8, align 8
  %subtmp7 = fsub double %7, %9
  %a8 = load ptr, ptr %a1, align 8
  %10 = getelementptr inbounds %Vector3, ptr %a8, i32 0, i32 3
  %11 = load double, ptr %10, align 8
  %b9 = load ptr, ptr %b2, align 8
  %12 = getelementptr inbounds %Vector3, ptr %b9, i32 0, i32 3
  %13 = load double, ptr %12, align 8
  %subtmp10 = fsub double %11, %13
  %14 = call ptr @Vector3_constructor(ptr null, ptr %0, double %subtmp, double %subtmp7, double %subtmp10)
  ret ptr %0
}

; Function Attrs: alwaysinline
define ptr @Vector3_static_mul(ptr %context, ptr %a, double %b) #0 {
entry:
  %b2 = alloca double, align 8
  %a1 = alloca ptr, align 8
  store ptr %a, ptr %a1, align 8
  store double %b, ptr %b2, align 8
  %0 = call ptr @ts_alloc(i64 32)
  %1 = getelementptr inbounds %Vector3, ptr %0, i32 0, i32 0
  store ptr @Vector3_VTable_Global, ptr %1, align 8
  %a3 = load ptr, ptr %a1, align 8
  %2 = getelementptr inbounds %Vector3, ptr %a3, i32 0, i32 1
  %3 = load double, ptr %2, align 8
  %b4 = load double, ptr %b2, align 8
  %multmp = fmul double %3, %b4
  %a5 = load ptr, ptr %a1, align 8
  %4 = getelementptr inbounds %Vector3, ptr %a5, i32 0, i32 2
  %5 = load double, ptr %4, align 8
  %b6 = load double, ptr %b2, align 8
  %multmp7 = fmul double %5, %b6
  %a8 = load ptr, ptr %a1, align 8
  %6 = getelementptr inbounds %Vector3, ptr %a8, i32 0, i32 3
  %7 = load double, ptr %6, align 8
  %b9 = load double, ptr %b2, align 8
  %multmp10 = fmul double %7, %b9
  %8 = call ptr @Vector3_constructor(ptr null, ptr %0, double %multmp, double %multmp7, double %multmp10)
  ret ptr %0
}

; Function Attrs: alwaysinline
define double @Vector3_static_dot(ptr %context, ptr %a, ptr %b) #0 {
entry:
  %b2 = alloca ptr, align 8
  %a1 = alloca ptr, align 8
  store ptr %a, ptr %a1, align 8
  store ptr %b, ptr %b2, align 8
  %a3 = load ptr, ptr %a1, align 8
  %0 = getelementptr inbounds %Vector3, ptr %a3, i32 0, i32 1
  %1 = load double, ptr %0, align 8
  %b4 = load ptr, ptr %b2, align 8
  %2 = getelementptr inbounds %Vector3, ptr %b4, i32 0, i32 1
  %3 = load double, ptr %2, align 8
  %multmp = fmul double %1, %3
  %a5 = load ptr, ptr %a1, align 8
  %4 = getelementptr inbounds %Vector3, ptr %a5, i32 0, i32 2
  %5 = load double, ptr %4, align 8
  %b6 = load ptr, ptr %b2, align 8
  %6 = getelementptr inbounds %Vector3, ptr %b6, i32 0, i32 2
  %7 = load double, ptr %6, align 8
  %multmp7 = fmul double %5, %7
  %addtmp = fadd double %multmp, %multmp7
  %a8 = load ptr, ptr %a1, align 8
  %8 = getelementptr inbounds %Vector3, ptr %a8, i32 0, i32 3
  %9 = load double, ptr %8, align 8
  %b9 = load ptr, ptr %b2, align 8
  %10 = getelementptr inbounds %Vector3, ptr %b9, i32 0, i32 3
  %11 = load double, ptr %10, align 8
  %multmp10 = fmul double %9, %11
  %addtmp11 = fadd double %addtmp, %multmp10
  ret double %addtmp11
}

; Function Attrs: alwaysinline
define ptr @Vector3_static_cross(ptr %context, ptr %a, ptr %b) #0 {
entry:
  %b2 = alloca ptr, align 8
  %a1 = alloca ptr, align 8
  store ptr %a, ptr %a1, align 8
  store ptr %b, ptr %b2, align 8
  %0 = call ptr @ts_alloc(i64 32)
  %1 = getelementptr inbounds %Vector3, ptr %0, i32 0, i32 0
  store ptr @Vector3_VTable_Global, ptr %1, align 8
  %a3 = load ptr, ptr %a1, align 8
  %2 = getelementptr inbounds %Vector3, ptr %a3, i32 0, i32 2
  %3 = load double, ptr %2, align 8
  %b4 = load ptr, ptr %b2, align 8
  %4 = getelementptr inbounds %Vector3, ptr %b4, i32 0, i32 3
  %5 = load double, ptr %4, align 8
  %multmp = fmul double %3, %5
  %a5 = load ptr, ptr %a1, align 8
  %6 = getelementptr inbounds %Vector3, ptr %a5, i32 0, i32 3
  %7 = load double, ptr %6, align 8
  %b6 = load ptr, ptr %b2, align 8
  %8 = getelementptr inbounds %Vector3, ptr %b6, i32 0, i32 2
  %9 = load double, ptr %8, align 8
  %multmp7 = fmul double %7, %9
  %subtmp = fsub double %multmp, %multmp7
  %a8 = load ptr, ptr %a1, align 8
  %10 = getelementptr inbounds %Vector3, ptr %a8, i32 0, i32 3
  %11 = load double, ptr %10, align 8
  %b9 = load ptr, ptr %b2, align 8
  %12 = getelementptr inbounds %Vector3, ptr %b9, i32 0, i32 1
  %13 = load double, ptr %12, align 8
  %multmp10 = fmul double %11, %13
  %a11 = load ptr, ptr %a1, align 8
  %14 = getelementptr inbounds %Vector3, ptr %a11, i32 0, i32 1
  %15 = load double, ptr %14, align 8
  %b12 = load ptr, ptr %b2, align 8
  %16 = getelementptr inbounds %Vector3, ptr %b12, i32 0, i32 3
  %17 = load double, ptr %16, align 8
  %multmp13 = fmul double %15, %17
  %subtmp14 = fsub double %multmp10, %multmp13
  %a15 = load ptr, ptr %a1, align 8
  %18 = getelementptr inbounds %Vector3, ptr %a15, i32 0, i32 1
  %19 = load double, ptr %18, align 8
  %b16 = load ptr, ptr %b2, align 8
  %20 = getelementptr inbounds %Vector3, ptr %b16, i32 0, i32 2
  %21 = load double, ptr %20, align 8
  %multmp17 = fmul double %19, %21
  %a18 = load ptr, ptr %a1, align 8
  %22 = getelementptr inbounds %Vector3, ptr %a18, i32 0, i32 2
  %23 = load double, ptr %22, align 8
  %b19 = load ptr, ptr %b2, align 8
  %24 = getelementptr inbounds %Vector3, ptr %b19, i32 0, i32 1
  %25 = load double, ptr %24, align 8
  %multmp20 = fmul double %23, %25
  %subtmp21 = fsub double %multmp17, %multmp20
  %26 = call ptr @Vector3_constructor(ptr null, ptr %0, double %subtmp, double %subtmp14, double %subtmp21)
  ret ptr %0
}

; Function Attrs: alwaysinline
define ptr @Vector3_static_normalize(ptr %context, ptr %a) #0 {
entry:
  %len = alloca double, align 8
  %a1 = alloca ptr, align 8
  store ptr %a, ptr %a1, align 8
  %a2 = load ptr, ptr %a1, align 8
  %a3 = load ptr, ptr %a1, align 8
  %0 = call double @Vector3_static_dot(ptr null, ptr %a2, ptr %a3)
  %1 = call double @ts_math_sqrt(double %0)
  store double %1, ptr %len, align 8
  %a4 = load ptr, ptr %a1, align 8
  %len5 = load double, ptr %len, align 8
  %divtmp = fdiv double 1.000000e+00, %len5
  %2 = call ptr @Vector3_static_mul(ptr null, ptr %a4, double %divtmp)
  ret ptr %2
}

declare ptr @ts_value_make_object(ptr)

declare ptr @ts_string_create(ptr)

declare void @ts_console_log(ptr)

declare i64 @Date_static_now()

declare ptr @ts_string_from_double(double)

declare ptr @ts_string_concat(ptr, ptr)

declare ptr @ts_array_create()

declare void @ts_array_push(ptr, ptr)

declare ptr @ts_alloc(i64)

declare i64 @ts_array_length(ptr)

declare ptr @ts_array_get(ptr, i64)

declare ptr @ts_value_get_object(ptr)

declare double @ts_math_sqrt(double)

declare double @ts_value_get_double(ptr)

declare i32 @ts_main(i32, ptr, ptr)

define i32 @main(i32 %0, ptr %1) {
entry:
  %2 = call i32 @ts_main(i32 %0, ptr %1, ptr @user_main)
  ret i32 0
}

attributes #0 = { alwaysinline }
