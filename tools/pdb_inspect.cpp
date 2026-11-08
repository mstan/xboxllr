// Optional Windows DIA SDK frontend. It reads local PDB2/PDB7 files without
// registering COM classes and writes UTF-8 JSONL for native_guest_symbols.py.
// No DIA dependency is needed by portable JSON lookup or normal title builds.
#include <cstdio>
#include <dia2.h>
#include <map>
#include <set>
#include <string>
#include <vector>
#include <windows.h>

static std::set<DWORD> seen;
static std::map<DWORD, unsigned> counts;
static std::vector<DWORD> type_refs;
static void quoted(const wchar_t *value) {
  std::string utf8;
  if (value) {
    int n = WideCharToMultiByte(CP_UTF8, 0, value, -1, nullptr, 0, nullptr,
                                nullptr);
    utf8.resize(n);
    WideCharToMultiByte(CP_UTF8, 0, value, -1, utf8.data(), n, nullptr,
                        nullptr);
    if (!utf8.empty())
      utf8.pop_back();
  }
  putchar('"');
  for (unsigned char c : utf8) {
    if (c == '"' || c == '\\') {
      putchar('\\');
      putchar(c);
    } else if (c < 32)
      printf("\\u%04x", c);
    else
      putchar(c);
  }
  putchar('"');
}
static void record(IDiaSymbol *symbol, DWORD parent = 0) {
  DWORD id = 0, tag = 0, section = 0, offset = 0, rva = 0, kind = 0,
        typeid_ = 0, location = 0, basic = 0, count = 0, calling = 0;
  ULONGLONG length = 0;
  LONG field_offset = 0;
  symbol->get_symIndexId(&id);
  if (!seen.insert(id).second)
    return;
  symbol->get_symTag(&tag);
  symbol->get_addressSection(&section);
  symbol->get_addressOffset(&offset);
  symbol->get_relativeVirtualAddress(&rva);
  symbol->get_length(&length);
  symbol->get_dataKind(&kind);
  symbol->get_offset(&field_offset);
  symbol->get_locationType(&location);
  IDiaSymbol *type = nullptr;
  if (SUCCEEDED(symbol->get_type(&type)) && type) {
    type->get_symIndexId(&typeid_);
    type_refs.push_back(typeid_);
    type->Release();
  }
  symbol->get_baseType(&basic);
  symbol->get_count(&count);
  symbol->get_callingConvention(&calling);
  BSTR name = nullptr, pretty = nullptr;
  symbol->get_name(&name);
  symbol->get_undecoratedName(&pretty);
  printf("{\"id\":%lu,\"parent\":%lu,\"tag\":%lu,\"section\":%lu,\"offset\":%"
         "lu,\"rva\":%lu,\"length\":%llu,\"data_kind\":%lu,\"location\":%lu,"
         "\"field_offset\":%ld,\"type\":%lu,\"basic\":%lu,\"count\":%lu,"
         "\"calling\":%lu,\"name\":",
         id, parent, tag, section, offset, rva, length, kind, location,
         field_offset, typeid_, basic, count, calling);
  quoted(name);
  printf(",\"undecorated\":");
  quoted(pretty);
  printf("}\n");
  SysFreeString(name);
  SysFreeString(pretty);
  counts[tag]++;
}
static void children(IDiaSymbol *parent, enum SymTagEnum tag,
                     bool fields = false) {
  DWORD parent_id = 0;
  parent->get_symIndexId(&parent_id);
  IDiaEnumSymbols *items = nullptr;
  if (FAILED(parent->findChildren(tag, nullptr, nsNone, &items)) || !items)
    return;
  IDiaSymbol *item = nullptr;
  ULONG got = 0;
  while (items->Next(1, &item, &got) == S_OK && got) {
    record(item, parent_id);
    if (fields) {
      children(item, SymTagData);
      children(item, SymTagBaseClass);
    }
    item->Release();
    item = nullptr;
  }
  items->Release();
}
int wmain(int argc, wchar_t **argv) {
  if (argc != 3) {
    fprintf(stderr, "usage: pdb_inspect msdia140.dll input.pdb\n");
    return 2;
  }
  CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  HMODULE dll = LoadLibraryW(argv[1]);
  if (!dll) {
    fprintf(stderr, "DIA DLL load failed %lu\n", GetLastError());
    return 2;
  }
  using Create = HRESULT(WINAPI *)(REFCLSID, REFIID, LPVOID *);
  auto create = (Create)GetProcAddress(dll, "DllGetClassObject");
  IClassFactory *factory = nullptr;
  HRESULT hr = create ? create(__uuidof(DiaSource), __uuidof(IClassFactory),
                               (void **)&factory)
                      : E_FAIL;
  if (FAILED(hr)) {
    fprintf(stderr, "DIA factory failed %08lx\n", hr);
    return 2;
  }
  IDiaDataSource *source = nullptr;
  hr = factory->CreateInstance(nullptr, __uuidof(IDiaDataSource),
                               (void **)&source);
  factory->Release();
  if (FAILED(hr)) {
    fprintf(stderr, "DIA source failed %08lx\n", hr);
    return 2;
  }
  hr = source->loadDataFromPdb(argv[2]);
  if (FAILED(hr)) {
    fprintf(stderr, "PDB load failed %08lx\n", hr);
    source->Release();
    return 3;
  }
  IDiaSession *session = nullptr;
  hr = source->openSession(&session);
  if (FAILED(hr)) {
    fprintf(stderr, "PDB session failed %08lx\n", hr);
    return 3;
  }
  IDiaSymbol *global = nullptr;
  session->get_globalScope(&global);
  if (!global)
    return 3;
  DWORD age = 0, signature = 0, machine = 0;
  GUID guid = {};
  global->get_age(&age);
  global->get_signature(&signature);
  global->get_guid(&guid);
  global->get_machineType(&machine);
  wchar_t guid_text[40] = {};
  StringFromGUID2(guid, guid_text, 40);
  printf(
      "{\"pdb\":true,\"age\":%lu,\"signature\":%lu,\"machine\":%lu,\"guid\":",
      age, signature, machine);
  quoted(guid_text);
  printf("}\n");
  children(global, SymTagPublicSymbol);
  children(global, SymTagFunction);
  children(global, SymTagData);
  children(global, SymTagUDT, true);
  children(global, SymTagEnum, true);
  children(global, SymTagTypedef);
  IDiaEnumSymbols *compilands = nullptr;
  global->findChildren(SymTagCompiland, nullptr, nsNone, &compilands);
  if (compilands) {
    IDiaSymbol *item = nullptr;
    ULONG got = 0;
    while (compilands->Next(1, &item, &got) == S_OK && got) {
      children(item, SymTagFunction);
      children(item, SymTagData);
      item->Release();
    }
    compilands->Release();
  }
  for (size_t index = 0; index < type_refs.size(); ++index) {
    DWORD id = type_refs[index];
    if (seen.count(id))
      continue;
    IDiaSymbol *type = nullptr;
    if (SUCCEEDED(session->symbolById(id, &type)) && type) {
      record(type);
      DWORD tag = 0;
      type->get_symTag(&tag);
      if (tag == SymTagUDT || tag == SymTagEnum) {
        children(type, SymTagData);
        children(type, SymTagBaseClass);
      }
      if (tag == SymTagFunctionType)
        children(type, SymTagFunctionArgType);
      type->Release();
    }
  }
  fprintf(stderr, "PDB read:");
  for (auto pair : counts)
    fprintf(stderr, " tag%lu=%u", pair.first, pair.second);
  fprintf(stderr, "\n");
  global->Release();
  session->Release();
  source->Release();
  CoUninitialize();
  return 0;
}
