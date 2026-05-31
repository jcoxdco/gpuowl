// included from both C++ and OpenCL.

u32 bitposToWord(u64 E, u32 N, u32 offset) { return (u32) (offset * ((u64) N) / E); }
u32 wordToBitpos(u64 E, u32 N, u32 word) { return (u32) ((word * ((u64) E) + (N - 1)) / N); }
