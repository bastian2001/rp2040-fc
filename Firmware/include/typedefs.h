#include <Arduino.h>
#pragma once
// Rust just has better names for these types

// deriving from uint32_t etc. would result in problems with function overloading (e.g. when using the same function for i32 variables and int literals, the compiler expects a function for int and one for i32)
typedef float f32;
typedef double f64;
typedef signed char i8;
typedef signed short i16;
typedef signed int i32;
typedef signed long long i64;
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long long u64;

#define CHECK_TYPE_SIZE(type, expected) static_assert((sizeof(type)) == (expected), "Size of " #type " is not as expected.")