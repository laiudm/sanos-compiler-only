#define CONFIG_TCCDIR "/usr"				// dcm: overridden by TCC_OPTION_B = "B" (-B dir       set tcc internal library path) or by env variable CCPREFIX
#define CONFIG_TCC_CRT_PREFIX "/usr/lib"	// dcm: only ever referenced in elf.c
#define GCC_MAJOR 2							// dcm: never used
#define HOST_I386 1							// dcm: never used
#define TCC_VERSION "0.9.24"				// dcm: defines symbol "_TCC_VER" to be avail. to C source code being compiled. Also output as header.
#define TCC_TARGET_PE						// dcm: never used
#undef _WIN32

// dcm: __linux, _MSC_VER aren't defined or used anywhere. So TCC_PLATFORM defaults to be set to "Sanos".
// dcm: TCC_FLATFORM is only ever used in compiler.c to define symbol _TCC_PLATFORM to be available to the C source code being compiled.
#if defined(__linux)
#define TCC_PLATFORM "Linux"
#elif defined(_MSC_VER)
#define TCC_PLATFORM "Windows"
#else
#define TCC_PLATFORM "Sanos"
#endif

