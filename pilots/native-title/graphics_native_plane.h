/* Internal R09 ownership contract. Original donor images remain canonical.
 * Each retained native image may own an enhanced presentation companion;
 * aliases share the pair through the existing logical image owner. The
 * graphics lock serializes every call. Enhanced failure never replaces or
 * reconstructs the canonical source image. */
#ifdef PILOT_D3D_PHOBOS
struct PilotNativeViewCache;
struct PilotNativePlane {
    ID3D11Texture2D *image,*enhanced;
    ID3D11RenderTargetView *rtv;
    ID3D11DepthStencilView *dsv;
    unsigned width,height,native_width,native_height,refs,coherent,valid_aspects;
    uint64_t admitted_bytes;
    RhiImage *presentation_image;
    RhiView *presentation_view;
    struct PilotNativeViewCache *views;
};
struct PilotNativeMap;
/* create(source-native, enhanced-width, enhanced-height) never modifies source.
 * rtv/dsv and width/height describe the enhanced companion. Returned pair/map
 * refs are owned; maps retain pair/source per physical native-image lifetime. */
static struct PilotNativePlane *pilot_native_plane_create(ID3D11Texture2D *,unsigned,unsigned);
static void pilot_native_plane_retain(struct PilotNativePlane *);
static void pilot_native_plane_release(struct PilotNativePlane *);
static struct PilotNativeMap *pilot_native_map_create(ID3D11Texture2D *,struct PilotNativePlane *);
static void pilot_native_map_release(struct PilotNativeMap *);
/* Borrowed while the corresponding graphics-owned map remains retained. */
static struct PilotNativePlane *pilot_native_plane_find(ID3D11Resource *);
/* Return an enhanced SRV with the original native view interpretation. Owned. */
static HRESULT pilot_native_plane_view(struct PilotNativePlane *,ID3D11ShaderResourceView *,ID3D11ShaderResourceView **);
/* Root integrates these policy/guest-query predicates with the existing owner. */
static int pilot_native_enabled(void);
static int pilot_native_frame_valid(void);
static int pilot_native_dimensions(unsigned,unsigned,unsigned,unsigned,unsigned *,unsigned *);
static int pilot_native_query_active(void);
static int pilot_visibility_internal_try_begin(ID3D11Query **);
static void pilot_visibility_internal_end(ID3D11Query *);
static void pilot_native_degrade(void);
static int pilot_native_refresh_color(struct PilotNativePlane *);
#endif
