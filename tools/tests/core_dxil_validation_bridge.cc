/* Windows SDK COM ABI only. The C89 runner owns files, buffers and test loops.
 * Optional development tool: no compiler/validator DLL is needed by the core. */
#include <windows.h>
#include <dxcapi.h>
#include <new>
struct ValidatorContext { HMODULE library; IDxcValidator* validator; };
class BorrowedBlob final : public IDxcBlob {
 public:
  BorrowedBlob(void* data, size_t size) : data_(data), size_(size) {}
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** result) override {
    *result = nullptr;
    if (id != __uuidof(IUnknown) && id != __uuidof(IDxcBlob)) return E_NOINTERFACE;
    *result = this; return S_OK;
  }
  ULONG STDMETHODCALLTYPE AddRef() override { return 1; }
  ULONG STDMETHODCALLTYPE Release() override { return 1; }
  LPVOID STDMETHODCALLTYPE GetBufferPointer() override { return data_; }
  SIZE_T STDMETHODCALLTYPE GetBufferSize() override { return size_; }
 private:
  void* data_;
  size_t size_;
};
extern "C" void* xe_test_validator_open(const char* path) {
  auto* context = new (std::nothrow) ValidatorContext{};
  if (!context) return nullptr;
  context->library = LoadLibraryA(path);
  auto create = context->library ? reinterpret_cast<DxcCreateInstanceProc>(
      GetProcAddress(context->library, "DxcCreateInstance")) : nullptr;
  if (!create || FAILED(create(CLSID_DxcValidator, __uuidof(IDxcValidator),
                              reinterpret_cast<void**>(&context->validator)))) {
    if (context->library) FreeLibrary(context->library);
    delete context; return nullptr;
  }
  return context;
}
extern "C" int xe_test_validator_check(void* opaque, void* data, size_t size,
    void (*report)(const char*, size_t)) {
  auto* context = static_cast<ValidatorContext*>(opaque);
  BorrowedBlob blob(data, size);
  IDxcOperationResult* operation = nullptr;
  HRESULT status = E_FAIL;
  HRESULT result = context->validator->Validate(&blob, 0, &operation);
  if (SUCCEEDED(result) && operation) result = operation->GetStatus(&status);
  if (FAILED(result) || FAILED(status)) {
    IDxcBlobEncoding* error = nullptr;
    if (operation && SUCCEEDED(operation->GetErrorBuffer(&error)) && error) {
      report(static_cast<const char*>(error->GetBufferPointer()), error->GetBufferSize());
      error->Release();
    }
  }
  if (operation) operation->Release();
  return SUCCEEDED(result) && SUCCEEDED(status);
}
extern "C" void xe_test_validator_close(void* opaque) {
  auto* context = static_cast<ValidatorContext*>(opaque);
  context->validator->Release(); FreeLibrary(context->library); delete context;
}
