/* Fixed MinGW Windows configuration for the core. MSVC C89-compatible. */
#ifndef XE_LIBUSB_MINGW_CONFIG_H
#define XE_LIBUSB_MINGW_CONFIG_H
#define DEFAULT_VISIBILITY
#define ENABLE_LOGGING 1
#define PLATFORM_WINDOWS 1
#define PRINTF_FORMAT(a, b)
#endif
