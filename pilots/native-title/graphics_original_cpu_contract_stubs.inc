/* This fixture has authored callbacks rather than generated game CPU units.
 * Keep original CPU services linkable but fail if a test unexpectedly calls
 * one: their real behavior belongs to original-image integration evidence.
 * These definitions are never compiled into a title executable. */
#ifdef PILOT_D3D_NATIVE_CPU_SERVICE_VERSION
#define TEST_ORIGINAL_GRAPHICS_CPU(name) \
    void pilot_original_d3d_##name(void) {stop("contract fixture invoked original graphics CPU service: " #name,0);}
TEST_ORIGINAL_GRAPHICS_CPU(D3D_GetAdapterModeCount)
TEST_ORIGINAL_GRAPHICS_CPU(D3D_EnumAdapterModes)
TEST_ORIGINAL_GRAPHICS_CPU(CMiniport_GetDisplayCapabilities)
TEST_ORIGINAL_GRAPHICS_CPU(D3D_GetDeviceCaps)
TEST_ORIGINAL_GRAPHICS_CPU(D3D_CheckDeviceFormat)
TEST_ORIGINAL_GRAPHICS_CPU(D3DDevice_SetLight)
TEST_ORIGINAL_GRAPHICS_CPU(D3DDevice_LightEnable)
TEST_ORIGINAL_GRAPHICS_CPU(D3DDevice_SetMaterial)
TEST_ORIGINAL_GRAPHICS_CPU(D3DDevice_GetGammaRamp)
TEST_ORIGINAL_GRAPHICS_CPU(D3D_AllocContiguousMemory)
TEST_ORIGINAL_GRAPHICS_CPU(D3DResource_Register)
TEST_ORIGINAL_GRAPHICS_CPU(D3DDevice_GetTile)
TEST_ORIGINAL_GRAPHICS_CPU(D3DSurface_GetDesc)
TEST_ORIGINAL_GRAPHICS_CPU(Get2DSurfaceDesc)
TEST_ORIGINAL_GRAPHICS_CPU(D3DResource_GetType)
#undef TEST_ORIGINAL_GRAPHICS_CPU
#endif
