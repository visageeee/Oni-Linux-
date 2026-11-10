/* Pause-menu settings for the Linux fork. Changes take effect at next start. */
#include "Oni_ExtraSettings.h"
#if defined(__linux__) && UUmSDL
#include "ONi_BundlePath.h"
#include "Oni_OutGameUI.h"
#include "WM_Button.h"
#include "WM_Text.h"
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ONI_SETTINGS_FILE "graphics.ini"
#define ONI_SETTINGS_ENTRY 1900
#define ONI_SETTINGS_SAVE 1901
#define ONI_SETTINGS_CANCEL 1902
#define ONI_SETTINGS_ORIGINAL 1903
#define ONI_SETTINGS_FIRST 2000
#define ONI_SETTINGS_MAX_OLD 128

typedef enum OniSettingKind { OniSetting_Toggle, OniSetting_Number, OniSetting_Grade } OniSettingKind;
typedef struct OniSetting {
    const char *key,*label;
    OniSettingKind kind;
    float value,def,min,max,step;
    int decimals;
} OniSetting;
static OniSetting oniSettings[]={
    {"ONI_RELIGHT", "Scene lighting", OniSetting_Toggle,0,0,0,1,1,0},
    {"ONI_WORLD_LIGHT", "Weapon lights", OniSetting_Toggle,0,0,0,1,1,0},
    {"ONI_HIT_LIGHT", "Hit-flash lights", OniSetting_Toggle,0,0,0,1,1,0},
    {"ONI_STENCIL_SHADOWS", "Stencil shadows", OniSetting_Toggle,0,0,0,1,1,0},
    {"ONI_DISTANCE_FOG", "Distance fog", OniSetting_Toggle,0,0,0,1,1,0},
    {"ONI_AO", "Ambient occlusion", OniSetting_Toggle,0,0,0,1,1,0},
    {"ONI_SHADER", "Color grading", OniSetting_Grade,0,0,0,2,1,0},
    {"ONI_FPS_CAP", "60 FPS limiter", OniSetting_Toggle,1,1,0,1,1,0},
    {"ONI_FOG_START", "Fog start", OniSetting_Number,180,180,0,100000,50,0},
    {"ONI_FOG_END", "Fog end", OniSetting_Number,1400,1400,1,100000,100,0},
    {"ONI_FOG_STRENGTH", "Fog strength", OniSetting_Number,.55f,.55f,0,1,.05f,2},
    {"ONI_AO_RADIUS", "AO radius", OniSetting_Number,12,12,1,60,1,0},
    {"ONI_AO_STRENGTH", "AO strength", OniSetting_Number,.9f,.9f,0,2,.1f,1},
    {"ONI_AO_BIAS", "AO bias", OniSetting_Number,.08f,.08f,0,.5f,.01f,2}
};
#define ONI_SETTINGS_COUNT (sizeof(oniSettings)/sizeof(oniSettings[0]))
static int oniSettingsLoaded;
static char oniSettingsPath[2048];
static float oniSettingsBackup[ONI_SETTINGS_COUNT];
typedef struct OniSettingsUI {
    WMtDialog *dialog;
    WMtWindow *entry,*heading,*note,*save,*cancel,*original;
    WMtWindow *value[ONI_SETTINGS_COUNT],*minus[ONI_SETTINGS_COUNT];
    WMtWindow *old[ONI_SETTINGS_MAX_OLD];
    unsigned oldCount;
    UUtBool open, from_options;
} OniSettingsUI;
static OniSettingsUI oniUI;

