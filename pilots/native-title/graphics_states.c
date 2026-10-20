/* Xbox state encodings to the pinned donor's desktop-style state interface.
 * Numeric ABI facts: original XDK routines, nxdk nv_regs.h and Cxbx type
 * declarations. No external emulator conversion implementation is copied. */
#include "d3d8_internal.h"
#include "graphics_backend.h"

static uint32_t compare_value(uint32_t value)
{
    return value>=0x200 && value<=0x207 ? value-0x1ff : 0;
}
static uint32_t blend_value(uint32_t value)
{
    if (value<=1) return value+1;
    return value>=0x300 && value<=0x308 ? value-0x2fd : 0;
}
static uint32_t stencil_value(uint32_t value)
{
    switch(value) {
    case 0x1e00:return 1; case 0:return 2; case 0x1e01:return 3;
    case 0x1e02:return 4; case 0x1e03:return 5; case 0x150a:return 6;
    case 0x8507:return 7; case 0x8508:return 8; default:return 0;
    }
}
static int render_state(uint32_t state,uint32_t value,int apply)
{
    uint32_t host,converted=value;
    switch(state) {
    case 65:case 76:case 112:case 117:case 137:case 142:case 149:
        return apply?pilot_gfx_initializer_set(state,value):pilot_gfx_initializer_admits(state,value);
    case 150:return apply?pilot_gfx_logic_op(value):0;
    case 157:
        /* The shared RHI rasterizes unit-width lines. Other widths must not
         * return success while retaining that fixed native raster policy. */
        return value==0x3f800000u;
    case 159:
        /* Preserve the caller's boolean cache contract using native compressed
         * sampling. Exact console noise pixels remain a documented open bound;
         * this retains the pilot's existing native sampling approximation. */
        if(value>1 || !apply)return 0;pilot_gfx_noise_state(value);return 1;
    case 57:host=D3DRS_ZFUNC; goto compare;
    case 58:host=D3DRS_ALPHAFUNC; goto compare;
    case 59:host=D3DRS_ALPHABLENDENABLE; goto check_boolean;
    case 60:host=D3DRS_ALPHATESTENABLE; goto check_boolean;
    case 61:host=D3DRS_ALPHAREF; converted=value&255; break;
    case 62:host=D3DRS_SRCBLEND; goto blend;
    case 63:host=D3DRS_DESTBLEND; goto blend;
    case 64:host=D3DRS_ZWRITEENABLE; goto check_boolean;
    case 66:
        if (value!=0x1d00 && value!=0x1d01) return 0;
        host=D3DRS_SHADEMODE; converted=value==0x1d00?1:2; break;
    case 67:
        if (value&~0x01010101u) return 0;
        host=D3DRS_COLORWRITEENABLE;
        converted=((value>>16)&1)|((value>>7)&2)|((value<<2)&4)|((value>>21)&8); break;
    case 68:host=D3DRS_STENCILZFAIL; goto stencil;
    case 69:host=D3DRS_STENCILPASS; goto stencil;
    case 70:host=D3DRS_STENCILFUNC; goto compare;
    case 71:host=D3DRS_STENCILREF; converted=value&255; break;
    case 72:host=D3DRS_STENCILMASK; converted=value&255; break;
    case 73:host=D3DRS_STENCILWRITEMASK; converted=value&255; break;
    case 74:
        host=D3DRS_BLENDOP;
        switch(value) {
        case 0x8006:converted=1; break; case 0x800a:converted=2; break;
        case 0x800b:converted=3; break; case 0x8007:converted=4; break;
        case 0x8008:converted=5; break; default:return 0;
        } break;
    case 92:host=D3DRS_FOGENABLE; goto check_boolean;
    case 93:if(value>3)return 0; host=D3DRS_FOGTABLEMODE; break;
    case 94:host=D3DRS_FOGSTART; goto floating;
    case 95:host=D3DRS_FOGEND; goto floating;
    case 96:host=D3DRS_FOGDENSITY; goto floating;
    case 97:host=D3DRS_RANGEFOGENABLE; goto check_boolean;
    case 102:host=D3DRS_LIGHTING; goto check_boolean;
    case 103:host=D3DRS_SPECULARENABLE; goto check_boolean;
    case 115:host=D3DRS_AMBIENT; break;
    case 138:host=D3DRS_FOGCOLOR; break;
    case 139:
        if(value!=0x1b01 && value!=0x1b02)return 0;
        host=D3DRS_FILLMODE; converted=value==0x1b01?2:3; break;
    case 143:host=D3DRS_ZENABLE; goto check_boolean; /* W-buffer remains unsupported. */
    case 144:host=D3DRS_STENCILENABLE; goto check_boolean;
    case 145:host=D3DRS_STENCILFAIL; goto stencil;
    case 147:
        host=D3DRS_CULLMODE;
        if(value==0)converted=1;
        else if(value==0x900)converted=2;
        else if(value==0x901)converted=3;
        else return 0;
        break;
    case 148:host=D3DRS_TEXTUREFACTOR; break;
    case 151:
        return value<=1 && (!apply || pilot_gfx_edge_antialias(value));
    default:return 0;
    }
    goto apply;
check_boolean: if(value>1)return 0; goto apply;
compare: converted=compare_value(value); if(!converted)return 0; goto apply;
blend: converted=blend_value(value); if(!converted)return 0; goto apply;
stencil: converted=stencil_value(value); if(!converted)return 0; goto apply;
floating: if((value&0x7f800000u)==0x7f800000u)return 0;
apply: if(apply)pilot_gfx_set_state(host,converted); return 1;
}
int pilot_gfx_xbox_render_state(uint32_t state,uint32_t value)
{return render_state(state,value,1);}
int pilot_gfx_command_method(uint32_t method,uint32_t value,int apply)
{
    if(method==0x1710)return !value && pilot_gfx_vertex_cache_break(apply);
    if((method>=0x1880 && method<0x1900) || (method>=0x1940 && method<0x1b00))
        return pilot_gfx_vertex_current_method(method,value,apply);
    /* A zero CONTROL0 disables this sampler. The runtime separately validates
     * the original software NULL-binding transaction and its resource owner.
     * Enabled stages/LOD controls require their complete texture state. */
    if(method>=0x1b0c && method<=0x1bcc && !((method-0x1b0c)&63)) {
        if(value)return 0;
        if(apply)pilot_gfx_texture_bind((method-0x1b0c)/64,0);
        return 1;
    }
    uint32_t state;
    if(!pilot_graphics_simple_decode(0x40000u|method,&state))return 0;
    return render_state(state,value,apply);
}
int pilot_gfx_xbox_texture_state(uint32_t stage,uint32_t state,uint32_t value)
{
    uint32_t host;
    if(stage>=4)return 0;
    switch(state) {
    case 0:host=D3DTSS_ADDRESSU; goto address;
    case 1:host=D3DTSS_ADDRESSV; goto address;
    case 2:host=31 /* adapter extension; donor slot25 is COLORSIGN */; goto address;
    case 3:host=D3DTSS_MAGFILTER; goto filter;
    case 4:host=D3DTSS_MINFILTER; goto filter;
    case 5:if(value>2)return 0; host=D3DTSS_MIPFILTER; break;
    case 6:if((value&0x7f800000u)==0x7f800000u)return 0; host=D3DTSS_MIPMAPLODBIAS; break;
    case 7:host=D3DTSS_MAXMIPLEVEL; break;
    case 8:if(!value || value>16)return 0; host=D3DTSS_MAXANISOTROPY; break;
    case 9:return value==0; /* Color key disabled; enabling remains guarded. */
    case 30:return 1; /* Key color is inert while the only admitted op is disabled. */
    case 12:
        host=D3DTSS_COLOROP;
        if(value==25 || value==26) {
            if(stage==3)return 0;
            value-=3;break; /* Xbox BUMPENVMAP/LUMINANCE -> desktop 22/23. */
        }
        goto operation;
    case 14:host=D3DTSS_COLORARG1; goto argument;
    case 15:host=D3DTSS_COLORARG2; goto argument;
    case 16:host=D3DTSS_ALPHAOP; goto operation;
    case 18:host=D3DTSS_ALPHAARG1; goto argument;
    case 19:host=D3DTSS_ALPHAARG2; goto argument;
    case 20:if(value!=1 && value!=5)return 0; host=D3DTSS_RESULTARG; break;
    case 21:
#ifdef PILOT_D3D_PHOBOS
        if(value!=0 && value!=2)return 0;
        host=24;break; /* desktop TEXTURETRANSFORMFLAGS: disabled / COUNT2 */
#else
        return 0;
#endif
    case 22:host=D3DTSS_BUMPENVMAT00; break;
    case 23:host=D3DTSS_BUMPENVMAT01; break;
    case 24:host=D3DTSS_BUMPENVMAT11; break; /* Xbox swaps the final two entries. */
    case 25:host=D3DTSS_BUMPENVMAT10; break;
    case 26:host=22; break; /* desktop BUMPENVLSCALE */
    case 27:host=23; break; /* desktop BUMPENVLOFFSET */
    case 28:
        if(value>3) {
#ifdef PILOT_D3D_PHOBOS
            /* The fixed-function VS consumes camera-space normal generation
             * before its stage texture matrix. The low bits retain the source
             * set selector; generated coordinates do not use that selector.
             * Other generation modes require their own output qualification. */
            if((value&~3u)!=0x10000u)return 0;
#else
            return 0;
#endif
        }
        host=D3DTSS_TEXCOORDINDEX;break;
    case 29:host=D3DTSS_BORDERCOLOR; break;
    default:return 0;
    }
    goto apply;
address: if(value<1 || value>5)return 0; goto apply;
filter: if(value<1 || value>3)return 0; goto apply;
operation: if(value<1 || value>7)return 0; goto apply;
argument: if((value&~0x3fu) || (value&15)>5)return 0;
apply: pilot_gfx_set_texture_state(stage,host,value); return 1;
}
