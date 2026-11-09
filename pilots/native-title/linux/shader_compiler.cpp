/* SPDX-License-Identifier: GPL-3.0-or-later
 * vkd3d-utils is an ELF library but its D3D compiler exports and COM blob
 * vtables retain the Microsoft x64 calling convention. DXVK Native's D3D11
 * interfaces use System V. Keep that ABI boundary here, copying the actual
 * compiled DXBC/diagnostics into a native System V blob.
 */
#include <d3dcompiler.h>
#include <atomic>
#include <cstring>
#include <new>
#include <string>
#include <stdexcept>
#include <vector>
#if !defined(__x86_64__)
#error "Qualify the vkd3d compiler ABI before enabling another Linux architecture"
#endif
#define VKD_CALL __attribute__((ms_abi))
struct VkdBlob;
struct VkdBlobTable {
    HRESULT (VKD_CALL *query)(VkdBlob*,const GUID*,void**);
    ULONG (VKD_CALL *add_ref)(VkdBlob*);
    ULONG (VKD_CALL *release)(VkdBlob*);
    void *(VKD_CALL *data)(VkdBlob*);
    SIZE_T (VKD_CALL *size)(VkdBlob*);
};
struct VkdBlob {const VkdBlobTable *table;};
extern "C" HRESULT VKD_CALL vkd_compile(const void*,SIZE_T,LPCSTR,const D3D_SHADER_MACRO*,void*,
    LPCSTR,LPCSTR,UINT,UINT,VkdBlob**,VkdBlob**) __asm__("D3DCompile");
class Blob final:public ID3DBlob {
    std::atomic<ULONG> references{1};
    SIZE_T length;
    std::vector<unsigned char> bytes;
public:
    explicit Blob(SIZE_T size):length(size),bytes(size+1,0){}
    HRESULT QueryInterface(REFIID iid,void **out) override {
        if(!out)return E_POINTER;*out=nullptr;
        if(iid==__uuidof(IUnknown) || iid==__uuidof(ID3DBlob)){*out=this;AddRef();return S_OK;}
        return E_NOINTERFACE;
    }
    ULONG AddRef() override {return ++references;}
    ULONG Release() override {ULONG refs=--references;if(!refs)delete this;return refs;}
    void *GetBufferPointer() override {return bytes.data();}
    SIZE_T GetBufferSize() override {return length;}
};
extern "C" HRESULT pilot_linux_D3DCreateBlob(SIZE_T bytes,ID3DBlob **out)
{
    if(!out)return E_POINTER;*out=nullptr;if(bytes==SIZE_MAX)return E_OUTOFMEMORY;
    try{*out=new Blob(bytes);return S_OK;}catch(const std::bad_alloc&){return E_OUTOFMEMORY;}
    catch(const std::length_error&){return E_OUTOFMEMORY;}
}
static HRESULT copy_blob(VkdBlob *source,ID3DBlob **out)
{
    if(out)*out=nullptr;if(!source)return S_OK;
    HRESULT result=S_OK;
    if(out) {
        SIZE_T size=source->table->size(source);
        result=pilot_linux_D3DCreateBlob(size,out);
        if(SUCCEEDED(result) && size)std::memcpy((*out)->GetBufferPointer(),source->table->data(source),size);
    }
    source->table->release(source);return result;
}
extern "C" HRESULT pilot_linux_D3DCompile(const void *data,SIZE_T size,LPCSTR filename,
    const D3D_SHADER_MACRO *defines,ID3DInclude *include,LPCSTR entry,LPCSTR target,
    UINT flags,UINT effect_flags,ID3DBlob **shader,ID3DBlob **errors)
{
    if(shader)*shader=nullptr;if(errors)*errors=nullptr;
    if(!data || !entry || !target)return E_INVALIDARG;
    if(include)return E_NOTIMPL; /* Shared generated shaders are self-contained. */
    VkdBlob *native_shader=nullptr,*native_errors=nullptr;
    /* vkd3d 2.1 lacks SM4 isnan/isinf lowering. Classify IEEE-754 bits directly,
     * retaining scalar/vector NaN semantics without floating-point folding. */
    std::string source;
    try {
        source="#define isnan(x) ((asuint(x)&0x7fffffffu)>0x7f800000u)\n"
               "#define isinf(x) ((asuint(x)&0x7fffffffu)==0x7f800000u)\n"
               "#define isfinite(x) ((asuint(x)&0x7fffffffu)<0x7f800000u)\n";
        source.append(static_cast<const char*>(data),size);
    }catch(const std::bad_alloc&){return E_OUTOFMEMORY;}
    HRESULT result=vkd_compile(source.data(),source.size(),filename,defines,nullptr,entry,target,flags,effect_flags,&native_shader,&native_errors);
    HRESULT code_copy=copy_blob(native_shader,shader),error_copy=copy_blob(native_errors,errors);
    if(FAILED(code_copy))return code_copy;if(FAILED(error_copy))return error_copy;return result;
}