static UUtBool ONiSetting_Parse(const OniSetting *setting,const char *text,float *out) {
    char *end=NULL;float value;
    if(!text || !*text)return UUcFalse;
    if(setting->kind==OniSetting_Grade) {
        if(!strcmp(text,"off") || !strcmp(text,"0"))value=0;
        else if(!strcmp(text,"warm") || !strcmp(text,"1"))value=1;
        else if(!strcmp(text,"neutral"))value=2;
        else return UUcFalse;
    } else {
        value=strtof(text,&end);
        if(end==text || *end || !isfinite(value))return UUcFalse;
        if(!strcmp(setting->key,"ONI_FPS_CAP"))value=(value==0.f)?0.f:1.f;
        if(setting->kind==OniSetting_Toggle && value!=0.f && value!=1.f)return UUcFalse;
        if(value<setting->min || value>setting->max)return UUcFalse;
    }
    *out=value;return UUcTrue;
}
static void ONiSetting_Format(const OniSetting *setting,char *out,size_t size) {
    if(setting->kind==OniSetting_Grade)snprintf(out,size,"%s",setting->value<.5f?"off":setting->value<1.5f?"warm":"neutral");
    else if(setting->kind==OniSetting_Toggle)snprintf(out,size,"%d",setting->value>.5f?1:0);
    else snprintf(out,size,"%.*f",setting->decimals,setting->value);
}
void ONrExtraSettings_Load(void) {
    if(oniSettingsLoaded)return;
    oniSettingsLoaded=1;
    for(size_t i=0;i<ONI_SETTINGS_COUNT;++i)oniSettings[i].value=oniSettings[i].def;
    if(ONiBundlePath_ResolveStateFile(ONI_SETTINGS_FILE,oniSettingsPath,sizeof(oniSettingsPath))!=UUcError_None)oniSettingsPath[0]='\0';
    FILE *file=oniSettingsPath[0]?fopen(oniSettingsPath,"r"):NULL;
    if(file) {
        char line[256];
        while(fgets(line,sizeof(line),file)) {
            char *newline=strpbrk(line,"\r\n");if(newline)*newline='\0';
            char *eq=strchr(line,'=');if(!eq)continue;*eq++='\0';
            for(size_t i=0;i<ONI_SETTINGS_COUNT;++i)if(!strcmp(line,oniSettings[i].key)) {
                float value;if(ONiSetting_Parse(&oniSettings[i],eq,&value))setenv(line,eq,0);
                break;
            }
        }
        fclose(file);
    }
    for(size_t i=0;i<ONI_SETTINGS_COUNT;++i) {
        const char *env=getenv(oniSettings[i].key);float value;
        if(ONiSetting_Parse(&oniSettings[i],env,&value))oniSettings[i].value=value;
    }
    if(!getenv("ONI_RELIGHT")) {const char *legacy=getenv("ONI_WAREHOUSE_LIGHT");if(legacy && !strcmp(legacy,"1"))oniSettings[0].value=1;}
}
static UUtBool ONiSettings_Save(void) {
    if(oniSettings[9].value<=oniSettings[8].value || !oniSettingsPath[0])return UUcFalse;
    char temp[sizeof(oniSettingsPath)+8];int n=snprintf(temp,sizeof(temp),"%s.tmp",oniSettingsPath);
    if(n<0 || (size_t)n>=sizeof(temp))return UUcFalse;
    FILE *file=fopen(temp,"w");if(!file)return UUcFalse;
    UUtBool ok=UUcTrue;
    if(fprintf(file,"# Oni Linux graphics settings; applied on restart.\n")<0)ok=UUcFalse;
    for(size_t i=0;i<ONI_SETTINGS_COUNT;++i){char value[48];ONiSetting_Format(&oniSettings[i],value,sizeof(value));if(fprintf(file,"%s=%s\n",oniSettings[i].key,value)<0)ok=UUcFalse;}
    if(fclose(file)!=0)ok=UUcFalse;
    if(ok && rename(temp,oniSettingsPath)!=0)ok=UUcFalse;
    if(!ok)remove(temp);
    return ok;
}
static WMtWindow *ONiSettings_Control(WMtDialog *dialog,UUtUns16 type,UUtUns16 id,const char *title,int x,int y,int w,int h,UUtUns32 style,UUtBool visible) {
    UUtUns32 flags=WMcWindowFlag_Child|(visible?WMcWindowFlag_Visible:0);
    WMtWindow *window=WMrWindow_New(type,(char *)title,flags,style,id,x,y,w,h,dialog,0);
    WMtWindow *model=WMrDialog_GetItemByID(dialog,101);TStFontInfo font;
    if(window && model && WMrWindow_GetFontInfo(model,&font))WMrWindow_SetFontInfo(window,&font);
    /* Existing menu decorations may span the dialog and intercept clicks. */
    if(window)WMrWindow_BringToFront(window);
    return window;
}
static void ONiSettings_UpdateLabel(size_t index) {
    OniSetting *s=&oniSettings[index];char value[48],title[128];ONiSetting_Format(s,value,sizeof(value));
    if(s->kind==OniSetting_Toggle)snprintf(title,sizeof(title),"%s: %s",s->label,s->value>.5f?"ON":"OFF");
    else if(s->kind==OniSetting_Grade)snprintf(title,sizeof(title),"%s: %s",s->label,value);
    else snprintf(title,sizeof(title),"%s: %s  +",s->label,value);
    if(oniUI.value[index])WMrWindow_SetTitle(oniUI.value[index],title,WMcMaxTitleLength);
}
static void ONiSettings_SetPageVisible(UUtBool visible) {
    WMtWindow *fixed[]={oniUI.heading,oniUI.note,oniUI.save,oniUI.cancel,oniUI.original};
    for(size_t i=0;i<sizeof(fixed)/sizeof(fixed[0]);++i)if(fixed[i])WMrWindow_SetVisible(fixed[i],visible && !(oniUI.from_options && fixed[i]==oniUI.original));
    for(size_t i=0;i<ONI_SETTINGS_COUNT;++i){if(oniUI.value[i])WMrWindow_SetVisible(oniUI.value[i],visible);if(oniUI.minus[i])WMrWindow_SetVisible(oniUI.minus[i],visible);}
}
static void ONiSettings_Close(UUtBool restore) {
    if(!oniUI.open)return;
    if(restore)for(size_t i=0;i<ONI_SETTINGS_COUNT;++i)oniSettings[i].value=oniSettingsBackup[i];
    ONiSettings_SetPageVisible(UUcFalse);
    for(unsigned i=0;i<oniUI.oldCount;++i)if(oniUI.old[i])WMrWindow_SetVisible(oniUI.old[i],UUcTrue);
    if(oniUI.entry)WMrWindow_SetVisible(oniUI.entry,UUcTrue);
    oniUI.oldCount=0;oniUI.open=UUcFalse;
}
static void ONiSettings_Open(WMtDialog *dialog) {
    if(oniUI.open)return;
    for(size_t i=0;i<ONI_SETTINGS_COUNT;++i)oniSettingsBackup[i]=oniSettings[i].value;
    oniUI.oldCount=0;
    for(unsigned id=0;id<1024 && oniUI.oldCount<ONI_SETTINGS_MAX_OLD;++id) {
        WMtWindow *w=WMrDialog_GetItemByID(dialog,(UUtUns16)id);
        if(w && WMrWindow_GetVisible(w)){oniUI.old[oniUI.oldCount++]=w;WMrWindow_SetVisible(w,UUcFalse);}
    }
    if(oniUI.entry)WMrWindow_SetVisible(oniUI.entry,UUcFalse);
    for(size_t i=0;i<ONI_SETTINGS_COUNT;++i)ONiSettings_UpdateLabel(i);
    if(oniUI.note)WMrWindow_SetTitle(oniUI.note,"Restart Oni to apply. Launch environment variables take precedence.",WMcMaxTitleLength);
    oniUI.open=UUcTrue;ONiSettings_SetPageVisible(UUcTrue);
}
void ONrExtraSettings_Attach(WMtDialog *dialog,UUtBool from_options) {
    if(oniUI.dialog)return; /* The original options dialog may be nested in our page. */
    ONrExtraSettings_Load();memset(&oniUI,0,sizeof(oniUI));oniUI.dialog=dialog;oniUI.from_options=from_options;
    UUtInt16 w=640,h=480;WMrWindow_GetSize(dialog,&w,&h);
    oniUI.entry=ONiSettings_Control(dialog,WMcWindowType_Button,ONI_SETTINGS_ENTRY,from_options?"Graphics settings":"Settings",from_options?350:25,from_options?405:315,from_options?175:130,40,WMcButtonStyle_PushButton,UUcTrue);
    oniUI.heading=ONiSettings_Control(dialog,WMcWindowType_Text,2190,"GRAPHICS AND EFFECTS",20,12,w-40,28,WMcTextStyle_Standard,UUcFalse);
    oniUI.note=ONiSettings_Control(dialog,WMcWindowType_Text,2191,"Restart Oni to apply.",20,h-98,w-40,22,WMcTextStyle_Standard,UUcFalse);
    int columnWidth=(w-50)/2;
    for(size_t i=0;i<ONI_SETTINGS_COUNT;++i) {
        int col=(int)(i/7),row=(int)(i%7),x=20+col*(columnWidth+10),y=48+row*43;
        int minus=oniSettings[i].kind==OniSetting_Number?32:0;
        oniUI.value[i]=ONiSettings_Control(dialog,WMcWindowType_Button,(UUtUns16)(ONI_SETTINGS_FIRST+i*2),"",x,y,columnWidth-minus-3,34,WMcButtonStyle_PushButton,UUcFalse);
        if(minus)oniUI.minus[i]=ONiSettings_Control(dialog,WMcWindowType_Button,(UUtUns16)(ONI_SETTINGS_FIRST+i*2+1),"-",x+columnWidth-minus,y,minus-3,34,WMcButtonStyle_PushButton,UUcFalse);
    }
    oniUI.original=ONiSettings_Control(dialog,WMcWindowType_Button,ONI_SETTINGS_ORIGINAL,"Game options",20,h-58,175,36,WMcButtonStyle_PushButton,UUcFalse);
    oniUI.cancel=ONiSettings_Control(dialog,WMcWindowType_Button,ONI_SETTINGS_CANCEL,"Cancel / Back",w-285,h-58,130,36,WMcButtonStyle_PushButton,UUcFalse);
    oniUI.save=ONiSettings_Control(dialog,WMcWindowType_Button,ONI_SETTINGS_SAVE,"Save",w-145,h-58,125,36,WMcButtonStyle_PushButton,UUcFalse);
}
UUtBool ONrExtraSettings_Command(WMtDialog *dialog,UUtUns16 id) {
    if(dialog!=oniUI.dialog)return UUcFalse;
    if(id==ONI_SETTINGS_ENTRY){ONiSettings_Open(dialog);return UUcTrue;}
    if(!oniUI.open)return UUcFalse;
    if(id==ONI_SETTINGS_CANCEL || id==WMcDialogItem_Cancel){ONiSettings_Close(UUcTrue);return UUcTrue;}
    if(id==ONI_SETTINGS_ORIGINAL){ONrOutGameUI_Options_Display();return UUcTrue;}
    if(id==ONI_SETTINGS_SAVE){
        if(oniSettings[9].value<=oniSettings[8].value) {if(oniUI.note)WMrWindow_SetTitle(oniUI.note,"Fog end must be greater than fog start.",WMcMaxTitleLength);return UUcTrue;}
        if(!ONiSettings_Save()){if(oniUI.note)WMrWindow_SetTitle(oniUI.note,"Could not save settings. Check file permissions.",WMcMaxTitleLength);return UUcTrue;}
        for(size_t i=0;i<ONI_SETTINGS_COUNT;++i)oniSettingsBackup[i]=oniSettings[i].value;
        if(oniUI.note)WMrWindow_SetTitle(oniUI.note,"Saved. Restart Oni to apply the changes.",WMcMaxTitleLength);
        return UUcTrue;
    }
    if(id>=ONI_SETTINGS_FIRST && id<ONI_SETTINGS_FIRST+ONI_SETTINGS_COUNT*2) {
        size_t index=(id-ONI_SETTINGS_FIRST)/2;UUtBool decrease=(id-ONI_SETTINGS_FIRST)%2;OniSetting *setting=&oniSettings[index];
        if(setting->kind==OniSetting_Toggle)setting->value=setting->value>.5f?0.f:1.f;
        else if(setting->kind==OniSetting_Grade)setting->value=fmodf(setting->value+1.f,3.f);
        else {setting->value+=decrease?-setting->step:setting->step;if(setting->value<setting->min)setting->value=setting->min;if(setting->value>setting->max)setting->value=setting->max;}
        ONiSettings_UpdateLabel(index);
        if(oniUI.note)WMrWindow_SetTitle(oniUI.note,"Unsaved changes. Save and restart Oni to apply.",WMcMaxTitleLength);
        return UUcTrue;
    }
    return UUcFalse;
}
void ONrExtraSettings_Detach(WMtDialog *dialog) {
    if(dialog!=oniUI.dialog)return;
    if(oniUI.open)for(size_t i=0;i<ONI_SETTINGS_COUNT;++i)oniSettings[i].value=oniSettingsBackup[i];
    memset(&oniUI,0,sizeof(oniUI));
}
#endif
