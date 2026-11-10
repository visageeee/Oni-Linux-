#ifndef ONI_EXTRA_SETTINGS_H
#define ONI_EXTRA_SETTINGS_H
#include "BFW.h"
#if defined(__linux__) && UUmSDL
#include "BFW_WindowManager.h"
void ONrExtraSettings_Load(void);
void ONrExtraSettings_Attach(WMtDialog *dialog, UUtBool from_options);
UUtBool ONrExtraSettings_Command(WMtDialog *dialog, UUtUns16 id);
void ONrExtraSettings_Detach(WMtDialog *dialog);
#endif
#endif
