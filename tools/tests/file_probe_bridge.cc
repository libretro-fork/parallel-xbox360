/* Native path/enum ABI only. No Emulator object, setup, CPU or GPU. */
#include "xenia/emulator.h"
#include "xenia/base/string.h"
#include "xenia/base/logging.h"
extern "C" int xe_file_signature_native(const char *path) {
 static bool initialized=false;
 if(!initialized) {xe::InitializeLogging("file_probe_test");initialized=true;}
 return static_cast<int>(xe::Emulator::GetFileSignature(xe::to_path(path)));
}
