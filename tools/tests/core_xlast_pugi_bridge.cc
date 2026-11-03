/* Interoperability with the parser currently used by the core. */
#include "pugixml.hpp"
extern "C" void* xe_pugi_open(const void* input,size_t size) {
  pugi::xml_document* doc=new pugi::xml_document;
  if(doc->load_buffer(input,size)) return doc;
  delete doc; return nullptr;
}
extern "C" void xe_pugi_close(void* handle) {
  delete static_cast<pugi::xml_document*>(handle);
}
extern "C" const char* xe_pugi_attr(void* handle,const char* path,const char* name) {
  return static_cast<pugi::xml_document*>(handle)->select_node(path).node().attribute(name).value();
}
extern "C" const char* xe_pugi_text(void* handle,const char* path) {
  return static_cast<pugi::xml_document*>(handle)->select_node(path).node().child_value();
}
