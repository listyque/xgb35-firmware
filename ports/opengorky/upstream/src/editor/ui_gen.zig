const __root = @This();
pub const __builtin = @import("std").zig.c_translation.builtins;
pub const __helpers = @import("std").zig.c_translation.helpers;
pub const __builtin_va_list = [*c]u8;
pub const __gnuc_va_list = __builtin_va_list;
pub const va_list = __gnuc_va_list;
pub extern fn __mingw_get_crt_info() [*c]const u8;
pub const rsize_t = usize;
pub const ptrdiff_t = c_longlong;
pub const wchar_t = c_ushort;
pub const wint_t = c_ushort;
pub const wctype_t = c_ushort;
pub const errno_t = c_int;
pub const __time32_t = c_long;
pub const __time64_t = c_longlong;
pub const time_t = __time64_t;
pub extern fn _invalid_parameter_noinfo() void;
pub extern fn _invalid_parameter_noinfo_noreturn() noreturn;
pub extern fn _invoke_watson(expression: [*c]const wchar_t, function_name: [*c]const wchar_t, file_name: [*c]const wchar_t, line_number: c_uint, reserved: c_ulonglong) noreturn;
pub const struct_threadlocaleinfostruct = opaque {};
pub const struct_threadmbcinfostruct = opaque {};
pub const pthreadlocinfo = ?*struct_threadlocaleinfostruct;
pub const pthreadmbcinfo = ?*struct_threadmbcinfostruct;
pub const struct___lc_time_data = opaque {};
pub const struct_localeinfo_struct = extern struct {
    locinfo: pthreadlocinfo = null,
    mbcinfo: pthreadmbcinfo = null,
};
pub const _locale_tstruct = struct_localeinfo_struct;
pub const _locale_t = [*c]struct_localeinfo_struct;
pub const struct_tagLC_ID = extern struct {
    wLanguage: c_ushort = 0,
    wCountry: c_ushort = 0,
    wCodePage: c_ushort = 0,
};
pub const LC_ID = struct_tagLC_ID;
pub const LPLC_ID = [*c]struct_tagLC_ID;
pub const max_align_t = extern struct {
    __aro_max_align_ll: c_longlong = 0,
    __aro_max_align_ld: c_longdouble = 0,
};
pub const int_least8_t = i8;
pub const uint_least8_t = u8;
pub const int_least16_t = c_short;
pub const uint_least16_t = c_ushort;
pub const int_least32_t = c_int;
pub const uint_least32_t = c_uint;
pub const int_least64_t = c_longlong;
pub const uint_least64_t = c_ulonglong;
pub const int_fast8_t = i8;
pub const uint_fast8_t = u8;
pub const int_fast16_t = c_short;
pub const uint_fast16_t = c_ushort;
pub const int_fast32_t = c_int;
pub const uint_fast32_t = c_uint;
pub const int_fast64_t = c_longlong;
pub const uint_fast64_t = c_ulonglong;
pub const intmax_t = c_longlong;
pub const uintmax_t = c_ulonglong;
pub const __m64 = @Vector(1, c_longlong);
pub const __v1di = @Vector(1, c_longlong);
pub const __v2si = @Vector(2, c_int);
pub const __v4hi = @Vector(4, c_short);
pub const __v8qi = @Vector(8, u8);
pub const __v1du = @Vector(1, c_ulonglong);
pub const __v2su = @Vector(2, c_uint);
pub const __v4hu = @Vector(4, c_ushort);
pub const __v8qu = @Vector(8, u8);
pub const __v8qs = @Vector(8, i8);
pub const __m128i = @Vector(2, c_longlong);
pub const __v2di = @Vector(2, c_longlong);
pub const __v4si = @Vector(4, c_int);
pub const __v8hi = @Vector(8, c_short);
pub const __v16qi = @Vector(16, u8);
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\mmintrin.h:68:3: warning: TODO implement function '__builtin_ia32_emms' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\mmintrin.h:67:5: warning: unable to translate function, demoted to extern
pub extern fn _mm_empty() void;
pub inline fn _mm_cvtsi32_si64(arg___i: c_int) __m64 {
    var __i = arg___i;
    _ = &__i;
    return @bitCast(@as(__v2si, [2]c_int{
        __i,
        0,
    }));
}
pub inline fn _mm_cvtsi64_si32(arg___m: __m64) c_int {
    var __m = arg___m;
    _ = &__m;
    return @as(__v2si, @bitCast(__m))[@as(c_int, 0)];
}
pub inline fn _mm_cvtsi64_m64(arg___i: c_longlong) __m64 {
    var __i = arg___i;
    _ = &__i;
    return @as(__v1di, [1]c_longlong{
        __i,
    });
}
pub inline fn _mm_cvtm64_si64(arg___m: __m64) c_longlong {
    var __m = arg___m;
    _ = &__m;
    return __m[@as(c_int, 0)];
}
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\mmintrin.h:150:20: warning: TODO implement function '__builtin_ia32_packsswb128' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\mmintrin.h:148:49: warning: unable to translate function, demoted to extern
pub extern fn _mm_packs_pi16(arg___m1: __m64, arg___m2: __m64) __m64;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\mmintrin.h:175:20: warning: TODO implement function '__builtin_ia32_packssdw128' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\mmintrin.h:173:49: warning: unable to translate function, demoted to extern
pub extern fn _mm_packs_pi32(arg___m1: __m64, arg___m2: __m64) __m64;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\mmintrin.h:200:20: warning: TODO implement function '__builtin_ia32_packuswb128' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\mmintrin.h:198:49: warning: unable to translate function, demoted to extern
pub extern fn _mm_packs_pu16(arg___m1: __m64, arg___m2: __m64) __m64;
pub inline fn _mm_unpackhi_pi8(arg___m1: __m64, arg___m2: __m64) __m64 {
    var __m1 = arg___m1;
    _ = &__m1;
    var __m2 = arg___m2;
    _ = &__m2;
    return @bitCast(@shuffle(@typeInfo(@TypeOf(@as(__v8qi, @bitCast(__m1)))).vector.child, @as(__v8qi, @bitCast(__m1)), @as(__v8qi, @bitCast(__m2)), @Vector(8, i32){
        __helpers.shuffleVectorIndex(4, @typeInfo(@TypeOf(@as(__v8qi, @bitCast(__m1)))).vector.len),
        __helpers.shuffleVectorIndex(12, @typeInfo(@TypeOf(@as(__v8qi, @bitCast(__m1)))).vector.len),
        __helpers.shuffleVectorIndex(5, @typeInfo(@TypeOf(@as(__v8qi, @bitCast(__m1)))).vector.len),
        __helpers.shuffleVectorIndex(13, @typeInfo(@TypeOf(@as(__v8qi, @bitCast(__m1)))).vector.len),
        __helpers.shuffleVectorIndex(6, @typeInfo(@TypeOf(@as(__v8qi, @bitCast(__m1)))).vector.len),
        __helpers.shuffleVectorIndex(14, @typeInfo(@TypeOf(@as(__v8qi, @bitCast(__m1)))).vector.len),
        __helpers.shuffleVectorIndex(7, @typeInfo(@TypeOf(@as(__v8qi, @bitCast(__m1)))).vector.len),
        __helpers.shuffleVectorIndex(15, @typeInfo(@TypeOf(@as(__v8qi, @bitCast(__m1)))).vector.len),
    }));
}
pub inline fn _mm_unpackhi_pi16(arg___m1: __m64, arg___m2: __m64) __m64 {
    var __m1 = arg___m1;
    _ = &__m1;
    var __m2 = arg___m2;
    _ = &__m2;
    return @bitCast(@shuffle(@typeInfo(@TypeOf(@as(__v4hi, @bitCast(__m1)))).vector.child, @as(__v4hi, @bitCast(__m1)), @as(__v4hi, @bitCast(__m2)), @Vector(4, i32){
        __helpers.shuffleVectorIndex(2, @typeInfo(@TypeOf(@as(__v4hi, @bitCast(__m1)))).vector.len),
        __helpers.shuffleVectorIndex(6, @typeInfo(@TypeOf(@as(__v4hi, @bitCast(__m1)))).vector.len),
        __helpers.shuffleVectorIndex(3, @typeInfo(@TypeOf(@as(__v4hi, @bitCast(__m1)))).vector.len),
        __helpers.shuffleVectorIndex(7, @typeInfo(@TypeOf(@as(__v4hi, @bitCast(__m1)))).vector.len),
    }));
}
pub inline fn _mm_unpackhi_pi32(arg___m1: __m64, arg___m2: __m64) __m64 {
    var __m1 = arg___m1;
    _ = &__m1;
    var __m2 = arg___m2;
    _ = &__m2;
    return @bitCast(@shuffle(@typeInfo(@TypeOf(@as(__v2si, @bitCast(__m1)))).vector.child, @as(__v2si, @bitCast(__m1)), @as(__v2si, @bitCast(__m2)), @Vector(2, i32){
        __helpers.shuffleVectorIndex(1, @typeInfo(@TypeOf(@as(__v2si, @bitCast(__m1)))).vector.len),
        __helpers.shuffleVectorIndex(3, @typeInfo(@TypeOf(@as(__v2si, @bitCast(__m1)))).vector.len),
    }));
}
pub inline fn _mm_unpacklo_pi8(arg___m1: __m64, arg___m2: __m64) __m64 {
    var __m1 = arg___m1;
    _ = &__m1;
    var __m2 = arg___m2;
    _ = &__m2;
    return @bitCast(@shuffle(@typeInfo(@TypeOf(@as(__v8qi, @bitCast(__m1)))).vector.child, @as(__v8qi, @bitCast(__m1)), @as(__v8qi, @bitCast(__m2)), @Vector(8, i32){
        __helpers.shuffleVectorIndex(0, @typeInfo(@TypeOf(@as(__v8qi, @bitCast(__m1)))).vector.len),
        __helpers.shuffleVectorIndex(8, @typeInfo(@TypeOf(@as(__v8qi, @bitCast(__m1)))).vector.len),
        __helpers.shuffleVectorIndex(1, @typeInfo(@TypeOf(@as(__v8qi, @bitCast(__m1)))).vector.len),
        __helpers.shuffleVectorIndex(9, @typeInfo(@TypeOf(@as(__v8qi, @bitCast(__m1)))).vector.len),
        __helpers.shuffleVectorIndex(2, @typeInfo(@TypeOf(@as(__v8qi, @bitCast(__m1)))).vector.len),
        __helpers.shuffleVectorIndex(10, @typeInfo(@TypeOf(@as(__v8qi, @bitCast(__m1)))).vector.len),
        __helpers.shuffleVectorIndex(3, @typeInfo(@TypeOf(@as(__v8qi, @bitCast(__m1)))).vector.len),
        __helpers.shuffleVectorIndex(11, @typeInfo(@TypeOf(@as(__v8qi, @bitCast(__m1)))).vector.len),
    }));
}
pub inline fn _mm_unpacklo_pi16(arg___m1: __m64, arg___m2: __m64) __m64 {
    var __m1 = arg___m1;
    _ = &__m1;
    var __m2 = arg___m2;
    _ = &__m2;
    return @bitCast(@shuffle(@typeInfo(@TypeOf(@as(__v4hi, @bitCast(__m1)))).vector.child, @as(__v4hi, @bitCast(__m1)), @as(__v4hi, @bitCast(__m2)), @Vector(4, i32){
        __helpers.shuffleVectorIndex(0, @typeInfo(@TypeOf(@as(__v4hi, @bitCast(__m1)))).vector.len),
        __helpers.shuffleVectorIndex(4, @typeInfo(@TypeOf(@as(__v4hi, @bitCast(__m1)))).vector.len),
        __helpers.shuffleVectorIndex(1, @typeInfo(@TypeOf(@as(__v4hi, @bitCast(__m1)))).vector.len),
        __helpers.shuffleVectorIndex(5, @typeInfo(@TypeOf(@as(__v4hi, @bitCast(__m1)))).vector.len),
    }));
}
pub inline fn _mm_unpacklo_pi32(arg___m1: __m64, arg___m2: __m64) __m64 {
    var __m1 = arg___m1;
    _ = &__m1;
    var __m2 = arg___m2;
    _ = &__m2;
    return @bitCast(@shuffle(@typeInfo(@TypeOf(@as(__v2si, @bitCast(__m1)))).vector.child, @as(__v2si, @bitCast(__m1)), @as(__v2si, @bitCast(__m2)), @Vector(2, i32){
        __helpers.shuffleVectorIndex(0, @typeInfo(@TypeOf(@as(__v2si, @bitCast(__m1)))).vector.len),
        __helpers.shuffleVectorIndex(2, @typeInfo(@TypeOf(@as(__v2si, @bitCast(__m1)))).vector.len),
    }));
}
pub inline fn _mm_add_pi8(arg___m1: __m64, arg___m2: __m64) __m64 {
    var __m1 = arg___m1;
    _ = &__m1;
    var __m2 = arg___m2;
    _ = &__m2;
    return @bitCast(@as(__v8qu, @bitCast(__m1)) + @as(__v8qu, @bitCast(__m2)));
}
pub inline fn _mm_add_pi16(arg___m1: __m64, arg___m2: __m64) __m64 {
    var __m1 = arg___m1;
    _ = &__m1;
    var __m2 = arg___m2;
    _ = &__m2;
    return @bitCast(@as(__v4hu, @bitCast(__m1)) + @as(__v4hu, @bitCast(__m2)));
}
pub inline fn _mm_add_pi32(arg___m1: __m64, arg___m2: __m64) __m64 {
    var __m1 = arg___m1;
    _ = &__m1;
    var __m2 = arg___m2;
    _ = &__m2;
    return @bitCast(@as(__v2su, @bitCast(__m1)) + @as(__v2su, @bitCast(__m2)));
}
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\mmintrin.h:422:17: warning: TODO implement function '__builtin_elementwise_add_sat' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\mmintrin.h:420:49: warning: unable to translate function, demoted to extern
pub extern fn _mm_adds_pi8(arg___m1: __m64, arg___m2: __m64) __m64;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\mmintrin.h:445:17: warning: TODO implement function '__builtin_elementwise_add_sat' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\mmintrin.h:443:49: warning: unable to translate function, demoted to extern
pub extern fn _mm_adds_pi16(arg___m1: __m64, arg___m2: __m64) __m64;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\mmintrin.h:467:17: warning: TODO implement function '__builtin_elementwise_add_sat' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\mmintrin.h:465:49: warning: unable to translate function, demoted to extern
pub extern fn _mm_adds_pu8(arg___m1: __m64, arg___m2: __m64) __m64;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\mmintrin.h:489:17: warning: TODO implement function '__builtin_elementwise_add_sat' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\mmintrin.h:487:49: warning: unable to translate function, demoted to extern
pub extern fn _mm_adds_pu16(arg___m1: __m64, arg___m2: __m64) __m64;
pub inline fn _mm_sub_pi8(arg___m1: __m64, arg___m2: __m64) __m64 {
    var __m1 = arg___m1;
    _ = &__m1;
    var __m2 = arg___m2;
    _ = &__m2;
    return @bitCast(@as(__v8qu, @bitCast(__m1)) - @as(__v8qu, @bitCast(__m2)));
}
pub inline fn _mm_sub_pi16(arg___m1: __m64, arg___m2: __m64) __m64 {
    var __m1 = arg___m1;
    _ = &__m1;
    var __m2 = arg___m2;
    _ = &__m2;
    return @bitCast(@as(__v4hu, @bitCast(__m1)) - @as(__v4hu, @bitCast(__m2)));
}
pub inline fn _mm_sub_pi32(arg___m1: __m64, arg___m2: __m64) __m64 {
    var __m1 = arg___m1;
    _ = &__m1;
    var __m2 = arg___m2;
    _ = &__m2;
    return @bitCast(@as(__v2su, @bitCast(__m1)) - @as(__v2su, @bitCast(__m2)));
}
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\mmintrin.h:572:17: warning: TODO implement function '__builtin_elementwise_sub_sat' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\mmintrin.h:570:49: warning: unable to translate function, demoted to extern
pub extern fn _mm_subs_pi8(arg___m1: __m64, arg___m2: __m64) __m64;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\mmintrin.h:595:17: warning: TODO implement function '__builtin_elementwise_sub_sat' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\mmintrin.h:593:49: warning: unable to translate function, demoted to extern
pub extern fn _mm_subs_pi16(arg___m1: __m64, arg___m2: __m64) __m64;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\mmintrin.h:618:17: warning: TODO implement function '__builtin_elementwise_sub_sat' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\mmintrin.h:616:49: warning: unable to translate function, demoted to extern
pub extern fn _mm_subs_pu8(arg___m1: __m64, arg___m2: __m64) __m64;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\mmintrin.h:641:17: warning: TODO implement function '__builtin_elementwise_sub_sat' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\mmintrin.h:639:49: warning: unable to translate function, demoted to extern
pub extern fn _mm_subs_pu16(arg___m1: __m64, arg___m2: __m64) __m64;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\mmintrin.h:667:20: warning: TODO implement function '__builtin_ia32_pmaddwd128' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\mmintrin.h:665:49: warning: unable to translate function, demoted to extern
pub extern fn _mm_madd_pi16(arg___m1: __m64, arg___m2: __m64) __m64;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\mmintrin.h:688:20: warning: TODO implement function '__builtin_ia32_pmulhw128' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\mmintrin.h:686:49: warning: unable to translate function, demoted to extern
pub extern fn _mm_mulhi_pi16(arg___m1: __m64, arg___m2: __m64) __m64;
pub inline fn _mm_mullo_pi16(arg___m1: __m64, arg___m2: __m64) __m64 {
    var __m1 = arg___m1;
    _ = &__m1;
    var __m2 = arg___m2;
    _ = &__m2;
    return @bitCast(@as(__v4hu, @bitCast(__m1)) * @as(__v4hu, @bitCast(__m2)));
}
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\mmintrin.h:732:20: warning: TODO implement function '__builtin_ia32_psllw128' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\mmintrin.h:730:1: warning: unable to translate function, demoted to extern
pub extern fn _mm_sll_pi16(arg___m: __m64, arg___count: __m64) __m64;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\mmintrin.h:754:20: warning: TODO implement function '__builtin_ia32_psllwi128' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\mmintrin.h:752:49: warning: unable to translate function, demoted to extern
pub extern fn _mm_slli_pi16(arg___m: __m64, arg___count: c_int) __m64;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\mmintrin.h:777:20: warning: TODO implement function '__builtin_ia32_pslld128' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\mmintrin.h:775:1: warning: unable to translate function, demoted to extern
pub extern fn _mm_sll_pi32(arg___m: __m64, arg___count: __m64) __m64;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\mmintrin.h:799:20: warning: TODO implement function '__builtin_ia32_pslldi128' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\mmintrin.h:797:49: warning: unable to translate function, demoted to extern
pub extern fn _mm_slli_pi32(arg___m: __m64, arg___count: c_int) __m64;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\mmintrin.h:819:20: warning: TODO implement function '__builtin_ia32_psllq128' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\mmintrin.h:817:1: warning: unable to translate function, demoted to extern
pub extern fn _mm_sll_si64(arg___m: __m64, arg___count: __m64) __m64;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\mmintrin.h:839:20: warning: TODO implement function '__builtin_ia32_psllqi128' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\mmintrin.h:837:49: warning: unable to translate function, demoted to extern
pub extern fn _mm_slli_si64(arg___m: __m64, arg___count: c_int) __m64;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\mmintrin.h:863:20: warning: TODO implement function '__builtin_ia32_psraw128' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\mmintrin.h:861:1: warning: unable to translate function, demoted to extern
pub extern fn _mm_sra_pi16(arg___m: __m64, arg___count: __m64) __m64;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\mmintrin.h:886:20: warning: TODO implement function '__builtin_ia32_psrawi128' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\mmintrin.h:884:49: warning: unable to translate function, demoted to extern
pub extern fn _mm_srai_pi16(arg___m: __m64, arg___count: c_int) __m64;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\mmintrin.h:910:20: warning: TODO implement function '__builtin_ia32_psrad128' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\mmintrin.h:908:1: warning: unable to translate function, demoted to extern
pub extern fn _mm_sra_pi32(arg___m: __m64, arg___count: __m64) __m64;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\mmintrin.h:933:20: warning: TODO implement function '__builtin_ia32_psradi128' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\mmintrin.h:931:49: warning: unable to translate function, demoted to extern
pub extern fn _mm_srai_pi32(arg___m: __m64, arg___count: c_int) __m64;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\mmintrin.h:956:20: warning: TODO implement function '__builtin_ia32_psrlw128' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\mmintrin.h:954:1: warning: unable to translate function, demoted to extern
pub extern fn _mm_srl_pi16(arg___m: __m64, arg___count: __m64) __m64;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\mmintrin.h:978:20: warning: TODO implement function '__builtin_ia32_psrlwi128' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\mmintrin.h:976:49: warning: unable to translate function, demoted to extern
pub extern fn _mm_srli_pi16(arg___m: __m64, arg___count: c_int) __m64;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\mmintrin.h:1001:20: warning: TODO implement function '__builtin_ia32_psrld128' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\mmintrin.h:999:1: warning: unable to translate function, demoted to extern
pub extern fn _mm_srl_pi32(arg___m: __m64, arg___count: __m64) __m64;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\mmintrin.h:1023:20: warning: TODO implement function '__builtin_ia32_psrldi128' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\mmintrin.h:1021:49: warning: unable to translate function, demoted to extern
pub extern fn _mm_srli_pi32(arg___m: __m64, arg___count: c_int) __m64;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\mmintrin.h:1043:20: warning: TODO implement function '__builtin_ia32_psrlq128' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\mmintrin.h:1041:1: warning: unable to translate function, demoted to extern
pub extern fn _mm_srl_si64(arg___m: __m64, arg___count: __m64) __m64;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\mmintrin.h:1064:20: warning: TODO implement function '__builtin_ia32_psrlqi128' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\mmintrin.h:1062:49: warning: unable to translate function, demoted to extern
pub extern fn _mm_srli_si64(arg___m: __m64, arg___count: c_int) __m64;
pub inline fn _mm_and_si64(arg___m1: __m64, arg___m2: __m64) __m64 {
    var __m1 = arg___m1;
    _ = &__m1;
    var __m2 = arg___m2;
    _ = &__m2;
    return @bitCast(@as(__v1du, @bitCast(__m1)) & @as(__v1du, @bitCast(__m2)));
}
pub inline fn _mm_andnot_si64(arg___m1: __m64, arg___m2: __m64) __m64 {
    var __m1 = arg___m1;
    _ = &__m1;
    var __m2 = arg___m2;
    _ = &__m2;
    return @bitCast(~@as(__v1du, @bitCast(__m1)) & @as(__v1du, @bitCast(__m2)));
}
pub inline fn _mm_or_si64(arg___m1: __m64, arg___m2: __m64) __m64 {
    var __m1 = arg___m1;
    _ = &__m1;
    var __m2 = arg___m2;
    _ = &__m2;
    return @bitCast(@as(__v1du, @bitCast(__m1)) | @as(__v1du, @bitCast(__m2)));
}
pub inline fn _mm_xor_si64(arg___m1: __m64, arg___m2: __m64) __m64 {
    var __m1 = arg___m1;
    _ = &__m1;
    var __m2 = arg___m2;
    _ = &__m2;
    return @bitCast(@as(__v1du, @bitCast(__m1)) ^ @as(__v1du, @bitCast(__m2)));
}
pub inline fn _mm_cmpeq_pi8(arg___m1: __m64, arg___m2: __m64) __m64 {
    var __m1 = arg___m1;
    _ = &__m1;
    var __m2 = arg___m2;
    _ = &__m2;
    return @bitCast(@as(__v8qi, @bitCast(__m1)) == @as(__v8qi, @bitCast(__m2)));
}
pub inline fn _mm_cmpeq_pi16(arg___m1: __m64, arg___m2: __m64) __m64 {
    var __m1 = arg___m1;
    _ = &__m1;
    var __m2 = arg___m2;
    _ = &__m2;
    return @bitCast(@as(__v4hi, @bitCast(__m1)) == @as(__v4hi, @bitCast(__m2)));
}
pub inline fn _mm_cmpeq_pi32(arg___m1: __m64, arg___m2: __m64) __m64 {
    var __m1 = arg___m1;
    _ = &__m1;
    var __m2 = arg___m2;
    _ = &__m2;
    return @bitCast(@as(__v2si, @bitCast(__m1)) == @as(__v2si, @bitCast(__m2)));
}
pub inline fn _mm_cmpgt_pi8(arg___m1: __m64, arg___m2: __m64) __m64 {
    var __m1 = arg___m1;
    _ = &__m1;
    var __m2 = arg___m2;
    _ = &__m2;
    return @bitCast(@as(__v8qs, @bitCast(__m1)) > @as(__v8qs, @bitCast(__m2)));
}
pub inline fn _mm_cmpgt_pi16(arg___m1: __m64, arg___m2: __m64) __m64 {
    var __m1 = arg___m1;
    _ = &__m1;
    var __m2 = arg___m2;
    _ = &__m2;
    return @bitCast(@as(__v4hi, @bitCast(__m1)) > @as(__v4hi, @bitCast(__m2)));
}
pub inline fn _mm_cmpgt_pi32(arg___m1: __m64, arg___m2: __m64) __m64 {
    var __m1 = arg___m1;
    _ = &__m1;
    var __m2 = arg___m2;
    _ = &__m2;
    return @bitCast(@as(__v2si, @bitCast(__m1)) > @as(__v2si, @bitCast(__m2)));
}
pub inline fn _mm_setzero_si64() __m64 {
    return [1]c_longlong{
        0,
    };
}
pub inline fn _mm_set_pi32(arg___i1: c_int, arg___i0: c_int) __m64 {
    var __i1 = arg___i1;
    _ = &__i1;
    var __i0 = arg___i0;
    _ = &__i0;
    return @bitCast(@as(__v2si, [2]c_int{
        __i0,
        __i1,
    }));
}
pub inline fn _mm_set_pi16(arg___s3: c_short, arg___s2: c_short, arg___s1: c_short, arg___s0: c_short) __m64 {
    var __s3 = arg___s3;
    _ = &__s3;
    var __s2 = arg___s2;
    _ = &__s2;
    var __s1 = arg___s1;
    _ = &__s1;
    var __s0 = arg___s0;
    _ = &__s0;
    return @bitCast(@as(__v4hi, [4]c_short{
        __s0,
        __s1,
        __s2,
        __s3,
    }));
}
pub inline fn _mm_set_pi8(arg___b7: u8, arg___b6: u8, arg___b5: u8, arg___b4: u8, arg___b3: u8, arg___b2: u8, arg___b1: u8, arg___b0: u8) __m64 {
    var __b7 = arg___b7;
    _ = &__b7;
    var __b6 = arg___b6;
    _ = &__b6;
    var __b5 = arg___b5;
    _ = &__b5;
    var __b4 = arg___b4;
    _ = &__b4;
    var __b3 = arg___b3;
    _ = &__b3;
    var __b2 = arg___b2;
    _ = &__b2;
    var __b1 = arg___b1;
    _ = &__b1;
    var __b0 = arg___b0;
    _ = &__b0;
    return @bitCast(@as(__v8qi, [8]u8{
        __b0,
        __b1,
        __b2,
        __b3,
        __b4,
        __b5,
        __b6,
        __b7,
    }));
}
pub inline fn _mm_set1_pi32(arg___i: c_int) __m64 {
    var __i = arg___i;
    _ = &__i;
    return _mm_set_pi32(__i, __i);
}
pub inline fn _mm_set1_pi16(arg___w: c_short) __m64 {
    var __w = arg___w;
    _ = &__w;
    return _mm_set_pi16(__w, __w, __w, __w);
}
pub inline fn _mm_set1_pi8(arg___b: u8) __m64 {
    var __b = arg___b;
    _ = &__b;
    return _mm_set_pi8(__b, __b, __b, __b, __b, __b, __b, __b);
}
pub inline fn _mm_setr_pi32(arg___i0: c_int, arg___i1: c_int) __m64 {
    var __i0 = arg___i0;
    _ = &__i0;
    var __i1 = arg___i1;
    _ = &__i1;
    return _mm_set_pi32(__i1, __i0);
}
pub inline fn _mm_setr_pi16(arg___w0: c_short, arg___w1: c_short, arg___w2: c_short, arg___w3: c_short) __m64 {
    var __w0 = arg___w0;
    _ = &__w0;
    var __w1 = arg___w1;
    _ = &__w1;
    var __w2 = arg___w2;
    _ = &__w2;
    var __w3 = arg___w3;
    _ = &__w3;
    return _mm_set_pi16(__w3, __w2, __w1, __w0);
}
pub inline fn _mm_setr_pi8(arg___b0: u8, arg___b1: u8, arg___b2: u8, arg___b3: u8, arg___b4: u8, arg___b5: u8, arg___b6: u8, arg___b7: u8) __m64 {
    var __b0 = arg___b0;
    _ = &__b0;
    var __b1 = arg___b1;
    _ = &__b1;
    var __b2 = arg___b2;
    _ = &__b2;
    var __b3 = arg___b3;
    _ = &__b3;
    var __b4 = arg___b4;
    _ = &__b4;
    var __b5 = arg___b5;
    _ = &__b5;
    var __b6 = arg___b6;
    _ = &__b6;
    var __b7 = arg___b7;
    _ = &__b7;
    return _mm_set_pi8(__b7, __b6, __b5, __b4, __b3, __b2, __b1, __b0);
}
pub const __v4sf = @Vector(4, f32);
pub const __m128 = @Vector(4, f32);
pub const __m128_u = @Vector(4, f32);
pub const __v4su = @Vector(4, c_uint);
pub const __v8hu = @Vector(8, c_ushort);
pub const __v16qu = @Vector(16, u8);
pub extern fn _wdupenv_s(_Buffer: [*c][*c]wchar_t, _BufferSizeInWords: [*c]usize, _VarName: [*c]const wchar_t) errno_t;
pub extern fn _itow_s(_Val: c_int, _DstBuf: [*c]wchar_t, _SizeInWords: usize, _Radix: c_int) errno_t;
pub extern fn _ltow_s(_Val: c_long, _DstBuf: [*c]wchar_t, _SizeInWords: usize, _Radix: c_int) errno_t;
pub extern fn _ultow_s(_Val: c_ulong, _DstBuf: [*c]wchar_t, _SizeInWords: usize, _Radix: c_int) errno_t;
pub extern fn _wgetenv_s(_ReturnSize: [*c]usize, _DstBuf: [*c]wchar_t, _DstSizeInWords: usize, _VarName: [*c]const wchar_t) errno_t;
pub extern fn _i64tow_s(_Val: c_longlong, _DstBuf: [*c]wchar_t, _SizeInWords: usize, _Radix: c_int) errno_t;
pub extern fn _ui64tow_s(_Val: c_ulonglong, _DstBuf: [*c]wchar_t, _SizeInWords: usize, _Radix: c_int) errno_t;
pub extern fn _wmakepath_s(_PathResult: [*c]wchar_t, _SizeInWords: usize, _Drive: [*c]const wchar_t, _Dir: [*c]const wchar_t, _Filename: [*c]const wchar_t, _Ext: [*c]const wchar_t) errno_t;
pub extern fn _wputenv_s(_Name: [*c]const wchar_t, _Value: [*c]const wchar_t) errno_t;
pub extern fn _wsearchenv_s(_Filename: [*c]const wchar_t, _EnvVar: [*c]const wchar_t, _ResultPath: [*c]wchar_t, _SizeInWords: usize) errno_t;
pub extern fn _wsplitpath_s(_FullPath: [*c]const wchar_t, _Drive: [*c]wchar_t, _DriveSizeInWords: usize, _Dir: [*c]wchar_t, _DirSizeInWords: usize, _Filename: [*c]wchar_t, _FilenameSizeInWords: usize, _Ext: [*c]wchar_t, _ExtSizeInWords: usize) errno_t;
pub const _onexit_t = ?*const fn () callconv(.c) c_int;
pub const struct__div_t = extern struct {
    quot: c_int = 0,
    rem: c_int = 0,
};
pub const div_t = struct__div_t;
pub const struct__ldiv_t = extern struct {
    quot: c_long = 0,
    rem: c_long = 0,
};
pub const ldiv_t = struct__ldiv_t;
pub const _LDOUBLE = extern struct {
    ld: [10]u8 = @import("std").mem.zeroes([10]u8),
    pub const _atoldbl = __root._atoldbl;
    pub const _atoldbl_l = __root._atoldbl_l;
    pub const atoldbl = __root._atoldbl;
    pub const l = __root._atoldbl_l;
};
pub const _CRT_DOUBLE = extern struct {
    x: f64 = 0,
    pub const _atodbl = __root._atodbl;
    pub const _atodbl_l = __root._atodbl_l;
    pub const atodbl = __root._atodbl;
    pub const l = __root._atodbl_l;
};
pub const _CRT_FLOAT = extern struct {
    f: f32 = 0,
    pub const _atoflt = __root._atoflt;
    pub const _atoflt_l = __root._atoflt_l;
    pub const atoflt = __root._atoflt;
    pub const l = __root._atoflt_l;
};
pub const _LONGDOUBLE = extern struct {
    x: c_longdouble = 0,
};
pub const _LDBL12 = extern struct {
    ld12: [12]u8 = @import("std").mem.zeroes([12]u8),
};
pub extern fn ___mb_cur_max_func() c_int;
pub const _purecall_handler = ?*const fn () callconv(.c) void;
pub extern fn _set_purecall_handler(_Handler: _purecall_handler) _purecall_handler;
pub extern fn _get_purecall_handler() _purecall_handler;
pub const _invalid_parameter_handler = ?*const fn ([*c]const wchar_t, [*c]const wchar_t, [*c]const wchar_t, c_uint, usize) callconv(.c) void;
pub extern fn _set_invalid_parameter_handler(_Handler: _invalid_parameter_handler) _invalid_parameter_handler;
pub extern fn _get_invalid_parameter_handler() _invalid_parameter_handler;
pub extern fn _errno() [*c]c_int;
pub extern fn _set_errno(_Value: c_int) errno_t;
pub extern fn _get_errno(_Value: [*c]c_int) errno_t;
pub extern fn __doserrno() [*c]c_ulong;
pub extern fn _set_doserrno(_Value: c_ulong) errno_t;
pub extern fn _get_doserrno(_Value: [*c]c_ulong) errno_t;
pub extern fn __sys_errlist() [*c][*c]u8;
pub extern fn __sys_nerr() [*c]c_int;
pub extern fn __p___argv() [*c][*c][*c]u8;
pub extern fn __p__fmode() [*c]c_int;
pub extern fn __p___argc() [*c]c_int;
pub extern fn __p___wargv() [*c][*c][*c]wchar_t;
pub extern fn __p__pgmptr() [*c][*c]u8;
pub extern fn __p__wpgmptr() [*c][*c]wchar_t;
pub extern fn _get_pgmptr(_Value: [*c][*c]u8) errno_t;
pub extern fn _get_wpgmptr(_Value: [*c][*c]wchar_t) errno_t;
pub extern fn _set_fmode(_Mode: c_int) errno_t;
pub extern fn _get_fmode(_PMode: [*c]c_int) errno_t;
pub extern fn __p__environ() [*c][*c][*c]u8;
pub extern fn __p__wenviron() [*c][*c][*c]wchar_t;
pub extern fn __p__osplatform() [*c]c_uint;
pub extern fn __p__osver() [*c]c_uint;
pub extern fn __p__winver() [*c]c_uint;
pub extern fn __p__winmajor() [*c]c_uint;
pub extern fn __p__winminor() [*c]c_uint;
pub extern fn _get_osplatform(_Value: [*c]c_uint) errno_t;
pub extern fn _get_osver(_Value: [*c]c_uint) errno_t;
pub extern fn _get_winver(_Value: [*c]c_uint) errno_t;
pub extern fn _get_winmajor(_Value: [*c]c_uint) errno_t;
pub extern fn _get_winminor(_Value: [*c]c_uint) errno_t;
pub extern fn exit(_Code: c_int) noreturn;
pub extern fn _exit(_Code: c_int) noreturn;
pub extern fn quick_exit(_Code: c_int) noreturn;
pub extern fn _Exit(c_int) noreturn;
pub extern fn abort() noreturn;
pub extern fn _set_abort_behavior(_Flags: c_uint, _Mask: c_uint) c_uint;
pub extern fn abs(_X: c_int) c_int;
pub extern fn labs(_X: c_long) c_long;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\libc\include\any-windows-any\stdlib.h:306:41: warning: cdecl calling convention ignored on inline function
pub inline fn _abs64(arg_x: c_longlong) c_longlong {
    var x = arg_x;
    _ = &x;
    return __builtin.llabs(x);
}
pub extern fn atexit(?*const fn () callconv(.c) void) c_int;
pub extern fn at_quick_exit(?*const fn () callconv(.c) void) c_int;
pub extern fn atof(_String: [*c]const u8) f64;
pub extern fn _atof_l(_String: [*c]const u8, _Locale: _locale_t) f64;
pub extern fn atoi(_Str: [*c]const u8) c_int;
pub extern fn _atoi_l(_Str: [*c]const u8, _Locale: _locale_t) c_int;
pub extern fn atol(_Str: [*c]const u8) c_long;
pub extern fn _atol_l(_Str: [*c]const u8, _Locale: _locale_t) c_long;
pub extern fn bsearch(_Key: ?*const anyopaque, _Base: ?*const anyopaque, _NumOfElements: usize, _SizeOfElements: usize, _PtFuncCompare: ?*const fn (?*const anyopaque, ?*const anyopaque) callconv(.c) c_int) ?*anyopaque;
pub extern fn qsort(_Base: ?*anyopaque, _NumOfElements: usize, _SizeOfElements: usize, _PtFuncCompare: ?*const fn (?*const anyopaque, ?*const anyopaque) callconv(.c) c_int) void;
pub extern fn _byteswap_ushort(_Short: c_ushort) c_ushort;
pub extern fn _byteswap_ulong(_Long: c_ulong) c_ulong;
pub extern fn _byteswap_uint64(_Int64: c_ulonglong) c_ulonglong;
pub extern fn div(_Numerator: c_int, _Denominator: c_int) div_t;
pub extern fn getenv(_VarName: [*c]const u8) [*c]u8;
pub extern fn _itoa(_Value: c_int, _Dest: [*c]u8, _Radix: c_int) [*c]u8;
pub extern fn _i64toa(_Val: c_longlong, _DstBuf: [*c]u8, _Radix: c_int) [*c]u8;
pub extern fn _ui64toa(_Val: c_ulonglong, _DstBuf: [*c]u8, _Radix: c_int) [*c]u8;
pub extern fn _atoi64(_String: [*c]const u8) c_longlong;
pub extern fn _atoi64_l(_String: [*c]const u8, _Locale: _locale_t) c_longlong;
pub extern fn _strtoi64(_String: [*c]const u8, _EndPtr: [*c][*c]u8, _Radix: c_int) c_longlong;
pub extern fn _strtoi64_l(_String: [*c]const u8, _EndPtr: [*c][*c]u8, _Radix: c_int, _Locale: _locale_t) c_longlong;
pub extern fn _strtoui64(_String: [*c]const u8, _EndPtr: [*c][*c]u8, _Radix: c_int) c_ulonglong;
pub extern fn _strtoui64_l(_String: [*c]const u8, _EndPtr: [*c][*c]u8, _Radix: c_int, _Locale: _locale_t) c_ulonglong;
pub extern fn ldiv(_Numerator: c_long, _Denominator: c_long) ldiv_t;
pub extern fn _ltoa(_Value: c_long, _Dest: [*c]u8, _Radix: c_int) [*c]u8;
pub extern fn mblen(_Ch: [*c]const u8, _MaxCount: usize) c_int;
pub extern fn _mblen_l(_Ch: [*c]const u8, _MaxCount: usize, _Locale: _locale_t) c_int;
pub extern fn _mbstrlen(_Str: [*c]const u8) usize;
pub extern fn _mbstrlen_l(_Str: [*c]const u8, _Locale: _locale_t) usize;
pub extern fn _mbstrnlen(_Str: [*c]const u8, _MaxCount: usize) usize;
pub extern fn _mbstrnlen_l(_Str: [*c]const u8, _MaxCount: usize, _Locale: _locale_t) usize;
pub extern fn mbtowc(noalias _DstCh: [*c]wchar_t, noalias _SrcCh: [*c]const u8, _SrcSizeInBytes: usize) c_int;
pub extern fn _mbtowc_l(noalias _DstCh: [*c]wchar_t, noalias _SrcCh: [*c]const u8, _SrcSizeInBytes: usize, _Locale: _locale_t) c_int;
pub extern fn mbstowcs(noalias _Dest: [*c]wchar_t, noalias _Source: [*c]const u8, _MaxCount: usize) usize;
pub extern fn _mbstowcs_l(noalias _Dest: [*c]wchar_t, noalias _Source: [*c]const u8, _MaxCount: usize, _Locale: _locale_t) usize;
pub extern fn mkstemp(_TemplateName: [*c]u8) c_int;
pub extern fn mkdtemp(_TemplateName: [*c]u8) [*c]u8;
pub extern fn rand() c_int;
pub extern fn _set_error_mode(_Mode: c_int) c_int;
pub extern fn srand(_Seed: c_uint) void;
pub extern fn strtod(noalias _Str: [*c]const u8, noalias _EndPtr: [*c][*c]u8) f64;
pub extern fn strtof(noalias nptr: [*c]const u8, noalias endptr: [*c][*c]u8) f32;
pub extern fn strtold(noalias [*c]const u8, noalias [*c][*c]u8) c_longdouble;
pub extern fn __strtod(noalias [*c]const u8, noalias [*c][*c]u8) f64;
pub extern fn __mingw_strtof(noalias [*c]const u8, noalias [*c][*c]u8) f32;
pub extern fn __mingw_strtod(noalias [*c]const u8, noalias [*c][*c]u8) f64;
pub extern fn __mingw_strtold(noalias [*c]const u8, noalias [*c][*c]u8) c_longdouble;
pub extern fn _strtof_l(noalias _Str: [*c]const u8, noalias _EndPtr: [*c][*c]u8, _Locale: _locale_t) f32;
pub extern fn _strtod_l(noalias _Str: [*c]const u8, noalias _EndPtr: [*c][*c]u8, _Locale: _locale_t) f64;
pub extern fn strtol(noalias _Str: [*c]const u8, noalias _EndPtr: [*c][*c]u8, _Radix: c_int) c_long;
pub extern fn _strtol_l(noalias _Str: [*c]const u8, noalias _EndPtr: [*c][*c]u8, _Radix: c_int, _Locale: _locale_t) c_long;
pub extern fn strtoul(noalias _Str: [*c]const u8, noalias _EndPtr: [*c][*c]u8, _Radix: c_int) c_ulong;
pub extern fn _strtoul_l(noalias _Str: [*c]const u8, noalias _EndPtr: [*c][*c]u8, _Radix: c_int, _Locale: _locale_t) c_ulong;
pub extern fn system(_Command: [*c]const u8) c_int;
pub extern fn _ultoa(_Value: c_ulong, _Dest: [*c]u8, _Radix: c_int) [*c]u8;
pub extern fn wctomb(_MbCh: [*c]u8, _WCh: wchar_t) c_int;
pub extern fn _wctomb_l(_MbCh: [*c]u8, _WCh: wchar_t, _Locale: _locale_t) c_int;
pub extern fn wcstombs(noalias _Dest: [*c]u8, noalias _Source: [*c]const wchar_t, _MaxCount: usize) usize;
pub extern fn _wcstombs_l(noalias _Dest: [*c]u8, noalias _Source: [*c]const wchar_t, _MaxCount: usize, _Locale: _locale_t) usize;
pub extern fn calloc(_NumOfElements: usize, _SizeOfElements: usize) ?*anyopaque;
pub extern fn free(_Memory: ?*anyopaque) void;
pub extern fn malloc(_Size: usize) ?*anyopaque;
pub extern fn realloc(_Memory: ?*anyopaque, _NewSize: usize) ?*anyopaque;
pub extern fn _aligned_free(_Memory: ?*anyopaque) void;
pub extern fn _aligned_malloc(_Size: usize, _Alignment: usize) ?*anyopaque;
pub extern fn _aligned_offset_malloc(_Size: usize, _Alignment: usize, _Offset: usize) ?*anyopaque;
pub extern fn _aligned_realloc(_Memory: ?*anyopaque, _Size: usize, _Alignment: usize) ?*anyopaque;
pub extern fn _aligned_offset_realloc(_Memory: ?*anyopaque, _Size: usize, _Alignment: usize, _Offset: usize) ?*anyopaque;
pub extern fn _recalloc(_Memory: ?*anyopaque, _Count: usize, _Size: usize) ?*anyopaque;
pub extern fn _aligned_recalloc(_Memory: ?*anyopaque, _Count: usize, _Size: usize, _Alignment: usize) ?*anyopaque;
pub extern fn _aligned_offset_recalloc(_Memory: ?*anyopaque, _Count: usize, _Size: usize, _Alignment: usize, _Offset: usize) ?*anyopaque;
pub extern fn _aligned_msize(_Memory: ?*anyopaque, _Alignment: usize, _Offset: usize) usize;
pub extern fn _itow(_Value: c_int, _Dest: [*c]wchar_t, _Radix: c_int) [*c]wchar_t;
pub extern fn _ltow(_Value: c_long, _Dest: [*c]wchar_t, _Radix: c_int) [*c]wchar_t;
pub extern fn _ultow(_Value: c_ulong, _Dest: [*c]wchar_t, _Radix: c_int) [*c]wchar_t;
pub extern fn __mingw_wcstod(noalias _Str: [*c]const wchar_t, noalias _EndPtr: [*c][*c]wchar_t) f64;
pub extern fn __mingw_wcstof(noalias nptr: [*c]const wchar_t, noalias endptr: [*c][*c]wchar_t) f32;
pub extern fn __mingw_wcstold(noalias [*c]const wchar_t, noalias [*c][*c]wchar_t) c_longdouble;
pub extern fn wcstod(noalias _Str: [*c]const wchar_t, noalias _EndPtr: [*c][*c]wchar_t) f64;
pub extern fn wcstof(noalias nptr: [*c]const wchar_t, noalias endptr: [*c][*c]wchar_t) f32;
pub extern fn wcstold(noalias [*c]const wchar_t, noalias [*c][*c]wchar_t) c_longdouble;
pub extern fn _wcstod_l(noalias _Str: [*c]const wchar_t, noalias _EndPtr: [*c][*c]wchar_t, _Locale: _locale_t) f64;
pub extern fn _wcstof_l(noalias _Str: [*c]const wchar_t, noalias _EndPtr: [*c][*c]wchar_t, _Locale: _locale_t) f32;
pub extern fn wcstol(noalias _Str: [*c]const wchar_t, noalias _EndPtr: [*c][*c]wchar_t, _Radix: c_int) c_long;
pub extern fn _wcstol_l(noalias _Str: [*c]const wchar_t, noalias _EndPtr: [*c][*c]wchar_t, _Radix: c_int, _Locale: _locale_t) c_long;
pub extern fn wcstoul(noalias _Str: [*c]const wchar_t, noalias _EndPtr: [*c][*c]wchar_t, _Radix: c_int) c_ulong;
pub extern fn _wcstoul_l(noalias _Str: [*c]const wchar_t, noalias _EndPtr: [*c][*c]wchar_t, _Radix: c_int, _Locale: _locale_t) c_ulong;
pub extern fn _wgetenv(_VarName: [*c]const wchar_t) [*c]wchar_t;
pub extern fn _wsystem(_Command: [*c]const wchar_t) c_int;
pub extern fn _wtof(_Str: [*c]const wchar_t) f64;
pub extern fn _wtof_l(_Str: [*c]const wchar_t, _Locale: _locale_t) f64;
pub extern fn _wtoi(_Str: [*c]const wchar_t) c_int;
pub extern fn _wtoi_l(_Str: [*c]const wchar_t, _Locale: _locale_t) c_int;
pub extern fn _wtol(_Str: [*c]const wchar_t) c_long;
pub extern fn _wtol_l(_Str: [*c]const wchar_t, _Locale: _locale_t) c_long;
pub extern fn _i64tow(_Val: c_longlong, _DstBuf: [*c]wchar_t, _Radix: c_int) [*c]wchar_t;
pub extern fn _ui64tow(_Val: c_ulonglong, _DstBuf: [*c]wchar_t, _Radix: c_int) [*c]wchar_t;
pub extern fn _wtoi64(_Str: [*c]const wchar_t) c_longlong;
pub extern fn _wtoi64_l(_Str: [*c]const wchar_t, _Locale: _locale_t) c_longlong;
pub extern fn _wcstoi64(_Str: [*c]const wchar_t, _EndPtr: [*c][*c]wchar_t, _Radix: c_int) c_longlong;
pub extern fn _wcstoi64_l(_Str: [*c]const wchar_t, _EndPtr: [*c][*c]wchar_t, _Radix: c_int, _Locale: _locale_t) c_longlong;
pub extern fn _wcstoui64(_Str: [*c]const wchar_t, _EndPtr: [*c][*c]wchar_t, _Radix: c_int) c_ulonglong;
pub extern fn _wcstoui64_l(_Str: [*c]const wchar_t, _EndPtr: [*c][*c]wchar_t, _Radix: c_int, _Locale: _locale_t) c_ulonglong;
pub extern fn _putenv(_EnvString: [*c]const u8) c_int;
pub extern fn _wputenv(_EnvString: [*c]const wchar_t) c_int;
pub extern fn _fullpath(_FullPath: [*c]u8, _Path: [*c]const u8, _SizeInBytes: usize) [*c]u8;
pub extern fn _ecvt(_Val: f64, _NumOfDigits: c_int, _PtDec: [*c]c_int, _PtSign: [*c]c_int) [*c]u8;
pub extern fn _fcvt(_Val: f64, _NumOfDec: c_int, _PtDec: [*c]c_int, _PtSign: [*c]c_int) [*c]u8;
pub extern fn _gcvt(_Val: f64, _NumOfDigits: c_int, _DstBuf: [*c]u8) [*c]u8;
pub extern fn _atodbl(_Result: [*c]_CRT_DOUBLE, _Str: [*c]u8) c_int;
pub extern fn _atoldbl(_Result: [*c]_LDOUBLE, _Str: [*c]u8) c_int;
pub extern fn _atoflt(_Result: [*c]_CRT_FLOAT, _Str: [*c]u8) c_int;
pub extern fn _atodbl_l(_Result: [*c]_CRT_DOUBLE, _Str: [*c]u8, _Locale: _locale_t) c_int;
pub extern fn _atoldbl_l(_Result: [*c]_LDOUBLE, _Str: [*c]u8, _Locale: _locale_t) c_int;
pub extern fn _atoflt_l(_Result: [*c]_CRT_FLOAT, _Str: [*c]u8, _Locale: _locale_t) c_int;
pub extern fn _lrotl(c_ulong, c_int) c_ulong;
pub extern fn _lrotr(c_ulong, c_int) c_ulong;
pub extern fn _makepath(_Path: [*c]u8, _Drive: [*c]const u8, _Dir: [*c]const u8, _Filename: [*c]const u8, _Ext: [*c]const u8) void;
pub extern fn _onexit(_Func: _onexit_t) _onexit_t;
pub extern fn perror(_ErrMsg: [*c]const u8) void;
pub extern fn _rotl64(_Val: c_ulonglong, _Shift: c_int) c_ulonglong;
pub extern fn _rotr64(Value: c_ulonglong, Shift: c_int) c_ulonglong;
pub extern fn _rotr(_Val: c_uint, _Shift: c_int) c_uint;
pub extern fn _rotl(_Val: c_uint, _Shift: c_int) c_uint;
pub extern fn _searchenv(_Filename: [*c]const u8, _EnvVar: [*c]const u8, _ResultPath: [*c]u8) void;
pub extern fn _splitpath(_FullPath: [*c]const u8, _Drive: [*c]u8, _Dir: [*c]u8, _Filename: [*c]u8, _Ext: [*c]u8) void;
pub extern fn _swab(_Buf1: [*c]u8, _Buf2: [*c]u8, _SizeInBytes: c_int) void;
pub extern fn _wfullpath(_FullPath: [*c]wchar_t, _Path: [*c]const wchar_t, _SizeInWords: usize) [*c]wchar_t;
pub extern fn _wmakepath(_ResultPath: [*c]wchar_t, _Drive: [*c]const wchar_t, _Dir: [*c]const wchar_t, _Filename: [*c]const wchar_t, _Ext: [*c]const wchar_t) void;
pub extern fn _wperror(_ErrMsg: [*c]const wchar_t) void;
pub extern fn _wsearchenv(_Filename: [*c]const wchar_t, _EnvVar: [*c]const wchar_t, _ResultPath: [*c]wchar_t) void;
pub extern fn _wsplitpath(_FullPath: [*c]const wchar_t, _Drive: [*c]wchar_t, _Dir: [*c]wchar_t, _Filename: [*c]wchar_t, _Ext: [*c]wchar_t) void;
pub extern fn _beep(_Frequency: c_uint, _Duration: c_uint) void;
pub extern fn _seterrormode(_Mode: c_int) void;
pub extern fn _sleep(_Duration: c_ulong) void;
pub extern fn ecvt(_Val: f64, _NumOfDigits: c_int, _PtDec: [*c]c_int, _PtSign: [*c]c_int) [*c]u8;
pub extern fn fcvt(_Val: f64, _NumOfDec: c_int, _PtDec: [*c]c_int, _PtSign: [*c]c_int) [*c]u8;
pub extern fn gcvt(_Val: f64, _NumOfDigits: c_int, _DstBuf: [*c]u8) [*c]u8;
pub extern fn itoa(_Val: c_int, _DstBuf: [*c]u8, _Radix: c_int) [*c]u8;
pub extern fn ltoa(_Val: c_long, _DstBuf: [*c]u8, _Radix: c_int) [*c]u8;
pub extern fn putenv(_EnvString: [*c]const u8) c_int;
pub extern fn swab(_Buf1: [*c]u8, _Buf2: [*c]u8, _SizeInBytes: c_int) void;
pub extern fn ultoa(_Val: c_ulong, _Dstbuf: [*c]u8, _Radix: c_int) [*c]u8;
pub extern fn onexit(_Func: _onexit_t) _onexit_t;
pub const lldiv_t = extern struct {
    quot: c_longlong = 0,
    rem: c_longlong = 0,
};
pub extern fn lldiv(c_longlong, c_longlong) lldiv_t;
pub extern fn llabs(c_longlong) c_longlong;
pub extern fn strtoll(noalias [*c]const u8, noalias [*c][*c]u8, c_int) c_longlong;
pub extern fn strtoull(noalias [*c]const u8, noalias [*c][*c]u8, c_int) c_ulonglong;
pub extern fn atoll([*c]const u8) c_longlong;
pub extern fn wtoll([*c]const wchar_t) c_longlong;
pub extern fn lltoa(c_longlong, [*c]u8, c_int) [*c]u8;
pub extern fn ulltoa(c_ulonglong, [*c]u8, c_int) [*c]u8;
pub extern fn lltow(c_longlong, [*c]wchar_t, c_int) [*c]wchar_t;
pub extern fn ulltow(c_ulonglong, [*c]wchar_t, c_int) [*c]wchar_t;
pub extern fn _dupenv_s(_PBuffer: [*c][*c]u8, _PBufferSizeInBytes: [*c]usize, _VarName: [*c]const u8) errno_t;
pub extern fn bsearch_s(_Key: ?*const anyopaque, _Base: ?*const anyopaque, _NumOfElements: rsize_t, _SizeOfElements: rsize_t, _PtFuncCompare: ?*const fn (?*anyopaque, ?*const anyopaque, ?*const anyopaque) callconv(.c) c_int, _Context: ?*anyopaque) ?*anyopaque;
pub extern fn getenv_s(_ReturnSize: [*c]usize, _DstBuf: [*c]u8, _DstSize: rsize_t, _VarName: [*c]const u8) errno_t;
pub extern fn _itoa_s(_Value: c_int, _DstBuf: [*c]u8, _Size: usize, _Radix: c_int) errno_t;
pub extern fn _i64toa_s(_Val: c_longlong, _DstBuf: [*c]u8, _Size: usize, _Radix: c_int) errno_t;
pub extern fn _ui64toa_s(_Val: c_ulonglong, _DstBuf: [*c]u8, _Size: usize, _Radix: c_int) errno_t;
pub extern fn _ltoa_s(_Val: c_long, _DstBuf: [*c]u8, _Size: usize, _Radix: c_int) errno_t;
pub extern fn mbstowcs_s(_PtNumOfCharConverted: [*c]usize, _DstBuf: [*c]wchar_t, _SizeInWords: usize, _SrcBuf: [*c]const u8, _MaxCount: usize) errno_t;
pub extern fn _mbstowcs_s_l(_PtNumOfCharConverted: [*c]usize, _DstBuf: [*c]wchar_t, _SizeInWords: usize, _SrcBuf: [*c]const u8, _MaxCount: usize, _Locale: _locale_t) errno_t;
pub extern fn _ultoa_s(_Val: c_ulong, _DstBuf: [*c]u8, _Size: usize, _Radix: c_int) errno_t;
pub extern fn wctomb_s(_SizeConverted: [*c]c_int, _MbCh: [*c]u8, _SizeInBytes: rsize_t, _WCh: wchar_t) errno_t;
pub extern fn _wctomb_s_l(_SizeConverted: [*c]c_int, _MbCh: [*c]u8, _SizeInBytes: usize, _WCh: wchar_t, _Locale: _locale_t) errno_t;
pub extern fn wcstombs_s(_PtNumOfCharConverted: [*c]usize, _Dst: [*c]u8, _DstSizeInBytes: usize, _Src: [*c]const wchar_t, _MaxCountInBytes: usize) errno_t;
pub extern fn _wcstombs_s_l(_PtNumOfCharConverted: [*c]usize, _Dst: [*c]u8, _DstSizeInBytes: usize, _Src: [*c]const wchar_t, _MaxCountInBytes: usize, _Locale: _locale_t) errno_t;
pub extern fn _ecvt_s(_DstBuf: [*c]u8, _Size: usize, _Val: f64, _NumOfDights: c_int, _PtDec: [*c]c_int, _PtSign: [*c]c_int) errno_t;
pub extern fn _fcvt_s(_DstBuf: [*c]u8, _Size: usize, _Val: f64, _NumOfDec: c_int, _PtDec: [*c]c_int, _PtSign: [*c]c_int) errno_t;
pub extern fn _gcvt_s(_DstBuf: [*c]u8, _Size: usize, _Val: f64, _NumOfDigits: c_int) errno_t;
pub extern fn _makepath_s(_PathResult: [*c]u8, _Size: usize, _Drive: [*c]const u8, _Dir: [*c]const u8, _Filename: [*c]const u8, _Ext: [*c]const u8) errno_t;
pub extern fn _putenv_s(_Name: [*c]const u8, _Value: [*c]const u8) errno_t;
pub extern fn _searchenv_s(_Filename: [*c]const u8, _EnvVar: [*c]const u8, _ResultPath: [*c]u8, _SizeInBytes: usize) errno_t;
pub extern fn _splitpath_s(_FullPath: [*c]const u8, _Drive: [*c]u8, _DriveSize: usize, _Dir: [*c]u8, _DirSize: usize, _Filename: [*c]u8, _FilenameSize: usize, _Ext: [*c]u8, _ExtSize: usize) errno_t;
pub extern fn qsort_s(_Base: ?*anyopaque, _NumOfElements: usize, _SizeOfElements: usize, _PtFuncCompare: ?*const fn (?*anyopaque, ?*const anyopaque, ?*const anyopaque) callconv(.c) c_int, _Context: ?*anyopaque) void;
pub const struct__heapinfo = extern struct {
    _pentry: [*c]c_int = null,
    _size: usize = 0,
    _useflag: c_int = 0,
    pub const _heapwalk = __root._heapwalk;
    pub const heapwalk = __root._heapwalk;
};
pub const _HEAPINFO = struct__heapinfo;
pub extern fn __p__amblksiz() [*c]c_uint;
pub extern fn __mingw_aligned_malloc(_Size: usize, _Alignment: usize) ?*anyopaque;
pub extern fn __mingw_aligned_free(_Memory: ?*anyopaque) void;
pub extern fn __mingw_aligned_offset_realloc(_Memory: ?*anyopaque, _Size: usize, _Alignment: usize, _Offset: usize) ?*anyopaque;
pub extern fn __mingw_aligned_offset_malloc(usize, usize, usize) ?*anyopaque;
pub extern fn __mingw_aligned_realloc(_Memory: ?*anyopaque, _Size: usize, _Offset: usize) ?*anyopaque;
pub extern fn __mingw_aligned_msize(memblock: ?*anyopaque, alignment: usize, offset: usize) usize;
pub extern fn _resetstkoflw() c_int;
pub extern fn _set_malloc_crt_max_wait(_NewValue: c_ulong) c_ulong;
pub extern fn _expand(_Memory: ?*anyopaque, _NewSize: usize) ?*anyopaque;
pub extern fn _msize(_Memory: ?*anyopaque) usize;
pub extern fn _get_sbh_threshold() usize;
pub extern fn _set_sbh_threshold(_NewValue: usize) c_int;
pub extern fn _set_amblksiz(_Value: usize) errno_t;
pub extern fn _get_amblksiz(_Value: [*c]usize) errno_t;
pub extern fn _heapadd(_Memory: ?*anyopaque, _Size: usize) c_int;
pub extern fn _heapchk() c_int;
pub extern fn _heapmin() c_int;
pub extern fn _heapset(_Fill: c_uint) c_int;
pub extern fn _heapwalk(_EntryInfo: [*c]_HEAPINFO) c_int;
pub extern fn _heapused(_Used: [*c]usize, _Commit: [*c]usize) usize;
pub extern fn _get_heap_handle() isize;
pub fn _MarkAllocaS(arg__Ptr: ?*anyopaque, arg__Marker: c_uint) callconv(.c) ?*anyopaque {
    var _Ptr = arg__Ptr;
    _ = &_Ptr;
    var _Marker = arg__Marker;
    _ = &_Marker;
    if (_Ptr != null) {
        @as([*c]c_uint, @ptrCast(@alignCast(_Ptr))).* = _Marker;
        _Ptr = @ptrCast(@alignCast(@as([*c]u8, @ptrCast(@alignCast(_Ptr))) + @as(usize, @bitCast(@as(isize, @intCast(_ALLOCA_S_MARKER_SIZE))))));
    }
    return _Ptr;
}
pub fn _freea(arg__Memory: ?*anyopaque) callconv(.c) void {
    var _Memory = arg__Memory;
    _ = &_Memory;
    var _Marker: c_uint = undefined;
    _ = &_Marker;
    if (_Memory != null) {
        _Memory = @ptrCast(@alignCast(@as([*c]u8, @ptrCast(@alignCast(_Memory))) - @as(usize, @bitCast(@as(isize, @intCast(_ALLOCA_S_MARKER_SIZE))))));
        _Marker = @as([*c]c_uint, @ptrCast(@alignCast(_Memory))).*;
        if (_Marker == @as(c_uint, _ALLOCA_S_HEAP_MARKER)) {
            free(_Memory);
        }
    }
}
pub inline fn _mm_malloc(arg___size: usize, arg___align: usize) ?*anyopaque {
    var __size = arg___size;
    _ = &__size;
    var __align = arg___align;
    _ = &__align;
    if (__align == @as(usize, 1)) {
        return malloc(__size);
    }
    if (!((__align & (__align -% @as(usize, 1))) != 0) and (__align < @sizeOf(?*anyopaque))) {
        __align = @sizeOf(?*anyopaque);
    }
    var __mallocedMemory: ?*anyopaque = undefined;
    _ = &__mallocedMemory;
    __mallocedMemory = __mingw_aligned_malloc(__size, __align);
    return __mallocedMemory;
}
pub inline fn _mm_free(arg___p: ?*anyopaque) void {
    var __p = arg___p;
    _ = &__p;
    __mingw_aligned_free(__p);
}
pub inline fn _mm_add_ss(arg___a: __m128, arg___b: __m128) __m128 {
    var __a = arg___a;
    _ = &__a;
    var __b = arg___b;
    _ = &__b;
    __a[@as(c_int, 0)] += __b[@as(c_int, 0)];
    return __a;
}
pub inline fn _mm_add_ps(arg___a: __m128, arg___b: __m128) __m128 {
    var __a = arg___a;
    _ = &__a;
    var __b = arg___b;
    _ = &__b;
    return __a + __b;
}
pub inline fn _mm_sub_ss(arg___a: __m128, arg___b: __m128) __m128 {
    var __a = arg___a;
    _ = &__a;
    var __b = arg___b;
    _ = &__b;
    __a[@as(c_int, 0)] -= __b[@as(c_int, 0)];
    return __a;
}
pub inline fn _mm_sub_ps(arg___a: __m128, arg___b: __m128) __m128 {
    var __a = arg___a;
    _ = &__a;
    var __b = arg___b;
    _ = &__b;
    return __a - __b;
}
pub inline fn _mm_mul_ss(arg___a: __m128, arg___b: __m128) __m128 {
    var __a = arg___a;
    _ = &__a;
    var __b = arg___b;
    _ = &__b;
    __a[@as(c_int, 0)] *= __b[@as(c_int, 0)];
    return __a;
}
pub inline fn _mm_mul_ps(arg___a: __m128, arg___b: __m128) __m128 {
    var __a = arg___a;
    _ = &__a;
    var __b = arg___b;
    _ = &__b;
    return __a * __b;
}
pub inline fn _mm_div_ss(arg___a: __m128, arg___b: __m128) __m128 {
    var __a = arg___a;
    _ = &__a;
    var __b = arg___b;
    _ = &__b;
    __a[@as(c_int, 0)] /= __b[@as(c_int, 0)];
    return __a;
}
pub inline fn _mm_div_ps(arg___a: __m128, arg___b: __m128) __m128 {
    var __a = arg___a;
    _ = &__a;
    var __b = arg___b;
    _ = &__b;
    return __a / __b;
}
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\xmmintrin.h:235:12: warning: TODO implement function '__builtin_elementwise_sqrt' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\xmmintrin.h:234:45: warning: unable to translate function, demoted to extern
pub extern fn _mm_sqrt_ss(arg___a: __m128) __m128;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\xmmintrin.h:251:10: warning: TODO implement function '__builtin_elementwise_sqrt' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\xmmintrin.h:250:45: warning: unable to translate function, demoted to extern
pub extern fn _mm_sqrt_ps(arg___a: __m128) __m128;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\xmmintrin.h:269:18: warning: TODO implement function '__builtin_ia32_rcpss' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\xmmintrin.h:267:1: warning: unable to translate function, demoted to extern
pub extern fn _mm_rcp_ss(arg___a: __m128) __m128;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\xmmintrin.h:286:18: warning: TODO implement function '__builtin_ia32_rcpps' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\xmmintrin.h:284:1: warning: unable to translate function, demoted to extern
pub extern fn _mm_rcp_ps(arg___a: __m128) __m128;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\xmmintrin.h:305:10: warning: TODO implement function '__builtin_ia32_rsqrtss' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\xmmintrin.h:303:1: warning: unable to translate function, demoted to extern
pub extern fn _mm_rsqrt_ss(arg___a: __m128) __m128;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\xmmintrin.h:322:10: warning: TODO implement function '__builtin_ia32_rsqrtps' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\xmmintrin.h:320:1: warning: unable to translate function, demoted to extern
pub extern fn _mm_rsqrt_ps(arg___a: __m128) __m128;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\xmmintrin.h:345:10: warning: TODO implement function '__builtin_ia32_minss' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\xmmintrin.h:344:45: warning: unable to translate function, demoted to extern
pub extern fn _mm_min_ss(arg___a: __m128, arg___b: __m128) __m128;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\xmmintrin.h:365:10: warning: TODO implement function '__builtin_ia32_minps' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\xmmintrin.h:363:55: warning: unable to translate function, demoted to extern
pub extern fn _mm_min_ps(arg___a: __m128, arg___b: __m128) __m128;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\xmmintrin.h:388:10: warning: TODO implement function '__builtin_ia32_maxss' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\xmmintrin.h:387:45: warning: unable to translate function, demoted to extern
pub extern fn _mm_max_ss(arg___a: __m128, arg___b: __m128) __m128;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\xmmintrin.h:408:10: warning: TODO implement function '__builtin_ia32_maxps' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\xmmintrin.h:406:55: warning: unable to translate function, demoted to extern
pub extern fn _mm_max_ps(arg___a: __m128, arg___b: __m128) __m128;
pub inline fn _mm_and_ps(arg___a: __m128, arg___b: __m128) __m128 {
    var __a = arg___a;
    _ = &__a;
    var __b = arg___b;
    _ = &__b;
    return @bitCast(@as(__v4su, @bitCast(__a)) & @as(__v4su, @bitCast(__b)));
}
pub inline fn _mm_andnot_ps(arg___a: __m128, arg___b: __m128) __m128 {
    var __a = arg___a;
    _ = &__a;
    var __b = arg___b;
    _ = &__b;
    return @bitCast(~@as(__v4su, @bitCast(__a)) & @as(__v4su, @bitCast(__b)));
}
pub inline fn _mm_or_ps(arg___a: __m128, arg___b: __m128) __m128 {
    var __a = arg___a;
    _ = &__a;
    var __b = arg___b;
    _ = &__b;
    return @bitCast(@as(__v4su, @bitCast(__a)) | @as(__v4su, @bitCast(__b)));
}
pub inline fn _mm_xor_ps(arg___a: __m128, arg___b: __m128) __m128 {
    var __a = arg___a;
    _ = &__a;
    var __b = arg___b;
    _ = &__b;
    return @bitCast(@as(__v4su, @bitCast(__a)) ^ @as(__v4su, @bitCast(__b)));
}
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\xmmintrin.h:506:18: warning: TODO implement function '__builtin_ia32_cmpeqss' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\xmmintrin.h:504:1: warning: unable to translate function, demoted to extern
pub extern fn _mm_cmpeq_ss(arg___a: __m128, arg___b: __m128) __m128;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\xmmintrin.h:527:18: warning: TODO implement function '__builtin_ia32_cmpeqps' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\xmmintrin.h:525:1: warning: unable to translate function, demoted to extern
pub extern fn _mm_cmpeq_ps(arg___a: __m128, arg___b: __m128) __m128;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\xmmintrin.h:553:18: warning: TODO implement function '__builtin_ia32_cmpltss' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\xmmintrin.h:551:1: warning: unable to translate function, demoted to extern
pub extern fn _mm_cmplt_ss(arg___a: __m128, arg___b: __m128) __m128;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\xmmintrin.h:575:18: warning: TODO implement function '__builtin_ia32_cmpltps' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\xmmintrin.h:573:1: warning: unable to translate function, demoted to extern
pub extern fn _mm_cmplt_ps(arg___a: __m128, arg___b: __m128) __m128;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\xmmintrin.h:601:18: warning: TODO implement function '__builtin_ia32_cmpless' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\xmmintrin.h:599:1: warning: unable to translate function, demoted to extern
pub extern fn _mm_cmple_ss(arg___a: __m128, arg___b: __m128) __m128;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\xmmintrin.h:623:18: warning: TODO implement function '__builtin_ia32_cmpleps' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\xmmintrin.h:621:1: warning: unable to translate function, demoted to extern
pub extern fn _mm_cmple_ps(arg___a: __m128, arg___b: __m128) __m128;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\xmmintrin.h:650:50: warning: TODO implement function '__builtin_ia32_cmpltss' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\xmmintrin.h:647:1: warning: unable to translate function, demoted to extern
pub extern fn _mm_cmpgt_ss(arg___a: __m128, arg___b: __m128) __m128;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\xmmintrin.h:673:18: warning: TODO implement function '__builtin_ia32_cmpltps' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\xmmintrin.h:671:1: warning: unable to translate function, demoted to extern
pub extern fn _mm_cmpgt_ps(arg___a: __m128, arg___b: __m128) __m128;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\xmmintrin.h:700:50: warning: TODO implement function '__builtin_ia32_cmpless' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\xmmintrin.h:697:1: warning: unable to translate function, demoted to extern
pub extern fn _mm_cmpge_ss(arg___a: __m128, arg___b: __m128) __m128;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\xmmintrin.h:723:18: warning: TODO implement function '__builtin_ia32_cmpleps' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\xmmintrin.h:721:1: warning: unable to translate function, demoted to extern
pub extern fn _mm_cmpge_ps(arg___a: __m128, arg___b: __m128) __m128;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\xmmintrin.h:749:18: warning: TODO implement function '__builtin_ia32_cmpneqss' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\xmmintrin.h:747:1: warning: unable to translate function, demoted to extern
pub extern fn _mm_cmpneq_ss(arg___a: __m128, arg___b: __m128) __m128;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\xmmintrin.h:771:18: warning: TODO implement function '__builtin_ia32_cmpneqps' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\xmmintrin.h:769:1: warning: unable to translate function, demoted to extern
pub extern fn _mm_cmpneq_ps(arg___a: __m128, arg___b: __m128) __m128;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\xmmintrin.h:798:18: warning: TODO implement function '__builtin_ia32_cmpnltss' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\xmmintrin.h:796:1: warning: unable to translate function, demoted to extern
pub extern fn _mm_cmpnlt_ss(arg___a: __m128, arg___b: __m128) __m128;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\xmmintrin.h:821:18: warning: TODO implement function '__builtin_ia32_cmpnltps' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\xmmintrin.h:819:1: warning: unable to translate function, demoted to extern
pub extern fn _mm_cmpnlt_ps(arg___a: __m128, arg___b: __m128) __m128;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\xmmintrin.h:848:18: warning: TODO implement function '__builtin_ia32_cmpnless' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\xmmintrin.h:846:1: warning: unable to translate function, demoted to extern
pub extern fn _mm_cmpnle_ss(arg___a: __m128, arg___b: __m128) __m128;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\xmmintrin.h:871:18: warning: TODO implement function '__builtin_ia32_cmpnleps' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\xmmintrin.h:869:1: warning: unable to translate function, demoted to extern
pub extern fn _mm_cmpnle_ps(arg___a: __m128, arg___b: __m128) __m128;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\xmmintrin.h:899:50: warning: TODO implement function '__builtin_ia32_cmpnltss' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\xmmintrin.h:896:1: warning: unable to translate function, demoted to extern
pub extern fn _mm_cmpngt_ss(arg___a: __m128, arg___b: __m128) __m128;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\xmmintrin.h:923:18: warning: TODO implement function '__builtin_ia32_cmpnltps' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\xmmintrin.h:921:1: warning: unable to translate function, demoted to extern
pub extern fn _mm_cmpngt_ps(arg___a: __m128, arg___b: __m128) __m128;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\xmmintrin.h:951:50: warning: TODO implement function '__builtin_ia32_cmpnless' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\xmmintrin.h:948:1: warning: unable to translate function, demoted to extern
pub extern fn _mm_cmpnge_ss(arg___a: __m128, arg___b: __m128) __m128;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\xmmintrin.h:975:18: warning: TODO implement function '__builtin_ia32_cmpnleps' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\xmmintrin.h:973:1: warning: unable to translate function, demoted to extern
pub extern fn _mm_cmpnge_ps(arg___a: __m128, arg___b: __m128) __m128;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\xmmintrin.h:1002:18: warning: TODO implement function '__builtin_ia32_cmpordss' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\xmmintrin.h:1000:1: warning: unable to translate function, demoted to extern
pub extern fn _mm_cmpord_ss(arg___a: __m128, arg___b: __m128) __m128;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\xmmintrin.h:1026:18: warning: TODO implement function '__builtin_ia32_cmpordps' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\xmmintrin.h:1024:1: warning: unable to translate function, demoted to extern
pub extern fn _mm_cmpord_ps(arg___a: __m128, arg___b: __m128) __m128;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\xmmintrin.h:1053:18: warning: TODO implement function '__builtin_ia32_cmpunordss' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\xmmintrin.h:1051:1: warning: unable to translate function, demoted to extern
pub extern fn _mm_cmpunord_ss(arg___a: __m128, arg___b: __m128) __m128;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\xmmintrin.h:1077:18: warning: TODO implement function '__builtin_ia32_cmpunordps' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\xmmintrin.h:1075:1: warning: unable to translate function, demoted to extern
pub extern fn _mm_cmpunord_ps(arg___a: __m128, arg___b: __m128) __m128;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\xmmintrin.h:1101:10: warning: TODO implement function '__builtin_ia32_comieq' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\xmmintrin.h:1099:1: warning: unable to translate function, demoted to extern
pub extern fn _mm_comieq_ss(arg___a: __m128, arg___b: __m128) c_int;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\xmmintrin.h:1126:10: warning: TODO implement function '__builtin_ia32_comilt' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\xmmintrin.h:1124:1: warning: unable to translate function, demoted to extern
pub extern fn _mm_comilt_ss(arg___a: __m128, arg___b: __m128) c_int;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\xmmintrin.h:1150:10: warning: TODO implement function '__builtin_ia32_comile' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\xmmintrin.h:1148:1: warning: unable to translate function, demoted to extern
pub extern fn _mm_comile_ss(arg___a: __m128, arg___b: __m128) c_int;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\xmmintrin.h:1174:10: warning: TODO implement function '__builtin_ia32_comigt' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\xmmintrin.h:1172:1: warning: unable to translate function, demoted to extern
pub extern fn _mm_comigt_ss(arg___a: __m128, arg___b: __m128) c_int;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\xmmintrin.h:1198:10: warning: TODO implement function '__builtin_ia32_comige' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\xmmintrin.h:1196:1: warning: unable to translate function, demoted to extern
pub extern fn _mm_comige_ss(arg___a: __m128, arg___b: __m128) c_int;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\xmmintrin.h:1222:10: warning: TODO implement function '__builtin_ia32_comineq' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\xmmintrin.h:1220:1: warning: unable to translate function, demoted to extern
pub extern fn _mm_comineq_ss(arg___a: __m128, arg___b: __m128) c_int;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\xmmintrin.h:1245:10: warning: TODO implement function '__builtin_ia32_ucomieq' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\xmmintrin.h:1243:1: warning: unable to translate function, demoted to extern
pub extern fn _mm_ucomieq_ss(arg___a: __m128, arg___b: __m128) c_int;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\xmmintrin.h:1269:10: warning: TODO implement function '__builtin_ia32_ucomilt' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\xmmintrin.h:1267:1: warning: unable to translate function, demoted to extern
pub extern fn _mm_ucomilt_ss(arg___a: __m128, arg___b: __m128) c_int;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\xmmintrin.h:1293:10: warning: TODO implement function '__builtin_ia32_ucomile' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\xmmintrin.h:1291:1: warning: unable to translate function, demoted to extern
pub extern fn _mm_ucomile_ss(arg___a: __m128, arg___b: __m128) c_int;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\xmmintrin.h:1317:10: warning: TODO implement function '__builtin_ia32_ucomigt' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\xmmintrin.h:1315:1: warning: unable to translate function, demoted to extern
pub extern fn _mm_ucomigt_ss(arg___a: __m128, arg___b: __m128) c_int;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\xmmintrin.h:1341:10: warning: TODO implement function '__builtin_ia32_ucomige' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\xmmintrin.h:1339:1: warning: unable to translate function, demoted to extern
pub extern fn _mm_ucomige_ss(arg___a: __m128, arg___b: __m128) c_int;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\xmmintrin.h:1364:10: warning: TODO implement function '__builtin_ia32_ucomineq' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\xmmintrin.h:1362:1: warning: unable to translate function, demoted to extern
pub extern fn _mm_ucomineq_ss(arg___a: __m128, arg___b: __m128) c_int;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\xmmintrin.h:1386:10: warning: TODO implement function '__builtin_ia32_cvtss2si' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\xmmintrin.h:1384:1: warning: unable to translate function, demoted to extern
pub extern fn _mm_cvtss_si32(arg___a: __m128) c_int;
pub inline fn _mm_cvt_ss2si(arg___a: __m128) c_int {
    var __a = arg___a;
    _ = &__a;
    return _mm_cvtss_si32(__a);
}
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\xmmintrin.h:1432:10: warning: TODO implement function '__builtin_ia32_cvtss2si64' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\xmmintrin.h:1430:1: warning: unable to translate function, demoted to extern
pub extern fn _mm_cvtss_si64(arg___a: __m128) c_longlong;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\xmmintrin.h:1454:20: warning: TODO implement function '__builtin_ia32_cvtps2dq' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\xmmintrin.h:1452:1: warning: unable to translate function, demoted to extern
pub extern fn _mm_cvtps_pi32(arg___a: __m128) __m64;
pub inline fn _mm_cvt_ps2pi(arg___a: __m128) __m64 {
    var __a = arg___a;
    _ = &__a;
    return _mm_cvtps_pi32(__a);
}
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\xmmintrin.h:1496:10: warning: TODO implement function '__builtin_ia32_cvttss2si' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\xmmintrin.h:1494:1: warning: unable to translate function, demoted to extern
pub extern fn _mm_cvttss_si32(arg___a: __m128) c_int;
pub inline fn _mm_cvtt_ss2si(arg___a: __m128) c_int {
    var __a = arg___a;
    _ = &__a;
    return _mm_cvttss_si32(__a);
}
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\xmmintrin.h:1541:10: warning: TODO implement function '__builtin_ia32_cvttss2si64' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\xmmintrin.h:1539:1: warning: unable to translate function, demoted to extern
pub extern fn _mm_cvttss_si64(arg___a: __m128) c_longlong;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\xmmintrin.h:1564:20: warning: TODO implement function '__builtin_ia32_cvttps2dq' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\xmmintrin.h:1562:1: warning: unable to translate function, demoted to extern
pub extern fn _mm_cvttps_pi32(arg___a: __m128) __m64;
pub inline fn _mm_cvtt_ps2pi(arg___a: __m128) __m64 {
    var __a = arg___a;
    _ = &__a;
    return _mm_cvttps_pi32(__a);
}
pub inline fn _mm_cvtsi32_ss(arg___a: __m128, arg___b: c_int) __m128 {
    var __a = arg___a;
    _ = &__a;
    var __b = arg___b;
    _ = &__b;
    __a[@as(c_int, 0)] = @floatFromInt(__b);
    return __a;
}
pub inline fn _mm_cvt_si2ss(arg___a: __m128, arg___b: c_int) __m128 {
    var __a = arg___a;
    _ = &__a;
    var __b = arg___b;
    _ = &__b;
    return _mm_cvtsi32_ss(__a, __b);
}
pub inline fn _mm_cvtsi64_ss(arg___a: __m128, arg___b: c_longlong) __m128 {
    var __a = arg___a;
    _ = &__a;
    var __b = arg___b;
    _ = &__b;
    __a[@as(c_int, 0)] = @floatFromInt(__b);
    return __a;
}
pub inline fn _mm_cvtpi32_ps(arg___a: __m128, arg___b: __m64) __m128 {
    var __a = arg___a;
    _ = &__a;
    var __b = arg___b;
    _ = &__b;
    return @shuffle(@typeInfo(@TypeOf(__a)).vector.child, __a, blk: {
        const tmp = @as(__v4si, @bitCast(@as(__m128i, @bitCast(@shuffle(@typeInfo(@TypeOf(@as(__v2si, @bitCast(__b)))).vector.child, @as(__v2si, @bitCast(__b)), @as(__v2si, @as([2]c_int, @splat(0))), @Vector(4, i32){
            __helpers.shuffleVectorIndex(0, @typeInfo(@TypeOf(@as(__v2si, @bitCast(__b)))).vector.len),
            __helpers.shuffleVectorIndex(1, @typeInfo(@TypeOf(@as(__v2si, @bitCast(__b)))).vector.len),
            __helpers.shuffleVectorIndex(2, @typeInfo(@TypeOf(@as(__v2si, @bitCast(__b)))).vector.len),
            __helpers.shuffleVectorIndex(3, @typeInfo(@TypeOf(@as(__v2si, @bitCast(__b)))).vector.len),
        })))));
        break :blk __v4sf{
            @floatFromInt(tmp[0]),
            @floatFromInt(tmp[1]),
            @floatFromInt(tmp[2]),
            @floatFromInt(tmp[3]),
        };
    }, @Vector(4, i32){
        __helpers.shuffleVectorIndex(4, @typeInfo(@TypeOf(__a)).vector.len),
        __helpers.shuffleVectorIndex(5, @typeInfo(@TypeOf(__a)).vector.len),
        __helpers.shuffleVectorIndex(2, @typeInfo(@TypeOf(__a)).vector.len),
        __helpers.shuffleVectorIndex(3, @typeInfo(@TypeOf(__a)).vector.len),
    });
}
pub inline fn _mm_cvt_pi2ps(arg___a: __m128, arg___b: __m64) __m128 {
    var __a = arg___a;
    _ = &__a;
    var __b = arg___b;
    _ = &__b;
    return _mm_cvtpi32_ps(__a, __b);
}
pub inline fn _mm_cvtss_f32(arg___a: __m128) f32 {
    var __a = arg___a;
    _ = &__a;
    return __a[@as(c_int, 0)];
}
pub inline fn _mm_loadh_pi(arg___a: __m128, arg___p: [*c]const __m64) __m128 {
    var __a = arg___a;
    _ = &__a;
    var __p = arg___p;
    _ = &__p;
    const __mm_loadh_pi_v2f32 = @Vector(2, f32);
    _ = &__mm_loadh_pi_v2f32;
    const struct___mm_loadh_pi_struct = extern struct {
        __u: __mm_loadh_pi_v2f32 align(1) = @import("std").mem.zeroes(__mm_loadh_pi_v2f32),
    };
    _ = &struct___mm_loadh_pi_struct;
    var __b: __mm_loadh_pi_v2f32 = @as([*c]const struct___mm_loadh_pi_struct, @ptrCast(@alignCast(__p))).*.__u;
    _ = &__b;
    var __bb: __m128 = @shuffle(@typeInfo(@TypeOf(__b)).vector.child, __b, __b, @Vector(4, i32){
        __helpers.shuffleVectorIndex(0, @typeInfo(@TypeOf(__b)).vector.len),
        __helpers.shuffleVectorIndex(1, @typeInfo(@TypeOf(__b)).vector.len),
        __helpers.shuffleVectorIndex(0, @typeInfo(@TypeOf(__b)).vector.len),
        __helpers.shuffleVectorIndex(1, @typeInfo(@TypeOf(__b)).vector.len),
    });
    _ = &__bb;
    return @shuffle(@typeInfo(@TypeOf(__a)).vector.child, __a, __bb, @Vector(4, i32){
        __helpers.shuffleVectorIndex(0, @typeInfo(@TypeOf(__a)).vector.len),
        __helpers.shuffleVectorIndex(1, @typeInfo(@TypeOf(__a)).vector.len),
        __helpers.shuffleVectorIndex(4, @typeInfo(@TypeOf(__a)).vector.len),
        __helpers.shuffleVectorIndex(5, @typeInfo(@TypeOf(__a)).vector.len),
    });
}
pub inline fn _mm_loadl_pi(arg___a: __m128, arg___p: [*c]const __m64) __m128 {
    var __a = arg___a;
    _ = &__a;
    var __p = arg___p;
    _ = &__p;
    const __mm_loadl_pi_v2f32 = @Vector(2, f32);
    _ = &__mm_loadl_pi_v2f32;
    const struct___mm_loadl_pi_struct = extern struct {
        __u: __mm_loadl_pi_v2f32 align(1) = @import("std").mem.zeroes(__mm_loadl_pi_v2f32),
    };
    _ = &struct___mm_loadl_pi_struct;
    var __b: __mm_loadl_pi_v2f32 = @as([*c]const struct___mm_loadl_pi_struct, @ptrCast(@alignCast(__p))).*.__u;
    _ = &__b;
    var __bb: __m128 = @shuffle(@typeInfo(@TypeOf(__b)).vector.child, __b, __b, @Vector(4, i32){
        __helpers.shuffleVectorIndex(0, @typeInfo(@TypeOf(__b)).vector.len),
        __helpers.shuffleVectorIndex(1, @typeInfo(@TypeOf(__b)).vector.len),
        __helpers.shuffleVectorIndex(0, @typeInfo(@TypeOf(__b)).vector.len),
        __helpers.shuffleVectorIndex(1, @typeInfo(@TypeOf(__b)).vector.len),
    });
    _ = &__bb;
    return @shuffle(@typeInfo(@TypeOf(__a)).vector.child, __a, __bb, @Vector(4, i32){
        __helpers.shuffleVectorIndex(4, @typeInfo(@TypeOf(__a)).vector.len),
        __helpers.shuffleVectorIndex(5, @typeInfo(@TypeOf(__a)).vector.len),
        __helpers.shuffleVectorIndex(2, @typeInfo(@TypeOf(__a)).vector.len),
        __helpers.shuffleVectorIndex(3, @typeInfo(@TypeOf(__a)).vector.len),
    });
}
pub inline fn _mm_load_ss(arg___p: [*c]const f32) __m128 {
    var __p = arg___p;
    _ = &__p;
    const struct___mm_load_ss_struct = extern struct {
        __u: f32 align(1) = 0,
    };
    _ = &struct___mm_load_ss_struct;
    var __u: f32 = @as([*c]const struct___mm_load_ss_struct, @ptrCast(@alignCast(__p))).*.__u;
    _ = &__u;
    return [4]f32{
        __u,
        @floatFromInt(@as(c_int, 0)),
        @floatFromInt(@as(c_int, 0)),
        @floatFromInt(@as(c_int, 0)),
    };
}
pub inline fn _mm_load1_ps(arg___p: [*c]const f32) __m128 {
    var __p = arg___p;
    _ = &__p;
    const struct___mm_load1_ps_struct = extern struct {
        __u: f32 align(1) = 0,
    };
    _ = &struct___mm_load1_ps_struct;
    var __u: f32 = @as([*c]const struct___mm_load1_ps_struct, @ptrCast(@alignCast(__p))).*.__u;
    _ = &__u;
    return [4]f32{
        __u,
        __u,
        __u,
        __u,
    };
}
pub inline fn _mm_load_ps(arg___p: [*c]const f32) __m128 {
    var __p = arg___p;
    _ = &__p;
    return @as([*c]const __m128, @ptrCast(@alignCast(__p))).*;
}
pub inline fn _mm_loadu_ps(arg___p: [*c]const f32) __m128 {
    var __p = arg___p;
    _ = &__p;
    const struct___loadu_ps = extern struct {
        __v: __m128_u = @import("std").mem.zeroes(__m128_u),
    };
    _ = &struct___loadu_ps;
    return @as([*c]const struct___loadu_ps, @ptrCast(@alignCast(__p))).*.__v;
}
pub inline fn _mm_loadr_ps(arg___p: [*c]const f32) __m128 {
    var __p = arg___p;
    _ = &__p;
    var __a: __m128 = _mm_load_ps(__p);
    _ = &__a;
    return @shuffle(@typeInfo(@TypeOf(__a)).vector.child, __a, __a, @Vector(4, i32){
        __helpers.shuffleVectorIndex(3, @typeInfo(@TypeOf(__a)).vector.len),
        __helpers.shuffleVectorIndex(2, @typeInfo(@TypeOf(__a)).vector.len),
        __helpers.shuffleVectorIndex(1, @typeInfo(@TypeOf(__a)).vector.len),
        __helpers.shuffleVectorIndex(0, @typeInfo(@TypeOf(__a)).vector.len),
    });
}
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\xmmintrin.h:1892:18: warning: TODO implement function '__builtin_ia32_undef128' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\xmmintrin.h:1890:1: warning: unable to translate function, demoted to extern
pub extern fn _mm_undefined_ps() __m128;
pub inline fn _mm_set_ss(arg___w: f32) __m128 {
    var __w = arg___w;
    _ = &__w;
    return [4]f32{
        __w,
        0.0,
        0.0,
        0.0,
    };
}
pub inline fn _mm_set1_ps(arg___w: f32) __m128 {
    var __w = arg___w;
    _ = &__w;
    return [4]f32{
        __w,
        __w,
        __w,
        __w,
    };
}
pub inline fn _mm_set_ps1(arg___w: f32) __m128 {
    var __w = arg___w;
    _ = &__w;
    return _mm_set1_ps(__w);
}
pub inline fn _mm_set_ps(arg___z: f32, arg___y: f32, arg___x: f32, arg___w: f32) __m128 {
    var __z = arg___z;
    _ = &__z;
    var __y = arg___y;
    _ = &__y;
    var __x = arg___x;
    _ = &__x;
    var __w = arg___w;
    _ = &__w;
    return [4]f32{
        __w,
        __x,
        __y,
        __z,
    };
}
pub inline fn _mm_setr_ps(arg___z: f32, arg___y: f32, arg___x: f32, arg___w: f32) __m128 {
    var __z = arg___z;
    _ = &__z;
    var __y = arg___y;
    _ = &__y;
    var __x = arg___x;
    _ = &__x;
    var __w = arg___w;
    _ = &__w;
    return [4]f32{
        __z,
        __y,
        __x,
        __w,
    };
}
pub inline fn _mm_setzero_ps() __m128 {
    return [4]f32{
        0.0,
        0.0,
        0.0,
        0.0,
    };
}
pub inline fn _mm_storeh_pi(arg___p: [*c]__m64, arg___a: __m128) void {
    var __p = arg___p;
    _ = &__p;
    var __a = arg___a;
    _ = &__a;
    const __mm_storeh_pi_v2f32 = @Vector(2, f32);
    _ = &__mm_storeh_pi_v2f32;
    const struct___mm_storeh_pi_struct = extern struct {
        __u: __mm_storeh_pi_v2f32 align(1) = @import("std").mem.zeroes(__mm_storeh_pi_v2f32),
    };
    _ = &struct___mm_storeh_pi_struct;
    @as([*c]struct___mm_storeh_pi_struct, @ptrCast(@alignCast(__p))).*.__u = @shuffle(@typeInfo(@TypeOf(__a)).vector.child, __a, __a, @Vector(2, i32){
        __helpers.shuffleVectorIndex(2, @typeInfo(@TypeOf(__a)).vector.len),
        __helpers.shuffleVectorIndex(3, @typeInfo(@TypeOf(__a)).vector.len),
    });
}
pub inline fn _mm_storel_pi(arg___p: [*c]__m64, arg___a: __m128) void {
    var __p = arg___p;
    _ = &__p;
    var __a = arg___a;
    _ = &__a;
    const __mm_storeh_pi_v2f32 = @Vector(2, f32);
    _ = &__mm_storeh_pi_v2f32;
    const struct___mm_storeh_pi_struct = extern struct {
        __u: __mm_storeh_pi_v2f32 align(1) = @import("std").mem.zeroes(__mm_storeh_pi_v2f32),
    };
    _ = &struct___mm_storeh_pi_struct;
    @as([*c]struct___mm_storeh_pi_struct, @ptrCast(@alignCast(__p))).*.__u = @shuffle(@typeInfo(@TypeOf(__a)).vector.child, __a, __a, @Vector(2, i32){
        __helpers.shuffleVectorIndex(0, @typeInfo(@TypeOf(__a)).vector.len),
        __helpers.shuffleVectorIndex(1, @typeInfo(@TypeOf(__a)).vector.len),
    });
}
pub inline fn _mm_store_ss(arg___p: [*c]f32, arg___a: __m128) void {
    var __p = arg___p;
    _ = &__p;
    var __a = arg___a;
    _ = &__a;
    const struct___mm_store_ss_struct = extern struct {
        __u: f32 align(1) = 0,
    };
    _ = &struct___mm_store_ss_struct;
    @as([*c]struct___mm_store_ss_struct, @ptrCast(@alignCast(__p))).*.__u = __a[@as(c_int, 0)];
}
pub inline fn _mm_storeu_ps(arg___p: [*c]f32, arg___a: __m128) void {
    var __p = arg___p;
    _ = &__p;
    var __a = arg___a;
    _ = &__a;
    const struct___storeu_ps = extern struct {
        __v: __m128_u = @import("std").mem.zeroes(__m128_u),
    };
    _ = &struct___storeu_ps;
    @as([*c]struct___storeu_ps, @ptrCast(@alignCast(__p))).*.__v = __a;
}
pub inline fn _mm_store_ps(arg___p: [*c]f32, arg___a: __m128) void {
    var __p = arg___p;
    _ = &__p;
    var __a = arg___a;
    _ = &__a;
    @as([*c]__m128, @ptrCast(@alignCast(__p))).* = __a;
}
pub inline fn _mm_store1_ps(arg___p: [*c]f32, arg___a: __m128) void {
    var __p = arg___p;
    _ = &__p;
    var __a = arg___a;
    _ = &__a;
    __a = @shuffle(@typeInfo(@TypeOf(__a)).vector.child, __a, __a, @Vector(4, i32){
        __helpers.shuffleVectorIndex(0, @typeInfo(@TypeOf(__a)).vector.len),
        __helpers.shuffleVectorIndex(0, @typeInfo(@TypeOf(__a)).vector.len),
        __helpers.shuffleVectorIndex(0, @typeInfo(@TypeOf(__a)).vector.len),
        __helpers.shuffleVectorIndex(0, @typeInfo(@TypeOf(__a)).vector.len),
    });
    _mm_store_ps(__p, __a);
}
pub inline fn _mm_store_ps1(arg___p: [*c]f32, arg___a: __m128) void {
    var __p = arg___p;
    _ = &__p;
    var __a = arg___a;
    _ = &__a;
    _mm_store1_ps(__p, __a);
}
pub inline fn _mm_storer_ps(arg___p: [*c]f32, arg___a: __m128) void {
    var __p = arg___p;
    _ = &__p;
    var __a = arg___a;
    _ = &__a;
    __a = @shuffle(@typeInfo(@TypeOf(__a)).vector.child, __a, __a, @Vector(4, i32){
        __helpers.shuffleVectorIndex(3, @typeInfo(@TypeOf(__a)).vector.len),
        __helpers.shuffleVectorIndex(2, @typeInfo(@TypeOf(__a)).vector.len),
        __helpers.shuffleVectorIndex(1, @typeInfo(@TypeOf(__a)).vector.len),
        __helpers.shuffleVectorIndex(0, @typeInfo(@TypeOf(__a)).vector.len),
    });
    _mm_store_ps(__p, __a);
}
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\xmmintrin.h:2231:3: warning: TODO implement function '__builtin_nontemporal_store' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\xmmintrin.h:2229:1: warning: unable to translate function, demoted to extern
pub extern fn _mm_stream_pi(arg___p: ?*anyopaque, arg___a: __m64) void;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\xmmintrin.h:2250:3: warning: TODO implement function '__builtin_nontemporal_store' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\xmmintrin.h:2248:1: warning: unable to translate function, demoted to extern
pub extern fn _mm_stream_ps(arg___p: ?*anyopaque, arg___a: __m128) void;
pub extern fn _mm_sfence() void;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\xmmintrin.h:2341:17: warning: TODO implement function '__builtin_elementwise_max' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\xmmintrin.h:2340:1: warning: unable to translate function, demoted to extern
pub extern fn _mm_max_pi16(arg___a: __m64, arg___b: __m64) __m64;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\xmmintrin.h:2359:17: warning: TODO implement function '__builtin_elementwise_max' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\xmmintrin.h:2358:1: warning: unable to translate function, demoted to extern
pub extern fn _mm_max_pu8(arg___a: __m64, arg___b: __m64) __m64;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\xmmintrin.h:2377:17: warning: TODO implement function '__builtin_elementwise_min' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\xmmintrin.h:2376:1: warning: unable to translate function, demoted to extern
pub extern fn _mm_min_pi16(arg___a: __m64, arg___b: __m64) __m64;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\xmmintrin.h:2395:17: warning: TODO implement function '__builtin_elementwise_min' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\xmmintrin.h:2394:1: warning: unable to translate function, demoted to extern
pub extern fn _mm_min_pu8(arg___a: __m64, arg___b: __m64) __m64;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\xmmintrin.h:2412:10: warning: TODO implement function '__builtin_ia32_pmovmskb128' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\xmmintrin.h:2411:1: warning: unable to translate function, demoted to extern
pub extern fn _mm_movemask_pi8(arg___a: __m64) c_int;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\xmmintrin.h:2431:20: warning: TODO implement function '__builtin_ia32_pmulhuw128' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\xmmintrin.h:2429:1: warning: unable to translate function, demoted to extern
pub extern fn _mm_mulhi_pu16(arg___a: __m64, arg___b: __m64) __m64;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\xmmintrin.h:2511:23: warning: TODO implement function '__builtin_ia32_pslldqi128_byteshift' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\xmmintrin.h:2498:1: warning: unable to translate function, demoted to extern
pub extern fn _mm_maskmove_si64(arg___d: __m64, arg___n: __m64, arg___p: [*c]u8) void;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\xmmintrin.h:2533:20: warning: TODO implement function '__builtin_ia32_pavgb128' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\xmmintrin.h:2532:1: warning: unable to translate function, demoted to extern
pub extern fn _mm_avg_pu8(arg___a: __m64, arg___b: __m64) __m64;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\xmmintrin.h:2553:7: warning: TODO implement function '__builtin_ia32_pavgw128' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\xmmintrin.h:2551:1: warning: unable to translate function, demoted to extern
pub extern fn _mm_avg_pu16(arg___a: __m64, arg___b: __m64) __m64;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\xmmintrin.h:2575:20: warning: TODO implement function '__builtin_ia32_psadbw128' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\xmmintrin.h:2573:1: warning: unable to translate function, demoted to extern
pub extern fn _mm_sad_pu8(arg___a: __m64, arg___b: __m64) __m64;
pub extern fn _mm_getcsr() c_uint;
pub extern fn _mm_setcsr(__i: c_uint) void;
pub inline fn _mm_unpackhi_ps(arg___a: __m128, arg___b: __m128) __m128 {
    var __a = arg___a;
    _ = &__a;
    var __b = arg___b;
    _ = &__b;
    return @shuffle(@typeInfo(@TypeOf(__a)).vector.child, __a, __b, @Vector(4, i32){
        __helpers.shuffleVectorIndex(2, @typeInfo(@TypeOf(__a)).vector.len),
        __helpers.shuffleVectorIndex(6, @typeInfo(@TypeOf(__a)).vector.len),
        __helpers.shuffleVectorIndex(3, @typeInfo(@TypeOf(__a)).vector.len),
        __helpers.shuffleVectorIndex(7, @typeInfo(@TypeOf(__a)).vector.len),
    });
}
pub inline fn _mm_unpacklo_ps(arg___a: __m128, arg___b: __m128) __m128 {
    var __a = arg___a;
    _ = &__a;
    var __b = arg___b;
    _ = &__b;
    return @shuffle(@typeInfo(@TypeOf(__a)).vector.child, __a, __b, @Vector(4, i32){
        __helpers.shuffleVectorIndex(0, @typeInfo(@TypeOf(__a)).vector.len),
        __helpers.shuffleVectorIndex(4, @typeInfo(@TypeOf(__a)).vector.len),
        __helpers.shuffleVectorIndex(1, @typeInfo(@TypeOf(__a)).vector.len),
        __helpers.shuffleVectorIndex(5, @typeInfo(@TypeOf(__a)).vector.len),
    });
}
pub inline fn _mm_move_ss(arg___a: __m128, arg___b: __m128) __m128 {
    var __a = arg___a;
    _ = &__a;
    var __b = arg___b;
    _ = &__b;
    __a[@as(c_int, 0)] = __b[@as(c_int, 0)];
    return __a;
}
pub inline fn _mm_movehl_ps(arg___a: __m128, arg___b: __m128) __m128 {
    var __a = arg___a;
    _ = &__a;
    var __b = arg___b;
    _ = &__b;
    return @shuffle(@typeInfo(@TypeOf(__a)).vector.child, __a, __b, @Vector(4, i32){
        __helpers.shuffleVectorIndex(6, @typeInfo(@TypeOf(__a)).vector.len),
        __helpers.shuffleVectorIndex(7, @typeInfo(@TypeOf(__a)).vector.len),
        __helpers.shuffleVectorIndex(2, @typeInfo(@TypeOf(__a)).vector.len),
        __helpers.shuffleVectorIndex(3, @typeInfo(@TypeOf(__a)).vector.len),
    });
}
pub inline fn _mm_movelh_ps(arg___a: __m128, arg___b: __m128) __m128 {
    var __a = arg___a;
    _ = &__a;
    var __b = arg___b;
    _ = &__b;
    return @shuffle(@typeInfo(@TypeOf(__a)).vector.child, __a, __b, @Vector(4, i32){
        __helpers.shuffleVectorIndex(0, @typeInfo(@TypeOf(__a)).vector.len),
        __helpers.shuffleVectorIndex(1, @typeInfo(@TypeOf(__a)).vector.len),
        __helpers.shuffleVectorIndex(4, @typeInfo(@TypeOf(__a)).vector.len),
        __helpers.shuffleVectorIndex(5, @typeInfo(@TypeOf(__a)).vector.len),
    });
}
pub inline fn _mm_cvtpi16_ps(arg___a: __m64) __m128 {
    var __a = arg___a;
    _ = &__a;
    return blk: {
        const tmp = @as(__v4hi, @bitCast(__a));
        break :blk __v4sf{
            @floatFromInt(tmp[0]),
            @floatFromInt(tmp[1]),
            @floatFromInt(tmp[2]),
            @floatFromInt(tmp[3]),
        };
    };
}
pub inline fn _mm_cvtpu16_ps(arg___a: __m64) __m128 {
    var __a = arg___a;
    _ = &__a;
    return blk: {
        const tmp = @as(__v4hu, @bitCast(__a));
        break :blk __v4sf{
            @floatFromInt(tmp[0]),
            @floatFromInt(tmp[1]),
            @floatFromInt(tmp[2]),
            @floatFromInt(tmp[3]),
        };
    };
}
pub inline fn _mm_cvtpi8_ps(arg___a: __m64) __m128 {
    var __a = arg___a;
    _ = &__a;
    return blk: {
        const tmp = @shuffle(@typeInfo(@TypeOf(@as(__v8qs, @bitCast(__a)))).vector.child, @as(__v8qs, @bitCast(__a)), @as(__v8qs, @as([8]i8, @splat(0))), @Vector(4, i32){
            __helpers.shuffleVectorIndex(0, @typeInfo(@TypeOf(@as(__v8qs, @bitCast(__a)))).vector.len),
            __helpers.shuffleVectorIndex(1, @typeInfo(@TypeOf(@as(__v8qs, @bitCast(__a)))).vector.len),
            __helpers.shuffleVectorIndex(2, @typeInfo(@TypeOf(@as(__v8qs, @bitCast(__a)))).vector.len),
            __helpers.shuffleVectorIndex(3, @typeInfo(@TypeOf(@as(__v8qs, @bitCast(__a)))).vector.len),
        });
        break :blk __v4sf{
            @floatFromInt(tmp[0]),
            @floatFromInt(tmp[1]),
            @floatFromInt(tmp[2]),
            @floatFromInt(tmp[3]),
        };
    };
}
pub inline fn _mm_cvtpu8_ps(arg___a: __m64) __m128 {
    var __a = arg___a;
    _ = &__a;
    return blk: {
        const tmp = @shuffle(@typeInfo(@TypeOf(@as(__v8qu, @bitCast(__a)))).vector.child, @as(__v8qu, @bitCast(__a)), @as(__v8qu, @as([8]u8, @splat(0))), @Vector(4, i32){
            __helpers.shuffleVectorIndex(0, @typeInfo(@TypeOf(@as(__v8qu, @bitCast(__a)))).vector.len),
            __helpers.shuffleVectorIndex(1, @typeInfo(@TypeOf(@as(__v8qu, @bitCast(__a)))).vector.len),
            __helpers.shuffleVectorIndex(2, @typeInfo(@TypeOf(@as(__v8qu, @bitCast(__a)))).vector.len),
            __helpers.shuffleVectorIndex(3, @typeInfo(@TypeOf(@as(__v8qu, @bitCast(__a)))).vector.len),
        });
        break :blk __v4sf{
            @floatFromInt(tmp[0]),
            @floatFromInt(tmp[1]),
            @floatFromInt(tmp[2]),
            @floatFromInt(tmp[3]),
        };
    };
}
pub inline fn _mm_cvtpi32x2_ps(arg___a: __m64, arg___b: __m64) __m128 {
    var __a = arg___a;
    _ = &__a;
    var __b = arg___b;
    _ = &__b;
    return blk: {
        const tmp = @shuffle(@typeInfo(@TypeOf(@as(__v2si, @bitCast(__a)))).vector.child, @as(__v2si, @bitCast(__a)), @as(__v2si, @bitCast(__b)), @Vector(4, i32){
            __helpers.shuffleVectorIndex(0, @typeInfo(@TypeOf(@as(__v2si, @bitCast(__a)))).vector.len),
            __helpers.shuffleVectorIndex(1, @typeInfo(@TypeOf(@as(__v2si, @bitCast(__a)))).vector.len),
            __helpers.shuffleVectorIndex(2, @typeInfo(@TypeOf(@as(__v2si, @bitCast(__a)))).vector.len),
            __helpers.shuffleVectorIndex(3, @typeInfo(@TypeOf(@as(__v2si, @bitCast(__a)))).vector.len),
        });
        break :blk __v4sf{
            @floatFromInt(tmp[0]),
            @floatFromInt(tmp[1]),
            @floatFromInt(tmp[2]),
            @floatFromInt(tmp[3]),
        };
    };
}
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\xmmintrin.h:2961:20: warning: TODO implement function '__builtin_ia32_packssdw128' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\xmmintrin.h:2959:1: warning: unable to translate function, demoted to extern
pub extern fn _mm_cvtps_pi16(arg___a: __m128) __m64;
pub inline fn _mm_cvtps_pi8(arg___a: __m128) __m64 {
    var __a = arg___a;
    _ = &__a;
    var __b: __m64 = undefined;
    _ = &__b;
    var __c: __m64 = undefined;
    _ = &__c;
    __b = _mm_cvtps_pi16(__a);
    __c = _mm_setzero_si64();
    return _mm_packs_pi16(__b, __c);
}
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\xmmintrin.h:3009:10: warning: TODO implement function '__builtin_ia32_movmskps' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\xmmintrin.h:3008:52: warning: unable to translate function, demoted to extern
pub extern fn _mm_movemask_ps(arg___a: __m128) c_int;
pub const __m128d = @Vector(2, f64);
pub const __m128d_u = @Vector(2, f64);
pub const __m128i_u = @Vector(2, c_longlong);
pub const __v2df = @Vector(2, f64);
pub const __v2du = @Vector(2, c_ulonglong);
pub const __v16qs = @Vector(16, i8);
pub const __v8hf = @Vector(8, f16);
pub const __m128h = @Vector(8, f16);
pub const __m128h_u = @Vector(8, f16);
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:41:16: warning: TODO support bfloat16
pub const __v8bf = @compileError("unable to resolve typedef child type");
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:41:16

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:42:16: warning: TODO support bfloat16
pub const __m128bh = @compileError("unable to resolve typedef child type");
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:42:16
pub inline fn _mm_add_sd(arg___a: __m128d, arg___b: __m128d) __m128d {
    var __a = arg___a;
    _ = &__a;
    var __b = arg___b;
    _ = &__b;
    __a[@as(c_int, 0)] += __b[@as(c_int, 0)];
    return __a;
}
pub inline fn _mm_add_pd(arg___a: __m128d, arg___b: __m128d) __m128d {
    var __a = arg___a;
    _ = &__a;
    var __b = arg___b;
    _ = &__b;
    return __a + __b;
}
pub inline fn _mm_sub_sd(arg___a: __m128d, arg___b: __m128d) __m128d {
    var __a = arg___a;
    _ = &__a;
    var __b = arg___b;
    _ = &__b;
    __a[@as(c_int, 0)] -= __b[@as(c_int, 0)];
    return __a;
}
pub inline fn _mm_sub_pd(arg___a: __m128d, arg___b: __m128d) __m128d {
    var __a = arg___a;
    _ = &__a;
    var __b = arg___b;
    _ = &__b;
    return __a - __b;
}
pub inline fn _mm_mul_sd(arg___a: __m128d, arg___b: __m128d) __m128d {
    var __a = arg___a;
    _ = &__a;
    var __b = arg___b;
    _ = &__b;
    __a[@as(c_int, 0)] *= __b[@as(c_int, 0)];
    return __a;
}
pub inline fn _mm_mul_pd(arg___a: __m128d, arg___b: __m128d) __m128d {
    var __a = arg___a;
    _ = &__a;
    var __b = arg___b;
    _ = &__b;
    return __a * __b;
}
pub inline fn _mm_div_sd(arg___a: __m128d, arg___b: __m128d) __m128d {
    var __a = arg___a;
    _ = &__a;
    var __b = arg___b;
    _ = &__b;
    __a[@as(c_int, 0)] /= __b[@as(c_int, 0)];
    return __a;
}
pub inline fn _mm_div_pd(arg___a: __m128d, arg___b: __m128d) __m128d {
    var __a = arg___a;
    _ = &__a;
    var __b = arg___b;
    _ = &__b;
    return __a / __b;
}
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:244:33: warning: TODO implement function '__builtin_elementwise_sqrt' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:242:46: warning: unable to translate function, demoted to extern
pub extern fn _mm_sqrt_sd(arg___a: __m128d, arg___b: __m128d) __m128d;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:259:10: warning: TODO implement function '__builtin_elementwise_sqrt' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:258:46: warning: unable to translate function, demoted to extern
pub extern fn _mm_sqrt_pd(arg___a: __m128d) __m128d;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:284:10: warning: TODO implement function '__builtin_ia32_minsd' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:282:46: warning: unable to translate function, demoted to extern
pub extern fn _mm_min_sd(arg___a: __m128d, arg___b: __m128d) __m128d;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:305:10: warning: TODO implement function '__builtin_ia32_minpd' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:303:56: warning: unable to translate function, demoted to extern
pub extern fn _mm_min_pd(arg___a: __m128d, arg___b: __m128d) __m128d;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:330:10: warning: TODO implement function '__builtin_ia32_maxsd' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:328:46: warning: unable to translate function, demoted to extern
pub extern fn _mm_max_sd(arg___a: __m128d, arg___b: __m128d) __m128d;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:351:10: warning: TODO implement function '__builtin_ia32_maxpd' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:349:56: warning: unable to translate function, demoted to extern
pub extern fn _mm_max_pd(arg___a: __m128d, arg___b: __m128d) __m128d;
pub inline fn _mm_and_pd(arg___a: __m128d, arg___b: __m128d) __m128d {
    var __a = arg___a;
    _ = &__a;
    var __b = arg___b;
    _ = &__b;
    return @bitCast(@as(__v2du, @bitCast(__a)) & @as(__v2du, @bitCast(__b)));
}
pub inline fn _mm_andnot_pd(arg___a: __m128d, arg___b: __m128d) __m128d {
    var __a = arg___a;
    _ = &__a;
    var __b = arg___b;
    _ = &__b;
    return @bitCast(~@as(__v2du, @bitCast(__a)) & @as(__v2du, @bitCast(__b)));
}
pub inline fn _mm_or_pd(arg___a: __m128d, arg___b: __m128d) __m128d {
    var __a = arg___a;
    _ = &__a;
    var __b = arg___b;
    _ = &__b;
    return @bitCast(@as(__v2du, @bitCast(__a)) | @as(__v2du, @bitCast(__b)));
}
pub inline fn _mm_xor_pd(arg___a: __m128d, arg___b: __m128d) __m128d {
    var __a = arg___a;
    _ = &__a;
    var __b = arg___b;
    _ = &__b;
    return @bitCast(@as(__v2du, @bitCast(__a)) ^ @as(__v2du, @bitCast(__b)));
}
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:442:19: warning: TODO implement function '__builtin_ia32_cmpeqpd' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:440:46: warning: unable to translate function, demoted to extern
pub extern fn _mm_cmpeq_pd(arg___a: __m128d, arg___b: __m128d) __m128d;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:463:19: warning: TODO implement function '__builtin_ia32_cmpltpd' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:461:46: warning: unable to translate function, demoted to extern
pub extern fn _mm_cmplt_pd(arg___a: __m128d, arg___b: __m128d) __m128d;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:484:19: warning: TODO implement function '__builtin_ia32_cmplepd' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:482:46: warning: unable to translate function, demoted to extern
pub extern fn _mm_cmple_pd(arg___a: __m128d, arg___b: __m128d) __m128d;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:505:19: warning: TODO implement function '__builtin_ia32_cmpltpd' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:503:46: warning: unable to translate function, demoted to extern
pub extern fn _mm_cmpgt_pd(arg___a: __m128d, arg___b: __m128d) __m128d;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:526:19: warning: TODO implement function '__builtin_ia32_cmplepd' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:524:46: warning: unable to translate function, demoted to extern
pub extern fn _mm_cmpge_pd(arg___a: __m128d, arg___b: __m128d) __m128d;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:548:19: warning: TODO implement function '__builtin_ia32_cmpordpd' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:546:46: warning: unable to translate function, demoted to extern
pub extern fn _mm_cmpord_pd(arg___a: __m128d, arg___b: __m128d) __m128d;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:571:19: warning: TODO implement function '__builtin_ia32_cmpunordpd' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:569:46: warning: unable to translate function, demoted to extern
pub extern fn _mm_cmpunord_pd(arg___a: __m128d, arg___b: __m128d) __m128d;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:592:19: warning: TODO implement function '__builtin_ia32_cmpneqpd' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:590:46: warning: unable to translate function, demoted to extern
pub extern fn _mm_cmpneq_pd(arg___a: __m128d, arg___b: __m128d) __m128d;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:613:19: warning: TODO implement function '__builtin_ia32_cmpnltpd' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:611:46: warning: unable to translate function, demoted to extern
pub extern fn _mm_cmpnlt_pd(arg___a: __m128d, arg___b: __m128d) __m128d;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:634:19: warning: TODO implement function '__builtin_ia32_cmpnlepd' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:632:46: warning: unable to translate function, demoted to extern
pub extern fn _mm_cmpnle_pd(arg___a: __m128d, arg___b: __m128d) __m128d;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:655:19: warning: TODO implement function '__builtin_ia32_cmpnltpd' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:653:46: warning: unable to translate function, demoted to extern
pub extern fn _mm_cmpngt_pd(arg___a: __m128d, arg___b: __m128d) __m128d;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:676:19: warning: TODO implement function '__builtin_ia32_cmpnlepd' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:674:46: warning: unable to translate function, demoted to extern
pub extern fn _mm_cmpnge_pd(arg___a: __m128d, arg___b: __m128d) __m128d;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:699:19: warning: TODO implement function '__builtin_ia32_cmpeqsd' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:697:46: warning: unable to translate function, demoted to extern
pub extern fn _mm_cmpeq_sd(arg___a: __m128d, arg___b: __m128d) __m128d;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:724:19: warning: TODO implement function '__builtin_ia32_cmpltsd' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:722:46: warning: unable to translate function, demoted to extern
pub extern fn _mm_cmplt_sd(arg___a: __m128d, arg___b: __m128d) __m128d;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:749:19: warning: TODO implement function '__builtin_ia32_cmplesd' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:747:46: warning: unable to translate function, demoted to extern
pub extern fn _mm_cmple_sd(arg___a: __m128d, arg___b: __m128d) __m128d;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:774:17: warning: TODO implement function '__builtin_ia32_cmpltsd' in std.zig.c_builtins
pub inline fn _mm_cmpgt_sd(arg___a: __m128d, arg___b: __m128d) __m128d {
    var __a = arg___a;
    _ = &__a;
    var __b = arg___b;
    _ = &__b;
    const __c = if (true) @compileError("unable to resolve var init expr");
    _ = &__c;
    return [2]f64{
        __c[@as(c_int, 0)],
        __a[@as(c_int, 1)],
    };
}
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:800:17: warning: TODO implement function '__builtin_ia32_cmplesd' in std.zig.c_builtins
pub inline fn _mm_cmpge_sd(arg___a: __m128d, arg___b: __m128d) __m128d {
    var __a = arg___a;
    _ = &__a;
    var __b = arg___b;
    _ = &__b;
    const __c = if (true) @compileError("unable to resolve var init expr");
    _ = &__c;
    return [2]f64{
        __c[@as(c_int, 0)],
        __a[@as(c_int, 1)],
    };
}
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:827:19: warning: TODO implement function '__builtin_ia32_cmpordsd' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:825:46: warning: unable to translate function, demoted to extern
pub extern fn _mm_cmpord_sd(arg___a: __m128d, arg___b: __m128d) __m128d;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:854:19: warning: TODO implement function '__builtin_ia32_cmpunordsd' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:852:46: warning: unable to translate function, demoted to extern
pub extern fn _mm_cmpunord_sd(arg___a: __m128d, arg___b: __m128d) __m128d;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:879:19: warning: TODO implement function '__builtin_ia32_cmpneqsd' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:877:46: warning: unable to translate function, demoted to extern
pub extern fn _mm_cmpneq_sd(arg___a: __m128d, arg___b: __m128d) __m128d;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:904:19: warning: TODO implement function '__builtin_ia32_cmpnltsd' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:902:46: warning: unable to translate function, demoted to extern
pub extern fn _mm_cmpnlt_sd(arg___a: __m128d, arg___b: __m128d) __m128d;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:929:19: warning: TODO implement function '__builtin_ia32_cmpnlesd' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:927:46: warning: unable to translate function, demoted to extern
pub extern fn _mm_cmpnle_sd(arg___a: __m128d, arg___b: __m128d) __m128d;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:954:17: warning: TODO implement function '__builtin_ia32_cmpnltsd' in std.zig.c_builtins
pub inline fn _mm_cmpngt_sd(arg___a: __m128d, arg___b: __m128d) __m128d {
    var __a = arg___a;
    _ = &__a;
    var __b = arg___b;
    _ = &__b;
    const __c = if (true) @compileError("unable to resolve var init expr");
    _ = &__c;
    return [2]f64{
        __c[@as(c_int, 0)],
        __a[@as(c_int, 1)],
    };
}
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:980:17: warning: TODO implement function '__builtin_ia32_cmpnlesd' in std.zig.c_builtins
pub inline fn _mm_cmpnge_sd(arg___a: __m128d, arg___b: __m128d) __m128d {
    var __a = arg___a;
    _ = &__a;
    var __b = arg___b;
    _ = &__b;
    const __c = if (true) @compileError("unable to resolve var init expr");
    _ = &__c;
    return [2]f64{
        __c[@as(c_int, 0)],
        __a[@as(c_int, 1)],
    };
}
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:1003:10: warning: TODO implement function '__builtin_ia32_comisdeq' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:1001:42: warning: unable to translate function, demoted to extern
pub extern fn _mm_comieq_sd(arg___a: __m128d, arg___b: __m128d) c_int;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:1027:10: warning: TODO implement function '__builtin_ia32_comisdlt' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:1025:42: warning: unable to translate function, demoted to extern
pub extern fn _mm_comilt_sd(arg___a: __m128d, arg___b: __m128d) c_int;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:1051:10: warning: TODO implement function '__builtin_ia32_comisdle' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:1049:42: warning: unable to translate function, demoted to extern
pub extern fn _mm_comile_sd(arg___a: __m128d, arg___b: __m128d) c_int;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:1075:10: warning: TODO implement function '__builtin_ia32_comisdgt' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:1073:42: warning: unable to translate function, demoted to extern
pub extern fn _mm_comigt_sd(arg___a: __m128d, arg___b: __m128d) c_int;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:1099:10: warning: TODO implement function '__builtin_ia32_comisdge' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:1097:42: warning: unable to translate function, demoted to extern
pub extern fn _mm_comige_sd(arg___a: __m128d, arg___b: __m128d) c_int;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:1123:10: warning: TODO implement function '__builtin_ia32_comisdneq' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:1121:42: warning: unable to translate function, demoted to extern
pub extern fn _mm_comineq_sd(arg___a: __m128d, arg___b: __m128d) c_int;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:1145:10: warning: TODO implement function '__builtin_ia32_ucomisdeq' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:1143:42: warning: unable to translate function, demoted to extern
pub extern fn _mm_ucomieq_sd(arg___a: __m128d, arg___b: __m128d) c_int;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:1169:10: warning: TODO implement function '__builtin_ia32_ucomisdlt' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:1167:42: warning: unable to translate function, demoted to extern
pub extern fn _mm_ucomilt_sd(arg___a: __m128d, arg___b: __m128d) c_int;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:1193:10: warning: TODO implement function '__builtin_ia32_ucomisdle' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:1191:42: warning: unable to translate function, demoted to extern
pub extern fn _mm_ucomile_sd(arg___a: __m128d, arg___b: __m128d) c_int;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:1217:10: warning: TODO implement function '__builtin_ia32_ucomisdgt' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:1215:42: warning: unable to translate function, demoted to extern
pub extern fn _mm_ucomigt_sd(arg___a: __m128d, arg___b: __m128d) c_int;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:1241:10: warning: TODO implement function '__builtin_ia32_ucomisdge' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:1239:42: warning: unable to translate function, demoted to extern
pub extern fn _mm_ucomige_sd(arg___a: __m128d, arg___b: __m128d) c_int;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:1265:10: warning: TODO implement function '__builtin_ia32_ucomisdneq' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:1263:42: warning: unable to translate function, demoted to extern
pub extern fn _mm_ucomineq_sd(arg___a: __m128d, arg___b: __m128d) c_int;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:1283:10: warning: TODO implement function '__builtin_ia32_cvtpd2ps' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:1282:1: warning: unable to translate function, demoted to extern
pub extern fn _mm_cvtpd_ps(arg___a: __m128d) __m128;
pub inline fn _mm_cvtps_pd(arg___a: __m128) __m128d {
    var __a = arg___a;
    _ = &__a;
    return blk: {
        const tmp = @shuffle(@typeInfo(@TypeOf(__a)).vector.child, __a, __a, @Vector(2, i32){
            __helpers.shuffleVectorIndex(0, @typeInfo(@TypeOf(__a)).vector.len),
            __helpers.shuffleVectorIndex(1, @typeInfo(@TypeOf(__a)).vector.len),
        });
        break :blk __v2df{
            @floatCast(tmp[0]),
            @floatCast(tmp[1]),
        };
    };
}
pub inline fn _mm_cvtepi32_pd(arg___a: __m128i) __m128d {
    var __a = arg___a;
    _ = &__a;
    return blk: {
        const tmp = @shuffle(@typeInfo(@TypeOf(@as(__v4si, @bitCast(__a)))).vector.child, @as(__v4si, @bitCast(__a)), @as(__v4si, @bitCast(__a)), @Vector(2, i32){
            __helpers.shuffleVectorIndex(0, @typeInfo(@TypeOf(@as(__v4si, @bitCast(__a)))).vector.len),
            __helpers.shuffleVectorIndex(1, @typeInfo(@TypeOf(@as(__v4si, @bitCast(__a)))).vector.len),
        });
        break :blk __v2df{
            @floatFromInt(tmp[0]),
            @floatFromInt(tmp[1]),
        };
    };
}
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:1346:10: warning: TODO implement function '__builtin_ia32_cvtpd2dq' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:1345:46: warning: unable to translate function, demoted to extern
pub extern fn _mm_cvtpd_epi32(arg___a: __m128d) __m128i;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:1365:10: warning: TODO implement function '__builtin_ia32_cvtsd2si' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:1364:42: warning: unable to translate function, demoted to extern
pub extern fn _mm_cvtsd_si32(arg___a: __m128d) c_int;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:1389:18: warning: TODO implement function '__builtin_ia32_cvtsd2ss' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:1388:1: warning: unable to translate function, demoted to extern
pub extern fn _mm_cvtsd_ss(arg___a: __m128, arg___b: __m128d) __m128;
pub inline fn _mm_cvtsi32_sd(arg___a: __m128d, arg___b: c_int) __m128d {
    var __a = arg___a;
    _ = &__a;
    var __b = arg___b;
    _ = &__b;
    __a[@as(c_int, 0)] = @floatFromInt(__b);
    return __a;
}
pub inline fn _mm_cvtss_sd(arg___a: __m128d, arg___b: __m128) __m128d {
    var __a = arg___a;
    _ = &__a;
    var __b = arg___b;
    _ = &__b;
    __a[@as(c_int, 0)] = @floatCast(__b[@as(c_int, 0)]);
    return __a;
}
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:1459:19: warning: TODO implement function '__builtin_ia32_cvttpd2dq' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:1458:46: warning: unable to translate function, demoted to extern
pub extern fn _mm_cvttpd_epi32(arg___a: __m128d) __m128i;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:1479:10: warning: TODO implement function '__builtin_ia32_cvttsd2si' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:1478:42: warning: unable to translate function, demoted to extern
pub extern fn _mm_cvttsd_si32(arg___a: __m128d) c_int;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:1498:20: warning: TODO implement function '__builtin_ia32_cvtpd2dq' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:1497:44: warning: unable to translate function, demoted to extern
pub extern fn _mm_cvtpd_pi32(arg___a: __m128d) __m64;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:1517:20: warning: TODO implement function '__builtin_ia32_cvttpd2dq' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:1516:44: warning: unable to translate function, demoted to extern
pub extern fn _mm_cvttpd_pi32(arg___a: __m128d) __m64;
pub inline fn _mm_cvtpi32_pd(arg___a: __m64) __m128d {
    var __a = arg___a;
    _ = &__a;
    return blk: {
        const tmp = @as(__v2si, @bitCast(__a));
        break :blk __v2df{
            @floatFromInt(tmp[0]),
            @floatFromInt(tmp[1]),
        };
    };
}
pub inline fn _mm_cvtsd_f64(arg___a: __m128d) f64 {
    var __a = arg___a;
    _ = &__a;
    return __a[@as(c_int, 0)];
}
pub inline fn _mm_load_pd(arg___dp: [*c]const f64) __m128d {
    var __dp = arg___dp;
    _ = &__dp;
    return @as([*c]const __m128d, @ptrCast(@alignCast(__dp))).*;
}
pub inline fn _mm_load1_pd(arg___dp: [*c]const f64) __m128d {
    var __dp = arg___dp;
    _ = &__dp;
    const struct___mm_load1_pd_struct = extern struct {
        __u: f64 align(1) = 0,
    };
    _ = &struct___mm_load1_pd_struct;
    var __u: f64 = @as([*c]const struct___mm_load1_pd_struct, @ptrCast(@alignCast(__dp))).*.__u;
    _ = &__u;
    return [2]f64{
        __u,
        __u,
    };
}
pub inline fn _mm_loadr_pd(arg___dp: [*c]const f64) __m128d {
    var __dp = arg___dp;
    _ = &__dp;
    var __u: __m128d = @as([*c]const __m128d, @ptrCast(@alignCast(__dp))).*;
    _ = &__u;
    return @shuffle(@typeInfo(@TypeOf(__u)).vector.child, __u, __u, @Vector(2, i32){
        __helpers.shuffleVectorIndex(1, @typeInfo(@TypeOf(__u)).vector.len),
        __helpers.shuffleVectorIndex(0, @typeInfo(@TypeOf(__u)).vector.len),
    });
}
pub inline fn _mm_loadu_pd(arg___dp: [*c]const f64) __m128d {
    var __dp = arg___dp;
    _ = &__dp;
    const struct___loadu_pd = extern struct {
        __v: __m128d_u = @import("std").mem.zeroes(__m128d_u),
    };
    _ = &struct___loadu_pd;
    return @as([*c]const struct___loadu_pd, @ptrCast(@alignCast(__dp))).*.__v;
}
pub inline fn _mm_loadu_si64(arg___a: ?*const anyopaque) __m128i {
    var __a = arg___a;
    _ = &__a;
    const struct___loadu_si64 = extern struct {
        __v: c_longlong align(1) = 0,
    };
    _ = &struct___loadu_si64;
    var __u: c_longlong = @as([*c]const struct___loadu_si64, @ptrCast(@alignCast(__a))).*.__v;
    _ = &__u;
    return @as(__v2di, [2]c_longlong{
        __u,
        0,
    });
}
pub inline fn _mm_loadu_si32(arg___a: ?*const anyopaque) __m128i {
    var __a = arg___a;
    _ = &__a;
    const struct___loadu_si32 = extern struct {
        __v: c_int align(1) = 0,
    };
    _ = &struct___loadu_si32;
    var __u: c_int = @as([*c]const struct___loadu_si32, @ptrCast(@alignCast(__a))).*.__v;
    _ = &__u;
    return @bitCast(@as(__v4si, [4]c_int{
        __u,
        0,
        0,
        0,
    }));
}
pub inline fn _mm_loadu_si16(arg___a: ?*const anyopaque) __m128i {
    var __a = arg___a;
    _ = &__a;
    const struct___loadu_si16 = extern struct {
        __v: c_short align(1) = 0,
    };
    _ = &struct___loadu_si16;
    var __u: c_short = @as([*c]const struct___loadu_si16, @ptrCast(@alignCast(__a))).*.__v;
    _ = &__u;
    return @bitCast(@as(__v8hi, [8]c_short{
        __u,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
    }));
}
pub inline fn _mm_load_sd(arg___dp: [*c]const f64) __m128d {
    var __dp = arg___dp;
    _ = &__dp;
    const struct___mm_load_sd_struct = extern struct {
        __u: f64 align(1) = 0,
    };
    _ = &struct___mm_load_sd_struct;
    var __u: f64 = @as([*c]const struct___mm_load_sd_struct, @ptrCast(@alignCast(__dp))).*.__u;
    _ = &__u;
    return [2]f64{
        __u,
        @floatFromInt(@as(c_int, 0)),
    };
}
pub inline fn _mm_loadh_pd(arg___a: __m128d, arg___dp: [*c]const f64) __m128d {
    var __a = arg___a;
    _ = &__a;
    var __dp = arg___dp;
    _ = &__dp;
    const struct___mm_loadh_pd_struct = extern struct {
        __u: f64 align(1) = 0,
    };
    _ = &struct___mm_loadh_pd_struct;
    var __u: f64 = @as([*c]const struct___mm_loadh_pd_struct, @ptrCast(@alignCast(__dp))).*.__u;
    _ = &__u;
    return [2]f64{
        __a[@as(c_int, 0)],
        __u,
    };
}
pub inline fn _mm_loadl_pd(arg___a: __m128d, arg___dp: [*c]const f64) __m128d {
    var __a = arg___a;
    _ = &__a;
    var __dp = arg___dp;
    _ = &__dp;
    const struct___mm_loadl_pd_struct = extern struct {
        __u: f64 align(1) = 0,
    };
    _ = &struct___mm_loadl_pd_struct;
    var __u: f64 = @as([*c]const struct___mm_loadl_pd_struct, @ptrCast(@alignCast(__dp))).*.__u;
    _ = &__u;
    return [2]f64{
        __u,
        __a[@as(c_int, 1)],
    };
}
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:1766:19: warning: TODO implement function '__builtin_ia32_undef128' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:1765:46: warning: unable to translate function, demoted to extern
pub extern fn _mm_undefined_pd() __m128d;
pub inline fn _mm_set_sd(arg___w: f64) __m128d {
    var __w = arg___w;
    _ = &__w;
    return [2]f64{
        __w,
        0.0,
    };
}
pub inline fn _mm_set1_pd(arg___w: f64) __m128d {
    var __w = arg___w;
    _ = &__w;
    return [2]f64{
        __w,
        __w,
    };
}
pub inline fn _mm_set_pd1(arg___w: f64) __m128d {
    var __w = arg___w;
    _ = &__w;
    return _mm_set1_pd(__w);
}
pub inline fn _mm_set_pd(arg___w: f64, arg___x: f64) __m128d {
    var __w = arg___w;
    _ = &__w;
    var __x = arg___x;
    _ = &__x;
    return [2]f64{
        __x,
        __w,
    };
}
pub inline fn _mm_setr_pd(arg___w: f64, arg___x: f64) __m128d {
    var __w = arg___w;
    _ = &__w;
    var __x = arg___x;
    _ = &__x;
    return [2]f64{
        __w,
        __x,
    };
}
pub inline fn _mm_setzero_pd() __m128d {
    return [2]f64{
        0.0,
        0.0,
    };
}
pub inline fn _mm_move_sd(arg___a: __m128d, arg___b: __m128d) __m128d {
    var __a = arg___a;
    _ = &__a;
    var __b = arg___b;
    _ = &__b;
    __a[@as(c_int, 0)] = __b[@as(c_int, 0)];
    return __a;
}
pub inline fn _mm_store_sd(arg___dp: [*c]f64, arg___a: __m128d) void {
    var __dp = arg___dp;
    _ = &__dp;
    var __a = arg___a;
    _ = &__a;
    const struct___mm_store_sd_struct = extern struct {
        __u: f64 align(1) = 0,
    };
    _ = &struct___mm_store_sd_struct;
    @as([*c]struct___mm_store_sd_struct, @ptrCast(@alignCast(__dp))).*.__u = __a[@as(c_int, 0)];
}
pub inline fn _mm_store_pd(arg___dp: [*c]f64, arg___a: __m128d) void {
    var __dp = arg___dp;
    _ = &__dp;
    var __a = arg___a;
    _ = &__a;
    @as([*c]__m128d, @ptrCast(@alignCast(__dp))).* = __a;
}
pub inline fn _mm_store1_pd(arg___dp: [*c]f64, arg___a: __m128d) void {
    var __dp = arg___dp;
    _ = &__dp;
    var __a = arg___a;
    _ = &__a;
    __a = @shuffle(@typeInfo(@TypeOf(__a)).vector.child, __a, __a, @Vector(2, i32){
        __helpers.shuffleVectorIndex(0, @typeInfo(@TypeOf(__a)).vector.len),
        __helpers.shuffleVectorIndex(0, @typeInfo(@TypeOf(__a)).vector.len),
    });
    _mm_store_pd(__dp, __a);
}
pub inline fn _mm_store_pd1(arg___dp: [*c]f64, arg___a: __m128d) void {
    var __dp = arg___dp;
    _ = &__dp;
    var __a = arg___a;
    _ = &__a;
    _mm_store1_pd(__dp, __a);
}
pub inline fn _mm_storeu_pd(arg___dp: [*c]f64, arg___a: __m128d) void {
    var __dp = arg___dp;
    _ = &__dp;
    var __a = arg___a;
    _ = &__a;
    const struct___storeu_pd = extern struct {
        __v: __m128d_u = @import("std").mem.zeroes(__m128d_u),
    };
    _ = &struct___storeu_pd;
    @as([*c]struct___storeu_pd, @ptrCast(@alignCast(__dp))).*.__v = __a;
}
pub inline fn _mm_storer_pd(arg___dp: [*c]f64, arg___a: __m128d) void {
    var __dp = arg___dp;
    _ = &__dp;
    var __a = arg___a;
    _ = &__a;
    __a = @shuffle(@typeInfo(@TypeOf(__a)).vector.child, __a, __a, @Vector(2, i32){
        __helpers.shuffleVectorIndex(1, @typeInfo(@TypeOf(__a)).vector.len),
        __helpers.shuffleVectorIndex(0, @typeInfo(@TypeOf(__a)).vector.len),
    });
    @as([*c]__m128d, @ptrCast(@alignCast(__dp))).* = __a;
}
pub inline fn _mm_storeh_pd(arg___dp: [*c]f64, arg___a: __m128d) void {
    var __dp = arg___dp;
    _ = &__dp;
    var __a = arg___a;
    _ = &__a;
    const struct___mm_storeh_pd_struct = extern struct {
        __u: f64 align(1) = 0,
    };
    _ = &struct___mm_storeh_pd_struct;
    @as([*c]struct___mm_storeh_pd_struct, @ptrCast(@alignCast(__dp))).*.__u = __a[@as(c_int, 1)];
}
pub inline fn _mm_storel_pd(arg___dp: [*c]f64, arg___a: __m128d) void {
    var __dp = arg___dp;
    _ = &__dp;
    var __a = arg___a;
    _ = &__a;
    const struct___mm_storeh_pd_struct = extern struct {
        __u: f64 align(1) = 0,
    };
    _ = &struct___mm_storeh_pd_struct;
    @as([*c]struct___mm_storeh_pd_struct, @ptrCast(@alignCast(__dp))).*.__u = __a[@as(c_int, 0)];
}
pub inline fn _mm_add_epi8(arg___a: __m128i, arg___b: __m128i) __m128i {
    var __a = arg___a;
    _ = &__a;
    var __b = arg___b;
    _ = &__b;
    return @bitCast(@as(__v16qu, @bitCast(__a)) + @as(__v16qu, @bitCast(__b)));
}
pub inline fn _mm_add_epi16(arg___a: __m128i, arg___b: __m128i) __m128i {
    var __a = arg___a;
    _ = &__a;
    var __b = arg___b;
    _ = &__b;
    return @bitCast(@as(__v8hu, @bitCast(__a)) + @as(__v8hu, @bitCast(__b)));
}
pub inline fn _mm_add_epi32(arg___a: __m128i, arg___b: __m128i) __m128i {
    var __a = arg___a;
    _ = &__a;
    var __b = arg___b;
    _ = &__b;
    return @bitCast(@as(__v4su, @bitCast(__a)) + @as(__v4su, @bitCast(__b)));
}
pub inline fn _mm_add_si64(arg___a: __m64, arg___b: __m64) __m64 {
    var __a = arg___a;
    _ = &__a;
    var __b = arg___b;
    _ = &__b;
    return @bitCast(@as(__v1du, @bitCast(__a))[@as(c_int, 0)] +% @as(__v1du, @bitCast(__b))[@as(c_int, 0)]);
}
pub inline fn _mm_add_epi64(arg___a: __m128i, arg___b: __m128i) __m128i {
    var __a = arg___a;
    _ = &__a;
    var __b = arg___b;
    _ = &__b;
    return @bitCast(@as(__v2du, @bitCast(__a)) + @as(__v2du, @bitCast(__b)));
}
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:2166:19: warning: TODO implement function '__builtin_elementwise_add_sat' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:2165:1: warning: unable to translate function, demoted to extern
pub extern fn _mm_adds_epi8(arg___a: __m128i, arg___b: __m128i) __m128i;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:2188:19: warning: TODO implement function '__builtin_elementwise_add_sat' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:2187:1: warning: unable to translate function, demoted to extern
pub extern fn _mm_adds_epi16(arg___a: __m128i, arg___b: __m128i) __m128i;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:2210:19: warning: TODO implement function '__builtin_elementwise_add_sat' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:2209:1: warning: unable to translate function, demoted to extern
pub extern fn _mm_adds_epu8(arg___a: __m128i, arg___b: __m128i) __m128i;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:2232:19: warning: TODO implement function '__builtin_elementwise_add_sat' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:2231:1: warning: unable to translate function, demoted to extern
pub extern fn _mm_adds_epu16(arg___a: __m128i, arg___b: __m128i) __m128i;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:2251:19: warning: TODO implement function '__builtin_ia32_pavgb128' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:2250:1: warning: unable to translate function, demoted to extern
pub extern fn _mm_avg_epu8(arg___a: __m128i, arg___b: __m128i) __m128i;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:2270:19: warning: TODO implement function '__builtin_ia32_pavgw128' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:2269:1: warning: unable to translate function, demoted to extern
pub extern fn _mm_avg_epu16(arg___a: __m128i, arg___b: __m128i) __m128i;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:2295:19: warning: TODO implement function '__builtin_ia32_pmaddwd128' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:2294:1: warning: unable to translate function, demoted to extern
pub extern fn _mm_madd_epi16(arg___a: __m128i, arg___b: __m128i) __m128i;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:2314:19: warning: TODO implement function '__builtin_elementwise_max' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:2313:1: warning: unable to translate function, demoted to extern
pub extern fn _mm_max_epi16(arg___a: __m128i, arg___b: __m128i) __m128i;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:2333:19: warning: TODO implement function '__builtin_elementwise_max' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:2332:1: warning: unable to translate function, demoted to extern
pub extern fn _mm_max_epu8(arg___a: __m128i, arg___b: __m128i) __m128i;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:2352:19: warning: TODO implement function '__builtin_elementwise_min' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:2351:1: warning: unable to translate function, demoted to extern
pub extern fn _mm_min_epi16(arg___a: __m128i, arg___b: __m128i) __m128i;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:2371:19: warning: TODO implement function '__builtin_elementwise_min' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:2370:1: warning: unable to translate function, demoted to extern
pub extern fn _mm_min_epu8(arg___a: __m128i, arg___b: __m128i) __m128i;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:2390:19: warning: TODO implement function '__builtin_ia32_pmulhw128' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:2389:1: warning: unable to translate function, demoted to extern
pub extern fn _mm_mulhi_epi16(arg___a: __m128i, arg___b: __m128i) __m128i;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:2409:19: warning: TODO implement function '__builtin_ia32_pmulhuw128' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:2408:1: warning: unable to translate function, demoted to extern
pub extern fn _mm_mulhi_epu16(arg___a: __m128i, arg___b: __m128i) __m128i;
pub inline fn _mm_mullo_epi16(arg___a: __m128i, arg___b: __m128i) __m128i {
    var __a = arg___a;
    _ = &__a;
    var __b = arg___b;
    _ = &__b;
    return @bitCast(@as(__v8hu, @bitCast(__a)) * @as(__v8hu, @bitCast(__b)));
}
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:2446:20: warning: TODO implement function '__builtin_ia32_pmuludq128' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:2444:54: warning: unable to translate function, demoted to extern
pub extern fn _mm_mul_su32(arg___a: __m64, arg___b: __m64) __m64;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:2465:10: warning: TODO implement function '__builtin_ia32_pmuludq128' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:2464:1: warning: unable to translate function, demoted to extern
pub extern fn _mm_mul_epu32(arg___a: __m128i, arg___b: __m128i) __m128i;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:2486:10: warning: TODO implement function '__builtin_ia32_psadbw128' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:2484:46: warning: unable to translate function, demoted to extern
pub extern fn _mm_sad_epu8(arg___a: __m128i, arg___b: __m128i) __m128i;
pub inline fn _mm_sub_epi8(arg___a: __m128i, arg___b: __m128i) __m128i {
    var __a = arg___a;
    _ = &__a;
    var __b = arg___b;
    _ = &__b;
    return @bitCast(@as(__v16qu, @bitCast(__a)) - @as(__v16qu, @bitCast(__b)));
}
pub inline fn _mm_sub_epi16(arg___a: __m128i, arg___b: __m128i) __m128i {
    var __a = arg___a;
    _ = &__a;
    var __b = arg___b;
    _ = &__b;
    return @bitCast(@as(__v8hu, @bitCast(__a)) - @as(__v8hu, @bitCast(__b)));
}
pub inline fn _mm_sub_epi32(arg___a: __m128i, arg___b: __m128i) __m128i {
    var __a = arg___a;
    _ = &__a;
    var __b = arg___b;
    _ = &__b;
    return @bitCast(@as(__v4su, @bitCast(__a)) - @as(__v4su, @bitCast(__b)));
}
pub inline fn _mm_sub_si64(arg___a: __m64, arg___b: __m64) __m64 {
    var __a = arg___a;
    _ = &__a;
    var __b = arg___b;
    _ = &__b;
    return @bitCast(@as(__v1du, @bitCast(__a))[@as(c_int, 0)] -% @as(__v1du, @bitCast(__b))[@as(c_int, 0)]);
}
pub inline fn _mm_sub_epi64(arg___a: __m128i, arg___b: __m128i) __m128i {
    var __a = arg___a;
    _ = &__a;
    var __b = arg___b;
    _ = &__b;
    return @bitCast(@as(__v2du, @bitCast(__a)) - @as(__v2du, @bitCast(__b)));
}
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:2594:19: warning: TODO implement function '__builtin_elementwise_sub_sat' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:2593:1: warning: unable to translate function, demoted to extern
pub extern fn _mm_subs_epi8(arg___a: __m128i, arg___b: __m128i) __m128i;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:2616:19: warning: TODO implement function '__builtin_elementwise_sub_sat' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:2615:1: warning: unable to translate function, demoted to extern
pub extern fn _mm_subs_epi16(arg___a: __m128i, arg___b: __m128i) __m128i;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:2637:19: warning: TODO implement function '__builtin_elementwise_sub_sat' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:2636:1: warning: unable to translate function, demoted to extern
pub extern fn _mm_subs_epu8(arg___a: __m128i, arg___b: __m128i) __m128i;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:2658:19: warning: TODO implement function '__builtin_elementwise_sub_sat' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:2657:1: warning: unable to translate function, demoted to extern
pub extern fn _mm_subs_epu16(arg___a: __m128i, arg___b: __m128i) __m128i;
pub inline fn _mm_and_si128(arg___a: __m128i, arg___b: __m128i) __m128i {
    var __a = arg___a;
    _ = &__a;
    var __b = arg___b;
    _ = &__b;
    return @bitCast(@as(__v2du, @bitCast(__a)) & @as(__v2du, @bitCast(__b)));
}
pub inline fn _mm_andnot_si128(arg___a: __m128i, arg___b: __m128i) __m128i {
    var __a = arg___a;
    _ = &__a;
    var __b = arg___b;
    _ = &__b;
    return @bitCast(~@as(__v2du, @bitCast(__a)) & @as(__v2du, @bitCast(__b)));
}
pub inline fn _mm_or_si128(arg___a: __m128i, arg___b: __m128i) __m128i {
    var __a = arg___a;
    _ = &__a;
    var __b = arg___b;
    _ = &__b;
    return @bitCast(@as(__v2du, @bitCast(__a)) | @as(__v2du, @bitCast(__b)));
}
pub inline fn _mm_xor_si128(arg___a: __m128i, arg___b: __m128i) __m128i {
    var __a = arg___a;
    _ = &__a;
    var __b = arg___b;
    _ = &__b;
    return @bitCast(@as(__v2du, @bitCast(__a)) ^ @as(__v2du, @bitCast(__b)));
}
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:2770:19: warning: TODO implement function '__builtin_ia32_psllwi128' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:2769:1: warning: unable to translate function, demoted to extern
pub extern fn _mm_slli_epi16(arg___a: __m128i, arg___count: c_int) __m128i;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:2788:19: warning: TODO implement function '__builtin_ia32_psllw128' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:2787:1: warning: unable to translate function, demoted to extern
pub extern fn _mm_sll_epi16(arg___a: __m128i, arg___count: __m128i) __m128i;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:2806:19: warning: TODO implement function '__builtin_ia32_pslldi128' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:2805:1: warning: unable to translate function, demoted to extern
pub extern fn _mm_slli_epi32(arg___a: __m128i, arg___count: c_int) __m128i;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:2824:19: warning: TODO implement function '__builtin_ia32_pslld128' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:2823:1: warning: unable to translate function, demoted to extern
pub extern fn _mm_sll_epi32(arg___a: __m128i, arg___count: __m128i) __m128i;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:2842:10: warning: TODO implement function '__builtin_ia32_psllqi128' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:2841:1: warning: unable to translate function, demoted to extern
pub extern fn _mm_slli_epi64(arg___a: __m128i, arg___count: c_int) __m128i;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:2860:10: warning: TODO implement function '__builtin_ia32_psllq128' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:2859:1: warning: unable to translate function, demoted to extern
pub extern fn _mm_sll_epi64(arg___a: __m128i, arg___count: __m128i) __m128i;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:2879:19: warning: TODO implement function '__builtin_ia32_psrawi128' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:2878:1: warning: unable to translate function, demoted to extern
pub extern fn _mm_srai_epi16(arg___a: __m128i, arg___count: c_int) __m128i;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:2898:19: warning: TODO implement function '__builtin_ia32_psraw128' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:2897:1: warning: unable to translate function, demoted to extern
pub extern fn _mm_sra_epi16(arg___a: __m128i, arg___count: __m128i) __m128i;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:2917:19: warning: TODO implement function '__builtin_ia32_psradi128' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:2916:1: warning: unable to translate function, demoted to extern
pub extern fn _mm_srai_epi32(arg___a: __m128i, arg___count: c_int) __m128i;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:2936:19: warning: TODO implement function '__builtin_ia32_psrad128' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:2935:1: warning: unable to translate function, demoted to extern
pub extern fn _mm_sra_epi32(arg___a: __m128i, arg___count: __m128i) __m128i;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:2979:19: warning: TODO implement function '__builtin_ia32_psrlwi128' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:2978:1: warning: unable to translate function, demoted to extern
pub extern fn _mm_srli_epi16(arg___a: __m128i, arg___count: c_int) __m128i;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:2997:19: warning: TODO implement function '__builtin_ia32_psrlw128' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:2996:1: warning: unable to translate function, demoted to extern
pub extern fn _mm_srl_epi16(arg___a: __m128i, arg___count: __m128i) __m128i;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:3015:19: warning: TODO implement function '__builtin_ia32_psrldi128' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:3014:1: warning: unable to translate function, demoted to extern
pub extern fn _mm_srli_epi32(arg___a: __m128i, arg___count: c_int) __m128i;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:3033:19: warning: TODO implement function '__builtin_ia32_psrld128' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:3032:1: warning: unable to translate function, demoted to extern
pub extern fn _mm_srl_epi32(arg___a: __m128i, arg___count: __m128i) __m128i;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:3051:10: warning: TODO implement function '__builtin_ia32_psrlqi128' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:3050:1: warning: unable to translate function, demoted to extern
pub extern fn _mm_srli_epi64(arg___a: __m128i, arg___count: c_int) __m128i;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:3069:10: warning: TODO implement function '__builtin_ia32_psrlq128' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:3068:1: warning: unable to translate function, demoted to extern
pub extern fn _mm_srl_epi64(arg___a: __m128i, arg___count: __m128i) __m128i;
pub inline fn _mm_cmpeq_epi8(arg___a: __m128i, arg___b: __m128i) __m128i {
    var __a = arg___a;
    _ = &__a;
    var __b = arg___b;
    _ = &__b;
    return @bitCast(@as(__v16qi, @bitCast(__a)) == @as(__v16qi, @bitCast(__b)));
}
pub inline fn _mm_cmpeq_epi16(arg___a: __m128i, arg___b: __m128i) __m128i {
    var __a = arg___a;
    _ = &__a;
    var __b = arg___b;
    _ = &__b;
    return @bitCast(@as(__v8hi, @bitCast(__a)) == @as(__v8hi, @bitCast(__b)));
}
pub inline fn _mm_cmpeq_epi32(arg___a: __m128i, arg___b: __m128i) __m128i {
    var __a = arg___a;
    _ = &__a;
    var __b = arg___b;
    _ = &__b;
    return @bitCast(@as(__v4si, @bitCast(__a)) == @as(__v4si, @bitCast(__b)));
}
pub inline fn _mm_cmpgt_epi8(arg___a: __m128i, arg___b: __m128i) __m128i {
    var __a = arg___a;
    _ = &__a;
    var __b = arg___b;
    _ = &__b;
    return @bitCast(@as(__v16qs, @bitCast(__a)) > @as(__v16qs, @bitCast(__b)));
}
pub inline fn _mm_cmpgt_epi16(arg___a: __m128i, arg___b: __m128i) __m128i {
    var __a = arg___a;
    _ = &__a;
    var __b = arg___b;
    _ = &__b;
    return @bitCast(@as(__v8hi, @bitCast(__a)) > @as(__v8hi, @bitCast(__b)));
}
pub inline fn _mm_cmpgt_epi32(arg___a: __m128i, arg___b: __m128i) __m128i {
    var __a = arg___a;
    _ = &__a;
    var __b = arg___b;
    _ = &__b;
    return @bitCast(@as(__v4si, @bitCast(__a)) > @as(__v4si, @bitCast(__b)));
}
pub inline fn _mm_cmplt_epi8(arg___a: __m128i, arg___b: __m128i) __m128i {
    var __a = arg___a;
    _ = &__a;
    var __b = arg___b;
    _ = &__b;
    return _mm_cmpgt_epi8(__b, __a);
}
pub inline fn _mm_cmplt_epi16(arg___a: __m128i, arg___b: __m128i) __m128i {
    var __a = arg___a;
    _ = &__a;
    var __b = arg___b;
    _ = &__b;
    return _mm_cmpgt_epi16(__b, __a);
}
pub inline fn _mm_cmplt_epi32(arg___a: __m128i, arg___b: __m128i) __m128i {
    var __a = arg___a;
    _ = &__a;
    var __b = arg___b;
    _ = &__b;
    return _mm_cmpgt_epi32(__b, __a);
}
pub inline fn _mm_cvtsi64_sd(arg___a: __m128d, arg___b: c_longlong) __m128d {
    var __a = arg___a;
    _ = &__a;
    var __b = arg___b;
    _ = &__b;
    __a[@as(c_int, 0)] = @floatFromInt(__b);
    return __a;
}
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:3291:10: warning: TODO implement function '__builtin_ia32_cvtsd2si64' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:3290:48: warning: unable to translate function, demoted to extern
pub extern fn _mm_cvtsd_si64(arg___a: __m128d) c_longlong;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:3311:10: warning: TODO implement function '__builtin_ia32_cvttsd2si64' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:3310:48: warning: unable to translate function, demoted to extern
pub extern fn _mm_cvttsd_si64(arg___a: __m128d) c_longlong;
pub inline fn _mm_cvtepi32_ps(arg___a: __m128i) __m128 {
    var __a = arg___a;
    _ = &__a;
    return blk: {
        const tmp = @as(__v4si, @bitCast(__a));
        break :blk __v4sf{
            @floatFromInt(tmp[0]),
            @floatFromInt(tmp[1]),
            @floatFromInt(tmp[2]),
            @floatFromInt(tmp[3]),
        };
    };
}
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:3344:19: warning: TODO implement function '__builtin_ia32_cvtps2dq' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:3343:46: warning: unable to translate function, demoted to extern
pub extern fn _mm_cvtps_epi32(arg___a: __m128) __m128i;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:3363:19: warning: TODO implement function '__builtin_ia32_cvttps2dq' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:3362:46: warning: unable to translate function, demoted to extern
pub extern fn _mm_cvttps_epi32(arg___a: __m128) __m128i;
pub inline fn _mm_cvtsi32_si128(arg___a: c_int) __m128i {
    var __a = arg___a;
    _ = &__a;
    return @bitCast(@as(__v4si, [4]c_int{
        __a,
        0,
        0,
        0,
    }));
}
pub inline fn _mm_cvtsi64_si128(arg___a: c_longlong) __m128i {
    var __a = arg___a;
    _ = &__a;
    return @as(__v2di, [2]c_longlong{
        __a,
        0,
    });
}
pub inline fn _mm_cvtsi128_si32(arg___a: __m128i) c_int {
    var __a = arg___a;
    _ = &__a;
    var __b: __v4si = @bitCast(__a);
    _ = &__b;
    return __b[@as(c_int, 0)];
}
pub inline fn _mm_cvtsi128_si64(arg___a: __m128i) c_longlong {
    var __a = arg___a;
    _ = &__a;
    return __a[@as(c_int, 0)];
}
pub inline fn _mm_load_si128(arg___p: [*c]const __m128i) __m128i {
    var __p = arg___p;
    _ = &__p;
    return __p.*;
}
pub inline fn _mm_loadu_si128(arg___p: [*c]const __m128i_u) __m128i {
    var __p = arg___p;
    _ = &__p;
    const struct___loadu_si128 = extern struct {
        __v: __m128i_u = @import("std").mem.zeroes(__m128i_u),
    };
    _ = &struct___loadu_si128;
    return @as([*c]const struct___loadu_si128, @ptrCast(@alignCast(__p))).*.__v;
}
pub inline fn _mm_loadl_epi64(arg___p: [*c]const __m128i_u) __m128i {
    var __p = arg___p;
    _ = &__p;
    const struct___mm_loadl_epi64_struct = extern struct {
        __u: c_longlong align(1) = 0,
    };
    _ = &struct___mm_loadl_epi64_struct;
    return [2]c_longlong{
        @as([*c]const struct___mm_loadl_epi64_struct, @ptrCast(@alignCast(__p))).*.__u,
        0,
    };
}
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:3494:19: warning: TODO implement function '__builtin_ia32_undef128' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:3493:46: warning: unable to translate function, demoted to extern
pub extern fn _mm_undefined_si128() __m128i;
pub inline fn _mm_set_epi64x(arg___q1: c_longlong, arg___q0: c_longlong) __m128i {
    var __q1 = arg___q1;
    _ = &__q1;
    var __q0 = arg___q0;
    _ = &__q0;
    return @as(__v2di, [2]c_longlong{
        __q0,
        __q1,
    });
}
pub inline fn _mm_set_epi64(arg___q1: __m64, arg___q0: __m64) __m128i {
    var __q1 = arg___q1;
    _ = &__q1;
    var __q0 = arg___q0;
    _ = &__q0;
    return _mm_set_epi64x(__q1[@as(c_int, 0)], __q0[@as(c_int, 0)]);
}
pub inline fn _mm_set_epi32(arg___i3: c_int, arg___i2: c_int, arg___i1: c_int, arg___i0: c_int) __m128i {
    var __i3 = arg___i3;
    _ = &__i3;
    var __i2 = arg___i2;
    _ = &__i2;
    var __i1 = arg___i1;
    _ = &__i1;
    var __i0 = arg___i0;
    _ = &__i0;
    return @bitCast(@as(__v4si, [4]c_int{
        __i0,
        __i1,
        __i2,
        __i3,
    }));
}
pub inline fn _mm_set_epi16(arg___w7: c_short, arg___w6: c_short, arg___w5: c_short, arg___w4: c_short, arg___w3: c_short, arg___w2: c_short, arg___w1: c_short, arg___w0: c_short) __m128i {
    var __w7 = arg___w7;
    _ = &__w7;
    var __w6 = arg___w6;
    _ = &__w6;
    var __w5 = arg___w5;
    _ = &__w5;
    var __w4 = arg___w4;
    _ = &__w4;
    var __w3 = arg___w3;
    _ = &__w3;
    var __w2 = arg___w2;
    _ = &__w2;
    var __w1 = arg___w1;
    _ = &__w1;
    var __w0 = arg___w0;
    _ = &__w0;
    return @bitCast(@as(__v8hi, [8]c_short{
        __w0,
        __w1,
        __w2,
        __w3,
        __w4,
        __w5,
        __w6,
        __w7,
    }));
}
pub inline fn _mm_set_epi8(arg___b15: u8, arg___b14: u8, arg___b13: u8, arg___b12: u8, arg___b11: u8, arg___b10: u8, arg___b9: u8, arg___b8: u8, arg___b7: u8, arg___b6: u8, arg___b5: u8, arg___b4: u8, arg___b3: u8, arg___b2: u8, arg___b1: u8, arg___b0: u8) __m128i {
    var __b15 = arg___b15;
    _ = &__b15;
    var __b14 = arg___b14;
    _ = &__b14;
    var __b13 = arg___b13;
    _ = &__b13;
    var __b12 = arg___b12;
    _ = &__b12;
    var __b11 = arg___b11;
    _ = &__b11;
    var __b10 = arg___b10;
    _ = &__b10;
    var __b9 = arg___b9;
    _ = &__b9;
    var __b8 = arg___b8;
    _ = &__b8;
    var __b7 = arg___b7;
    _ = &__b7;
    var __b6 = arg___b6;
    _ = &__b6;
    var __b5 = arg___b5;
    _ = &__b5;
    var __b4 = arg___b4;
    _ = &__b4;
    var __b3 = arg___b3;
    _ = &__b3;
    var __b2 = arg___b2;
    _ = &__b2;
    var __b1 = arg___b1;
    _ = &__b1;
    var __b0 = arg___b0;
    _ = &__b0;
    return @bitCast(@as(__v16qi, [16]u8{
        __b0,
        __b1,
        __b2,
        __b3,
        __b4,
        __b5,
        __b6,
        __b7,
        __b8,
        __b9,
        __b10,
        __b11,
        __b12,
        __b13,
        __b14,
        __b15,
    }));
}
pub inline fn _mm_set1_epi64x(arg___q: c_longlong) __m128i {
    var __q = arg___q;
    _ = &__q;
    return _mm_set_epi64x(__q, __q);
}
pub inline fn _mm_set1_epi64(arg___q: __m64) __m128i {
    var __q = arg___q;
    _ = &__q;
    return _mm_set_epi64(__q, __q);
}
pub inline fn _mm_set1_epi32(arg___i: c_int) __m128i {
    var __i = arg___i;
    _ = &__i;
    return _mm_set_epi32(__i, __i, __i, __i);
}
pub inline fn _mm_set1_epi16(arg___w: c_short) __m128i {
    var __w = arg___w;
    _ = &__w;
    return _mm_set_epi16(__w, __w, __w, __w, __w, __w, __w, __w);
}
pub inline fn _mm_set1_epi8(arg___b: u8) __m128i {
    var __b = arg___b;
    _ = &__b;
    return _mm_set_epi8(__b, __b, __b, __b, __b, __b, __b, __b, __b, __b, __b, __b, __b, __b, __b, __b);
}
pub inline fn _mm_setr_epi64(arg___q0: __m64, arg___q1: __m64) __m128i {
    var __q0 = arg___q0;
    _ = &__q0;
    var __q1 = arg___q1;
    _ = &__q1;
    return _mm_set_epi64(__q1, __q0);
}
pub inline fn _mm_setr_epi32(arg___i0: c_int, arg___i1: c_int, arg___i2: c_int, arg___i3: c_int) __m128i {
    var __i0 = arg___i0;
    _ = &__i0;
    var __i1 = arg___i1;
    _ = &__i1;
    var __i2 = arg___i2;
    _ = &__i2;
    var __i3 = arg___i3;
    _ = &__i3;
    return _mm_set_epi32(__i3, __i2, __i1, __i0);
}
pub inline fn _mm_setr_epi16(arg___w0: c_short, arg___w1: c_short, arg___w2: c_short, arg___w3: c_short, arg___w4: c_short, arg___w5: c_short, arg___w6: c_short, arg___w7: c_short) __m128i {
    var __w0 = arg___w0;
    _ = &__w0;
    var __w1 = arg___w1;
    _ = &__w1;
    var __w2 = arg___w2;
    _ = &__w2;
    var __w3 = arg___w3;
    _ = &__w3;
    var __w4 = arg___w4;
    _ = &__w4;
    var __w5 = arg___w5;
    _ = &__w5;
    var __w6 = arg___w6;
    _ = &__w6;
    var __w7 = arg___w7;
    _ = &__w7;
    return _mm_set_epi16(__w7, __w6, __w5, __w4, __w3, __w2, __w1, __w0);
}
pub inline fn _mm_setr_epi8(arg___b0: u8, arg___b1: u8, arg___b2: u8, arg___b3: u8, arg___b4: u8, arg___b5: u8, arg___b6: u8, arg___b7: u8, arg___b8: u8, arg___b9: u8, arg___b10: u8, arg___b11: u8, arg___b12: u8, arg___b13: u8, arg___b14: u8, arg___b15: u8) __m128i {
    var __b0 = arg___b0;
    _ = &__b0;
    var __b1 = arg___b1;
    _ = &__b1;
    var __b2 = arg___b2;
    _ = &__b2;
    var __b3 = arg___b3;
    _ = &__b3;
    var __b4 = arg___b4;
    _ = &__b4;
    var __b5 = arg___b5;
    _ = &__b5;
    var __b6 = arg___b6;
    _ = &__b6;
    var __b7 = arg___b7;
    _ = &__b7;
    var __b8 = arg___b8;
    _ = &__b8;
    var __b9 = arg___b9;
    _ = &__b9;
    var __b10 = arg___b10;
    _ = &__b10;
    var __b11 = arg___b11;
    _ = &__b11;
    var __b12 = arg___b12;
    _ = &__b12;
    var __b13 = arg___b13;
    _ = &__b13;
    var __b14 = arg___b14;
    _ = &__b14;
    var __b15 = arg___b15;
    _ = &__b15;
    return _mm_set_epi8(__b15, __b14, __b13, __b12, __b11, __b10, __b9, __b8, __b7, __b6, __b5, __b4, __b3, __b2, __b1, __b0);
}
pub inline fn _mm_setzero_si128() __m128i {
    return @as(__v2di, [2]c_longlong{
        0,
        0,
    });
}
pub inline fn _mm_store_si128(arg___p: [*c]__m128i, arg___b: __m128i) void {
    var __p = arg___p;
    _ = &__p;
    var __b = arg___b;
    _ = &__b;
    __p.* = __b;
}
pub inline fn _mm_storeu_si128(arg___p: [*c]__m128i_u, arg___b: __m128i) void {
    var __p = arg___p;
    _ = &__p;
    var __b = arg___b;
    _ = &__b;
    const struct___storeu_si128 = extern struct {
        __v: __m128i_u = @import("std").mem.zeroes(__m128i_u),
    };
    _ = &struct___storeu_si128;
    @as([*c]struct___storeu_si128, @ptrCast(@alignCast(__p))).*.__v = __b;
}
pub inline fn _mm_storeu_si64(arg___p: ?*anyopaque, arg___b: __m128i) void {
    var __p = arg___p;
    _ = &__p;
    var __b = arg___b;
    _ = &__b;
    const struct___storeu_si64 = extern struct {
        __v: c_longlong align(1) = 0,
    };
    _ = &struct___storeu_si64;
    @as([*c]struct___storeu_si64, @ptrCast(@alignCast(__p))).*.__v = __b[@as(c_int, 0)];
}
pub inline fn _mm_storeu_si32(arg___p: ?*anyopaque, arg___b: __m128i) void {
    var __p = arg___p;
    _ = &__p;
    var __b = arg___b;
    _ = &__b;
    const struct___storeu_si32 = extern struct {
        __v: c_int align(1) = 0,
    };
    _ = &struct___storeu_si32;
    @as([*c]struct___storeu_si32, @ptrCast(@alignCast(__p))).*.__v = @as(__v4si, @bitCast(__b))[@as(c_int, 0)];
}
pub inline fn _mm_storeu_si16(arg___p: ?*anyopaque, arg___b: __m128i) void {
    var __p = arg___p;
    _ = &__p;
    var __b = arg___b;
    _ = &__b;
    const struct___storeu_si16 = extern struct {
        __v: c_short align(1) = 0,
    };
    _ = &struct___storeu_si16;
    @as([*c]struct___storeu_si16, @ptrCast(@alignCast(__p))).*.__v = @as(__v8hi, @bitCast(__b))[@as(c_int, 0)];
}
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:4001:3: warning: TODO implement function '__builtin_ia32_maskmovdqu' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:3998:43: warning: unable to translate function, demoted to extern
pub extern fn _mm_maskmoveu_si128(arg___d: __m128i, arg___n: __m128i, arg___p: [*c]u8) void;
pub inline fn _mm_storel_epi64(arg___p: [*c]__m128i_u, arg___a: __m128i) void {
    var __p = arg___p;
    _ = &__p;
    var __a = arg___a;
    _ = &__a;
    const struct___mm_storel_epi64_struct = extern struct {
        __u: c_longlong align(1) = 0,
    };
    _ = &struct___mm_storel_epi64_struct;
    @as([*c]struct___mm_storel_epi64_struct, @ptrCast(@alignCast(__p))).*.__u = __a[@as(c_int, 0)];
}
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:4041:3: warning: TODO implement function '__builtin_nontemporal_store' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:4039:43: warning: unable to translate function, demoted to extern
pub extern fn _mm_stream_pd(arg___p: ?*anyopaque, arg___a: __m128d) void;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:4059:3: warning: TODO implement function '__builtin_nontemporal_store' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:4057:43: warning: unable to translate function, demoted to extern
pub extern fn _mm_stream_si128(arg___p: ?*anyopaque, arg___a: __m128i) void;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:4078:3: warning: TODO implement function '__builtin_ia32_movnti' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:4077:5: warning: unable to translate function, demoted to extern
pub extern fn _mm_stream_si32(arg___p: ?*anyopaque, arg___a: c_int) void;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:4098:3: warning: TODO implement function '__builtin_ia32_movnti64' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:4097:5: warning: unable to translate function, demoted to extern
pub extern fn _mm_stream_si64(arg___p: ?*anyopaque, arg___a: c_longlong) void;
pub extern fn _mm_clflush(__p: ?*const anyopaque) void;
pub extern fn _mm_lfence() void;
pub extern fn _mm_mfence() void;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:4164:19: warning: TODO implement function '__builtin_ia32_packsswb128' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:4163:1: warning: unable to translate function, demoted to extern
pub extern fn _mm_packs_epi16(arg___a: __m128i, arg___b: __m128i) __m128i;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:4187:19: warning: TODO implement function '__builtin_ia32_packssdw128' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:4186:1: warning: unable to translate function, demoted to extern
pub extern fn _mm_packs_epi32(arg___a: __m128i, arg___b: __m128i) __m128i;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:4210:19: warning: TODO implement function '__builtin_ia32_packuswb128' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:4209:1: warning: unable to translate function, demoted to extern
pub extern fn _mm_packus_epi16(arg___a: __m128i, arg___b: __m128i) __m128i;
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:4285:10: warning: TODO implement function '__builtin_ia32_pmovmskb128' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:4284:1: warning: unable to translate function, demoted to extern
pub extern fn _mm_movemask_epi8(arg___a: __m128i) c_int;
pub inline fn _mm_unpackhi_epi8(arg___a: __m128i, arg___b: __m128i) __m128i {
    var __a = arg___a;
    _ = &__a;
    var __b = arg___b;
    _ = &__b;
    return @bitCast(@shuffle(@typeInfo(@TypeOf(@as(__v16qi, @bitCast(__a)))).vector.child, @as(__v16qi, @bitCast(__a)), @as(__v16qi, @bitCast(__b)), @Vector(16, i32){
        __helpers.shuffleVectorIndex(8, @typeInfo(@TypeOf(@as(__v16qi, @bitCast(__a)))).vector.len),
        __helpers.shuffleVectorIndex(@as(c_int, 16) + @as(c_int, 8), @typeInfo(@TypeOf(@as(__v16qi, @bitCast(__a)))).vector.len),
        __helpers.shuffleVectorIndex(9, @typeInfo(@TypeOf(@as(__v16qi, @bitCast(__a)))).vector.len),
        __helpers.shuffleVectorIndex(@as(c_int, 16) + @as(c_int, 9), @typeInfo(@TypeOf(@as(__v16qi, @bitCast(__a)))).vector.len),
        __helpers.shuffleVectorIndex(10, @typeInfo(@TypeOf(@as(__v16qi, @bitCast(__a)))).vector.len),
        __helpers.shuffleVectorIndex(@as(c_int, 16) + @as(c_int, 10), @typeInfo(@TypeOf(@as(__v16qi, @bitCast(__a)))).vector.len),
        __helpers.shuffleVectorIndex(11, @typeInfo(@TypeOf(@as(__v16qi, @bitCast(__a)))).vector.len),
        __helpers.shuffleVectorIndex(@as(c_int, 16) + @as(c_int, 11), @typeInfo(@TypeOf(@as(__v16qi, @bitCast(__a)))).vector.len),
        __helpers.shuffleVectorIndex(12, @typeInfo(@TypeOf(@as(__v16qi, @bitCast(__a)))).vector.len),
        __helpers.shuffleVectorIndex(@as(c_int, 16) + @as(c_int, 12), @typeInfo(@TypeOf(@as(__v16qi, @bitCast(__a)))).vector.len),
        __helpers.shuffleVectorIndex(13, @typeInfo(@TypeOf(@as(__v16qi, @bitCast(__a)))).vector.len),
        __helpers.shuffleVectorIndex(@as(c_int, 16) + @as(c_int, 13), @typeInfo(@TypeOf(@as(__v16qi, @bitCast(__a)))).vector.len),
        __helpers.shuffleVectorIndex(14, @typeInfo(@TypeOf(@as(__v16qi, @bitCast(__a)))).vector.len),
        __helpers.shuffleVectorIndex(@as(c_int, 16) + @as(c_int, 14), @typeInfo(@TypeOf(@as(__v16qi, @bitCast(__a)))).vector.len),
        __helpers.shuffleVectorIndex(15, @typeInfo(@TypeOf(@as(__v16qi, @bitCast(__a)))).vector.len),
        __helpers.shuffleVectorIndex(@as(c_int, 16) + @as(c_int, 15), @typeInfo(@TypeOf(@as(__v16qi, @bitCast(__a)))).vector.len),
    }));
}
pub inline fn _mm_unpackhi_epi16(arg___a: __m128i, arg___b: __m128i) __m128i {
    var __a = arg___a;
    _ = &__a;
    var __b = arg___b;
    _ = &__b;
    return @bitCast(@shuffle(@typeInfo(@TypeOf(@as(__v8hi, @bitCast(__a)))).vector.child, @as(__v8hi, @bitCast(__a)), @as(__v8hi, @bitCast(__b)), @Vector(8, i32){
        __helpers.shuffleVectorIndex(4, @typeInfo(@TypeOf(@as(__v8hi, @bitCast(__a)))).vector.len),
        __helpers.shuffleVectorIndex(@as(c_int, 8) + @as(c_int, 4), @typeInfo(@TypeOf(@as(__v8hi, @bitCast(__a)))).vector.len),
        __helpers.shuffleVectorIndex(5, @typeInfo(@TypeOf(@as(__v8hi, @bitCast(__a)))).vector.len),
        __helpers.shuffleVectorIndex(@as(c_int, 8) + @as(c_int, 5), @typeInfo(@TypeOf(@as(__v8hi, @bitCast(__a)))).vector.len),
        __helpers.shuffleVectorIndex(6, @typeInfo(@TypeOf(@as(__v8hi, @bitCast(__a)))).vector.len),
        __helpers.shuffleVectorIndex(@as(c_int, 8) + @as(c_int, 6), @typeInfo(@TypeOf(@as(__v8hi, @bitCast(__a)))).vector.len),
        __helpers.shuffleVectorIndex(7, @typeInfo(@TypeOf(@as(__v8hi, @bitCast(__a)))).vector.len),
        __helpers.shuffleVectorIndex(@as(c_int, 8) + @as(c_int, 7), @typeInfo(@TypeOf(@as(__v8hi, @bitCast(__a)))).vector.len),
    }));
}
pub inline fn _mm_unpackhi_epi32(arg___a: __m128i, arg___b: __m128i) __m128i {
    var __a = arg___a;
    _ = &__a;
    var __b = arg___b;
    _ = &__b;
    return @bitCast(@shuffle(@typeInfo(@TypeOf(@as(__v4si, @bitCast(__a)))).vector.child, @as(__v4si, @bitCast(__a)), @as(__v4si, @bitCast(__b)), @Vector(4, i32){
        __helpers.shuffleVectorIndex(2, @typeInfo(@TypeOf(@as(__v4si, @bitCast(__a)))).vector.len),
        __helpers.shuffleVectorIndex(@as(c_int, 4) + @as(c_int, 2), @typeInfo(@TypeOf(@as(__v4si, @bitCast(__a)))).vector.len),
        __helpers.shuffleVectorIndex(3, @typeInfo(@TypeOf(@as(__v4si, @bitCast(__a)))).vector.len),
        __helpers.shuffleVectorIndex(@as(c_int, 4) + @as(c_int, 3), @typeInfo(@TypeOf(@as(__v4si, @bitCast(__a)))).vector.len),
    }));
}
pub inline fn _mm_unpackhi_epi64(arg___a: __m128i, arg___b: __m128i) __m128i {
    var __a = arg___a;
    _ = &__a;
    var __b = arg___b;
    _ = &__b;
    return @shuffle(@typeInfo(@TypeOf(__a)).vector.child, __a, __b, @Vector(2, i32){
        __helpers.shuffleVectorIndex(1, @typeInfo(@TypeOf(__a)).vector.len),
        __helpers.shuffleVectorIndex(@as(c_int, 2) + @as(c_int, 1), @typeInfo(@TypeOf(__a)).vector.len),
    });
}
pub inline fn _mm_unpacklo_epi8(arg___a: __m128i, arg___b: __m128i) __m128i {
    var __a = arg___a;
    _ = &__a;
    var __b = arg___b;
    _ = &__b;
    return @bitCast(@shuffle(@typeInfo(@TypeOf(@as(__v16qi, @bitCast(__a)))).vector.child, @as(__v16qi, @bitCast(__a)), @as(__v16qi, @bitCast(__b)), @Vector(16, i32){
        __helpers.shuffleVectorIndex(0, @typeInfo(@TypeOf(@as(__v16qi, @bitCast(__a)))).vector.len),
        __helpers.shuffleVectorIndex(@as(c_int, 16) + @as(c_int, 0), @typeInfo(@TypeOf(@as(__v16qi, @bitCast(__a)))).vector.len),
        __helpers.shuffleVectorIndex(1, @typeInfo(@TypeOf(@as(__v16qi, @bitCast(__a)))).vector.len),
        __helpers.shuffleVectorIndex(@as(c_int, 16) + @as(c_int, 1), @typeInfo(@TypeOf(@as(__v16qi, @bitCast(__a)))).vector.len),
        __helpers.shuffleVectorIndex(2, @typeInfo(@TypeOf(@as(__v16qi, @bitCast(__a)))).vector.len),
        __helpers.shuffleVectorIndex(@as(c_int, 16) + @as(c_int, 2), @typeInfo(@TypeOf(@as(__v16qi, @bitCast(__a)))).vector.len),
        __helpers.shuffleVectorIndex(3, @typeInfo(@TypeOf(@as(__v16qi, @bitCast(__a)))).vector.len),
        __helpers.shuffleVectorIndex(@as(c_int, 16) + @as(c_int, 3), @typeInfo(@TypeOf(@as(__v16qi, @bitCast(__a)))).vector.len),
        __helpers.shuffleVectorIndex(4, @typeInfo(@TypeOf(@as(__v16qi, @bitCast(__a)))).vector.len),
        __helpers.shuffleVectorIndex(@as(c_int, 16) + @as(c_int, 4), @typeInfo(@TypeOf(@as(__v16qi, @bitCast(__a)))).vector.len),
        __helpers.shuffleVectorIndex(5, @typeInfo(@TypeOf(@as(__v16qi, @bitCast(__a)))).vector.len),
        __helpers.shuffleVectorIndex(@as(c_int, 16) + @as(c_int, 5), @typeInfo(@TypeOf(@as(__v16qi, @bitCast(__a)))).vector.len),
        __helpers.shuffleVectorIndex(6, @typeInfo(@TypeOf(@as(__v16qi, @bitCast(__a)))).vector.len),
        __helpers.shuffleVectorIndex(@as(c_int, 16) + @as(c_int, 6), @typeInfo(@TypeOf(@as(__v16qi, @bitCast(__a)))).vector.len),
        __helpers.shuffleVectorIndex(7, @typeInfo(@TypeOf(@as(__v16qi, @bitCast(__a)))).vector.len),
        __helpers.shuffleVectorIndex(@as(c_int, 16) + @as(c_int, 7), @typeInfo(@TypeOf(@as(__v16qi, @bitCast(__a)))).vector.len),
    }));
}
pub inline fn _mm_unpacklo_epi16(arg___a: __m128i, arg___b: __m128i) __m128i {
    var __a = arg___a;
    _ = &__a;
    var __b = arg___b;
    _ = &__b;
    return @bitCast(@shuffle(@typeInfo(@TypeOf(@as(__v8hi, @bitCast(__a)))).vector.child, @as(__v8hi, @bitCast(__a)), @as(__v8hi, @bitCast(__b)), @Vector(8, i32){
        __helpers.shuffleVectorIndex(0, @typeInfo(@TypeOf(@as(__v8hi, @bitCast(__a)))).vector.len),
        __helpers.shuffleVectorIndex(@as(c_int, 8) + @as(c_int, 0), @typeInfo(@TypeOf(@as(__v8hi, @bitCast(__a)))).vector.len),
        __helpers.shuffleVectorIndex(1, @typeInfo(@TypeOf(@as(__v8hi, @bitCast(__a)))).vector.len),
        __helpers.shuffleVectorIndex(@as(c_int, 8) + @as(c_int, 1), @typeInfo(@TypeOf(@as(__v8hi, @bitCast(__a)))).vector.len),
        __helpers.shuffleVectorIndex(2, @typeInfo(@TypeOf(@as(__v8hi, @bitCast(__a)))).vector.len),
        __helpers.shuffleVectorIndex(@as(c_int, 8) + @as(c_int, 2), @typeInfo(@TypeOf(@as(__v8hi, @bitCast(__a)))).vector.len),
        __helpers.shuffleVectorIndex(3, @typeInfo(@TypeOf(@as(__v8hi, @bitCast(__a)))).vector.len),
        __helpers.shuffleVectorIndex(@as(c_int, 8) + @as(c_int, 3), @typeInfo(@TypeOf(@as(__v8hi, @bitCast(__a)))).vector.len),
    }));
}
pub inline fn _mm_unpacklo_epi32(arg___a: __m128i, arg___b: __m128i) __m128i {
    var __a = arg___a;
    _ = &__a;
    var __b = arg___b;
    _ = &__b;
    return @bitCast(@shuffle(@typeInfo(@TypeOf(@as(__v4si, @bitCast(__a)))).vector.child, @as(__v4si, @bitCast(__a)), @as(__v4si, @bitCast(__b)), @Vector(4, i32){
        __helpers.shuffleVectorIndex(0, @typeInfo(@TypeOf(@as(__v4si, @bitCast(__a)))).vector.len),
        __helpers.shuffleVectorIndex(@as(c_int, 4) + @as(c_int, 0), @typeInfo(@TypeOf(@as(__v4si, @bitCast(__a)))).vector.len),
        __helpers.shuffleVectorIndex(1, @typeInfo(@TypeOf(@as(__v4si, @bitCast(__a)))).vector.len),
        __helpers.shuffleVectorIndex(@as(c_int, 4) + @as(c_int, 1), @typeInfo(@TypeOf(@as(__v4si, @bitCast(__a)))).vector.len),
    }));
}
pub inline fn _mm_unpacklo_epi64(arg___a: __m128i, arg___b: __m128i) __m128i {
    var __a = arg___a;
    _ = &__a;
    var __b = arg___b;
    _ = &__b;
    return @shuffle(@typeInfo(@TypeOf(__a)).vector.child, __a, __b, @Vector(2, i32){
        __helpers.shuffleVectorIndex(0, @typeInfo(@TypeOf(__a)).vector.len),
        __helpers.shuffleVectorIndex(@as(c_int, 2) + @as(c_int, 0), @typeInfo(@TypeOf(__a)).vector.len),
    });
}
pub inline fn _mm_movepi64_pi64(arg___a: __m128i) __m64 {
    var __a = arg___a;
    _ = &__a;
    return @bitCast(__a[@as(c_int, 0)]);
}
pub inline fn _mm_movpi64_epi64(arg___a: __m64) __m128i {
    var __a = arg___a;
    _ = &__a;
    return @shuffle(@typeInfo(@TypeOf(__a)).vector.child, __a, _mm_setzero_si64(), @Vector(2, i32){
        __helpers.shuffleVectorIndex(0, @typeInfo(@TypeOf(__a)).vector.len),
        __helpers.shuffleVectorIndex(1, @typeInfo(@TypeOf(__a)).vector.len),
    });
}
pub inline fn _mm_move_epi64(arg___a: __m128i) __m128i {
    var __a = arg___a;
    _ = &__a;
    return @shuffle(@typeInfo(@TypeOf(__a)).vector.child, __a, _mm_setzero_si128(), @Vector(2, i32){
        __helpers.shuffleVectorIndex(0, @typeInfo(@TypeOf(__a)).vector.len),
        __helpers.shuffleVectorIndex(2, @typeInfo(@TypeOf(__a)).vector.len),
    });
}
pub inline fn _mm_unpackhi_pd(arg___a: __m128d, arg___b: __m128d) __m128d {
    var __a = arg___a;
    _ = &__a;
    var __b = arg___b;
    _ = &__b;
    return @shuffle(@typeInfo(@TypeOf(__a)).vector.child, __a, __b, @Vector(2, i32){
        __helpers.shuffleVectorIndex(1, @typeInfo(@TypeOf(__a)).vector.len),
        __helpers.shuffleVectorIndex(@as(c_int, 2) + @as(c_int, 1), @typeInfo(@TypeOf(__a)).vector.len),
    });
}
pub inline fn _mm_unpacklo_pd(arg___a: __m128d, arg___b: __m128d) __m128d {
    var __a = arg___a;
    _ = &__a;
    var __b = arg___b;
    _ = &__b;
    return @shuffle(@typeInfo(@TypeOf(__a)).vector.child, __a, __b, @Vector(2, i32){
        __helpers.shuffleVectorIndex(0, @typeInfo(@TypeOf(__a)).vector.len),
        __helpers.shuffleVectorIndex(@as(c_int, 2) + @as(c_int, 0), @typeInfo(@TypeOf(__a)).vector.len),
    });
}
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:4705:10: warning: TODO implement function '__builtin_ia32_movmskpd' in std.zig.c_builtins

// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:4704:1: warning: unable to translate function, demoted to extern
pub extern fn _mm_movemask_pd(arg___a: __m128d) c_int;
pub inline fn _mm_castpd_ps(arg___a: __m128d) __m128 {
    var __a = arg___a;
    _ = &__a;
    return @bitCast(__a);
}
pub inline fn _mm_castpd_si128(arg___a: __m128d) __m128i {
    var __a = arg___a;
    _ = &__a;
    return @bitCast(__a);
}
pub inline fn _mm_castps_pd(arg___a: __m128) __m128d {
    var __a = arg___a;
    _ = &__a;
    return @bitCast(__a);
}
pub inline fn _mm_castps_si128(arg___a: __m128) __m128i {
    var __a = arg___a;
    _ = &__a;
    return @bitCast(__a);
}
pub inline fn _mm_castsi128_ps(arg___a: __m128i) __m128 {
    var __a = arg___a;
    _ = &__a;
    return @bitCast(__a);
}
pub inline fn _mm_castsi128_pd(arg___a: __m128i) __m128d {
    var __a = arg___a;
    _ = &__a;
    return @bitCast(__a);
}
pub extern fn _mm_pause() void;
pub var CLAY__ELEMENT_DEFINITION_LATCH: u8 = 0;
pub fn Clay__SuppressUnusedLatchDefinitionVariableWarning() callconv(.c) void {
    _ = &CLAY__ELEMENT_DEFINITION_LATCH;
}
pub const struct_Clay_String = extern struct {
    isStaticallyAllocated: bool = false,
    length: i32 = 0,
    chars: [*c]const u8 = null,
    pub const Clay_GetElementId = __root.Clay_GetElementId;
    pub const Clay_GetElementIdWithIndex = __root.Clay_GetElementIdWithIndex;
    pub const Clay__HashString = __root.Clay__HashString;
    pub const Clay__OpenTextElement = __root.Clay__OpenTextElement;
    pub const GetElementId = __root.Clay_GetElementId;
    pub const GetElementIdWithIndex = __root.Clay_GetElementIdWithIndex;
    pub const HashString = __root.Clay__HashString;
    pub const OpenTextElement = __root.Clay__OpenTextElement;
};
pub const Clay_String = struct_Clay_String;
pub const struct_Clay_StringSlice = extern struct {
    length: i32 = 0,
    chars: [*c]const u8 = null,
    baseChars: [*c]const u8 = null,
};
pub const Clay_StringSlice = struct_Clay_StringSlice;
pub const struct_Clay_Context = opaque {
    pub const Clay_SetCurrentContext = __root.Clay_SetCurrentContext;
    pub const SetCurrentContext = __root.Clay_SetCurrentContext;
};
pub const Clay_Context = struct_Clay_Context;
pub const struct_Clay_Arena = extern struct {
    nextAllocation: usize = 0,
    capacity: usize = 0,
    memory: [*c]u8 = null,
    pub const Clay_Initialize = __root.Clay_Initialize;
    pub const Initialize = __root.Clay_Initialize;
};
pub const Clay_Arena = struct_Clay_Arena;
pub const struct_Clay_Dimensions = extern struct {
    width: f32 = 0,
    height: f32 = 0,
    pub const Clay_SetLayoutDimensions = __root.Clay_SetLayoutDimensions;
    pub const SetLayoutDimensions = __root.Clay_SetLayoutDimensions;
};
pub const Clay_Dimensions = struct_Clay_Dimensions;
pub const struct_Clay_Vector2 = extern struct {
    x: f32 = 0,
    y: f32 = 0,
    pub const Clay_SetPointerState = __root.Clay_SetPointerState;
    pub const SetPointerState = __root.Clay_SetPointerState;
};
pub const Clay_Vector2 = struct_Clay_Vector2;
pub const struct_Clay_Color = extern struct {
    r: f32 = 0,
    g: f32 = 0,
    b: f32 = 0,
    a: f32 = 0,
};
pub const Clay_Color = struct_Clay_Color;
pub const struct_Clay_BoundingBox = extern struct {
    x: f32 = 0,
    y: f32 = 0,
    width: f32 = 0,
    height: f32 = 0,
};
pub const Clay_BoundingBox = struct_Clay_BoundingBox;
pub const struct_Clay_ElementId = extern struct {
    id: u32 = 0,
    offset: u32 = 0,
    baseId: u32 = 0,
    stringId: Clay_String = @import("std").mem.zeroes(Clay_String),
    pub const Clay_GetElementData = __root.Clay_GetElementData;
    pub const Clay_PointerOver = __root.Clay_PointerOver;
    pub const Clay_GetScrollContainerData = __root.Clay_GetScrollContainerData;
    pub const GetElementData = __root.Clay_GetElementData;
    pub const PointerOver = __root.Clay_PointerOver;
    pub const GetScrollContainerData = __root.Clay_GetScrollContainerData;
};
pub const Clay_ElementId = struct_Clay_ElementId;
pub const Clay_ElementIdArray = extern struct {
    capacity: i32 = 0,
    length: i32 = 0,
    internalArray: [*c]Clay_ElementId = null,
};
pub const struct_Clay_CornerRadius = extern struct {
    topLeft: f32 = 0,
    topRight: f32 = 0,
    bottomLeft: f32 = 0,
    bottomRight: f32 = 0,
};
pub const Clay_CornerRadius = struct_Clay_CornerRadius;
pub const CLAY_LEFT_TO_RIGHT: c_int = 0;
pub const CLAY_TOP_TO_BOTTOM: c_int = 1;
pub const Clay_LayoutDirection = u8;
pub const CLAY_ALIGN_X_LEFT: c_int = 0;
pub const CLAY_ALIGN_X_RIGHT: c_int = 1;
pub const CLAY_ALIGN_X_CENTER: c_int = 2;
pub const Clay_LayoutAlignmentX = u8;
pub const CLAY_ALIGN_Y_TOP: c_int = 0;
pub const CLAY_ALIGN_Y_BOTTOM: c_int = 1;
pub const CLAY_ALIGN_Y_CENTER: c_int = 2;
pub const Clay_LayoutAlignmentY = u8;
pub const CLAY__SIZING_TYPE_FIT: c_int = 0;
pub const CLAY__SIZING_TYPE_GROW: c_int = 1;
pub const CLAY__SIZING_TYPE_PERCENT: c_int = 2;
pub const CLAY__SIZING_TYPE_FIXED: c_int = 3;
pub const Clay__SizingType = u8;
pub const struct_Clay_ChildAlignment = extern struct {
    x: Clay_LayoutAlignmentX = @import("std").mem.zeroes(Clay_LayoutAlignmentX),
    y: Clay_LayoutAlignmentY = @import("std").mem.zeroes(Clay_LayoutAlignmentY),
};
pub const Clay_ChildAlignment = struct_Clay_ChildAlignment;
pub const struct_Clay_SizingMinMax = extern struct {
    min: f32 = 0,
    max: f32 = 0,
};
pub const Clay_SizingMinMax = struct_Clay_SizingMinMax;
const union_unnamed_1 = extern union {
    minMax: Clay_SizingMinMax,
    percent: f32,
};
pub const struct_Clay_SizingAxis = extern struct {
    size: union_unnamed_1 = @import("std").mem.zeroes(union_unnamed_1),
    type: Clay__SizingType = @import("std").mem.zeroes(Clay__SizingType),
};
pub const Clay_SizingAxis = struct_Clay_SizingAxis;
pub const struct_Clay_Sizing = extern struct {
    width: Clay_SizingAxis = @import("std").mem.zeroes(Clay_SizingAxis),
    height: Clay_SizingAxis = @import("std").mem.zeroes(Clay_SizingAxis),
};
pub const Clay_Sizing = struct_Clay_Sizing;
pub const struct_Clay_Padding = extern struct {
    left: u16 = 0,
    right: u16 = 0,
    top: u16 = 0,
    bottom: u16 = 0,
};
pub const Clay_Padding = struct_Clay_Padding;
pub const Clay__Clay_PaddingWrapper = extern struct {
    wrapped: Clay_Padding = @import("std").mem.zeroes(Clay_Padding),
};
pub const struct_Clay_LayoutConfig = extern struct {
    sizing: Clay_Sizing = @import("std").mem.zeroes(Clay_Sizing),
    padding: Clay_Padding = @import("std").mem.zeroes(Clay_Padding),
    childGap: u16 = 0,
    childAlignment: Clay_ChildAlignment = @import("std").mem.zeroes(Clay_ChildAlignment),
    layoutDirection: Clay_LayoutDirection = @import("std").mem.zeroes(Clay_LayoutDirection),
};
pub const Clay_LayoutConfig = struct_Clay_LayoutConfig;
pub const Clay__Clay_LayoutConfigWrapper = extern struct {
    wrapped: Clay_LayoutConfig = @import("std").mem.zeroes(Clay_LayoutConfig),
};
pub extern var CLAY_LAYOUT_DEFAULT: Clay_LayoutConfig;
pub const CLAY_TEXT_WRAP_WORDS: c_int = 0;
pub const CLAY_TEXT_WRAP_NEWLINES: c_int = 1;
pub const CLAY_TEXT_WRAP_NONE: c_int = 2;
pub const Clay_TextElementConfigWrapMode = u8;
pub const CLAY_TEXT_ALIGN_LEFT: c_int = 0;
pub const CLAY_TEXT_ALIGN_CENTER: c_int = 1;
pub const CLAY_TEXT_ALIGN_RIGHT: c_int = 2;
pub const Clay_TextAlignment = u8;
pub const struct_Clay_TextElementConfig = extern struct {
    userData: ?*anyopaque = null,
    textColor: Clay_Color = @import("std").mem.zeroes(Clay_Color),
    fontId: u16 = 0,
    fontSize: u16 = 0,
    letterSpacing: u16 = 0,
    lineHeight: u16 = 0,
    wrapMode: Clay_TextElementConfigWrapMode = @import("std").mem.zeroes(Clay_TextElementConfigWrapMode),
    textAlignment: Clay_TextAlignment = @import("std").mem.zeroes(Clay_TextAlignment),
    pub const Clay__StoreTextElementConfig = __root.Clay__StoreTextElementConfig;
    pub const StoreTextElementConfig = __root.Clay__StoreTextElementConfig;
};
pub const Clay_TextElementConfig = struct_Clay_TextElementConfig;
pub const Clay__Clay_TextElementConfigWrapper = extern struct {
    wrapped: Clay_TextElementConfig = @import("std").mem.zeroes(Clay_TextElementConfig),
};
pub const struct_Clay_AspectRatioElementConfig = extern struct {
    aspectRatio: f32 = 0,
};
pub const Clay_AspectRatioElementConfig = struct_Clay_AspectRatioElementConfig;
pub const Clay__Clay_AspectRatioElementConfigWrapper = extern struct {
    wrapped: Clay_AspectRatioElementConfig = @import("std").mem.zeroes(Clay_AspectRatioElementConfig),
};
pub const struct_Clay_ImageElementConfig = extern struct {
    imageData: ?*anyopaque = null,
};
pub const Clay_ImageElementConfig = struct_Clay_ImageElementConfig;
pub const Clay__Clay_ImageElementConfigWrapper = extern struct {
    wrapped: Clay_ImageElementConfig = @import("std").mem.zeroes(Clay_ImageElementConfig),
};
pub const CLAY_ATTACH_POINT_LEFT_TOP: c_int = 0;
pub const CLAY_ATTACH_POINT_LEFT_CENTER: c_int = 1;
pub const CLAY_ATTACH_POINT_LEFT_BOTTOM: c_int = 2;
pub const CLAY_ATTACH_POINT_CENTER_TOP: c_int = 3;
pub const CLAY_ATTACH_POINT_CENTER_CENTER: c_int = 4;
pub const CLAY_ATTACH_POINT_CENTER_BOTTOM: c_int = 5;
pub const CLAY_ATTACH_POINT_RIGHT_TOP: c_int = 6;
pub const CLAY_ATTACH_POINT_RIGHT_CENTER: c_int = 7;
pub const CLAY_ATTACH_POINT_RIGHT_BOTTOM: c_int = 8;
pub const Clay_FloatingAttachPointType = u8;
pub const struct_Clay_FloatingAttachPoints = extern struct {
    element: Clay_FloatingAttachPointType = @import("std").mem.zeroes(Clay_FloatingAttachPointType),
    parent: Clay_FloatingAttachPointType = @import("std").mem.zeroes(Clay_FloatingAttachPointType),
};
pub const Clay_FloatingAttachPoints = struct_Clay_FloatingAttachPoints;
pub const CLAY_POINTER_CAPTURE_MODE_CAPTURE: c_int = 0;
pub const CLAY_POINTER_CAPTURE_MODE_PASSTHROUGH: c_int = 1;
pub const Clay_PointerCaptureMode = u8;
pub const CLAY_ATTACH_TO_NONE: c_int = 0;
pub const CLAY_ATTACH_TO_PARENT: c_int = 1;
pub const CLAY_ATTACH_TO_ELEMENT_WITH_ID: c_int = 2;
pub const CLAY_ATTACH_TO_ROOT: c_int = 3;
pub const Clay_FloatingAttachToElement = u8;
pub const CLAY_CLIP_TO_NONE: c_int = 0;
pub const CLAY_CLIP_TO_ATTACHED_PARENT: c_int = 1;
pub const Clay_FloatingClipToElement = u8;
pub const struct_Clay_FloatingElementConfig = extern struct {
    offset: Clay_Vector2 = @import("std").mem.zeroes(Clay_Vector2),
    expand: Clay_Dimensions = @import("std").mem.zeroes(Clay_Dimensions),
    parentId: u32 = 0,
    zIndex: i16 = 0,
    attachPoints: Clay_FloatingAttachPoints = @import("std").mem.zeroes(Clay_FloatingAttachPoints),
    pointerCaptureMode: Clay_PointerCaptureMode = @import("std").mem.zeroes(Clay_PointerCaptureMode),
    attachTo: Clay_FloatingAttachToElement = @import("std").mem.zeroes(Clay_FloatingAttachToElement),
    clipTo: Clay_FloatingClipToElement = @import("std").mem.zeroes(Clay_FloatingClipToElement),
};
pub const Clay_FloatingElementConfig = struct_Clay_FloatingElementConfig;
pub const Clay__Clay_FloatingElementConfigWrapper = extern struct {
    wrapped: Clay_FloatingElementConfig = @import("std").mem.zeroes(Clay_FloatingElementConfig),
};
pub const struct_Clay_CustomElementConfig = extern struct {
    customData: ?*anyopaque = null,
};
pub const Clay_CustomElementConfig = struct_Clay_CustomElementConfig;
pub const Clay__Clay_CustomElementConfigWrapper = extern struct {
    wrapped: Clay_CustomElementConfig = @import("std").mem.zeroes(Clay_CustomElementConfig),
};
pub const struct_Clay_ClipElementConfig = extern struct {
    horizontal: bool = false,
    vertical: bool = false,
    childOffset: Clay_Vector2 = @import("std").mem.zeroes(Clay_Vector2),
};
pub const Clay_ClipElementConfig = struct_Clay_ClipElementConfig;
pub const Clay__Clay_ClipElementConfigWrapper = extern struct {
    wrapped: Clay_ClipElementConfig = @import("std").mem.zeroes(Clay_ClipElementConfig),
};
pub const struct_Clay_BorderWidth = extern struct {
    left: u16 = 0,
    right: u16 = 0,
    top: u16 = 0,
    bottom: u16 = 0,
    betweenChildren: u16 = 0,
};
pub const Clay_BorderWidth = struct_Clay_BorderWidth;
pub const struct_Clay_BorderElementConfig = extern struct {
    color: Clay_Color = @import("std").mem.zeroes(Clay_Color),
    width: Clay_BorderWidth = @import("std").mem.zeroes(Clay_BorderWidth),
};
pub const Clay_BorderElementConfig = struct_Clay_BorderElementConfig;
pub const Clay__Clay_BorderElementConfigWrapper = extern struct {
    wrapped: Clay_BorderElementConfig = @import("std").mem.zeroes(Clay_BorderElementConfig),
};
pub const struct_Clay_TextRenderData = extern struct {
    stringContents: Clay_StringSlice = @import("std").mem.zeroes(Clay_StringSlice),
    textColor: Clay_Color = @import("std").mem.zeroes(Clay_Color),
    fontId: u16 = 0,
    fontSize: u16 = 0,
    letterSpacing: u16 = 0,
    lineHeight: u16 = 0,
};
pub const Clay_TextRenderData = struct_Clay_TextRenderData;
pub const struct_Clay_RectangleRenderData = extern struct {
    backgroundColor: Clay_Color = @import("std").mem.zeroes(Clay_Color),
    cornerRadius: Clay_CornerRadius = @import("std").mem.zeroes(Clay_CornerRadius),
};
pub const Clay_RectangleRenderData = struct_Clay_RectangleRenderData;
pub const struct_Clay_ImageRenderData = extern struct {
    backgroundColor: Clay_Color = @import("std").mem.zeroes(Clay_Color),
    cornerRadius: Clay_CornerRadius = @import("std").mem.zeroes(Clay_CornerRadius),
    imageData: ?*anyopaque = null,
};
pub const Clay_ImageRenderData = struct_Clay_ImageRenderData;
pub const struct_Clay_CustomRenderData = extern struct {
    backgroundColor: Clay_Color = @import("std").mem.zeroes(Clay_Color),
    cornerRadius: Clay_CornerRadius = @import("std").mem.zeroes(Clay_CornerRadius),
    customData: ?*anyopaque = null,
};
pub const Clay_CustomRenderData = struct_Clay_CustomRenderData;
pub const struct_Clay_ScrollRenderData = extern struct {
    horizontal: bool = false,
    vertical: bool = false,
};
pub const Clay_ClipRenderData = struct_Clay_ScrollRenderData;
pub const struct_Clay_BorderRenderData = extern struct {
    color: Clay_Color = @import("std").mem.zeroes(Clay_Color),
    cornerRadius: Clay_CornerRadius = @import("std").mem.zeroes(Clay_CornerRadius),
    width: Clay_BorderWidth = @import("std").mem.zeroes(Clay_BorderWidth),
};
pub const Clay_BorderRenderData = struct_Clay_BorderRenderData;
pub const union_Clay_RenderData = extern union {
    rectangle: Clay_RectangleRenderData,
    text: Clay_TextRenderData,
    image: Clay_ImageRenderData,
    custom: Clay_CustomRenderData,
    border: Clay_BorderRenderData,
    clip: Clay_ClipRenderData,
};
pub const Clay_RenderData = union_Clay_RenderData;
pub const struct_Clay_ScrollContainerData = extern struct {
    scrollPosition: [*c]Clay_Vector2 = null,
    scrollContainerDimensions: Clay_Dimensions = @import("std").mem.zeroes(Clay_Dimensions),
    contentDimensions: Clay_Dimensions = @import("std").mem.zeroes(Clay_Dimensions),
    config: Clay_ClipElementConfig = @import("std").mem.zeroes(Clay_ClipElementConfig),
    found: bool = false,
};
pub const Clay_ScrollContainerData = struct_Clay_ScrollContainerData;
pub const struct_Clay_ElementData = extern struct {
    boundingBox: Clay_BoundingBox = @import("std").mem.zeroes(Clay_BoundingBox),
    found: bool = false,
};
pub const Clay_ElementData = struct_Clay_ElementData;
pub const CLAY_RENDER_COMMAND_TYPE_NONE: c_int = 0;
pub const CLAY_RENDER_COMMAND_TYPE_RECTANGLE: c_int = 1;
pub const CLAY_RENDER_COMMAND_TYPE_BORDER: c_int = 2;
pub const CLAY_RENDER_COMMAND_TYPE_TEXT: c_int = 3;
pub const CLAY_RENDER_COMMAND_TYPE_IMAGE: c_int = 4;
pub const CLAY_RENDER_COMMAND_TYPE_SCISSOR_START: c_int = 5;
pub const CLAY_RENDER_COMMAND_TYPE_SCISSOR_END: c_int = 6;
pub const CLAY_RENDER_COMMAND_TYPE_CUSTOM: c_int = 7;
pub const Clay_RenderCommandType = u8;
pub const struct_Clay_RenderCommand = extern struct {
    boundingBox: Clay_BoundingBox = @import("std").mem.zeroes(Clay_BoundingBox),
    renderData: Clay_RenderData = @import("std").mem.zeroes(Clay_RenderData),
    userData: ?*anyopaque = null,
    id: u32 = 0,
    zIndex: i16 = 0,
    commandType: Clay_RenderCommandType = @import("std").mem.zeroes(Clay_RenderCommandType),
};
pub const Clay_RenderCommand = struct_Clay_RenderCommand;
pub const struct_Clay_RenderCommandArray = extern struct {
    capacity: i32 = 0,
    length: i32 = 0,
    internalArray: [*c]Clay_RenderCommand = null,
    pub const Clay_RenderCommandArray_Get = __root.Clay_RenderCommandArray_Get;
    pub const Get = __root.Clay_RenderCommandArray_Get;
};
pub const Clay_RenderCommandArray = struct_Clay_RenderCommandArray;
pub const CLAY_POINTER_DATA_PRESSED_THIS_FRAME: c_int = 0;
pub const CLAY_POINTER_DATA_PRESSED: c_int = 1;
pub const CLAY_POINTER_DATA_RELEASED_THIS_FRAME: c_int = 2;
pub const CLAY_POINTER_DATA_RELEASED: c_int = 3;
pub const Clay_PointerDataInteractionState = u8;
pub const struct_Clay_PointerData = extern struct {
    position: Clay_Vector2 = @import("std").mem.zeroes(Clay_Vector2),
    state: Clay_PointerDataInteractionState = @import("std").mem.zeroes(Clay_PointerDataInteractionState),
};
pub const Clay_PointerData = struct_Clay_PointerData;
pub const struct_Clay_ElementDeclaration = extern struct {
    id: Clay_ElementId = @import("std").mem.zeroes(Clay_ElementId),
    layout: Clay_LayoutConfig = @import("std").mem.zeroes(Clay_LayoutConfig),
    backgroundColor: Clay_Color = @import("std").mem.zeroes(Clay_Color),
    cornerRadius: Clay_CornerRadius = @import("std").mem.zeroes(Clay_CornerRadius),
    aspectRatio: Clay_AspectRatioElementConfig = @import("std").mem.zeroes(Clay_AspectRatioElementConfig),
    image: Clay_ImageElementConfig = @import("std").mem.zeroes(Clay_ImageElementConfig),
    floating: Clay_FloatingElementConfig = @import("std").mem.zeroes(Clay_FloatingElementConfig),
    custom: Clay_CustomElementConfig = @import("std").mem.zeroes(Clay_CustomElementConfig),
    clip: Clay_ClipElementConfig = @import("std").mem.zeroes(Clay_ClipElementConfig),
    border: Clay_BorderElementConfig = @import("std").mem.zeroes(Clay_BorderElementConfig),
    userData: ?*anyopaque = null,
    pub const Clay__ConfigureOpenElement = __root.Clay__ConfigureOpenElement;
    pub const Clay__ConfigureOpenElementPtr = __root.Clay__ConfigureOpenElementPtr;
    pub const ConfigureOpenElement = __root.Clay__ConfigureOpenElement;
    pub const ConfigureOpenElementPtr = __root.Clay__ConfigureOpenElementPtr;
};
pub const Clay_ElementDeclaration = struct_Clay_ElementDeclaration;
pub const Clay__Clay_ElementDeclarationWrapper = extern struct {
    wrapped: Clay_ElementDeclaration = @import("std").mem.zeroes(Clay_ElementDeclaration),
};
pub const CLAY_ERROR_TYPE_TEXT_MEASUREMENT_FUNCTION_NOT_PROVIDED: c_int = 0;
pub const CLAY_ERROR_TYPE_ARENA_CAPACITY_EXCEEDED: c_int = 1;
pub const CLAY_ERROR_TYPE_ELEMENTS_CAPACITY_EXCEEDED: c_int = 2;
pub const CLAY_ERROR_TYPE_TEXT_MEASUREMENT_CAPACITY_EXCEEDED: c_int = 3;
pub const CLAY_ERROR_TYPE_DUPLICATE_ID: c_int = 4;
pub const CLAY_ERROR_TYPE_FLOATING_CONTAINER_PARENT_NOT_FOUND: c_int = 5;
pub const CLAY_ERROR_TYPE_PERCENTAGE_OVER_1: c_int = 6;
pub const CLAY_ERROR_TYPE_INTERNAL_ERROR: c_int = 7;
pub const Clay_ErrorType = u8;
pub const struct_Clay_ErrorData = extern struct {
    errorType: Clay_ErrorType = @import("std").mem.zeroes(Clay_ErrorType),
    errorText: Clay_String = @import("std").mem.zeroes(Clay_String),
    userData: ?*anyopaque = null,
};
pub const Clay_ErrorData = struct_Clay_ErrorData;
pub const Clay_ErrorHandler = extern struct {
    errorHandlerFunction: ?*const fn (errorText: Clay_ErrorData) callconv(.c) void = null,
    userData: ?*anyopaque = null,
};
pub extern fn Clay_MinMemorySize() u32;
pub extern fn Clay_CreateArenaWithCapacityAndMemory(capacity: usize, memory: ?*anyopaque) Clay_Arena;
pub extern fn Clay_SetPointerState(position: Clay_Vector2, pointerDown: bool) void;
pub extern fn Clay_Initialize(arena: Clay_Arena, layoutDimensions: Clay_Dimensions, errorHandler: Clay_ErrorHandler) ?*Clay_Context;
pub extern fn Clay_GetCurrentContext() ?*Clay_Context;
pub extern fn Clay_SetCurrentContext(context: ?*Clay_Context) void;
pub extern fn Clay_UpdateScrollContainers(enableDragScrolling: bool, scrollDelta: Clay_Vector2, deltaTime: f32) void;
pub extern fn Clay_GetScrollOffset() Clay_Vector2;
pub extern fn Clay_SetLayoutDimensions(dimensions: Clay_Dimensions) void;
pub extern fn Clay_BeginLayout() void;
pub extern fn Clay_EndLayout() Clay_RenderCommandArray;
pub extern fn Clay_GetElementId(idString: Clay_String) Clay_ElementId;
pub extern fn Clay_GetElementIdWithIndex(idString: Clay_String, index: u32) Clay_ElementId;
pub extern fn Clay_GetElementData(id: Clay_ElementId) Clay_ElementData;
pub extern fn Clay_Hovered() bool;
pub extern fn Clay_OnHover(onHoverFunction: ?*const fn (elementId: Clay_ElementId, pointerData: Clay_PointerData, userData: isize) callconv(.c) void, userData: isize) void;
pub extern fn Clay_PointerOver(elementId: Clay_ElementId) bool;
pub extern fn Clay_GetPointerOverIds() Clay_ElementIdArray;
pub extern fn Clay_GetScrollContainerData(id: Clay_ElementId) Clay_ScrollContainerData;
pub extern fn Clay_SetMeasureTextFunction(measureTextFunction: ?*const fn (text: Clay_StringSlice, config: [*c]Clay_TextElementConfig, userData: ?*anyopaque) callconv(.c) Clay_Dimensions, userData: ?*anyopaque) void;
pub extern fn Clay_SetQueryScrollOffsetFunction(queryScrollOffsetFunction: ?*const fn (elementId: u32, userData: ?*anyopaque) callconv(.c) Clay_Vector2, userData: ?*anyopaque) void;
pub extern fn Clay_RenderCommandArray_Get(array: [*c]Clay_RenderCommandArray, index: i32) [*c]Clay_RenderCommand;
pub extern fn Clay_SetDebugModeEnabled(enabled: bool) void;
pub extern fn Clay_IsDebugModeEnabled() bool;
pub extern fn Clay_SetCullingEnabled(enabled: bool) void;
pub extern fn Clay_GetMaxElementCount() i32;
pub extern fn Clay_SetMaxElementCount(maxElementCount: i32) void;
pub extern fn Clay_GetMaxMeasureTextCacheWordCount() i32;
pub extern fn Clay_SetMaxMeasureTextCacheWordCount(maxMeasureTextCacheWordCount: i32) void;
pub extern fn Clay_ResetMeasureTextCache() void;
pub extern fn Clay__OpenElement() void;
pub extern fn Clay__ConfigureOpenElement(config: Clay_ElementDeclaration) void;
pub extern fn Clay__ConfigureOpenElementPtr(config: [*c]const Clay_ElementDeclaration) void;
pub extern fn Clay__CloseElement() void;
pub extern fn Clay__HashString(key: Clay_String, offset: u32, seed: u32) Clay_ElementId;
pub extern fn Clay__OpenTextElement(text: Clay_String, textConfig: [*c]Clay_TextElementConfig) void;
pub extern fn Clay__StoreTextElementConfig(config: Clay_TextElementConfig) [*c]Clay_TextElementConfig;
pub extern fn Clay__GetParentElementId() u32;
pub extern var Clay__debugViewHighlightColor: Clay_Color;
pub extern var Clay__debugViewWidth: u32;
pub const stbtt__buf = extern struct {
    data: [*c]u8 = null,
    cursor: c_int = 0,
    size: c_int = 0,
};
pub const stbtt_bakedchar = extern struct {
    x0: c_ushort = 0,
    y0: c_ushort = 0,
    x1: c_ushort = 0,
    y1: c_ushort = 0,
    xoff: f32 = 0,
    yoff: f32 = 0,
    xadvance: f32 = 0,
    pub const stbtt_GetBakedQuad = __root.stbtt_GetBakedQuad;
    pub const GetBakedQuad = __root.stbtt_GetBakedQuad;
};
pub extern fn stbtt_BakeFontBitmap(data: [*c]const u8, offset: c_int, pixel_height: f32, pixels: [*c]u8, pw: c_int, ph: c_int, first_char: c_int, num_chars: c_int, chardata: [*c]stbtt_bakedchar) c_int;
pub const stbtt_aligned_quad = extern struct {
    x0: f32 = 0,
    y0: f32 = 0,
    s0: f32 = 0,
    t0: f32 = 0,
    x1: f32 = 0,
    y1: f32 = 0,
    s1: f32 = 0,
    t1: f32 = 0,
};
pub extern fn stbtt_GetBakedQuad(chardata: [*c]const stbtt_bakedchar, pw: c_int, ph: c_int, char_index: c_int, xpos: [*c]f32, ypos: [*c]f32, q: [*c]stbtt_aligned_quad, opengl_fillrule: c_int) void;
pub extern fn stbtt_GetScaledFontVMetrics(fontdata: [*c]const u8, index: c_int, size: f32, ascent: [*c]f32, descent: [*c]f32, lineGap: [*c]f32) void;
pub const stbtt_packedchar = extern struct {
    x0: c_ushort = 0,
    y0: c_ushort = 0,
    x1: c_ushort = 0,
    y1: c_ushort = 0,
    xoff: f32 = 0,
    yoff: f32 = 0,
    xadvance: f32 = 0,
    xoff2: f32 = 0,
    yoff2: f32 = 0,
    pub const stbtt_GetPackedQuad = __root.stbtt_GetPackedQuad;
    pub const GetPackedQuad = __root.stbtt_GetPackedQuad;
};
pub const struct_stbtt_pack_context = extern struct {
    user_allocator_context: ?*anyopaque = null,
    pack_info: ?*anyopaque = null,
    width: c_int = 0,
    height: c_int = 0,
    stride_in_bytes: c_int = 0,
    padding: c_int = 0,
    skip_missing: c_int = 0,
    h_oversample: c_uint = 0,
    v_oversample: c_uint = 0,
    pixels: [*c]u8 = null,
    nodes: ?*anyopaque = null,
    pub const stbtt_PackBegin = __root.stbtt_PackBegin;
    pub const stbtt_PackEnd = __root.stbtt_PackEnd;
    pub const stbtt_PackFontRange = __root.stbtt_PackFontRange;
    pub const stbtt_PackFontRanges = __root.stbtt_PackFontRanges;
    pub const stbtt_PackSetOversampling = __root.stbtt_PackSetOversampling;
    pub const stbtt_PackSetSkipMissingCodepoints = __root.stbtt_PackSetSkipMissingCodepoints;
    pub const stbtt_PackFontRangesGatherRects = __root.stbtt_PackFontRangesGatherRects;
    pub const stbtt_PackFontRangesPackRects = __root.stbtt_PackFontRangesPackRects;
    pub const stbtt_PackFontRangesRenderIntoRects = __root.stbtt_PackFontRangesRenderIntoRects;
    pub const PackBegin = __root.stbtt_PackBegin;
    pub const PackEnd = __root.stbtt_PackEnd;
    pub const PackFontRange = __root.stbtt_PackFontRange;
    pub const PackFontRanges = __root.stbtt_PackFontRanges;
    pub const PackSetOversampling = __root.stbtt_PackSetOversampling;
    pub const PackSetSkipMissingCodepoints = __root.stbtt_PackSetSkipMissingCodepoints;
    pub const PackFontRangesGatherRects = __root.stbtt_PackFontRangesGatherRects;
    pub const PackFontRangesPackRects = __root.stbtt_PackFontRangesPackRects;
    pub const PackFontRangesRenderIntoRects = __root.stbtt_PackFontRangesRenderIntoRects;
};
pub const stbtt_pack_context = struct_stbtt_pack_context;
pub const struct_stbtt_fontinfo = extern struct {
    userdata: ?*anyopaque = null,
    data: [*c]u8 = null,
    fontstart: c_int = 0,
    numGlyphs: c_int = 0,
    loca: c_int = 0,
    head: c_int = 0,
    glyf: c_int = 0,
    hhea: c_int = 0,
    hmtx: c_int = 0,
    kern: c_int = 0,
    gpos: c_int = 0,
    svg: c_int = 0,
    index_map: c_int = 0,
    indexToLocFormat: c_int = 0,
    cff: stbtt__buf = @import("std").mem.zeroes(stbtt__buf),
    charstrings: stbtt__buf = @import("std").mem.zeroes(stbtt__buf),
    gsubrs: stbtt__buf = @import("std").mem.zeroes(stbtt__buf),
    subrs: stbtt__buf = @import("std").mem.zeroes(stbtt__buf),
    fontdicts: stbtt__buf = @import("std").mem.zeroes(stbtt__buf),
    fdselect: stbtt__buf = @import("std").mem.zeroes(stbtt__buf),
    pub const stbtt_InitFont = __root.stbtt_InitFont;
    pub const stbtt_FindGlyphIndex = __root.stbtt_FindGlyphIndex;
    pub const stbtt_ScaleForPixelHeight = __root.stbtt_ScaleForPixelHeight;
    pub const stbtt_ScaleForMappingEmToPixels = __root.stbtt_ScaleForMappingEmToPixels;
    pub const stbtt_GetFontVMetrics = __root.stbtt_GetFontVMetrics;
    pub const stbtt_GetFontVMetricsOS2 = __root.stbtt_GetFontVMetricsOS2;
    pub const stbtt_GetFontBoundingBox = __root.stbtt_GetFontBoundingBox;
    pub const stbtt_GetCodepointHMetrics = __root.stbtt_GetCodepointHMetrics;
    pub const stbtt_GetCodepointKernAdvance = __root.stbtt_GetCodepointKernAdvance;
    pub const stbtt_GetCodepointBox = __root.stbtt_GetCodepointBox;
    pub const stbtt_GetGlyphHMetrics = __root.stbtt_GetGlyphHMetrics;
    pub const stbtt_GetGlyphKernAdvance = __root.stbtt_GetGlyphKernAdvance;
    pub const stbtt_GetGlyphBox = __root.stbtt_GetGlyphBox;
    pub const stbtt_GetKerningTableLength = __root.stbtt_GetKerningTableLength;
    pub const stbtt_GetKerningTable = __root.stbtt_GetKerningTable;
    pub const stbtt_IsGlyphEmpty = __root.stbtt_IsGlyphEmpty;
    pub const stbtt_GetCodepointShape = __root.stbtt_GetCodepointShape;
    pub const stbtt_GetGlyphShape = __root.stbtt_GetGlyphShape;
    pub const stbtt_FreeShape = __root.stbtt_FreeShape;
    pub const stbtt_FindSVGDoc = __root.stbtt_FindSVGDoc;
    pub const stbtt_GetCodepointSVG = __root.stbtt_GetCodepointSVG;
    pub const stbtt_GetGlyphSVG = __root.stbtt_GetGlyphSVG;
    pub const stbtt_GetCodepointBitmap = __root.stbtt_GetCodepointBitmap;
    pub const stbtt_GetCodepointBitmapSubpixel = __root.stbtt_GetCodepointBitmapSubpixel;
    pub const stbtt_MakeCodepointBitmap = __root.stbtt_MakeCodepointBitmap;
    pub const stbtt_MakeCodepointBitmapSubpixel = __root.stbtt_MakeCodepointBitmapSubpixel;
    pub const stbtt_MakeCodepointBitmapSubpixelPrefilter = __root.stbtt_MakeCodepointBitmapSubpixelPrefilter;
    pub const stbtt_GetCodepointBitmapBox = __root.stbtt_GetCodepointBitmapBox;
    pub const stbtt_GetCodepointBitmapBoxSubpixel = __root.stbtt_GetCodepointBitmapBoxSubpixel;
    pub const stbtt_GetGlyphBitmap = __root.stbtt_GetGlyphBitmap;
    pub const stbtt_GetGlyphBitmapSubpixel = __root.stbtt_GetGlyphBitmapSubpixel;
    pub const stbtt_MakeGlyphBitmap = __root.stbtt_MakeGlyphBitmap;
    pub const stbtt_MakeGlyphBitmapSubpixel = __root.stbtt_MakeGlyphBitmapSubpixel;
    pub const stbtt_MakeGlyphBitmapSubpixelPrefilter = __root.stbtt_MakeGlyphBitmapSubpixelPrefilter;
    pub const stbtt_GetGlyphBitmapBox = __root.stbtt_GetGlyphBitmapBox;
    pub const stbtt_GetGlyphBitmapBoxSubpixel = __root.stbtt_GetGlyphBitmapBoxSubpixel;
    pub const stbtt_GetGlyphSDF = __root.stbtt_GetGlyphSDF;
    pub const stbtt_GetCodepointSDF = __root.stbtt_GetCodepointSDF;
    pub const stbtt_GetFontNameString = __root.stbtt_GetFontNameString;
    pub const InitFont = __root.stbtt_InitFont;
    pub const FindGlyphIndex = __root.stbtt_FindGlyphIndex;
    pub const ScaleForPixelHeight = __root.stbtt_ScaleForPixelHeight;
    pub const ScaleForMappingEmToPixels = __root.stbtt_ScaleForMappingEmToPixels;
    pub const GetFontVMetrics = __root.stbtt_GetFontVMetrics;
    pub const GetFontVMetricsOS2 = __root.stbtt_GetFontVMetricsOS2;
    pub const GetFontBoundingBox = __root.stbtt_GetFontBoundingBox;
    pub const GetCodepointHMetrics = __root.stbtt_GetCodepointHMetrics;
    pub const GetCodepointKernAdvance = __root.stbtt_GetCodepointKernAdvance;
    pub const GetCodepointBox = __root.stbtt_GetCodepointBox;
    pub const GetGlyphHMetrics = __root.stbtt_GetGlyphHMetrics;
    pub const GetGlyphKernAdvance = __root.stbtt_GetGlyphKernAdvance;
    pub const GetGlyphBox = __root.stbtt_GetGlyphBox;
    pub const GetKerningTableLength = __root.stbtt_GetKerningTableLength;
    pub const GetKerningTable = __root.stbtt_GetKerningTable;
    pub const IsGlyphEmpty = __root.stbtt_IsGlyphEmpty;
    pub const GetCodepointShape = __root.stbtt_GetCodepointShape;
    pub const GetGlyphShape = __root.stbtt_GetGlyphShape;
    pub const FreeShape = __root.stbtt_FreeShape;
    pub const FindSVGDoc = __root.stbtt_FindSVGDoc;
    pub const GetCodepointSVG = __root.stbtt_GetCodepointSVG;
    pub const GetGlyphSVG = __root.stbtt_GetGlyphSVG;
    pub const GetCodepointBitmap = __root.stbtt_GetCodepointBitmap;
    pub const GetCodepointBitmapSubpixel = __root.stbtt_GetCodepointBitmapSubpixel;
    pub const MakeCodepointBitmap = __root.stbtt_MakeCodepointBitmap;
    pub const MakeCodepointBitmapSubpixel = __root.stbtt_MakeCodepointBitmapSubpixel;
    pub const MakeCodepointBitmapSubpixelPrefilter = __root.stbtt_MakeCodepointBitmapSubpixelPrefilter;
    pub const GetCodepointBitmapBox = __root.stbtt_GetCodepointBitmapBox;
    pub const GetCodepointBitmapBoxSubpixel = __root.stbtt_GetCodepointBitmapBoxSubpixel;
    pub const GetGlyphBitmap = __root.stbtt_GetGlyphBitmap;
    pub const GetGlyphBitmapSubpixel = __root.stbtt_GetGlyphBitmapSubpixel;
    pub const MakeGlyphBitmap = __root.stbtt_MakeGlyphBitmap;
    pub const MakeGlyphBitmapSubpixel = __root.stbtt_MakeGlyphBitmapSubpixel;
    pub const MakeGlyphBitmapSubpixelPrefilter = __root.stbtt_MakeGlyphBitmapSubpixelPrefilter;
    pub const GetGlyphBitmapBox = __root.stbtt_GetGlyphBitmapBox;
    pub const GetGlyphBitmapBoxSubpixel = __root.stbtt_GetGlyphBitmapBoxSubpixel;
    pub const GetGlyphSDF = __root.stbtt_GetGlyphSDF;
    pub const GetCodepointSDF = __root.stbtt_GetCodepointSDF;
    pub const GetFontNameString = __root.stbtt_GetFontNameString;
};
pub const stbtt_fontinfo = struct_stbtt_fontinfo;
pub const struct_stbrp_rect = opaque {};
pub const stbrp_rect = struct_stbrp_rect;
pub extern fn stbtt_PackBegin(spc: [*c]stbtt_pack_context, pixels: [*c]u8, width: c_int, height: c_int, stride_in_bytes: c_int, padding: c_int, alloc_context: ?*anyopaque) c_int;
pub extern fn stbtt_PackEnd(spc: [*c]stbtt_pack_context) void;
pub extern fn stbtt_PackFontRange(spc: [*c]stbtt_pack_context, fontdata: [*c]const u8, font_index: c_int, font_size: f32, first_unicode_char_in_range: c_int, num_chars_in_range: c_int, chardata_for_range: [*c]stbtt_packedchar) c_int;
pub const stbtt_pack_range = extern struct {
    font_size: f32 = 0,
    first_unicode_codepoint_in_range: c_int = 0,
    array_of_unicode_codepoints: [*c]c_int = null,
    num_chars: c_int = 0,
    chardata_for_range: [*c]stbtt_packedchar = null,
    h_oversample: u8 = 0,
    v_oversample: u8 = 0,
};
pub extern fn stbtt_PackFontRanges(spc: [*c]stbtt_pack_context, fontdata: [*c]const u8, font_index: c_int, ranges: [*c]stbtt_pack_range, num_ranges: c_int) c_int;
pub extern fn stbtt_PackSetOversampling(spc: [*c]stbtt_pack_context, h_oversample: c_uint, v_oversample: c_uint) void;
pub extern fn stbtt_PackSetSkipMissingCodepoints(spc: [*c]stbtt_pack_context, skip: c_int) void;
pub extern fn stbtt_GetPackedQuad(chardata: [*c]const stbtt_packedchar, pw: c_int, ph: c_int, char_index: c_int, xpos: [*c]f32, ypos: [*c]f32, q: [*c]stbtt_aligned_quad, align_to_integer: c_int) void;
pub extern fn stbtt_PackFontRangesGatherRects(spc: [*c]stbtt_pack_context, info: [*c]const stbtt_fontinfo, ranges: [*c]stbtt_pack_range, num_ranges: c_int, rects: ?*stbrp_rect) c_int;
pub extern fn stbtt_PackFontRangesPackRects(spc: [*c]stbtt_pack_context, rects: ?*stbrp_rect, num_rects: c_int) void;
pub extern fn stbtt_PackFontRangesRenderIntoRects(spc: [*c]stbtt_pack_context, info: [*c]const stbtt_fontinfo, ranges: [*c]stbtt_pack_range, num_ranges: c_int, rects: ?*stbrp_rect) c_int;
pub extern fn stbtt_GetNumberOfFonts(data: [*c]const u8) c_int;
pub extern fn stbtt_GetFontOffsetForIndex(data: [*c]const u8, index: c_int) c_int;
pub extern fn stbtt_InitFont(info: [*c]stbtt_fontinfo, data: [*c]const u8, offset: c_int) c_int;
pub extern fn stbtt_FindGlyphIndex(info: [*c]const stbtt_fontinfo, unicode_codepoint: c_int) c_int;
pub extern fn stbtt_ScaleForPixelHeight(info: [*c]const stbtt_fontinfo, pixels: f32) f32;
pub extern fn stbtt_ScaleForMappingEmToPixels(info: [*c]const stbtt_fontinfo, pixels: f32) f32;
pub extern fn stbtt_GetFontVMetrics(info: [*c]const stbtt_fontinfo, ascent: [*c]c_int, descent: [*c]c_int, lineGap: [*c]c_int) void;
pub extern fn stbtt_GetFontVMetricsOS2(info: [*c]const stbtt_fontinfo, typoAscent: [*c]c_int, typoDescent: [*c]c_int, typoLineGap: [*c]c_int) c_int;
pub extern fn stbtt_GetFontBoundingBox(info: [*c]const stbtt_fontinfo, x0: [*c]c_int, y0: [*c]c_int, x1: [*c]c_int, y1: [*c]c_int) void;
pub extern fn stbtt_GetCodepointHMetrics(info: [*c]const stbtt_fontinfo, codepoint: c_int, advanceWidth: [*c]c_int, leftSideBearing: [*c]c_int) void;
pub extern fn stbtt_GetCodepointKernAdvance(info: [*c]const stbtt_fontinfo, ch1: c_int, ch2: c_int) c_int;
pub extern fn stbtt_GetCodepointBox(info: [*c]const stbtt_fontinfo, codepoint: c_int, x0: [*c]c_int, y0: [*c]c_int, x1: [*c]c_int, y1: [*c]c_int) c_int;
pub extern fn stbtt_GetGlyphHMetrics(info: [*c]const stbtt_fontinfo, glyph_index: c_int, advanceWidth: [*c]c_int, leftSideBearing: [*c]c_int) void;
pub extern fn stbtt_GetGlyphKernAdvance(info: [*c]const stbtt_fontinfo, glyph1: c_int, glyph2: c_int) c_int;
pub extern fn stbtt_GetGlyphBox(info: [*c]const stbtt_fontinfo, glyph_index: c_int, x0: [*c]c_int, y0: [*c]c_int, x1: [*c]c_int, y1: [*c]c_int) c_int;
pub const struct_stbtt_kerningentry = extern struct {
    glyph1: c_int = 0,
    glyph2: c_int = 0,
    advance: c_int = 0,
};
pub const stbtt_kerningentry = struct_stbtt_kerningentry;
pub extern fn stbtt_GetKerningTableLength(info: [*c]const stbtt_fontinfo) c_int;
pub extern fn stbtt_GetKerningTable(info: [*c]const stbtt_fontinfo, table: [*c]stbtt_kerningentry, table_length: c_int) c_int;
pub const STBTT_vmove: c_int = 1;
pub const STBTT_vline: c_int = 2;
pub const STBTT_vcurve: c_int = 3;
pub const STBTT_vcubic: c_int = 4;
const enum_unnamed_2 = c_uint;
pub const stbtt_vertex = extern struct {
    x: c_short = 0,
    y: c_short = 0,
    cx: c_short = 0,
    cy: c_short = 0,
    cx1: c_short = 0,
    cy1: c_short = 0,
    type: u8 = 0,
    padding: u8 = 0,
};
pub extern fn stbtt_IsGlyphEmpty(info: [*c]const stbtt_fontinfo, glyph_index: c_int) c_int;
pub extern fn stbtt_GetCodepointShape(info: [*c]const stbtt_fontinfo, unicode_codepoint: c_int, vertices: [*c][*c]stbtt_vertex) c_int;
pub extern fn stbtt_GetGlyphShape(info: [*c]const stbtt_fontinfo, glyph_index: c_int, vertices: [*c][*c]stbtt_vertex) c_int;
pub extern fn stbtt_FreeShape(info: [*c]const stbtt_fontinfo, vertices: [*c]stbtt_vertex) void;
pub extern fn stbtt_FindSVGDoc(info: [*c]const stbtt_fontinfo, gl: c_int) [*c]u8;
pub extern fn stbtt_GetCodepointSVG(info: [*c]const stbtt_fontinfo, unicode_codepoint: c_int, svg: [*c][*c]const u8) c_int;
pub extern fn stbtt_GetGlyphSVG(info: [*c]const stbtt_fontinfo, gl: c_int, svg: [*c][*c]const u8) c_int;
pub extern fn stbtt_FreeBitmap(bitmap: [*c]u8, userdata: ?*anyopaque) void;
pub extern fn stbtt_GetCodepointBitmap(info: [*c]const stbtt_fontinfo, scale_x: f32, scale_y: f32, codepoint: c_int, width: [*c]c_int, height: [*c]c_int, xoff: [*c]c_int, yoff: [*c]c_int) [*c]u8;
pub extern fn stbtt_GetCodepointBitmapSubpixel(info: [*c]const stbtt_fontinfo, scale_x: f32, scale_y: f32, shift_x: f32, shift_y: f32, codepoint: c_int, width: [*c]c_int, height: [*c]c_int, xoff: [*c]c_int, yoff: [*c]c_int) [*c]u8;
pub extern fn stbtt_MakeCodepointBitmap(info: [*c]const stbtt_fontinfo, output: [*c]u8, out_w: c_int, out_h: c_int, out_stride: c_int, scale_x: f32, scale_y: f32, codepoint: c_int) void;
pub extern fn stbtt_MakeCodepointBitmapSubpixel(info: [*c]const stbtt_fontinfo, output: [*c]u8, out_w: c_int, out_h: c_int, out_stride: c_int, scale_x: f32, scale_y: f32, shift_x: f32, shift_y: f32, codepoint: c_int) void;
pub extern fn stbtt_MakeCodepointBitmapSubpixelPrefilter(info: [*c]const stbtt_fontinfo, output: [*c]u8, out_w: c_int, out_h: c_int, out_stride: c_int, scale_x: f32, scale_y: f32, shift_x: f32, shift_y: f32, oversample_x: c_int, oversample_y: c_int, sub_x: [*c]f32, sub_y: [*c]f32, codepoint: c_int) void;
pub extern fn stbtt_GetCodepointBitmapBox(font: [*c]const stbtt_fontinfo, codepoint: c_int, scale_x: f32, scale_y: f32, ix0: [*c]c_int, iy0: [*c]c_int, ix1: [*c]c_int, iy1: [*c]c_int) void;
pub extern fn stbtt_GetCodepointBitmapBoxSubpixel(font: [*c]const stbtt_fontinfo, codepoint: c_int, scale_x: f32, scale_y: f32, shift_x: f32, shift_y: f32, ix0: [*c]c_int, iy0: [*c]c_int, ix1: [*c]c_int, iy1: [*c]c_int) void;
pub extern fn stbtt_GetGlyphBitmap(info: [*c]const stbtt_fontinfo, scale_x: f32, scale_y: f32, glyph: c_int, width: [*c]c_int, height: [*c]c_int, xoff: [*c]c_int, yoff: [*c]c_int) [*c]u8;
pub extern fn stbtt_GetGlyphBitmapSubpixel(info: [*c]const stbtt_fontinfo, scale_x: f32, scale_y: f32, shift_x: f32, shift_y: f32, glyph: c_int, width: [*c]c_int, height: [*c]c_int, xoff: [*c]c_int, yoff: [*c]c_int) [*c]u8;
pub extern fn stbtt_MakeGlyphBitmap(info: [*c]const stbtt_fontinfo, output: [*c]u8, out_w: c_int, out_h: c_int, out_stride: c_int, scale_x: f32, scale_y: f32, glyph: c_int) void;
pub extern fn stbtt_MakeGlyphBitmapSubpixel(info: [*c]const stbtt_fontinfo, output: [*c]u8, out_w: c_int, out_h: c_int, out_stride: c_int, scale_x: f32, scale_y: f32, shift_x: f32, shift_y: f32, glyph: c_int) void;
pub extern fn stbtt_MakeGlyphBitmapSubpixelPrefilter(info: [*c]const stbtt_fontinfo, output: [*c]u8, out_w: c_int, out_h: c_int, out_stride: c_int, scale_x: f32, scale_y: f32, shift_x: f32, shift_y: f32, oversample_x: c_int, oversample_y: c_int, sub_x: [*c]f32, sub_y: [*c]f32, glyph: c_int) void;
pub extern fn stbtt_GetGlyphBitmapBox(font: [*c]const stbtt_fontinfo, glyph: c_int, scale_x: f32, scale_y: f32, ix0: [*c]c_int, iy0: [*c]c_int, ix1: [*c]c_int, iy1: [*c]c_int) void;
pub extern fn stbtt_GetGlyphBitmapBoxSubpixel(font: [*c]const stbtt_fontinfo, glyph: c_int, scale_x: f32, scale_y: f32, shift_x: f32, shift_y: f32, ix0: [*c]c_int, iy0: [*c]c_int, ix1: [*c]c_int, iy1: [*c]c_int) void;
pub const stbtt__bitmap = extern struct {
    w: c_int = 0,
    h: c_int = 0,
    stride: c_int = 0,
    pixels: [*c]u8 = null,
    pub const stbtt_Rasterize = __root.stbtt_Rasterize;
    pub const Rasterize = __root.stbtt_Rasterize;
};
pub extern fn stbtt_Rasterize(result: [*c]stbtt__bitmap, flatness_in_pixels: f32, vertices: [*c]stbtt_vertex, num_verts: c_int, scale_x: f32, scale_y: f32, shift_x: f32, shift_y: f32, x_off: c_int, y_off: c_int, invert: c_int, userdata: ?*anyopaque) void;
pub extern fn stbtt_FreeSDF(bitmap: [*c]u8, userdata: ?*anyopaque) void;
pub extern fn stbtt_GetGlyphSDF(info: [*c]const stbtt_fontinfo, scale: f32, glyph: c_int, padding: c_int, onedge_value: u8, pixel_dist_scale: f32, width: [*c]c_int, height: [*c]c_int, xoff: [*c]c_int, yoff: [*c]c_int) [*c]u8;
pub extern fn stbtt_GetCodepointSDF(info: [*c]const stbtt_fontinfo, scale: f32, codepoint: c_int, padding: c_int, onedge_value: u8, pixel_dist_scale: f32, width: [*c]c_int, height: [*c]c_int, xoff: [*c]c_int, yoff: [*c]c_int) [*c]u8;
pub extern fn stbtt_FindMatchingFont(fontdata: [*c]const u8, name: [*c]const u8, flags: c_int) c_int;
pub extern fn stbtt_CompareUTF8toUTF16_bigendian(s1: [*c]const u8, len1: c_int, s2: [*c]const u8, len2: c_int) c_int;
pub extern fn stbtt_GetFontNameString(font: [*c]const stbtt_fontinfo, length: [*c]c_int, platformID: c_int, encodingID: c_int, languageID: c_int, nameID: c_int) [*c]const u8;
pub const STBTT_PLATFORM_ID_UNICODE: c_int = 0;
pub const STBTT_PLATFORM_ID_MAC: c_int = 1;
pub const STBTT_PLATFORM_ID_ISO: c_int = 2;
pub const STBTT_PLATFORM_ID_MICROSOFT: c_int = 3;
const enum_unnamed_3 = c_uint;
pub const STBTT_UNICODE_EID_UNICODE_1_0: c_int = 0;
pub const STBTT_UNICODE_EID_UNICODE_1_1: c_int = 1;
pub const STBTT_UNICODE_EID_ISO_10646: c_int = 2;
pub const STBTT_UNICODE_EID_UNICODE_2_0_BMP: c_int = 3;
pub const STBTT_UNICODE_EID_UNICODE_2_0_FULL: c_int = 4;
const enum_unnamed_4 = c_uint;
pub const STBTT_MS_EID_SYMBOL: c_int = 0;
pub const STBTT_MS_EID_UNICODE_BMP: c_int = 1;
pub const STBTT_MS_EID_SHIFTJIS: c_int = 2;
pub const STBTT_MS_EID_UNICODE_FULL: c_int = 10;
const enum_unnamed_5 = c_uint;
pub const STBTT_MAC_EID_ROMAN: c_int = 0;
pub const STBTT_MAC_EID_ARABIC: c_int = 4;
pub const STBTT_MAC_EID_JAPANESE: c_int = 1;
pub const STBTT_MAC_EID_HEBREW: c_int = 5;
pub const STBTT_MAC_EID_CHINESE_TRAD: c_int = 2;
pub const STBTT_MAC_EID_GREEK: c_int = 6;
pub const STBTT_MAC_EID_KOREAN: c_int = 3;
pub const STBTT_MAC_EID_RUSSIAN: c_int = 7;
const enum_unnamed_6 = c_uint;
pub const STBTT_MS_LANG_ENGLISH: c_int = 1033;
pub const STBTT_MS_LANG_ITALIAN: c_int = 1040;
pub const STBTT_MS_LANG_CHINESE: c_int = 2052;
pub const STBTT_MS_LANG_JAPANESE: c_int = 1041;
pub const STBTT_MS_LANG_DUTCH: c_int = 1043;
pub const STBTT_MS_LANG_KOREAN: c_int = 1042;
pub const STBTT_MS_LANG_FRENCH: c_int = 1036;
pub const STBTT_MS_LANG_RUSSIAN: c_int = 1049;
pub const STBTT_MS_LANG_GERMAN: c_int = 1031;
pub const STBTT_MS_LANG_SPANISH: c_int = 1033;
pub const STBTT_MS_LANG_HEBREW: c_int = 1037;
pub const STBTT_MS_LANG_SWEDISH: c_int = 1053;
const enum_unnamed_7 = c_uint;
pub const STBTT_MAC_LANG_ENGLISH: c_int = 0;
pub const STBTT_MAC_LANG_JAPANESE: c_int = 11;
pub const STBTT_MAC_LANG_ARABIC: c_int = 12;
pub const STBTT_MAC_LANG_KOREAN: c_int = 23;
pub const STBTT_MAC_LANG_DUTCH: c_int = 4;
pub const STBTT_MAC_LANG_RUSSIAN: c_int = 32;
pub const STBTT_MAC_LANG_FRENCH: c_int = 1;
pub const STBTT_MAC_LANG_SPANISH: c_int = 6;
pub const STBTT_MAC_LANG_GERMAN: c_int = 2;
pub const STBTT_MAC_LANG_SWEDISH: c_int = 5;
pub const STBTT_MAC_LANG_HEBREW: c_int = 10;
pub const STBTT_MAC_LANG_CHINESE_SIMPLIFIED: c_int = 33;
pub const STBTT_MAC_LANG_ITALIAN: c_int = 3;
pub const STBTT_MAC_LANG_CHINESE_TRAD: c_int = 19;
const enum_unnamed_8 = c_uint;

pub const __VERSION__ = "Aro aro-zig";
pub const __Aro__ = "";
pub const __STDC__ = @as(c_int, 1);
pub const __STDC_HOSTED__ = @as(c_int, 1);
pub const __STDC_UTF_16__ = @as(c_int, 1);
pub const __STDC_UTF_32__ = @as(c_int, 1);
pub const __STDC_EMBED_NOT_FOUND__ = @as(c_int, 0);
pub const __STDC_EMBED_FOUND__ = @as(c_int, 1);
pub const __STDC_EMBED_EMPTY__ = @as(c_int, 2);
pub const __STDC_VERSION__ = @as(c_long, 201710);
pub const __GNUC__ = @as(c_int, 7);
pub const __GNUC_MINOR__ = @as(c_int, 1);
pub const __GNUC_PATCHLEVEL__ = @as(c_int, 0);
pub const __ARO_EMULATE_NO__ = @as(c_int, 0);
pub const __ARO_EMULATE_CLANG__ = @as(c_int, 1);
pub const __ARO_EMULATE_GCC__ = @as(c_int, 2);
pub const __ARO_EMULATE_MSVC__ = @as(c_int, 3);
pub const __ARO_EMULATE__ = __ARO_EMULATE_GCC__;
pub inline fn __building_module(x: anytype) @TypeOf(@as(c_int, 0)) {
    _ = &x;
    return @as(c_int, 0);
}
pub const _WIN32 = @as(c_int, 1);
pub const _WIN64 = @as(c_int, 1);
pub const WIN32 = @as(c_int, 1);
pub const __WIN32 = @as(c_int, 1);
pub const __WIN32__ = @as(c_int, 1);
pub const WINNT = @as(c_int, 1);
pub const __WINNT = @as(c_int, 1);
pub const __WINNT__ = @as(c_int, 1);
pub const WIN64 = @as(c_int, 1);
pub const __WIN64 = @as(c_int, 1);
pub const __WIN64__ = @as(c_int, 1);
pub const __MINGW64__ = @as(c_int, 1);
pub const __MSVCRT__ = @as(c_int, 1);
pub const __MINGW32__ = @as(c_int, 1);
pub const __declspec = @compileError("unable to translate C expr: unexpected token '__attribute__'");
// <builtin>:34:9
pub const _cdecl = @compileError("unable to translate macro: undefined identifier `__cdecl__`");
// <builtin>:35:9
pub const __cdecl = @compileError("unable to translate macro: undefined identifier `__cdecl__`");
// <builtin>:36:9
pub const _stdcall = @compileError("unable to translate macro: undefined identifier `__stdcall__`");
// <builtin>:37:9
pub const __stdcall = @compileError("unable to translate macro: undefined identifier `__stdcall__`");
// <builtin>:38:9
pub const _fastcall = @compileError("unable to translate macro: undefined identifier `__fastcall__`");
// <builtin>:39:9
pub const __fastcall = @compileError("unable to translate macro: undefined identifier `__fastcall__`");
// <builtin>:40:9
pub const _thiscall = @compileError("unable to translate macro: undefined identifier `__thiscall__`");
// <builtin>:41:9
pub const __thiscall = @compileError("unable to translate macro: undefined identifier `__thiscall__`");
// <builtin>:42:9
pub const __code_model_small__ = @as(c_int, 1);
pub const __amd64__ = @as(c_int, 1);
pub const __amd64 = @as(c_int, 1);
pub const __x86_64__ = @as(c_int, 1);
pub const __x86_64 = @as(c_int, 1);
pub const __SEG_GS = @as(c_int, 1);
pub const __SEG_FS = @as(c_int, 1);
pub const __seg_gs = @compileError("unable to translate macro: undefined identifier `address_space`");
// <builtin>:50:9
pub const __seg_fs = @compileError("unable to translate macro: undefined identifier `address_space`");
// <builtin>:51:9
pub const __LAHF_SAHF__ = @as(c_int, 1);
pub const __AES__ = @as(c_int, 1);
pub const __VAES__ = @as(c_int, 1);
pub const __PCLMUL__ = @as(c_int, 1);
pub const __VPCLMULQDQ__ = @as(c_int, 1);
pub const __LZCNT__ = @as(c_int, 1);
pub const __RDRND__ = @as(c_int, 1);
pub const __FSGSBASE__ = @as(c_int, 1);
pub const __BMI__ = @as(c_int, 1);
pub const __BMI2__ = @as(c_int, 1);
pub const __POPCNT__ = @as(c_int, 1);
pub const __PRFCHW__ = @as(c_int, 1);
pub const __RDSEED__ = @as(c_int, 1);
pub const __ADX__ = @as(c_int, 1);
pub const __MOVBE__ = @as(c_int, 1);
pub const __FMA__ = @as(c_int, 1);
pub const __F16C__ = @as(c_int, 1);
pub const __GFNI__ = @as(c_int, 1);
pub const __SHA__ = @as(c_int, 1);
pub const __SHA512__ = @as(c_int, 1);
pub const __FXSR__ = @as(c_int, 1);
pub const __XSAVE__ = @as(c_int, 1);
pub const __XSAVEOPT__ = @as(c_int, 1);
pub const __XSAVEC__ = @as(c_int, 1);
pub const __XSAVES__ = @as(c_int, 1);
pub const __CLFLUSHOPT__ = @as(c_int, 1);
pub const __CLWB__ = @as(c_int, 1);
pub const __SHSTK__ = @as(c_int, 1);
pub const __SM3__ = @as(c_int, 1);
pub const __SM4__ = @as(c_int, 1);
pub const __RDPID__ = @as(c_int, 1);
pub const __WAITPKG__ = @as(c_int, 1);
pub const __MOVDIRI__ = @as(c_int, 1);
pub const __MOVDIR64B__ = @as(c_int, 1);
pub const __PTWRITE__ = @as(c_int, 1);
pub const __INVPCID__ = @as(c_int, 1);
pub const __HRESET__ = @as(c_int, 1);
pub const __CMPCCXADD__ = @as(c_int, 1);
pub const __AVXIFMA__ = @as(c_int, 1);
pub const __AVXNECONVERT__ = @as(c_int, 1);
pub const __AVXVNNI__ = @as(c_int, 1);
pub const __AVXVNNIINT16__ = @as(c_int, 1);
pub const __AVXVNNIINT8__ = @as(c_int, 1);
pub const __SERIALIZE__ = @as(c_int, 1);
pub const __CRC32__ = @as(c_int, 1);
pub const __AVX2__ = @as(c_int, 1);
pub const __AVX__ = @as(c_int, 1);
pub const __SSE4_2__ = @as(c_int, 1);
pub const __SSE4_1__ = @as(c_int, 1);
pub const __SSSE3__ = @as(c_int, 1);
pub const __SSE3__ = @as(c_int, 1);
pub const __SSE2__ = @as(c_int, 1);
pub const __SSE__ = @as(c_int, 1);
pub const __SSE_MATH__ = @as(c_int, 1);
pub const __MMX__ = @as(c_int, 1);
pub const __GCC_HAVE_SYNC_COMPARE_AND_SWAP_8 = @as(c_int, 1);
pub const __ORDER_LITTLE_ENDIAN__ = @as(c_int, 1234);
pub const __ORDER_BIG_ENDIAN__ = @as(c_int, 4321);
pub const __ORDER_PDP_ENDIAN__ = @as(c_int, 3412);
pub const __BYTE_ORDER__ = __ORDER_LITTLE_ENDIAN__;
pub const __LITTLE_ENDIAN__ = @as(c_int, 1);
pub const __ATOMIC_RELAXED = @as(c_int, 0);
pub const __ATOMIC_CONSUME = @as(c_int, 1);
pub const __ATOMIC_ACQUIRE = @as(c_int, 2);
pub const __ATOMIC_RELEASE = @as(c_int, 3);
pub const __ATOMIC_ACQ_REL = @as(c_int, 4);
pub const __ATOMIC_SEQ_CST = @as(c_int, 5);
pub const __ATOMIC_BOOL_LOCK_FREE = @as(c_int, 1);
pub const __ATOMIC_CHAR_LOCK_FREE = @as(c_int, 1);
pub const __ATOMIC_CHAR16_T_LOCK_FREE = @as(c_int, 1);
pub const __ATOMIC_CHAR32_T_LOCK_FREE = @as(c_int, 1);
pub const __ATOMIC_WCHAR_T_LOCK_FREE = @as(c_int, 1);
pub const __ATOMIC_WINT_T_LOCK_FREE = @as(c_int, 1);
pub const __ATOMIC_SHORT_LOCK_FREE = @as(c_int, 1);
pub const __ATOMIC_INT_LOCK_FREE = @as(c_int, 1);
pub const __ATOMIC_LONG_LOCK_FREE = @as(c_int, 1);
pub const __ATOMIC_LLONG_LOCK_FREE = @as(c_int, 1);
pub const __ATOMIC_POINTER_LOCK_FREE = @as(c_int, 1);
pub const __WCHAR_UNSIGNED__ = @as(c_int, 1);
pub const __WINT_UNSIGNED__ = @as(c_int, 1);
pub const __CHAR_BIT__ = @as(c_int, 8);
pub const __BOOL_WIDTH__ = @as(c_int, 8);
pub const __SCHAR_MAX__ = @as(c_int, 127);
pub const __SCHAR_WIDTH__ = @as(c_int, 8);
pub const __SHRT_MAX__ = @as(c_int, 32767);
pub const __SHRT_WIDTH__ = @as(c_int, 16);
pub const __INT_MAX__ = __helpers.promoteIntLiteral(c_int, 2147483647, .decimal);
pub const __INT_WIDTH__ = @as(c_int, 32);
pub const __LONG_MAX__ = @as(c_long, 2147483647);
pub const __LONG_WIDTH__ = @as(c_int, 32);
pub const __LONG_LONG_MAX__ = @as(c_longlong, 9223372036854775807);
pub const __LONG_LONG_WIDTH__ = @as(c_int, 64);
pub const __WCHAR_MAX__ = __helpers.promoteIntLiteral(c_int, 65535, .decimal);
pub const __WCHAR_WIDTH__ = @as(c_int, 16);
pub const __WINT_MAX__ = __helpers.promoteIntLiteral(c_int, 65535, .decimal);
pub const __WINT_WIDTH__ = @as(c_int, 16);
pub const __INTMAX_MAX__ = @as(c_longlong, 9223372036854775807);
pub const __INTMAX_WIDTH__ = @as(c_int, 64);
pub const __SIZE_MAX__ = @as(c_ulonglong, 18446744073709551615);
pub const __SIZE_WIDTH__ = @as(c_int, 64);
pub const __UINTMAX_MAX__ = @as(c_ulonglong, 18446744073709551615);
pub const __UINTMAX_WIDTH__ = @as(c_int, 64);
pub const __PTRDIFF_MAX__ = @as(c_longlong, 9223372036854775807);
pub const __PTRDIFF_WIDTH__ = @as(c_int, 64);
pub const __INTPTR_MAX__ = @as(c_longlong, 9223372036854775807);
pub const __INTPTR_WIDTH__ = @as(c_int, 64);
pub const __UINTPTR_MAX__ = @as(c_ulonglong, 18446744073709551615);
pub const __UINTPTR_WIDTH__ = @as(c_int, 64);
pub const __SIG_ATOMIC_MAX__ = __helpers.promoteIntLiteral(c_int, 2147483647, .decimal);
pub const __SIG_ATOMIC_WIDTH__ = @as(c_int, 32);
pub const __BITINT_MAXWIDTH__ = __helpers.promoteIntLiteral(c_int, 65535, .decimal);
pub const __SIZEOF_FLOAT__ = @as(c_int, 4);
pub const __SIZEOF_DOUBLE__ = @as(c_int, 8);
pub const __SIZEOF_LONG_DOUBLE__ = @as(c_int, 10);
pub const __SIZEOF_SHORT__ = @as(c_int, 2);
pub const __SIZEOF_INT__ = @as(c_int, 4);
pub const __SIZEOF_LONG__ = @as(c_int, 4);
pub const __SIZEOF_LONG_LONG__ = @as(c_int, 8);
pub const __SIZEOF_POINTER__ = @as(c_int, 8);
pub const __SIZEOF_PTRDIFF_T__ = @as(c_int, 8);
pub const __SIZEOF_SIZE_T__ = @as(c_int, 8);
pub const __SIZEOF_WCHAR_T__ = @as(c_int, 2);
pub const __SIZEOF_WINT_T__ = @as(c_int, 2);
pub const __SIZEOF_INT128__ = @as(c_int, 16);
pub const __INTPTR_TYPE__ = c_longlong;
pub const __UINTPTR_TYPE__ = c_ulonglong;
pub const __INTMAX_TYPE__ = c_longlong;
pub const __INTMAX_C_SUFFIX__ = @compileError("unable to translate macro: undefined identifier `LL`");
// <builtin>:180:9
pub const __INTMAX_C = __helpers.LL_SUFFIX;
pub const __UINTMAX_TYPE__ = c_ulonglong;
pub const __UINTMAX_C_SUFFIX__ = @compileError("unable to translate macro: undefined identifier `ULL`");
// <builtin>:183:9
pub const __UINTMAX_C = __helpers.ULL_SUFFIX;
pub const __PTRDIFF_TYPE__ = c_longlong;
pub const __SIZE_TYPE__ = c_ulonglong;
pub const __WCHAR_TYPE__ = c_ushort;
pub const __WINT_TYPE__ = c_ushort;
pub const __CHAR16_TYPE__ = c_ushort;
pub const __CHAR32_TYPE__ = c_uint;
pub const __INT8_TYPE__ = i8;
pub const __INT8_FMTd__ = "hhd";
pub const __INT8_FMTi__ = "hhi";
pub const __INT8_C_SUFFIX__ = "";
pub inline fn __INT8_C(c: anytype) @TypeOf(c) {
    _ = &c;
    return c;
}
pub const __INT16_TYPE__ = c_short;
pub const __INT16_FMTd__ = "hd";
pub const __INT16_FMTi__ = "hi";
pub const __INT16_C_SUFFIX__ = "";
pub inline fn __INT16_C(c: anytype) @TypeOf(c) {
    _ = &c;
    return c;
}
pub const __INT32_TYPE__ = c_int;
pub const __INT32_FMTd__ = "d";
pub const __INT32_FMTi__ = "i";
pub const __INT32_C_SUFFIX__ = "";
pub inline fn __INT32_C(c: anytype) @TypeOf(c) {
    _ = &c;
    return c;
}
pub const __INT64_TYPE__ = c_longlong;
pub const __INT64_FMTd__ = "lld";
pub const __INT64_FMTi__ = "lli";
pub const __INT64_C_SUFFIX__ = @compileError("unable to translate macro: undefined identifier `LL`");
// <builtin>:209:9
pub const __INT64_C = __helpers.LL_SUFFIX;
pub const __UINT8_TYPE__ = u8;
pub const __UINT8_FMTo__ = "hho";
pub const __UINT8_FMTu__ = "hhu";
pub const __UINT8_FMTx__ = "hhx";
pub const __UINT8_FMTX__ = "hhX";
pub const __UINT8_C_SUFFIX__ = "";
pub inline fn __UINT8_C(c: anytype) @TypeOf(c) {
    _ = &c;
    return c;
}
pub const __UINT8_MAX__ = @as(c_int, 255);
pub const __INT8_MAX__ = @as(c_int, 127);
pub const __UINT16_TYPE__ = c_ushort;
pub const __UINT16_FMTo__ = "ho";
pub const __UINT16_FMTu__ = "hu";
pub const __UINT16_FMTx__ = "hx";
pub const __UINT16_FMTX__ = "hX";
pub const __UINT16_C_SUFFIX__ = "";
pub inline fn __UINT16_C(c: anytype) @TypeOf(c) {
    _ = &c;
    return c;
}
pub const __UINT16_MAX__ = __helpers.promoteIntLiteral(c_int, 65535, .decimal);
pub const __INT16_MAX__ = @as(c_int, 32767);
pub const __UINT32_TYPE__ = c_uint;
pub const __UINT32_FMTo__ = "o";
pub const __UINT32_FMTu__ = "u";
pub const __UINT32_FMTx__ = "x";
pub const __UINT32_FMTX__ = "X";
pub const __UINT32_C_SUFFIX__ = @compileError("unable to translate macro: undefined identifier `U`");
// <builtin>:234:9
pub const __UINT32_C = __helpers.U_SUFFIX;
pub const __UINT32_MAX__ = __helpers.promoteIntLiteral(c_uint, 4294967295, .decimal);
pub const __INT32_MAX__ = __helpers.promoteIntLiteral(c_int, 2147483647, .decimal);
pub const __UINT64_TYPE__ = c_ulonglong;
pub const __UINT64_FMTo__ = "llo";
pub const __UINT64_FMTu__ = "llu";
pub const __UINT64_FMTx__ = "llx";
pub const __UINT64_FMTX__ = "llX";
pub const __UINT64_C_SUFFIX__ = @compileError("unable to translate macro: undefined identifier `ULL`");
// <builtin>:243:9
pub const __UINT64_C = __helpers.ULL_SUFFIX;
pub const __UINT64_MAX__ = @as(c_ulonglong, 18446744073709551615);
pub const __INT64_MAX__ = @as(c_longlong, 9223372036854775807);
pub const __INT_LEAST8_TYPE__ = i8;
pub const __INT_LEAST8_MAX__ = @as(c_int, 127);
pub const __INT_LEAST8_WIDTH__ = @as(c_int, 8);
pub const INT_LEAST8_FMTd__ = "hhd";
pub const INT_LEAST8_FMTi__ = "hhi";
pub const __UINT_LEAST8_TYPE__ = u8;
pub const __UINT_LEAST8_MAX__ = @as(c_int, 255);
pub const UINT_LEAST8_FMTo__ = "hho";
pub const UINT_LEAST8_FMTu__ = "hhu";
pub const UINT_LEAST8_FMTx__ = "hhx";
pub const UINT_LEAST8_FMTX__ = "hhX";
pub const __INT_FAST8_TYPE__ = i8;
pub const __INT_FAST8_MAX__ = @as(c_int, 127);
pub const __INT_FAST8_WIDTH__ = @as(c_int, 8);
pub const INT_FAST8_FMTd__ = "hhd";
pub const INT_FAST8_FMTi__ = "hhi";
pub const __UINT_FAST8_TYPE__ = u8;
pub const __UINT_FAST8_MAX__ = @as(c_int, 255);
pub const UINT_FAST8_FMTo__ = "hho";
pub const UINT_FAST8_FMTu__ = "hhu";
pub const UINT_FAST8_FMTx__ = "hhx";
pub const UINT_FAST8_FMTX__ = "hhX";
pub const __INT_LEAST16_TYPE__ = c_short;
pub const __INT_LEAST16_MAX__ = @as(c_int, 32767);
pub const __INT_LEAST16_WIDTH__ = @as(c_int, 16);
pub const INT_LEAST16_FMTd__ = "hd";
pub const INT_LEAST16_FMTi__ = "hi";
pub const __UINT_LEAST16_TYPE__ = c_ushort;
pub const __UINT_LEAST16_MAX__ = __helpers.promoteIntLiteral(c_int, 65535, .decimal);
pub const UINT_LEAST16_FMTo__ = "ho";
pub const UINT_LEAST16_FMTu__ = "hu";
pub const UINT_LEAST16_FMTx__ = "hx";
pub const UINT_LEAST16_FMTX__ = "hX";
pub const __INT_FAST16_TYPE__ = c_short;
pub const __INT_FAST16_MAX__ = @as(c_int, 32767);
pub const __INT_FAST16_WIDTH__ = @as(c_int, 16);
pub const INT_FAST16_FMTd__ = "hd";
pub const INT_FAST16_FMTi__ = "hi";
pub const __UINT_FAST16_TYPE__ = c_ushort;
pub const __UINT_FAST16_MAX__ = __helpers.promoteIntLiteral(c_int, 65535, .decimal);
pub const UINT_FAST16_FMTo__ = "ho";
pub const UINT_FAST16_FMTu__ = "hu";
pub const UINT_FAST16_FMTx__ = "hx";
pub const UINT_FAST16_FMTX__ = "hX";
pub const __INT_LEAST32_TYPE__ = c_int;
pub const __INT_LEAST32_MAX__ = __helpers.promoteIntLiteral(c_int, 2147483647, .decimal);
pub const __INT_LEAST32_WIDTH__ = @as(c_int, 32);
pub const INT_LEAST32_FMTd__ = "d";
pub const INT_LEAST32_FMTi__ = "i";
pub const __UINT_LEAST32_TYPE__ = c_uint;
pub const __UINT_LEAST32_MAX__ = __helpers.promoteIntLiteral(c_uint, 4294967295, .decimal);
pub const UINT_LEAST32_FMTo__ = "o";
pub const UINT_LEAST32_FMTu__ = "u";
pub const UINT_LEAST32_FMTx__ = "x";
pub const UINT_LEAST32_FMTX__ = "X";
pub const __INT_FAST32_TYPE__ = c_int;
pub const __INT_FAST32_MAX__ = __helpers.promoteIntLiteral(c_int, 2147483647, .decimal);
pub const __INT_FAST32_WIDTH__ = @as(c_int, 32);
pub const INT_FAST32_FMTd__ = "d";
pub const INT_FAST32_FMTi__ = "i";
pub const __UINT_FAST32_TYPE__ = c_uint;
pub const __UINT_FAST32_MAX__ = __helpers.promoteIntLiteral(c_uint, 4294967295, .decimal);
pub const UINT_FAST32_FMTo__ = "o";
pub const UINT_FAST32_FMTu__ = "u";
pub const UINT_FAST32_FMTx__ = "x";
pub const UINT_FAST32_FMTX__ = "X";
pub const __INT_LEAST64_TYPE__ = c_longlong;
pub const __INT_LEAST64_MAX__ = @as(c_longlong, 9223372036854775807);
pub const __INT_LEAST64_WIDTH__ = @as(c_int, 64);
pub const INT_LEAST64_FMTd__ = "lld";
pub const INT_LEAST64_FMTi__ = "lli";
pub const __UINT_LEAST64_TYPE__ = c_ulonglong;
pub const __UINT_LEAST64_MAX__ = @as(c_ulonglong, 18446744073709551615);
pub const UINT_LEAST64_FMTo__ = "llo";
pub const UINT_LEAST64_FMTu__ = "llu";
pub const UINT_LEAST64_FMTx__ = "llx";
pub const UINT_LEAST64_FMTX__ = "llX";
pub const __INT_FAST64_TYPE__ = c_longlong;
pub const __INT_FAST64_MAX__ = @as(c_longlong, 9223372036854775807);
pub const __INT_FAST64_WIDTH__ = @as(c_int, 64);
pub const INT_FAST64_FMTd__ = "lld";
pub const INT_FAST64_FMTi__ = "lli";
pub const __UINT_FAST64_TYPE__ = c_ulonglong;
pub const __UINT_FAST64_MAX__ = @as(c_ulonglong, 18446744073709551615);
pub const UINT_FAST64_FMTo__ = "llo";
pub const UINT_FAST64_FMTu__ = "llu";
pub const UINT_FAST64_FMTx__ = "llx";
pub const UINT_FAST64_FMTX__ = "llX";
pub const __FLT16_DENORM_MIN__ = @as(f16, 5.9604644775390625e-8);
pub const __FLT16_HAS_DENORM__ = "";
pub const __FLT16_DIG__ = @as(c_int, 3);
pub const __FLT16_DECIMAL_DIG__ = @as(c_int, 5);
pub const __FLT16_EPSILON__ = @as(f16, 9.765625e-4);
pub const __FLT16_HAS_INFINITY__ = "";
pub const __FLT16_HAS_QUIET_NAN__ = "";
pub const __FLT16_MANT_DIG__ = @as(c_int, 11);
pub const __FLT16_MAX_10_EXP__ = @as(c_int, 4);
pub const __FLT16_MAX_EXP__ = @as(c_int, 16);
pub const __FLT16_MAX__ = @as(f16, 6.5504e+4);
pub const __FLT16_MIN_10_EXP__ = -@as(c_int, 4);
pub const __FLT16_MIN_EXP__ = -@as(c_int, 13);
pub const __FLT16_MIN__ = @as(f16, 6.103515625e-5);
pub const __FLT_DENORM_MIN__ = @as(f32, 1.40129846e-45);
pub const __FLT_HAS_DENORM__ = "";
pub const __FLT_DIG__ = @as(c_int, 6);
pub const __FLT_DECIMAL_DIG__ = @as(c_int, 9);
pub const __FLT_EPSILON__ = @as(f32, 1.19209290e-7);
pub const __FLT_HAS_INFINITY__ = "";
pub const __FLT_HAS_QUIET_NAN__ = "";
pub const __FLT_MANT_DIG__ = @as(c_int, 24);
pub const __FLT_MAX_10_EXP__ = @as(c_int, 38);
pub const __FLT_MAX_EXP__ = @as(c_int, 128);
pub const __FLT_MAX__ = @as(f32, 3.40282347e+38);
pub const __FLT_MIN_10_EXP__ = -@as(c_int, 37);
pub const __FLT_MIN_EXP__ = -@as(c_int, 125);
pub const __FLT_MIN__ = @as(f32, 1.17549435e-38);
pub const __DBL_DENORM_MIN__ = @as(f64, 4.9406564584124654e-324);
pub const __DBL_HAS_DENORM__ = "";
pub const __DBL_DIG__ = @as(c_int, 15);
pub const __DBL_DECIMAL_DIG__ = @as(c_int, 17);
pub const __DBL_EPSILON__ = @as(f64, 2.2204460492503131e-16);
pub const __DBL_HAS_INFINITY__ = "";
pub const __DBL_HAS_QUIET_NAN__ = "";
pub const __DBL_MANT_DIG__ = @as(c_int, 53);
pub const __DBL_MAX_10_EXP__ = @as(c_int, 308);
pub const __DBL_MAX_EXP__ = @as(c_int, 1024);
pub const __DBL_MAX__ = @as(f64, 1.7976931348623157e+308);
pub const __DBL_MIN_10_EXP__ = -@as(c_int, 307);
pub const __DBL_MIN_EXP__ = -@as(c_int, 1021);
pub const __DBL_MIN__ = @as(f64, 2.2250738585072014e-308);
pub const __LDBL_DENORM_MIN__ = @as(c_longdouble, 3.64519953188247460253e-4951);
pub const __LDBL_HAS_DENORM__ = "";
pub const __LDBL_DIG__ = @as(c_int, 18);
pub const __LDBL_DECIMAL_DIG__ = @as(c_int, 21);
pub const __LDBL_EPSILON__ = @as(c_longdouble, 1.08420217248550443401e-19);
pub const __LDBL_HAS_INFINITY__ = "";
pub const __LDBL_HAS_QUIET_NAN__ = "";
pub const __LDBL_MANT_DIG__ = @as(c_int, 64);
pub const __LDBL_MAX_10_EXP__ = @as(c_int, 4932);
pub const __LDBL_MAX_EXP__ = @as(c_int, 16384);
pub const __LDBL_MAX__ = @as(c_longdouble, 1.18973149535723176502e+4932);
pub const __LDBL_MIN_10_EXP__ = -@as(c_int, 4931);
pub const __LDBL_MIN_EXP__ = -@as(c_int, 16381);
pub const __LDBL_MIN__ = @as(c_longdouble, 3.36210314311209350626e-4932);
pub const __FLT_EVAL_METHOD__ = @as(c_int, 0);
pub const __FLT_RADIX__ = @as(c_int, 2);
pub const __DECIMAL_DIG__ = __LDBL_DECIMAL_DIG__;
pub const __pic__ = @as(c_int, 2);
pub const __PIC__ = @as(c_int, 2);
pub const __MSVCRT_VERSION__ = @as(c_int, 0xE00);
pub const _WIN32_WINNT = @as(c_int, 0x0a00);
pub const __CRT__NO_INLINE = @as(c_int, 1);
pub const __CLANG_STDINT_H = "";
pub const _STDINT_H = "";
pub const _INC_CRTDEFS = "";
pub const _INC_CORECRT = "";
pub const _INC__MINGW_H = "";
pub const _INC_CRTDEFS_MACRO = "";
pub const __MINGW64_PASTE2 = @compileError("unable to translate C expr: unexpected token '##'");
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\libc\include\any-windows-any\_mingw_mac.h:10:9
pub inline fn __MINGW64_PASTE(x: anytype, y: anytype) @TypeOf(__MINGW64_PASTE2(x, y)) {
    _ = &x;
    _ = &y;
    return __MINGW64_PASTE2(x, y);
}
pub const __STRINGIFY = @compileError("unable to translate C expr: unexpected token ''");
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\libc\include\any-windows-any\_mingw_mac.h:13:9
pub inline fn __MINGW64_STRINGIFY(x: anytype) @TypeOf(__STRINGIFY(x)) {
    _ = &x;
    return __STRINGIFY(x);
}
pub const __MINGW64_VERSION_MAJOR = @as(c_int, 15);
pub const __MINGW64_VERSION_MINOR = @as(c_int, 0);
pub const __MINGW64_VERSION_BUGFIX = @as(c_int, 0);
pub const __MINGW64_VERSION_RC = @as(c_int, 0);
pub const __MINGW64_VERSION_STR = __MINGW64_STRINGIFY(__MINGW64_VERSION_MAJOR) ++ "." ++ __MINGW64_STRINGIFY(__MINGW64_VERSION_MINOR) ++ "." ++ __MINGW64_STRINGIFY(__MINGW64_VERSION_BUGFIX);
pub const __MINGW64_VERSION_STATE = "alpha";
pub const __MINGW32_MAJOR_VERSION = @as(c_int, 3);
pub const __MINGW32_MINOR_VERSION = @as(c_int, 11);
pub const _M_AMD64 = @as(c_int, 100);
pub const _M_X64 = @as(c_int, 100);
pub const __MINGW_USE_UNDERSCORE_PREFIX = @as(c_int, 0);
pub const __MINGW_IMP_SYMBOL = @compileError("unable to translate macro: undefined identifier `__imp_`");
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\libc\include\any-windows-any\_mingw_mac.h:129:11
pub const __MINGW_IMP_LSYMBOL = @compileError("unable to translate macro: undefined identifier `__imp_`");
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\libc\include\any-windows-any\_mingw_mac.h:130:11
pub inline fn __MINGW_USYMBOL(sym: anytype) @TypeOf(sym) {
    _ = &sym;
    return sym;
}
pub const __MINGW_LSYMBOL = @compileError("unable to translate macro: undefined identifier `_`");
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\libc\include\any-windows-any\_mingw_mac.h:132:11
pub const __MINGW_ASM_CALL = @compileError("unable to translate C expr: unexpected token '__asm__'");
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\libc\include\any-windows-any\_mingw_mac.h:140:9
pub const __MINGW_ASM_CRT_CALL = @compileError("unable to translate C expr: unexpected token '__asm__'");
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\libc\include\any-windows-any\_mingw_mac.h:141:9
pub const __MINGW_EXTENSION = @compileError("unable to translate C expr: unexpected token '__extension__'");
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\libc\include\any-windows-any\_mingw_mac.h:173:13
pub const __C89_NAMELESS = __MINGW_EXTENSION;
pub const __C89_NAMELESSSTRUCTNAME = "";
pub const __C89_NAMELESSSTRUCTNAME1 = "";
pub const __C89_NAMELESSSTRUCTNAME2 = "";
pub const __C89_NAMELESSSTRUCTNAME3 = "";
pub const __C89_NAMELESSSTRUCTNAME4 = "";
pub const __C89_NAMELESSSTRUCTNAME5 = "";
pub const __C89_NAMELESSUNIONNAME = "";
pub const __C89_NAMELESSUNIONNAME1 = "";
pub const __C89_NAMELESSUNIONNAME2 = "";
pub const __C89_NAMELESSUNIONNAME3 = "";
pub const __C89_NAMELESSUNIONNAME4 = "";
pub const __C89_NAMELESSUNIONNAME5 = "";
pub const __C89_NAMELESSUNIONNAME6 = "";
pub const __C89_NAMELESSUNIONNAME7 = "";
pub const __C89_NAMELESSUNIONNAME8 = "";
pub const __GNU_EXTENSION = __MINGW_EXTENSION;
pub const __MINGW_HAVE_ANSI_C99_PRINTF = @as(c_int, 1);
pub const __MINGW_HAVE_WIDE_C99_PRINTF = @as(c_int, 1);
pub const __MINGW_HAVE_ANSI_C99_SCANF = @as(c_int, 1);
pub const __MINGW_HAVE_WIDE_C99_SCANF = @as(c_int, 1);
pub const __MINGW_POISON_NAME = @compileError("unable to translate macro: undefined identifier `_layout_has_not_been_verified_and_its_declaration_is_most_likely_incorrect`");
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\libc\include\any-windows-any\_mingw_mac.h:213:11
pub const __MSABI_LONG = __helpers.L_SUFFIX;
pub const __MINGW_GCC_VERSION = ((__GNUC__ * @as(c_int, 10000)) + (__GNUC_MINOR__ * @as(c_int, 100))) + __GNUC_PATCHLEVEL__;
pub inline fn __MINGW_GNUC_PREREQ(major: anytype, minor: anytype) @TypeOf((__GNUC__ > major) or ((__GNUC__ == major) and (__GNUC_MINOR__ >= minor))) {
    _ = &major;
    _ = &minor;
    return (__GNUC__ > major) or ((__GNUC__ == major) and (__GNUC_MINOR__ >= minor));
}
pub inline fn __MINGW_MSC_PREREQ(major: anytype, minor: anytype) @TypeOf(@as(c_int, 0)) {
    _ = &major;
    _ = &minor;
    return @as(c_int, 0);
}
pub inline fn __MINGW_ATTRIB_DEPRECATED_STR(X: anytype) void {
    _ = &X;
    return;
}
pub const __MINGW_SEC_WARN_STR = "This function or variable may be unsafe, use _CRT_SECURE_NO_WARNINGS to disable deprecation";
pub const __MINGW_MSVC2005_DEPREC_STR = "This POSIX function is deprecated beginning in Visual C++ 2005, use _CRT_NONSTDC_NO_DEPRECATE to disable deprecation";
pub const __MINGW_ATTRIB_DEPRECATED_MSVC2005 = __MINGW_ATTRIB_DEPRECATED_STR(__MINGW_MSVC2005_DEPREC_STR);
pub const __MINGW_ATTRIB_DEPRECATED_SEC_WARN = __MINGW_ATTRIB_DEPRECATED_STR(__MINGW_SEC_WARN_STR);
pub const __MINGW_MS_PRINTF = @compileError("unable to translate macro: undefined identifier `__format__`");
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\libc\include\any-windows-any\_mingw_mac.h:293:9
pub const __MINGW_MS_SCANF = @compileError("unable to translate macro: undefined identifier `__format__`");
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\libc\include\any-windows-any\_mingw_mac.h:296:9
pub const __MINGW_GNU_PRINTF = @compileError("unable to translate macro: undefined identifier `__format__`");
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\libc\include\any-windows-any\_mingw_mac.h:299:9
pub const __MINGW_GNU_SCANF = @compileError("unable to translate macro: undefined identifier `__format__`");
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\libc\include\any-windows-any\_mingw_mac.h:302:9
pub const __mingw_ovr = @compileError("unable to translate macro: undefined identifier `__unused__`");
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\libc\include\any-windows-any\_mingw_mac.h:311:11
pub const __mingw_attribute_artificial = @compileError("unable to translate macro: undefined identifier `__artificial__`");
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\libc\include\any-windows-any\_mingw_mac.h:318:11
pub const __MINGW_SELECTANY = @compileError("unable to translate macro: undefined identifier `__selectany__`");
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\libc\include\any-windows-any\_mingw_mac.h:324:9
pub const __MINGW_FORTIFY_LEVEL = @as(c_int, 0);
pub const __mingw_bos_ovr = __mingw_ovr;
pub const __MINGW_FORTIFY_VA_ARG = @as(c_int, 0);
pub const _INC_MINGW_SECAPI = "";
pub const _CRT_SECURE_CPP_OVERLOAD_SECURE_NAMES = @as(c_int, 0);
pub const _CRT_SECURE_CPP_OVERLOAD_SECURE_NAMES_MEMORY = @as(c_int, 0);
pub const _CRT_SECURE_CPP_OVERLOAD_STANDARD_NAMES = @as(c_int, 0);
pub const _CRT_SECURE_CPP_OVERLOAD_STANDARD_NAMES_COUNT = @as(c_int, 0);
pub const _CRT_SECURE_CPP_OVERLOAD_STANDARD_NAMES_MEMORY = @as(c_int, 0);
pub const __MINGW_CRT_NAME_CONCAT2 = @compileError("unable to translate macro: undefined identifier `_s`");
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\libc\include\any-windows-any\_mingw_secapi.h:41:9
pub const __CRT_SECURE_CPP_OVERLOAD_STANDARD_NAMES_MEMORY_0_3_ = @compileError("unable to translate C expr: unexpected token '__cdecl'");
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\libc\include\any-windows-any\_mingw_secapi.h:69:9
pub const __LONG32 = c_long;
pub const __MINGW_IMPORT = @compileError("unable to translate macro: undefined identifier `__dllimport__`");
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\libc\include\any-windows-any\_mingw.h:44:12
pub const __USE_CRTIMP = @as(c_int, 1);
pub const _CRTIMP = @compileError("unable to translate macro: undefined identifier `__dllimport__`");
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\libc\include\any-windows-any\_mingw.h:52:15
pub const __DECLSPEC_SUPPORTED = "";
pub const USE___UUIDOF = @as(c_int, 0);
pub const _inline = @compileError("unable to translate C expr: unexpected token '__inline'");
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\libc\include\any-windows-any\_mingw.h:74:9
pub const __CRT_INLINE = @compileError("unable to translate macro: undefined identifier `__gnu_inline__`");
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\libc\include\any-windows-any\_mingw.h:83:11
pub const __MINGW_INTRIN_INLINE = @compileError("unable to translate macro: undefined identifier `__always_inline__`");
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\libc\include\any-windows-any\_mingw.h:90:9
pub const __MINGW_CXX11_CONSTEXPR = "";
pub const __MINGW_CXX14_CONSTEXPR = "";
pub const __UNUSED_PARAM = @compileError("unable to translate macro: undefined identifier `__unused__`");
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\libc\include\any-windows-any\_mingw.h:118:11
pub const __restrict_arr = @compileError("unable to translate C expr: unexpected token '__restrict'");
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\libc\include\any-windows-any\_mingw.h:133:10
pub const __MINGW_ATTRIB_NORETURN = @compileError("unable to translate macro: undefined identifier `__noreturn__`");
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\libc\include\any-windows-any\_mingw.h:149:9
pub const __MINGW_ATTRIB_CONST = @compileError("unable to translate C expr: unexpected token '__attribute__'");
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\libc\include\any-windows-any\_mingw.h:150:9
pub const __MINGW_ATTRIB_MALLOC = @compileError("unable to translate macro: undefined identifier `__malloc__`");
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\libc\include\any-windows-any\_mingw.h:160:9
pub const __MINGW_ATTRIB_PURE = @compileError("unable to translate macro: undefined identifier `__pure__`");
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\libc\include\any-windows-any\_mingw.h:161:9
pub const __MINGW_ATTRIB_NONNULL = @compileError("unable to translate macro: undefined identifier `__nonnull__`");
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\libc\include\any-windows-any\_mingw.h:174:9
pub const __MINGW_ATTRIB_UNUSED = @compileError("unable to translate macro: undefined identifier `__unused__`");
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\libc\include\any-windows-any\_mingw.h:180:9
pub const __MINGW_ATTRIB_USED = @compileError("unable to translate macro: undefined identifier `__used__`");
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\libc\include\any-windows-any\_mingw.h:186:9
pub const __MINGW_ATTRIB_DEPRECATED = @compileError("unable to translate macro: undefined identifier `__deprecated__`");
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\libc\include\any-windows-any\_mingw.h:187:9
pub const __MINGW_ATTRIB_DEPRECATED_MSG = @compileError("unable to translate macro: undefined identifier `__deprecated__`");
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\libc\include\any-windows-any\_mingw.h:189:9
pub const __MINGW_NOTHROW = @compileError("unable to translate macro: undefined identifier `__nothrow__`");
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\libc\include\any-windows-any\_mingw.h:204:9
pub const __MINGW_ATTRIB_NO_OPTIMIZE = @compileError("unable to translate macro: undefined identifier `__optimize__`");
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\libc\include\any-windows-any\_mingw.h:212:9
pub const __MINGW_PRAGMA_PARAM = @compileError("unable to translate macro: undefined identifier `_Pragma`");
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\libc\include\any-windows-any\_mingw.h:218:9
pub const __MINGW_BROKEN_INTERFACE = @compileError("unable to translate macro: undefined identifier `message`");
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\libc\include\any-windows-any\_mingw.h:225:9
pub const _UCRT = "";
pub inline fn __MINGW_UCRT_ASM_CALL(func: anytype) @TypeOf(__MINGW_ASM_CALL(func)) {
    _ = &func;
    return __MINGW_ASM_CALL(func);
}
pub const _INT128_DEFINED = "";
pub const __int8 = u8;
pub const __int16 = c_short;
pub const __int32 = c_int;
pub const __int64 = c_longlong;
pub const __ptr32 = "";
pub const __ptr64 = "";
pub const __unaligned = "";
pub const __w64 = "";
pub const __forceinline = @compileError("unable to translate macro: undefined identifier `__always_inline__`");
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\libc\include\any-windows-any\_mingw.h:290:9
pub const __nothrow = "";
pub const _INC_VADEFS = "";
pub const __GNUC_VA_LIST = "";
pub const _VA_LIST_DEFINED = "";
pub inline fn _ADDRESSOF(v: anytype) @TypeOf(&v) {
    _ = &v;
    return &v;
}
pub const _crt_va_start = @compileError("unable to translate macro: undefined identifier `__builtin_va_start`");
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\libc\include\any-windows-any\vadefs.h:48:9
pub const _crt_va_arg = @compileError("unable to translate macro: undefined identifier `__builtin_va_arg`");
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\libc\include\any-windows-any\vadefs.h:49:9
pub const _crt_va_end = @compileError("unable to translate macro: undefined identifier `__builtin_va_end`");
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\libc\include\any-windows-any\vadefs.h:50:9
pub const _crt_va_copy = @compileError("unable to translate macro: undefined identifier `__builtin_va_copy`");
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\libc\include\any-windows-any\vadefs.h:51:9
pub const __CRT_STRINGIZE = @compileError("unable to translate C expr: unexpected token ''");
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\libc\include\any-windows-any\_mingw.h:309:9
pub inline fn _CRT_STRINGIZE(_Value: anytype) @TypeOf(__CRT_STRINGIZE(_Value)) {
    _ = &_Value;
    return __CRT_STRINGIZE(_Value);
}
pub const __CRT_WIDE = @compileError("unable to translate macro: undefined identifier `L`");
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\libc\include\any-windows-any\_mingw.h:314:9
pub inline fn _CRT_WIDE(_String: anytype) @TypeOf(__CRT_WIDE(_String)) {
    _ = &_String;
    return __CRT_WIDE(_String);
}
pub const _W64 = "";
pub const _CRTIMP_NOIA64 = _CRTIMP;
pub const _CRTIMP2 = _CRTIMP;
pub const _CRTIMP_ALTERNATIVE = _CRTIMP;
pub const _CRT_ALTERNATIVE_IMPORTED = "";
pub const _MRTIMP2 = _CRTIMP;
pub const _DLL = "";
pub const _MT = "";
pub const _MCRTIMP = _CRTIMP;
pub const _CRTIMP_PURE = _CRTIMP;
pub const _PGLOBAL = "";
pub const _AGLOBAL = "";
pub const _SECURECRT_FILL_BUFFER_PATTERN = @as(c_int, 0xFD);
pub const _CRT_DEPRECATE_TEXT = @compileError("unable to translate macro: undefined identifier `deprecated`");
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\libc\include\any-windows-any\_mingw.h:373:9
pub inline fn _CRT_INSECURE_DEPRECATE_MEMORY(_Replacement: anytype) void {
    _ = &_Replacement;
    return;
}
pub inline fn _CRT_INSECURE_DEPRECATE_GLOBALS(_Replacement: anytype) void {
    _ = &_Replacement;
    return;
}
pub const _CRT_MANAGED_HEAP_DEPRECATE = "";
pub inline fn _CRT_OBSOLETE(_NewItem: anytype) void {
    _ = &_NewItem;
    return;
}
pub const _CONST_RETURN = "";
pub const UNALIGNED = "";
pub const _CRT_ALIGN = @compileError("unable to translate macro: undefined identifier `__aligned__`");
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\libc\include\any-windows-any\_mingw.h:415:9
pub const __CRTDECL = @compileError("unable to translate C expr: unexpected token '__cdecl'");
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\libc\include\any-windows-any\_mingw.h:422:9
pub const _ARGMAX = @as(c_int, 100);
pub const _TRUNCATE = __helpers.cast(usize, -@as(c_int, 1));
pub inline fn _CRT_UNUSED(x: anytype) anyopaque {
    _ = &x;
    return __helpers.cast(anyopaque, x);
}
pub const __USE_MINGW_ANSI_STDIO = @as(c_int, 0);
pub const _CRT_glob = @compileError("unable to translate macro: undefined identifier `_dowildcard`");
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\libc\include\any-windows-any\_mingw.h:479:9
pub const __ANONYMOUS_DEFINED = "";
pub const _ANONYMOUS_UNION = __MINGW_EXTENSION;
pub const _ANONYMOUS_STRUCT = __MINGW_EXTENSION;
pub inline fn _UNION_NAME(x: anytype) void {
    _ = &x;
    return;
}
pub inline fn _STRUCT_NAME(x: anytype) void {
    _ = &x;
    return;
}
pub const DUMMYUNIONNAME = "";
pub const DUMMYUNIONNAME1 = "";
pub const DUMMYUNIONNAME2 = "";
pub const DUMMYUNIONNAME3 = "";
pub const DUMMYUNIONNAME4 = "";
pub const DUMMYUNIONNAME5 = "";
pub const DUMMYUNIONNAME6 = "";
pub const DUMMYUNIONNAME7 = "";
pub const DUMMYUNIONNAME8 = "";
pub const DUMMYUNIONNAME9 = "";
pub const DUMMYSTRUCTNAME = "";
pub const DUMMYSTRUCTNAME1 = "";
pub const DUMMYSTRUCTNAME2 = "";
pub const DUMMYSTRUCTNAME3 = "";
pub const DUMMYSTRUCTNAME4 = "";
pub const DUMMYSTRUCTNAME5 = "";
pub inline fn __CRT_UUID_DECL(@"type": anytype, l: anytype, w1: anytype, w2: anytype, b1: anytype, b2: anytype, b3: anytype, b4: anytype, b5: anytype, b6: anytype, b7: anytype, b8: anytype) void {
    _ = &@"type";
    _ = &l;
    _ = &w1;
    _ = &w2;
    _ = &b1;
    _ = &b2;
    _ = &b3;
    _ = &b4;
    _ = &b5;
    _ = &b6;
    _ = &b7;
    _ = &b8;
    return;
}
pub const __MINGW_DEBUGBREAK_IMPL = @compileError("unable to translate macro: undefined identifier `__debugbreak`");
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\libc\include\any-windows-any\_mingw.h:599:9
pub const __MINGW_FASTFAIL_IMPL = @compileError("unable to translate macro: undefined identifier `__fastfail`");
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\libc\include\any-windows-any\_mingw.h:620:9
pub const __MINGW_PREFETCH_IMPL = @compileError("unable to translate macro: undefined identifier `__prefetch`");
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\libc\include\any-windows-any\_mingw.h:644:9
pub const __STDC_SECURE_LIB__ = @as(c_long, 200411);
pub const __GOT_SECURE_LIB__ = __STDC_SECURE_LIB__;
pub const _CRT_PACKING = @as(c_int, 8);
pub const _CRTNOALIAS = "";
pub const _CRTRESTRICT = "";
pub const _SIZE_T_DEFINED = "";
pub const _SSIZE_T_DEFINED = "";
pub const _RSIZE_T_DEFINED = "";
pub const _INTPTR_T_DEFINED = "";
pub const __intptr_t_defined = "";
pub const _UINTPTR_T_DEFINED = "";
pub const __uintptr_t_defined = "";
pub const _PTRDIFF_T_DEFINED = "";
pub const _PTRDIFF_T_ = "";
pub const _WCHAR_T_DEFINED = "";
pub const _WCTYPE_T_DEFINED = "";
pub const _WINT_T = "";
pub const _ERRCODE_DEFINED = "";
pub const _TIME32_T_DEFINED = "";
pub const _TIME64_T_DEFINED = "";
pub const _TIME_T_DEFINED = "";
pub const _CRT_SECURE_CPP_NOTHROW = @compileError("unable to translate macro: undefined identifier `throw`");
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\libc\include\any-windows-any\corecrt.h:143:9
pub inline fn __DEFINE_CPP_OVERLOAD_SECURE_FUNC_0_0(__ret: anytype, __func: anytype, __dsttype: anytype, __dst: anytype) void {
    _ = &__ret;
    _ = &__func;
    _ = &__dsttype;
    _ = &__dst;
    return;
}
pub inline fn __DEFINE_CPP_OVERLOAD_SECURE_FUNC_0_1(__ret: anytype, __func: anytype, __dsttype: anytype, __dst: anytype, __type1: anytype, __arg1: anytype) void {
    _ = &__ret;
    _ = &__func;
    _ = &__dsttype;
    _ = &__dst;
    _ = &__type1;
    _ = &__arg1;
    return;
}
pub inline fn __DEFINE_CPP_OVERLOAD_SECURE_FUNC_0_2(__ret: anytype, __func: anytype, __dsttype: anytype, __dst: anytype, __type1: anytype, __arg1: anytype, __type2: anytype, __arg2: anytype) void {
    _ = &__ret;
    _ = &__func;
    _ = &__dsttype;
    _ = &__dst;
    _ = &__type1;
    _ = &__arg1;
    _ = &__type2;
    _ = &__arg2;
    return;
}
pub inline fn __DEFINE_CPP_OVERLOAD_SECURE_FUNC_0_3(__ret: anytype, __func: anytype, __dsttype: anytype, __dst: anytype, __type1: anytype, __arg1: anytype, __type2: anytype, __arg2: anytype, __type3: anytype, __arg3: anytype) void {
    _ = &__ret;
    _ = &__func;
    _ = &__dsttype;
    _ = &__dst;
    _ = &__type1;
    _ = &__arg1;
    _ = &__type2;
    _ = &__arg2;
    _ = &__type3;
    _ = &__arg3;
    return;
}
pub inline fn __DEFINE_CPP_OVERLOAD_SECURE_FUNC_0_4(__ret: anytype, __func: anytype, __dsttype: anytype, __dst: anytype, __type1: anytype, __arg1: anytype, __type2: anytype, __arg2: anytype, __type3: anytype, __arg3: anytype, __type4: anytype, __arg4: anytype) void {
    _ = &__ret;
    _ = &__func;
    _ = &__dsttype;
    _ = &__dst;
    _ = &__type1;
    _ = &__arg1;
    _ = &__type2;
    _ = &__arg2;
    _ = &__type3;
    _ = &__arg3;
    _ = &__type4;
    _ = &__arg4;
    return;
}
pub inline fn __DEFINE_CPP_OVERLOAD_SECURE_FUNC_1_1(__ret: anytype, __func: anytype, __type0: anytype, __arg0: anytype, __dsttype: anytype, __dst: anytype, __type1: anytype, __arg1: anytype) void {
    _ = &__ret;
    _ = &__func;
    _ = &__type0;
    _ = &__arg0;
    _ = &__dsttype;
    _ = &__dst;
    _ = &__type1;
    _ = &__arg1;
    return;
}
pub inline fn __DEFINE_CPP_OVERLOAD_SECURE_FUNC_1_2(__ret: anytype, __func: anytype, __type0: anytype, __arg0: anytype, __dsttype: anytype, __dst: anytype, __type1: anytype, __arg1: anytype, __type2: anytype, __arg2: anytype) void {
    _ = &__ret;
    _ = &__func;
    _ = &__type0;
    _ = &__arg0;
    _ = &__dsttype;
    _ = &__dst;
    _ = &__type1;
    _ = &__arg1;
    _ = &__type2;
    _ = &__arg2;
    return;
}
pub inline fn __DEFINE_CPP_OVERLOAD_SECURE_FUNC_1_3(__ret: anytype, __func: anytype, __type0: anytype, __arg0: anytype, __dsttype: anytype, __dst: anytype, __type1: anytype, __arg1: anytype, __type2: anytype, __arg2: anytype, __type3: anytype, __arg3: anytype) void {
    _ = &__ret;
    _ = &__func;
    _ = &__type0;
    _ = &__arg0;
    _ = &__dsttype;
    _ = &__dst;
    _ = &__type1;
    _ = &__arg1;
    _ = &__type2;
    _ = &__arg2;
    _ = &__type3;
    _ = &__arg3;
    return;
}
pub inline fn __DEFINE_CPP_OVERLOAD_SECURE_FUNC_2_0(__ret: anytype, __func: anytype, __type1: anytype, __arg1: anytype, __type2: anytype, __arg2: anytype, __dsttype: anytype, __dst: anytype) void {
    _ = &__ret;
    _ = &__func;
    _ = &__type1;
    _ = &__arg1;
    _ = &__type2;
    _ = &__arg2;
    _ = &__dsttype;
    _ = &__dst;
    return;
}
pub inline fn __DEFINE_CPP_OVERLOAD_SECURE_FUNC_0_1_ARGLIST(__ret: anytype, __func: anytype, __vfunc: anytype, __dsttype: anytype, __dst: anytype, __type1: anytype, __arg1: anytype) void {
    _ = &__ret;
    _ = &__func;
    _ = &__vfunc;
    _ = &__dsttype;
    _ = &__dst;
    _ = &__type1;
    _ = &__arg1;
    return;
}
pub inline fn __DEFINE_CPP_OVERLOAD_SECURE_FUNC_0_2_ARGLIST(__ret: anytype, __func: anytype, __vfunc: anytype, __dsttype: anytype, __dst: anytype, __type1: anytype, __arg1: anytype, __type2: anytype, __arg2: anytype) void {
    _ = &__ret;
    _ = &__func;
    _ = &__vfunc;
    _ = &__dsttype;
    _ = &__dst;
    _ = &__type1;
    _ = &__arg1;
    _ = &__type2;
    _ = &__arg2;
    return;
}
pub inline fn __DEFINE_CPP_OVERLOAD_SECURE_FUNC_SPLITPATH(__ret: anytype, __func: anytype, __dsttype: anytype, __src: anytype) void {
    _ = &__ret;
    _ = &__func;
    _ = &__dsttype;
    _ = &__src;
    return;
}
pub const __DEFINE_CPP_OVERLOAD_STANDARD_FUNC_0_0 = @compileError("unable to translate macro: undefined identifier `__func_name`");
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\libc\include\any-windows-any\corecrt.h:284:9
pub const __DEFINE_CPP_OVERLOAD_STANDARD_FUNC_0_1 = @compileError("unable to translate macro: undefined identifier `__func_name`");
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\libc\include\any-windows-any\corecrt.h:286:9
pub const __DEFINE_CPP_OVERLOAD_STANDARD_FUNC_0_2 = @compileError("unable to translate macro: undefined identifier `__func_name`");
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\libc\include\any-windows-any\corecrt.h:288:9
pub const __DEFINE_CPP_OVERLOAD_STANDARD_FUNC_0_3 = @compileError("unable to translate macro: undefined identifier `__func_name`");
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\libc\include\any-windows-any\corecrt.h:290:9
pub const __DEFINE_CPP_OVERLOAD_STANDARD_FUNC_0_4 = @compileError("unable to translate macro: undefined identifier `__func_name`");
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\libc\include\any-windows-any\corecrt.h:292:9
pub inline fn __DEFINE_CPP_OVERLOAD_STANDARD_FUNC_0_0_EX(__ret_type: anytype, __ret_policy: anytype, __decl_spec: anytype, __name: anytype, __sec_name: anytype, __dst_attr: anytype, __dst_type: anytype, __dst: anytype) void {
    _ = &__ret_type;
    _ = &__ret_policy;
    _ = &__decl_spec;
    _ = &__name;
    _ = &__sec_name;
    _ = &__dst_attr;
    _ = &__dst_type;
    _ = &__dst;
    return;
}
pub inline fn __DEFINE_CPP_OVERLOAD_STANDARD_FUNC_0_1_EX(__ret_type: anytype, __ret_policy: anytype, __decl_spec: anytype, __name: anytype, __sec_name: anytype, __dst_attr: anytype, __dst_type: anytype, __dst: anytype, __arg1_type: anytype, __arg1: anytype) void {
    _ = &__ret_type;
    _ = &__ret_policy;
    _ = &__decl_spec;
    _ = &__name;
    _ = &__sec_name;
    _ = &__dst_attr;
    _ = &__dst_type;
    _ = &__dst;
    _ = &__arg1_type;
    _ = &__arg1;
    return;
}
pub inline fn __DEFINE_CPP_OVERLOAD_STANDARD_FUNC_0_2_EX(__ret_type: anytype, __ret_policy: anytype, __decl_spec: anytype, __name: anytype, __sec_name: anytype, __dst_attr: anytype, __dst_type: anytype, __dst: anytype, __arg1_type: anytype, __arg1: anytype, __arg2_type: anytype, __arg2: anytype) void {
    _ = &__ret_type;
    _ = &__ret_policy;
    _ = &__decl_spec;
    _ = &__name;
    _ = &__sec_name;
    _ = &__dst_attr;
    _ = &__dst_type;
    _ = &__dst;
    _ = &__arg1_type;
    _ = &__arg1;
    _ = &__arg2_type;
    _ = &__arg2;
    return;
}
pub inline fn __DEFINE_CPP_OVERLOAD_STANDARD_FUNC_0_3_EX(__ret_type: anytype, __ret_policy: anytype, __decl_spec: anytype, __name: anytype, __sec_name: anytype, __dst_attr: anytype, __dst_type: anytype, __dst: anytype, __arg1_type: anytype, __arg1: anytype, __arg2_type: anytype, __arg2: anytype, __arg3_type: anytype, __arg3: anytype) void {
    _ = &__ret_type;
    _ = &__ret_policy;
    _ = &__decl_spec;
    _ = &__name;
    _ = &__sec_name;
    _ = &__dst_attr;
    _ = &__dst_type;
    _ = &__dst;
    _ = &__arg1_type;
    _ = &__arg1;
    _ = &__arg2_type;
    _ = &__arg2;
    _ = &__arg3_type;
    _ = &__arg3;
    return;
}
pub inline fn __DEFINE_CPP_OVERLOAD_STANDARD_FUNC_0_4_EX(__ret_type: anytype, __ret_policy: anytype, __decl_spec: anytype, __name: anytype, __sec_name: anytype, __dst_attr: anytype, __dst_type: anytype, __dst: anytype, __arg1_type: anytype, __arg1: anytype, __arg2_type: anytype, __arg2: anytype, __arg3_type: anytype, __arg3: anytype, __arg4_type: anytype, __arg4: anytype) void {
    _ = &__ret_type;
    _ = &__ret_policy;
    _ = &__decl_spec;
    _ = &__name;
    _ = &__sec_name;
    _ = &__dst_attr;
    _ = &__dst_type;
    _ = &__dst;
    _ = &__arg1_type;
    _ = &__arg1;
    _ = &__arg2_type;
    _ = &__arg2;
    _ = &__arg3_type;
    _ = &__arg3;
    _ = &__arg4_type;
    _ = &__arg4;
    return;
}
pub const _TAGLC_ID_DEFINED = "";
pub inline fn __crt_typefix(ctype: anytype) void {
    _ = &ctype;
    return;
}
pub const _CRT_USE_WINAPI_FAMILY_DESKTOP_APP = "";
pub const __need_wint_t = "";
pub const __need_wchar_t = "";
pub const __STDC_VERSION_STDDEF_H__ = @as(c_long, 202311);
pub const NULL = __helpers.cast(?*anyopaque, @as(c_int, 0));
pub const offsetof = @compileError("unable to translate macro: undefined identifier `__builtin_offsetof`");
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\compiler\aro\include\stddef.h:18:9
pub const INT8_MIN = -@as(c_int, 128);
pub const INT16_MIN = -__helpers.promoteIntLiteral(c_int, 32768, .decimal);
pub const INT32_MIN = -__helpers.promoteIntLiteral(c_int, 2147483647, .decimal) - @as(c_int, 1);
pub const INT64_MIN = -@as(c_longlong, 9223372036854775807) - @as(c_int, 1);
pub const INT8_MAX = @as(c_int, 127);
pub const INT16_MAX = @as(c_int, 32767);
pub const INT32_MAX = __helpers.promoteIntLiteral(c_int, 2147483647, .decimal);
pub const INT64_MAX = @as(c_longlong, 9223372036854775807);
pub const UINT8_MAX = @as(c_int, 255);
pub const UINT16_MAX = __helpers.promoteIntLiteral(c_int, 65535, .decimal);
pub const UINT32_MAX = __helpers.promoteIntLiteral(c_uint, 0xffffffff, .hex);
pub const UINT64_MAX = @as(c_ulonglong, 0xffffffffffffffff);
pub const INT_LEAST8_MIN = INT8_MIN;
pub const INT_LEAST16_MIN = INT16_MIN;
pub const INT_LEAST32_MIN = INT32_MIN;
pub const INT_LEAST64_MIN = INT64_MIN;
pub const INT_LEAST8_MAX = INT8_MAX;
pub const INT_LEAST16_MAX = INT16_MAX;
pub const INT_LEAST32_MAX = INT32_MAX;
pub const INT_LEAST64_MAX = INT64_MAX;
pub const UINT_LEAST8_MAX = UINT8_MAX;
pub const UINT_LEAST16_MAX = UINT16_MAX;
pub const UINT_LEAST32_MAX = UINT32_MAX;
pub const UINT_LEAST64_MAX = UINT64_MAX;
pub const INT_FAST8_MIN = INT8_MIN;
pub const INT_FAST16_MIN = INT16_MIN;
pub const INT_FAST32_MIN = INT32_MIN;
pub const INT_FAST64_MIN = INT64_MIN;
pub const INT_FAST8_MAX = INT8_MAX;
pub const INT_FAST16_MAX = INT16_MAX;
pub const INT_FAST32_MAX = INT32_MAX;
pub const INT_FAST64_MAX = INT64_MAX;
pub const UINT_FAST8_MAX = UINT8_MAX;
pub const UINT_FAST16_MAX = UINT16_MAX;
pub const UINT_FAST32_MAX = UINT32_MAX;
pub const UINT_FAST64_MAX = UINT64_MAX;
pub const INTPTR_MIN = INT64_MIN;
pub const INTPTR_MAX = INT64_MAX;
pub const UINTPTR_MAX = UINT64_MAX;
pub const INTMAX_MIN = INT64_MIN;
pub const INTMAX_MAX = INT64_MAX;
pub const UINTMAX_MAX = UINT64_MAX;
pub const PTRDIFF_MIN = INT64_MIN;
pub const PTRDIFF_MAX = INT64_MAX;
pub const SIG_ATOMIC_MIN = INT32_MIN;
pub const SIG_ATOMIC_MAX = INT32_MAX;
pub const SIZE_MAX = UINT64_MAX;
pub const WCHAR_MIN = @as(c_uint, 0);
pub const WCHAR_MAX = @as(c_uint, 0xffff);
pub const WINT_MIN = @as(c_uint, 0);
pub const WINT_MAX = @as(c_uint, 0xffff);
pub inline fn INT8_C(val: anytype) @TypeOf((INT_LEAST8_MAX - INT_LEAST8_MAX) + val) {
    _ = &val;
    return (INT_LEAST8_MAX - INT_LEAST8_MAX) + val;
}
pub inline fn INT16_C(val: anytype) @TypeOf((INT_LEAST16_MAX - INT_LEAST16_MAX) + val) {
    _ = &val;
    return (INT_LEAST16_MAX - INT_LEAST16_MAX) + val;
}
pub inline fn INT32_C(val: anytype) @TypeOf((INT_LEAST32_MAX - INT_LEAST32_MAX) + val) {
    _ = &val;
    return (INT_LEAST32_MAX - INT_LEAST32_MAX) + val;
}
pub const INT64_C = __helpers.LL_SUFFIX;
pub inline fn UINT8_C(val: anytype) @TypeOf(val) {
    _ = &val;
    return val;
}
pub inline fn UINT16_C(val: anytype) @TypeOf(val) {
    _ = &val;
    return val;
}
pub const UINT32_C = __helpers.U_SUFFIX;
pub const UINT64_C = __helpers.ULL_SUFFIX;
pub const INTMAX_C = __helpers.LL_SUFFIX;
pub const UINTMAX_C = __helpers.ULL_SUFFIX;
pub const @"bool" = bool;
pub const @"true" = @as(c_int, 1);
pub const @"false" = @as(c_int, 0);
pub const __bool_true_false_are_defined = @as(c_int, 1);
pub const __EMMINTRIN_H = "";
pub const __XMMINTRIN_H = "";
pub const __MMINTRIN_H = "";
pub const _m_empty = _mm_empty;
pub const _m_from_int = _mm_cvtsi32_si64;
pub const _m_from_int64 = _mm_cvtsi64_m64;
pub const _m_to_int = _mm_cvtsi64_si32;
pub const _m_to_int64 = _mm_cvtm64_si64;
pub const _m_packsswb = _mm_packs_pi16;
pub const _m_packssdw = _mm_packs_pi32;
pub const _m_packuswb = _mm_packs_pu16;
pub const _m_punpckhbw = _mm_unpackhi_pi8;
pub const _m_punpckhwd = _mm_unpackhi_pi16;
pub const _m_punpckhdq = _mm_unpackhi_pi32;
pub const _m_punpcklbw = _mm_unpacklo_pi8;
pub const _m_punpcklwd = _mm_unpacklo_pi16;
pub const _m_punpckldq = _mm_unpacklo_pi32;
pub const _m_paddb = _mm_add_pi8;
pub const _m_paddw = _mm_add_pi16;
pub const _m_paddd = _mm_add_pi32;
pub const _m_paddsb = _mm_adds_pi8;
pub const _m_paddsw = _mm_adds_pi16;
pub const _m_paddusb = _mm_adds_pu8;
pub const _m_paddusw = _mm_adds_pu16;
pub const _m_psubb = _mm_sub_pi8;
pub const _m_psubw = _mm_sub_pi16;
pub const _m_psubd = _mm_sub_pi32;
pub const _m_psubsb = _mm_subs_pi8;
pub const _m_psubsw = _mm_subs_pi16;
pub const _m_psubusb = _mm_subs_pu8;
pub const _m_psubusw = _mm_subs_pu16;
pub const _m_pmaddwd = _mm_madd_pi16;
pub const _m_pmulhw = _mm_mulhi_pi16;
pub const _m_pmullw = _mm_mullo_pi16;
pub const _m_psllw = _mm_sll_pi16;
pub const _m_psllwi = _mm_slli_pi16;
pub const _m_pslld = _mm_sll_pi32;
pub const _m_pslldi = _mm_slli_pi32;
pub const _m_psllq = _mm_sll_si64;
pub const _m_psllqi = _mm_slli_si64;
pub const _m_psraw = _mm_sra_pi16;
pub const _m_psrawi = _mm_srai_pi16;
pub const _m_psrad = _mm_sra_pi32;
pub const _m_psradi = _mm_srai_pi32;
pub const _m_psrlw = _mm_srl_pi16;
pub const _m_psrlwi = _mm_srli_pi16;
pub const _m_psrld = _mm_srl_pi32;
pub const _m_psrldi = _mm_srli_pi32;
pub const _m_psrlq = _mm_srl_si64;
pub const _m_psrlqi = _mm_srli_si64;
pub const _m_pand = _mm_and_si64;
pub const _m_pandn = _mm_andnot_si64;
pub const _m_por = _mm_or_si64;
pub const _m_pxor = _mm_xor_si64;
pub const _m_pcmpeqb = _mm_cmpeq_pi8;
pub const _m_pcmpeqw = _mm_cmpeq_pi16;
pub const _m_pcmpeqd = _mm_cmpeq_pi32;
pub const _m_pcmpgtb = _mm_cmpgt_pi8;
pub const _m_pcmpgtw = _mm_cmpgt_pi16;
pub const _m_pcmpgtd = _mm_cmpgt_pi32;
pub const __MM_MALLOC_H = "";
pub const _INC_STDLIB = "";
pub const _INC_CORECRT_WSTDLIB = "";
pub const _GCC_LIMITS_H_ = "";
pub const __CLANG_LIMITS_H = "";
pub const _INC_LIMITS = "";
pub const PATH_MAX = @as(c_int, 260);
pub const MB_LEN_MAX = @as(c_int, 5);
pub const _I8_MIN = -@as(c_int, 127) - @as(c_int, 1);
pub const _I8_MAX = @as(c_int, 127);
pub const _UI8_MAX = @as(c_uint, 0xff);
pub const _I16_MIN = -@as(c_int, 32767) - @as(c_int, 1);
pub const _I16_MAX = @as(c_int, 32767);
pub const _UI16_MAX = @as(c_uint, 0xffff);
pub const _I32_MIN = -__helpers.promoteIntLiteral(c_int, 2147483647, .decimal) - @as(c_int, 1);
pub const _I32_MAX = __helpers.promoteIntLiteral(c_int, 2147483647, .decimal);
pub const _UI32_MAX = __helpers.promoteIntLiteral(c_uint, 0xffffffff, .hex);
pub const _I64_MIN = -@as(c_longlong, 9223372036854775807) - @as(c_int, 1);
pub const _I64_MAX = @as(c_longlong, 9223372036854775807);
pub const _UI64_MAX = @as(c_ulonglong, 0xffffffffffffffff);
pub const SSIZE_MAX = _I64_MAX;
pub const LONG_LONG_MAX = __LONG_LONG_MAX__;
pub const LONG_LONG_MIN = -__LONG_LONG_MAX__ - @as(c_longlong, 1);
pub const ULONG_LONG_MAX = (__LONG_LONG_MAX__ * @as(c_ulonglong, 2)) + @as(c_ulonglong, 1);
pub const SCHAR_MAX = __SCHAR_MAX__;
pub const SHRT_MAX = __SHRT_MAX__;
pub const INT_MAX = __INT_MAX__;
pub const LONG_MAX = __LONG_MAX__;
pub const SCHAR_MIN = -__SCHAR_MAX__ - @as(c_int, 1);
pub const SHRT_MIN = -__SHRT_MAX__ - @as(c_int, 1);
pub const INT_MIN = -__INT_MAX__ - @as(c_int, 1);
pub const LONG_MIN = -__LONG_MAX__ - @as(c_long, 1);
pub const UCHAR_MAX = (__SCHAR_MAX__ * @as(c_int, 2)) + @as(c_int, 1);
pub const USHRT_MAX = (__SHRT_MAX__ * @as(c_int, 2)) + @as(c_int, 1);
pub const UINT_MAX = (__INT_MAX__ * @as(c_uint, 2)) + @as(c_uint, 1);
pub const ULONG_MAX = (__LONG_MAX__ * @as(c_ulong, 2)) + @as(c_ulong, 1);
pub const CHAR_BIT = __CHAR_BIT__;
pub const CHAR_MIN = SCHAR_MIN;
pub const CHAR_MAX = __SCHAR_MAX__;
pub const LLONG_MIN = -__LONG_LONG_MAX__ - @as(c_longlong, 1);
pub const LLONG_MAX = __LONG_LONG_MAX__;
pub const ULLONG_MAX = (__LONG_LONG_MAX__ * @as(c_ulonglong, 2)) + @as(c_ulonglong, 1);
pub const _SECIMP = @compileError("unable to translate macro: undefined identifier `dllimport`");
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\libc\include\any-windows-any\stdlib.h:22:9
pub const EXIT_SUCCESS = @as(c_int, 0);
pub const EXIT_FAILURE = @as(c_int, 1);
pub const _ONEXIT_T_DEFINED = "";
pub const onexit_t = _onexit_t;
pub const _DIV_T_DEFINED = "";
pub const _CRT_DOUBLE_DEC = "";
pub inline fn _PTR_LD(x: anytype) [*c]u8 {
    _ = &x;
    return __helpers.cast([*c]u8, &x.*.ld);
}
pub const RAND_MAX = @as(c_int, 0x7fff);
pub const MB_CUR_MAX = ___mb_cur_max_func();
pub const __mb_cur_max = ___mb_cur_max_func();
pub inline fn __max(a: anytype, b: anytype) @TypeOf(if (__helpers.cast(bool, a > b)) a else b) {
    _ = &a;
    _ = &b;
    return if (__helpers.cast(bool, a > b)) a else b;
}
pub inline fn __min(a: anytype, b: anytype) @TypeOf(if (__helpers.cast(bool, a < b)) a else b) {
    _ = &a;
    _ = &b;
    return if (__helpers.cast(bool, a < b)) a else b;
}
pub const _MAX_PATH = @as(c_int, 260);
pub const _MAX_DRIVE = @as(c_int, 3);
pub const _MAX_DIR = @as(c_int, 256);
pub const _MAX_FNAME = @as(c_int, 256);
pub const _MAX_EXT = @as(c_int, 256);
pub const _OUT_TO_DEFAULT = @as(c_int, 0);
pub const _OUT_TO_STDERR = @as(c_int, 1);
pub const _OUT_TO_MSGBOX = @as(c_int, 2);
pub const _REPORT_ERRMODE = @as(c_int, 3);
pub const _WRITE_ABORT_MSG = @as(c_int, 0x1);
pub const _CALL_REPORTFAULT = @as(c_int, 0x2);
pub const _MAX_ENV = @as(c_int, 32767);
pub const _CRT_ERRNO_DEFINED = "";
pub const errno = _errno().*;
pub const _doserrno = __doserrno().*;
pub const _sys_nerr = __sys_nerr().*;
pub const _sys_errlist = __sys_errlist();
pub const _fmode = __p__fmode().*;
pub const __argc = __p___argc().*;
pub const __argv = __p___argv().*;
pub const __wargv = __p___wargv().*;
pub const _pgmptr = __p__pgmptr().*;
pub const _wpgmptr = __p__wpgmptr().*;
pub const _environ = __p__environ().*;
pub const _wenviron = __p__wenviron().*;
pub const _osplatform = __p__osplatform().*;
pub const _osver = __p__osver().*;
pub const _winver = __p__winver().*;
pub const _winmajor = __p__winmajor().*;
pub const _winminor = __p__winminor().*;
pub inline fn _countof(_Array: anytype) @TypeOf(__helpers.div(__helpers.sizeof(_Array), __helpers.sizeof(_Array[@as(usize, @intCast(@as(c_int, 0)))]))) {
    _ = &_Array;
    return __helpers.div(__helpers.sizeof(_Array), __helpers.sizeof(_Array[@as(usize, @intCast(@as(c_int, 0)))]));
}
pub const _CRT_TERMINATE_DEFINED = "";
pub const _CRT_ABS_DEFINED = "";
pub const _CRT_ATOF_DEFINED = "";
pub const _CRT_ALGO_DEFINED = "";
pub const _CRT_SYSTEM_DEFINED = "";
pub const _CRT_ALLOCATION_DEFINED = "";
pub const _WSTDLIB_DEFINED = "";
pub const _CRT_WSYSTEM_DEFINED = "";
pub const _CVTBUFSIZE = @as(c_int, 309) + @as(c_int, 40);
pub const _CRT_PERROR_DEFINED = "";
pub const _WSTDLIBP_DEFINED = "";
pub const _CRT_WPERROR_DEFINED = "";
pub const sys_errlist = _sys_errlist;
pub const sys_nerr = _sys_nerr;
pub const environ = _environ;
pub const _CRT_SWAB_DEFINED = "";
pub const _INC_STDLIB_S = "";
pub const _QSORT_S_DEFINED = "";
pub const _MALLOC_H_ = "";
pub const _HEAP_MAXREQ = __helpers.promoteIntLiteral(c_int, 0xFFFFFFFFFFFFFFE0, .hex);
pub const _STATIC_ASSERT = @compileError("unable to translate C expr: unexpected token '_Static_assert'");
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\libc\include\any-windows-any\malloc.h:29:9
pub const _HEAPEMPTY = -@as(c_int, 1);
pub const _HEAPOK = -@as(c_int, 2);
pub const _HEAPBADBEGIN = -@as(c_int, 3);
pub const _HEAPBADNODE = -@as(c_int, 4);
pub const _HEAPEND = -@as(c_int, 5);
pub const _HEAPBADPTR = -@as(c_int, 6);
pub const _FREEENTRY = @as(c_int, 0);
pub const _USEDENTRY = @as(c_int, 1);
pub const _HEAPINFO_DEFINED = "";
pub const _amblksiz = __p__amblksiz().*;
pub const _MAX_WAIT_MALLOC_CRT = __helpers.promoteIntLiteral(c_int, 60000, .decimal);
pub const _alloca = @compileError("unable to translate macro: undefined identifier `__builtin_alloca`");
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\libc\include\any-windows-any\malloc.h:163:9
pub const _ALLOCA_S_THRESHOLD = @as(c_int, 1024);
pub const _ALLOCA_S_STACK_MARKER = __helpers.promoteIntLiteral(c_int, 0xCCCC, .hex);
pub const _ALLOCA_S_HEAP_MARKER = __helpers.promoteIntLiteral(c_int, 0xDDDD, .hex);
pub const _ALLOCA_S_MARKER_SIZE = @as(c_int, 16);
pub inline fn _malloca(size: anytype) @TypeOf(if (__helpers.cast(bool, (size + _ALLOCA_S_MARKER_SIZE) <= _ALLOCA_S_THRESHOLD)) _MarkAllocaS(_alloca(size + _ALLOCA_S_MARKER_SIZE), _ALLOCA_S_STACK_MARKER) else _MarkAllocaS(malloc(size + _ALLOCA_S_MARKER_SIZE), _ALLOCA_S_HEAP_MARKER)) {
    _ = &size;
    return if (__helpers.cast(bool, (size + _ALLOCA_S_MARKER_SIZE) <= _ALLOCA_S_THRESHOLD)) _MarkAllocaS(_alloca(size + _ALLOCA_S_MARKER_SIZE), _ALLOCA_S_STACK_MARKER) else _MarkAllocaS(malloc(size + _ALLOCA_S_MARKER_SIZE), _ALLOCA_S_HEAP_MARKER);
}
pub const _FREEA_INLINE = "";
pub const alloca = @compileError("unable to translate macro: undefined identifier `__builtin_alloca`");
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\libc\include\any-windows-any\malloc.h:238:9
pub inline fn _mm_load_ps1(p: anytype) @TypeOf(_mm_load1_ps(p)) {
    _ = &p;
    return _mm_load1_ps(p);
}
pub const _MM_HINT_ET0 = @as(c_int, 7);
pub const _MM_HINT_ET1 = @as(c_int, 6);
pub const _MM_HINT_T0 = @as(c_int, 3);
pub const _MM_HINT_T1 = @as(c_int, 2);
pub const _MM_HINT_T2 = @as(c_int, 1);
pub const _MM_HINT_NTA = @as(c_int, 0);
pub const _mm_prefetch = @compileError("unable to translate macro: undefined identifier `__builtin_prefetch`");
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\xmmintrin.h:2212:9
pub const _mm_extract_pi16 = @compileError("unable to translate macro: undefined identifier `__builtin_ia32_vec_ext_v4hi`");
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\xmmintrin.h:2292:9
pub const _mm_insert_pi16 = @compileError("unable to translate macro: undefined identifier `__builtin_ia32_vec_set_v4hi`");
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\xmmintrin.h:2323:9
pub const _mm_shuffle_pi16 = @compileError("unable to translate macro: undefined identifier `__builtin_shufflevector`");
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\xmmintrin.h:2469:9
pub const _mm_shuffle_ps = @compileError("unable to translate macro: undefined identifier `__builtin_ia32_shufps`");
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\xmmintrin.h:2732:9
pub const _CMP_EQ_OQ = @as(c_int, 0x00);
pub const _CMP_LT_OS = @as(c_int, 0x01);
pub const _CMP_LE_OS = @as(c_int, 0x02);
pub const _CMP_UNORD_Q = @as(c_int, 0x03);
pub const _CMP_NEQ_UQ = @as(c_int, 0x04);
pub const _CMP_NLT_US = @as(c_int, 0x05);
pub const _CMP_NLE_US = @as(c_int, 0x06);
pub const _CMP_ORD_Q = @as(c_int, 0x07);
pub const _mm_cmp_ps = @compileError("unable to translate macro: undefined identifier `__builtin_ia32_cmpps`");
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\xmmintrin.h:3054:9
pub const _mm_cmp_ss = @compileError("unable to translate macro: undefined identifier `__builtin_ia32_cmpss`");
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\xmmintrin.h:3089:9
pub const _MM_ALIGN16 = @compileError("unable to translate macro: undefined identifier `aligned`");
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\xmmintrin.h:3092:9
pub inline fn _MM_SHUFFLE(z: anytype, y: anytype, x: anytype, w: anytype) @TypeOf((((z << @as(c_int, 6)) | (y << @as(c_int, 4))) | (x << @as(c_int, 2))) | w) {
    _ = &z;
    _ = &y;
    _ = &x;
    _ = &w;
    return (((z << @as(c_int, 6)) | (y << @as(c_int, 4))) | (x << @as(c_int, 2))) | w;
}
pub const _MM_EXCEPT_INVALID = @as(c_uint, 0x0001);
pub const _MM_EXCEPT_DENORM = @as(c_uint, 0x0002);
pub const _MM_EXCEPT_DIV_ZERO = @as(c_uint, 0x0004);
pub const _MM_EXCEPT_OVERFLOW = @as(c_uint, 0x0008);
pub const _MM_EXCEPT_UNDERFLOW = @as(c_uint, 0x0010);
pub const _MM_EXCEPT_INEXACT = @as(c_uint, 0x0020);
pub const _MM_EXCEPT_MASK = @as(c_uint, 0x003f);
pub const _MM_MASK_INVALID = @as(c_uint, 0x0080);
pub const _MM_MASK_DENORM = @as(c_uint, 0x0100);
pub const _MM_MASK_DIV_ZERO = @as(c_uint, 0x0200);
pub const _MM_MASK_OVERFLOW = @as(c_uint, 0x0400);
pub const _MM_MASK_UNDERFLOW = @as(c_uint, 0x0800);
pub const _MM_MASK_INEXACT = @as(c_uint, 0x1000);
pub const _MM_MASK_MASK = @as(c_uint, 0x1f80);
pub const _MM_ROUND_NEAREST = @as(c_uint, 0x0000);
pub const _MM_ROUND_DOWN = @as(c_uint, 0x2000);
pub const _MM_ROUND_UP = @as(c_uint, 0x4000);
pub const _MM_ROUND_TOWARD_ZERO = @as(c_uint, 0x6000);
pub const _MM_ROUND_MASK = @as(c_uint, 0x6000);
pub const _MM_FLUSH_ZERO_MASK = @as(c_uint, 0x8000);
pub const _MM_FLUSH_ZERO_ON = @as(c_uint, 0x8000);
pub const _MM_FLUSH_ZERO_OFF = @as(c_uint, 0x0000);
pub inline fn _MM_GET_EXCEPTION_MASK() @TypeOf(_mm_getcsr() & _MM_MASK_MASK) {
    return _mm_getcsr() & _MM_MASK_MASK;
}
pub inline fn _MM_GET_EXCEPTION_STATE() @TypeOf(_mm_getcsr() & _MM_EXCEPT_MASK) {
    return _mm_getcsr() & _MM_EXCEPT_MASK;
}
pub inline fn _MM_GET_FLUSH_ZERO_MODE() @TypeOf(_mm_getcsr() & _MM_FLUSH_ZERO_MASK) {
    return _mm_getcsr() & _MM_FLUSH_ZERO_MASK;
}
pub inline fn _MM_GET_ROUNDING_MODE() @TypeOf(_mm_getcsr() & _MM_ROUND_MASK) {
    return _mm_getcsr() & _MM_ROUND_MASK;
}
pub inline fn _MM_SET_EXCEPTION_MASK(x: anytype) @TypeOf(_mm_setcsr((_mm_getcsr() & ~_MM_MASK_MASK) | x)) {
    _ = &x;
    return _mm_setcsr((_mm_getcsr() & ~_MM_MASK_MASK) | x);
}
pub inline fn _MM_SET_EXCEPTION_STATE(x: anytype) @TypeOf(_mm_setcsr((_mm_getcsr() & ~_MM_EXCEPT_MASK) | x)) {
    _ = &x;
    return _mm_setcsr((_mm_getcsr() & ~_MM_EXCEPT_MASK) | x);
}
pub inline fn _MM_SET_FLUSH_ZERO_MODE(x: anytype) @TypeOf(_mm_setcsr((_mm_getcsr() & ~_MM_FLUSH_ZERO_MASK) | x)) {
    _ = &x;
    return _mm_setcsr((_mm_getcsr() & ~_MM_FLUSH_ZERO_MASK) | x);
}
pub inline fn _MM_SET_ROUNDING_MODE(x: anytype) @TypeOf(_mm_setcsr((_mm_getcsr() & ~_MM_ROUND_MASK) | x)) {
    _ = &x;
    return _mm_setcsr((_mm_getcsr() & ~_MM_ROUND_MASK) | x);
}
pub const _MM_TRANSPOSE4_PS = @compileError("unable to translate macro: undefined identifier `tmp3`");
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\xmmintrin.h:3132:9
pub const _m_pextrw = _mm_extract_pi16;
pub const _m_pinsrw = _mm_insert_pi16;
pub const _m_pmaxsw = _mm_max_pi16;
pub const _m_pmaxub = _mm_max_pu8;
pub const _m_pminsw = _mm_min_pi16;
pub const _m_pminub = _mm_min_pu8;
pub const _m_pmovmskb = _mm_movemask_pi8;
pub const _m_pmulhuw = _mm_mulhi_pu16;
pub const _m_pshufw = _mm_shuffle_pi16;
pub const _m_maskmovq = _mm_maskmove_si64;
pub const _m_pavgb = _mm_avg_pu8;
pub const _m_pavgw = _mm_avg_pu16;
pub const _m_psadbw = _mm_sad_pu8;
pub const _m_ = @compileError("unable to translate macro: undefined identifier `_mm_`");
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\xmmintrin.h:3159:9
pub const __zext128 = @compileError("unable to translate macro: undefined identifier `__builtin_shufflevector`");
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:58:9
pub inline fn _mm_load_pd1(dp: anytype) @TypeOf(_mm_load1_pd(dp)) {
    _ = &dp;
    return _mm_load1_pd(dp);
}
pub const _mm_slli_si128 = @compileError("unable to translate macro: undefined identifier `__builtin_ia32_pslldqi128_byteshift`");
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:2747:9
pub const _mm_bslli_si128 = @compileError("unable to translate macro: undefined identifier `__builtin_ia32_pslldqi128_byteshift`");
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:2751:9
pub const _mm_srli_si128 = @compileError("unable to translate macro: undefined identifier `__builtin_ia32_psrldqi128_byteshift`");
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:2956:9
pub const _mm_bsrli_si128 = @compileError("unable to translate macro: undefined identifier `__builtin_ia32_psrldqi128_byteshift`");
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:2960:9
pub const _mm_extract_epi16 = @compileError("unable to translate macro: undefined identifier `__builtin_ia32_vec_ext_v8hi`");
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:4239:9
pub const _mm_insert_epi16 = @compileError("unable to translate macro: undefined identifier `__builtin_ia32_vec_set_v8hi`");
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:4267:9
pub const _mm_shuffle_epi32 = @compileError("unable to translate macro: undefined identifier `__builtin_ia32_pshufd`");
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:4319:9
pub const _mm_shufflelo_epi16 = @compileError("unable to translate macro: undefined identifier `__builtin_ia32_pshuflw`");
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:4352:9
pub const _mm_shufflehi_epi16 = @compileError("unable to translate macro: undefined identifier `__builtin_ia32_pshufhw`");
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:4385:9
pub const _mm_shuffle_pd = @compileError("unable to translate macro: undefined identifier `__builtin_ia32_shufpd`");
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:4735:9
pub const _mm_cmp_pd = @compileError("unable to translate macro: undefined identifier `__builtin_ia32_cmppd`");
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:4867:9
pub const _mm_cmp_sd = @compileError("unable to translate macro: undefined identifier `__builtin_ia32_cmpsd`");
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\include\emmintrin.h:4903:9
pub inline fn _MM_SHUFFLE2(x: anytype, y: anytype) @TypeOf((x << @as(c_int, 1)) | y) {
    _ = &x;
    _ = &y;
    return (x << @as(c_int, 1)) | y;
}
pub const _MM_DENORMALS_ZERO_ON = @as(c_uint, 0x0040);
pub const _MM_DENORMALS_ZERO_OFF = @as(c_uint, 0x0000);
pub const _MM_DENORMALS_ZERO_MASK = @as(c_uint, 0x0040);
pub inline fn _MM_GET_DENORMALS_ZERO_MODE() @TypeOf(_mm_getcsr() & _MM_DENORMALS_ZERO_MASK) {
    return _mm_getcsr() & _MM_DENORMALS_ZERO_MASK;
}
pub inline fn _MM_SET_DENORMALS_ZERO_MODE(x: anytype) @TypeOf(_mm_setcsr((_mm_getcsr() & ~_MM_DENORMALS_ZERO_MASK) | x)) {
    _ = &x;
    return _mm_setcsr((_mm_getcsr() & ~_MM_DENORMALS_ZERO_MASK) | x);
}
pub const CLAY_HEADER = "";
pub inline fn CLAY_WASM_EXPORT(@"null": anytype) void {
    _ = &@"null";
    return;
}
pub const CLAY_DLL_EXPORT = "";
pub inline fn CLAY__MAX(x: anytype, y: anytype) @TypeOf(if (__helpers.cast(bool, x > y)) x else y) {
    _ = &x;
    _ = &y;
    return if (__helpers.cast(bool, x > y)) x else y;
}
pub inline fn CLAY__MIN(x: anytype, y: anytype) @TypeOf(if (__helpers.cast(bool, x < y)) x else y) {
    _ = &x;
    _ = &y;
    return if (__helpers.cast(bool, x < y)) x else y;
}
pub const CLAY_TEXT_CONFIG = @compileError("unable to translate C expr: unexpected token '__VA_ARGS__'");
// third_party\clay\clay.h:56:9
pub const CLAY_BORDER_OUTSIDE = @compileError("unable to translate C expr: unexpected token '{'");
// third_party\clay\clay.h:58:9
pub const CLAY_BORDER_ALL = @compileError("unable to translate C expr: unexpected token '{'");
// third_party\clay\clay.h:60:9
pub inline fn CLAY_CORNER_RADIUS(radius: anytype) CLAY__INIT(Clay_CornerRadius) {
    _ = &radius;
    return @import("std").mem.zeroInit(CLAY__INIT(Clay_CornerRadius), .{ radius, radius, radius, radius });
}
pub const CLAY_PADDING_ALL = @compileError("unable to translate C expr: unexpected token '{'");
// third_party\clay\clay.h:64:9
pub const CLAY_SIZING_FIT = @compileError("unable to translate C expr: unexpected token '{'");
// third_party\clay\clay.h:66:9
pub const CLAY_SIZING_GROW = @compileError("unable to translate C expr: unexpected token '{'");
// third_party\clay\clay.h:68:9
pub const CLAY_SIZING_FIXED = @compileError("unable to translate C expr: unexpected token '{'");
// third_party\clay\clay.h:70:9
pub const CLAY_SIZING_PERCENT = @compileError("unable to translate C expr: unexpected token '{'");
// third_party\clay\clay.h:72:9
pub inline fn CLAY_ID(label: anytype) @TypeOf(CLAY_IDI(label, @as(c_int, 0))) {
    _ = &label;
    return CLAY_IDI(label, @as(c_int, 0));
}
pub inline fn CLAY_SID(label: anytype) @TypeOf(CLAY_SIDI(label, @as(c_int, 0))) {
    _ = &label;
    return CLAY_SIDI(label, @as(c_int, 0));
}
pub inline fn CLAY_IDI(label: anytype, index: anytype) @TypeOf(CLAY_SIDI(CLAY_STRING(label), index)) {
    _ = &label;
    _ = &index;
    return CLAY_SIDI(CLAY_STRING(label), index);
}
pub inline fn CLAY_SIDI(label: anytype, index: anytype) @TypeOf(Clay__HashString(label, index, @as(c_int, 0))) {
    _ = &label;
    _ = &index;
    return Clay__HashString(label, index, @as(c_int, 0));
}
pub inline fn CLAY_ID_LOCAL(label: anytype) @TypeOf(CLAY_IDI_LOCAL(label, @as(c_int, 0))) {
    _ = &label;
    return CLAY_IDI_LOCAL(label, @as(c_int, 0));
}
pub inline fn CLAY_SID_LOCAL(label: anytype) @TypeOf(CLAY_SIDI_LOCAL(label, @as(c_int, 0))) {
    _ = &label;
    return CLAY_SIDI_LOCAL(label, @as(c_int, 0));
}
pub inline fn CLAY_IDI_LOCAL(label: anytype, index: anytype) @TypeOf(CLAY_SIDI_LOCAL(CLAY_STRING(label), index)) {
    _ = &label;
    _ = &index;
    return CLAY_SIDI_LOCAL(CLAY_STRING(label), index);
}
pub inline fn CLAY_SIDI_LOCAL(label: anytype, index: anytype) @TypeOf(Clay__HashString(label, index, Clay__GetParentElementId())) {
    _ = &label;
    _ = &index;
    return Clay__HashString(label, index, Clay__GetParentElementId());
}
pub inline fn CLAY__STRING_LENGTH(s: anytype) @TypeOf(__helpers.div(__helpers.sizeof(s), __helpers.sizeof(s[@as(usize, @intCast(@as(c_int, 0)))])) - __helpers.sizeof(s[@as(usize, @intCast(@as(c_int, 0)))])) {
    _ = &s;
    return __helpers.div(__helpers.sizeof(s), __helpers.sizeof(s[@as(usize, @intCast(@as(c_int, 0)))])) - __helpers.sizeof(s[@as(usize, @intCast(@as(c_int, 0)))]);
}
pub inline fn CLAY__ENSURE_STRING_LITERAL(x: anytype) @TypeOf("" ++ x ++ "") {
    _ = &x;
    return "" ++ x ++ "";
}
pub inline fn CLAY_STRING(string: anytype) CLAY__INIT(Clay_String) {
    _ = &string;
    return @import("std").mem.zeroInit(CLAY__INIT(Clay_String), .{
        .isStaticallyAllocated = @"true",
        .length = CLAY__STRING_LENGTH(CLAY__ENSURE_STRING_LITERAL(string)),
        .chars = string,
    });
}
pub const CLAY_STRING_CONST = @compileError("unable to translate C expr: unexpected token '{'");
// third_party\clay\clay.h:101:9
pub const CLAY = @compileError("unable to translate C expr: unexpected token 'for'");
// third_party\clay\clay.h:135:9
pub const CLAY__WRAPPER_TYPE = @compileError("unable to translate macro: undefined identifier `Clay__`");
// third_party\clay\clay.h:147:9
pub const CLAY__WRAPPER_STRUCT = @compileError("unable to translate macro: undefined identifier `wrapped`");
// third_party\clay\clay.h:148:9
pub const CLAY__CONFIG_WRAPPER = @compileError("unable to translate C expr: unexpected token '__VA_ARGS__'");
// third_party\clay\clay.h:149:9
pub inline fn CLAY_TEXT(text: anytype, textConfig: anytype) @TypeOf(Clay__OpenTextElement(text, textConfig)) {
    _ = &text;
    _ = &textConfig;
    return Clay__OpenTextElement(text, textConfig);
}
pub inline fn CLAY__INIT(@"type": anytype) @TypeOf(@"type") {
    _ = &@"type";
    return @"type";
}
pub const CLAY_PACKED_ENUM = @compileError("unable to translate macro: undefined identifier `__packed__`");
// third_party\clay\clay.h:168:9
pub const CLAY__DEFAULT_STRUCT = @compileError("unable to translate C expr: unexpected token '{'");
// third_party\clay\clay.h:174:9
pub const __STB_INCLUDE_STB_TRUETYPE_H__ = "";
pub const STBTT_DEF = @compileError("unable to translate C expr: unexpected token 'extern'");
// third_party\stb\stb_truetype.h:505:9
pub inline fn STBTT_POINT_SIZE(x: anytype) @TypeOf(-x) {
    _ = &x;
    return -x;
}
pub const stbtt_vertex_type = c_short;
pub const STBTT_MACSTYLE_DONTCARE = @as(c_int, 0);
pub const STBTT_MACSTYLE_BOLD = @as(c_int, 1);
pub const STBTT_MACSTYLE_ITALIC = @as(c_int, 2);
pub const STBTT_MACSTYLE_UNDERSCORE = @as(c_int, 4);
pub const STBTT_MACSTYLE_NONE = @as(c_int, 8);
pub const threadlocaleinfostruct = struct_threadlocaleinfostruct;
pub const threadmbcinfostruct = struct_threadmbcinfostruct;
pub const __lc_time_data = struct___lc_time_data;
pub const localeinfo_struct = struct_localeinfo_struct;
pub const tagLC_ID = struct_tagLC_ID;
pub const _div_t = struct__div_t;
pub const _ldiv_t = struct__ldiv_t;
pub const _heapinfo = struct__heapinfo;
pub const Clay_ScrollRenderData = struct_Clay_ScrollRenderData;
