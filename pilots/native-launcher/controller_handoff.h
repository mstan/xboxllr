/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef XBOXLLR_CONTROLLER_HANDOFF_H
#define XBOXLLR_CONTROLLER_HANDOFF_H
#include "launcher_model.h"
#include "launcher_input.h"
#include <SDL.h>
#ifdef __cplusplus
extern "C" {
#endif
void xboxllr_controller_begin(const char *catalog,int automatic);
void xboxllr_controller_sync(LauncherModel *,const LauncherPad *,int count);
void xboxllr_controller_event(const SDL_Event *);
void xboxllr_controller_finish(LauncherModel *,const LauncherPad *,int count);
const char *xboxllr_controller_label(const char *guid,const char *name);
void xboxllr_controller_no_source(void);
int xboxllr_controller_automatic(void);
#ifdef __cplusplus
}
#endif
#endif
