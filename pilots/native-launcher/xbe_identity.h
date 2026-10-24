#ifndef XBOXLLR_XBE_IDENTITY_H
#define XBOXLLR_XBE_IDENTITY_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct XboxllrXbeIdentity {
    int valid;
    uint32_t title_id;
    char title_name[161];
    char title_label[48];
    char error[80];
} XboxllrXbeIdentity;
extern XboxllrXbeIdentity xboxllr_xbe_identity;
extern uint32_t xboxllr_expected_title_id;
extern int xboxllr_has_expected_title_id;
XboxllrXbeIdentity xboxllr_read_xbe_identity(const char *path);
void xboxllr_xbe_select(const char *path);
const char *xboxllr_xbe_rejection(void);
#ifdef __cplusplus
}
#endif
#endif
