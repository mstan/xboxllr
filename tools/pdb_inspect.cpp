// Optional Windows DIA SDK frontend. It reads local PDB2/PDB7 files without
// registering COM classes and writes UTF-8 JSONL for native_guest_symbols.py.
// No DIA dependency is needed by portable JSON lookup or normal title builds.
#include <cstdio>
#include <dia2.h>
#include <cvconst.h>
#include <map>
#include <set>
#include <string>
#include <vector>
#include <windows.h>

static std::set<DWORD> seen;
static std::map<DWORD, unsigned> counts;
static std::vector<DWORD> type_refs;
static std::set<DWORD> visited_scopes;
static void constant_value(const VARIANT &value) {
  long long signed_value = 0;
  unsigned long long unsigned_value = 0;
  unsigned bits = 0;
  bool is_signed = false;
  switch (value.vt) {
  case VT_I1: signed_value = (signed char)value.cVal; bits = 8; is_signed = true; break;
  case VT_I2: signed_value = value.iVal; bits = 16; is_signed = true; break;
  case VT_I4: signed_value = value.lVal; bits = 32; is_signed = true; break;
  case VT_INT: signed_value = value.intVal; bits = 32; is_signed = true; break;
  case VT_I8: signed_value = value.llVal; bits = 64; is_signed = true; break;
  case VT_UI1: unsigned_value = value.bVal; bits = 8; break;
  case VT_UI2: unsigned_value = value.uiVal; bits = 16; break;
  case VT_UI4: unsigned_value = value.ulVal; bits = 32; break;
  case VT_UINT: unsigned_value = value.uintVal; bits = 32; break;
  case VT_UI8: unsigned_value = value.ullVal; bits = 64; break;
  default:
    printf(",\"constant\":{\"kind\":\"unavailable\",\"variant_type\":%u}", value.vt);
    return;
  }
  // Decimal strings retain all 64 bits through JSON consumers with doubles.
  printf(",\"constant\":{\"kind\":\"integer\",\"bits\":%u,\"signed\":%s,\"value\":\"",
         bits, is_signed ? "true" : "false");
  if (is_signed) printf("%lld", signed_value);
  else printf("%llu", unsigned_value);
  printf("\"}");
}
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
  HRESULT length_hr = symbol->get_length(&length);
  symbol->get_dataKind(&kind);
  HRESULT offset_hr = symbol->get_offset(&field_offset);
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
  if (location == LocIsBitField) {
    DWORD position = 0;
    // S_FALSE is missing metadata, distinct from a real bit position of zero.
    if (symbol->get_bitPosition(&position) == S_OK)
      printf(",\"bit_position\":%lu", position);
    if (length_hr == S_OK)
      printf(",\"bit_width\":%llu", length);
    if (offset_hr == S_OK)
      printf(",\"storage_offset\":%ld", field_offset);
  }
  if (location == LocIsConstant) {
    VARIANT value;
    VariantInit(&value);
    if (symbol->get_value(&value) == S_OK)
      constant_value(value);
    else
      printf(",\"constant\":{\"kind\":\"unavailable\"}");
    VariantClear(&value);
  }
  if (tag == SymTagData) {
    DWORD reg = 0, live_section = 0, live_offset = 0, live_count = 0;
    ULONGLONG live_length = 0;
    BOOL optimized = FALSE;
    if (symbol->get_registerId(&reg) == S_OK)
      printf(",\"register_id\":%lu", reg);
    if (offset_hr == S_OK)
      printf(",\"location_offset\":%ld", field_offset);
    if (symbol->get_isOptimizedAway(&optimized) == S_OK)
      printf(",\"optimized_away\":%s", optimized ? "true" : "false");
    if (symbol->get_isLocationControlFlowDependent(&optimized) == S_OK)
      printf(",\"control_flow_dependent\":%s", optimized ? "true" : "false");
    if (symbol->get_countLiveRanges(&live_count) == S_OK)
      printf(",\"live_range_count\":%lu", live_count);
    if (symbol->get_liveRangeStartAddressSection(&live_section) == S_OK &&
        symbol->get_liveRangeStartAddressOffset(&live_offset) == S_OK &&
        symbol->get_liveRangeLength(&live_length) == S_OK)
      printf(",\"live_section\":%lu,\"live_offset\":%lu,\"live_length\":%llu",
             live_section, live_offset, live_length);
  }
  if (tag == SymTagFunction) {
    DWORD reg = 0;
    BOOL present = FALSE;
    if (symbol->get_framePointerPresent(&present) == S_OK)
      printf(",\"frame_pointer_present\":%s", present ? "true" : "false");
    if (symbol->get_localBasePointerRegisterId(&reg) == S_OK)
      printf(",\"local_base_register\":%lu", reg);
    if (symbol->get_paramBasePointerRegisterId(&reg) == S_OK)
      printf(",\"parameter_base_register\":%lu", reg);
  }
  printf("}\n");
  SysFreeString(name);
  SysFreeString(pretty);
  counts[tag]++;
}
static void scope_children(IDiaSymbol *scope, unsigned depth = 0);
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
    if (tag == SymTagFunction)
      scope_children(item);
    if (fields) {
      children(item, SymTagData);
      children(item, SymTagBaseClass);
    }
    item->Release();
    item = nullptr;
  }
  items->Release();
}
static void scope_children(IDiaSymbol *scope, unsigned depth) {
  DWORD id = 0;
  scope->get_symIndexId(&id);
  if (!visited_scopes.insert(id).second)
    return;
  if (depth >= 128) {
    fprintf(stderr, "lexical scope nesting exceeds limit\n");
    exit(4);
  }
  children(scope, SymTagData);
  children(scope, SymTagFuncDebugStart);
  children(scope, SymTagFuncDebugEnd);
  IDiaEnumSymbols *blocks = nullptr;
  if (FAILED(scope->findChildren(SymTagBlock, nullptr, nsNone, &blocks)) || !blocks)
    return;
  IDiaSymbol *block = nullptr;
  ULONG got = 0;
  while (blocks->Next(1, &block, &got) == S_OK && got) {
    record(block, id);
    scope_children(block, depth + 1);
    block->Release();
    block = nullptr;
  }
  blocks->Release();
}
static void frame_records(IDiaSession *session) {
  IDiaEnumTables *tables = nullptr;
  if (FAILED(session->getEnumTables(&tables)) || !tables)
    return;
  IDiaTable *table = nullptr;
  ULONG got = 0;
  while (tables->Next(1, &table, &got) == S_OK && got) {
    IDiaEnumFrameData *frames = nullptr;
    if (table->QueryInterface(__uuidof(IDiaEnumFrameData), (void **)&frames) == S_OK) {
      IDiaFrameData *frame = nullptr;
      while (frames->Next(1, &frame, &got) == S_OK && got) {
        DWORD number = 0;
        printf("{\"tag\":-1,\"kind\":\"frame\"");
        if (frame->get_addressSection(&number) == S_OK) printf(",\"section\":%lu",number);
        if (frame->get_addressOffset(&number) == S_OK) printf(",\"offset\":%lu",number);
        if (frame->get_relativeVirtualAddress(&number) == S_OK) printf(",\"rva\":%lu",number);
        if (frame->get_lengthBlock(&number) == S_OK) printf(",\"length\":%lu",number);
        if (frame->get_lengthProlog(&number) == S_OK) printf(",\"prolog_length\":%lu",number);
        if (frame->get_lengthLocals(&number) == S_OK) printf(",\"locals_length\":%lu",number);
        if (frame->get_lengthParams(&number) == S_OK) printf(",\"params_length\":%lu",number);
        if (frame->get_lengthSavedRegisters(&number) == S_OK) printf(",\"saved_registers_length\":%lu",number);
        if (frame->get_type(&number) == S_OK) printf(",\"frame_type\":%lu",number);
        BSTR program = nullptr;
        if (frame->get_program(&program) == S_OK && program) {
          printf(",\"program\":");quoted(program);
        }
        SysFreeString(program);
        printf("}\n");
        frame->Release();frame = nullptr;
      }
      frames->Release();
    }
    table->Release();table = nullptr;
  }
  tables->Release();
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
  frame_records(session);
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
