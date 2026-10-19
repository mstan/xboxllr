/* Common startup policy. Generated profiles supply title-specific paths and
 * branding; old optical-title inventories retain their original default ABI. */
#pragma once
#ifdef PILOT_TITLE_RUNTIME_PROFILE
#include "pilot_title_runtime.h"
#else
#define PILOT_TITLE_ID "native-title"
#define PILOT_TITLE_NAME "Xbox title"
#define PILOT_TITLE_IMAGE "default.xbe"
#define PILOT_TITLE_DEVICE "\\Device\\Cdrom0"
#define PILOT_TITLE_DRIVE "D:"
#endif
/* Older profiles let the host seed the initial content-drive link. Titles
 * which establish their own DOS namespace select TITLE in their profile. */
#ifndef PILOT_TITLE_DRIVE_OWNER
#define PILOT_TITLE_DRIVE_OWNER "HOST"
#endif
