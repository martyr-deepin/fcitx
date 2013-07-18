/***************************************************************************
 *   Copyright (C) 2002~2005 by Yuking                                     *
 *   yuking_net@sohu.com                                                   *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA.              *
 ***************************************************************************/
#include <limits.h>
#include <ctype.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <cairo.h>
#include <libintl.h>
#include <cairo-xlib.h>

#include "fcitx/fcitx.h"
#include "fcitx/module.h"

#include "fcitx/ime.h"
#include "fcitx/instance.h"
#include "fcitx-utils/log.h"
#include "fcitx/frontend.h"
#include "fcitx-config/xdg.h"
#include "fcitx/hook.h"
#include "fcitx-utils/utils.h"
#include "module/x11/fcitx-x11.h"
#include "ui/cairostuff/cairostuff.h"
#include "ui/cairostuff/font.h"
#include "ui/classic/fcitx-classicui.h"

#define VK_FILE "vk.conf"

/*#define VK_WINDOW_WIDTH     354*/
/*#define VK_WINDOW_HEIGHT    164*/
#define VK_WINDOW_WIDTH     783
#define VK_WINDOW_HEIGHT    309
#define VK_NUMBERS      47
#define VK_MAX          50

static int get_click_region(int x, int y);

struct _FcitxVKState;

typedef struct _VKS {
    char            strSymbol[VK_NUMBERS][2][UTF8_MAX_LENGTH + 1]; //相应的符号
    char           *strName;
} VKS;

typedef struct _VKWindow {
    Window          window;
    int fontSize;
    cairo_surface_t* surface;
    cairo_surface_t* keyboard;
    Display*        dpy;
    struct _FcitxVKState* owner;
    char *defaultFont;
    int iVKWindowX;
    int iVKWindowY;
} VKWindow;

typedef struct _FcitxVKState {
    VKWindow*       vkWindow;
    int             iCurrentVK ;
    int             iVKCount ;
    VKS             vks[VK_MAX];
    boolean         bShiftPressed;
    boolean         bVKCaps;
    boolean         bVK;
    FcitxUIMenu     vkmenu;
    FcitxInstance* owner;
} FcitxVKState;

const char            vkTable[VK_NUMBERS + 1] = "`1234567890-=qwertyuiop[]\\asdfghjkl;'zxcvbnm,./";
const char            strCharTable[] = "`~1!2@3#4$5%6^7&8*9(0)-_=+[{]}\\|;:'\",<.>/?";    //用于转换上/下档键

static boolean VKWindowEventHandler(void* arg, XEvent* event);
static void
VKInitWindowAttribute(FcitxVKState* vkstate, Visual ** vs, Colormap * cmap,
                      XSetWindowAttributes * attrib,
                      unsigned long *attribmask, int *depth);
static Visual * VKFindARGBVisual(FcitxVKState* vkstate);
static void VKSetWindowProperty(FcitxVKState* vkstate, Window window, FcitxXWindowType type, char *windowTitle);
static boolean VKMouseClick(FcitxVKState* vkstate, Window window, int *x, int *y);
static void SwitchVK(FcitxVKState *vkstate);
static int MyToUpper(int iChar);
static int MyToLower(int iChar);
static cairo_surface_t* LoadVKImage(VKWindow* vkWindow);
static void *VKCreate(FcitxInstance* instance);
static VKWindow* CreateVKWindow(FcitxVKState* vkstate);
static boolean GetVKState(void *arg);
static void ToggleVKState(void *arg);
static INPUT_RETURN_VALUE ToggleVKStateWithHotkey(void* arg);
static void DrawVKWindow(VKWindow* vkWindow);
static boolean VKMouseKey(FcitxVKState* vkstate, int x, int y);
static boolean VKPreFilter(void* arg, FcitxKeySym sym,
                           unsigned int state,
                           INPUT_RETURN_VALUE *retval
                          );
static  void VKReset(void* arg);
static void VKUpdate1(void* arg);
static void VKUpdate2(void* arg);
static INPUT_RETURN_VALUE DoVKInput(FcitxVKState* vkstate, KeySym sym, int state);
static void DisplayVKWindow(VKWindow* vkWindow);
static boolean VKMenuAction(FcitxUIMenu *menu, int index);
static void UpdateVKMenu(FcitxUIMenu *menu);
static void SelectVK(FcitxVKState* vkstate, int vkidx);
static void set_struct_partial(Display* display, Window window, guint32 strut, guint32 strut_start, guint32 strut_end);

static FcitxConfigColor blackColor = {0, 0, 0};

FCITX_DEFINE_PLUGIN(fcitx_vk, module, FcitxModule) = {
    VKCreate,
    NULL,
    NULL,
    NULL,
    NULL,
};

void *VKCreate(FcitxInstance* instance)
{
    FcitxVKState *vkstate = fcitx_utils_malloc0(sizeof(FcitxVKState));
    FcitxGlobalConfig* config = FcitxInstanceGetGlobalConfig(instance);
    vkstate->owner = instance;

    FcitxHotkeyHook hotkey;
    hotkey.hotkey = config->hkVK;
    hotkey.hotkeyhandle = ToggleVKStateWithHotkey;
    hotkey.arg = vkstate;
    FcitxInstanceRegisterHotkeyFilter(instance, hotkey);

    FcitxUIRegisterStatus(instance, vkstate, "vk", _("Toggle Virtual Keyboard"), _("Virtual Keyboard State"),  ToggleVKState, GetVKState);
    printf("regist status icon\n");

    /*FcitxKeyFilterHook hk;*/
    /*hk.arg = vkstate ;*/
    /*hk.func = VKPreFilter;*/
    /*FcitxInstanceRegisterPreInputFilter(instance, hk);*/

    FcitxIMEventHook resethk;
    resethk.arg = vkstate;
    resethk.func = VKReset;
    FcitxInstanceRegisterTriggerOnHook(instance, resethk);
    FcitxInstanceRegisterTriggerOffHook(instance, resethk);

    resethk.func = VKUpdate1;
    FcitxInstanceRegisterInputFocusHook(instance, resethk);
    resethk.func = VKUpdate2;
    FcitxInstanceRegisterInputUnFocusHook(instance, resethk);

    /*FcitxMenuInit(&vkstate->vkmenu);*/
    /*vkstate->vkmenu.candStatusBind = strdup("vk");*/
    /*vkstate->vkmenu.name = strdup(_("Virtual Keyboard"));*/

    /*vkstate->vkmenu.UpdateMenu = UpdateVKMenu;*/
    /*vkstate->vkmenu.MenuAction = VKMenuAction;*/
    /*vkstate->vkmenu.priv = vkstate;*/
    /*vkstate->vkmenu.isSubMenu = false;*/

    /*FcitxUIRegisterMenu(instance, &vkstate->vkmenu);*/

    return vkstate;
}

boolean VKMenuAction(FcitxUIMenu *menu, int index)
{
    FcitxVKState* vkstate = (FcitxVKState*) menu->priv;
    if (index < vkstate->iVKCount)
        SelectVK(vkstate, index);
    else {
        if (vkstate->bVK) {
            FcitxUIUpdateStatus(vkstate->owner, "vk");
        }
    }
    return true;
}

void UpdateVKMenu(FcitxUIMenu *menu)
{
    FcitxVKState* vkstate = (FcitxVKState*) menu->priv;
    FcitxMenuClear(menu);
    int i;
    for (i = 0; i < vkstate->iVKCount; i ++)
        FcitxMenuAddMenuItem(&vkstate->vkmenu, vkstate->vks[i].strName, MENUTYPE_SIMPLE, NULL);
    if (vkstate->bVK) {
        FcitxMenuAddMenuItem(&vkstate->vkmenu, _("Close virtual keyboard"), MENUTYPE_SIMPLE, NULL);
    }
    menu->mark = vkstate->iCurrentVK;
}

void VKReset(void* arg)
{
    printf("trigger on\n");
    FcitxVKState *vkstate = (FcitxVKState*) arg;
    VKWindow* vkWindow = vkstate->vkWindow;
    if (vkstate->bVK != false)
        FcitxUIUpdateStatus(vkstate->owner, "vk");
    if (vkWindow)
        XUnmapWindow(vkWindow->dpy, vkWindow->window);
}

void VKUpdate2(void* arg)
{
    printf("trigger input unfocus\n");
    FcitxVKState *vkstate = (FcitxVKState*) arg;
    VKWindow* vkWindow = vkstate->vkWindow;
    if (vkWindow) {
        XUnmapWindow(vkWindow->dpy, vkWindow->window);
        /*FcitxInstanceCleanInputWindow(instance);*/
        /*FcitxUIUpdateInputWindow(instance);*/
    }
}
void VKUpdate1(void* arg)
{
    printf("trigger input focus\n");
    FcitxVKState *vkstate = (FcitxVKState*) arg;
    VKWindow* vkWindow = vkstate->vkWindow;
    if (vkWindow) {

        if (FcitxInstanceGetCurrentState(vkstate->owner) != IS_CLOSED && vkstate->bVK) {
            DrawVKWindow(vkWindow);
            DisplayVKWindow(vkWindow);
        } else
            XUnmapWindow(vkWindow->dpy, vkWindow->window);
    }
}

boolean VKPreFilter(void* arg, FcitxKeySym sym, unsigned int state, INPUT_RETURN_VALUE* retval)
{
    FcitxVKState *vkstate = (FcitxVKState*) arg;
    if (vkstate->bVK) {
        INPUT_RETURN_VALUE ret = DoVKInput(vkstate, sym, state);
        *retval = ret;
        return true;
    }
    return false;
}

boolean GetVKState(void *arg)
{
    FcitxVKState *vkstate = (FcitxVKState*) arg;
    return vkstate->bVK;
}

void ToggleVKState(void *arg)
{
    FcitxVKState *vkstate = (FcitxVKState*) arg;
    SwitchVK(vkstate);
    printf("switch vk \n");
}

INPUT_RETURN_VALUE ToggleVKStateWithHotkey(void* arg)
{
    FcitxVKState *vkstate = (FcitxVKState*) arg;
    FcitxUIUpdateStatus(vkstate->owner, "vk");
    return IRV_DO_NOTHING;
}

VKWindow* CreateVKWindow(FcitxVKState* vkstate)
{
    XSetWindowAttributes attrib;
    unsigned long   attribmask;
    char        strWindowName[] = "Fcitx VK Window";
    Colormap cmap;
    Visual * vs;
    int depth;
    VKWindow* vkWindow = fcitx_utils_new(VKWindow);
    vkWindow->owner = vkstate;

    LoadVKImage(vkWindow);

    vs = VKFindARGBVisual(vkstate);
    VKInitWindowAttribute(vkstate, &vs, &cmap, &attrib, &attribmask, &depth);
    vkWindow->dpy = FcitxX11GetDisplay(vkstate->owner);

    vkWindow->fontSize = 12;
    vkWindow->defaultFont = strdup("sans");
#ifndef _ENABLE_PANGO
    GetValidFont("zh", &vkWindow->defaultFont);
#endif

    vkWindow->window = XCreateWindow(vkWindow->dpy,
                                     DefaultRootWindow(vkWindow->dpy),
                                     0, 0,
                                     VK_WINDOW_WIDTH, VK_WINDOW_HEIGHT,
                                     0, depth, InputOutput, vs, attribmask, &attrib);
    if (vkWindow->window == (Window) None)
        return NULL;

    vkWindow->surface = cairo_xlib_surface_create(vkWindow->dpy, vkWindow->window, vs, VK_WINDOW_WIDTH, VK_WINDOW_HEIGHT);

    XSelectInput(vkWindow->dpy, vkWindow->window, ExposureMask | ButtonPressMask | ButtonReleaseMask  | PointerMotionMask);

    VKSetWindowProperty(vkstate, vkWindow->window, FCITX_WINDOW_DOCK, strWindowName);

    FcitxX11AddXEventHandler(vkstate->owner, VKWindowEventHandler, vkWindow);

    return vkWindow;
}

boolean VKWindowEventHandler(void* arg, XEvent* event)
{
    VKWindow* vkWindow = arg;
    if (event->xany.window == vkWindow->window) {
        switch (event->type) {
        case Expose:
            DrawVKWindow(vkWindow);
            break;
        case ButtonPress:
            switch (event->xbutton.button) {
            case Button1: {
                if (!VKMouseKey(vkWindow->owner, event->xbutton.x, event->xbutton.y)) {
                    vkWindow->iVKWindowX = event->xbutton.x;
                    vkWindow->iVKWindowY = event->xbutton.y;
                    VKMouseClick(vkWindow->owner, vkWindow->window, &vkWindow->iVKWindowX, &vkWindow->iVKWindowY);
                    DrawVKWindow(vkWindow);
                }
            }
            break;
            }
            break;
        }
        return true;
    }

    return false;
}

cairo_surface_t* LoadVKImage(VKWindow* vkWindow)
{
    FcitxVKState* vkstate = vkWindow->owner;
    boolean fallback = true;
    char *vkimage = "keyboard.png";
    if (vkstate->bShiftPressed) {
        vkimage = "keyboard-shift.png";
    } else if (vkstate->bVKCaps) {
        vkimage = "keyboard-caps.png";
    }
    if (vkWindow->keyboard) {
        cairo_surface_destroy(vkWindow->keyboard);
    }

    cairo_surface_t *image = FcitxClassicUILoadImage(vkstate->owner,
                                                     vkimage, &fallback);
    if (image)
        return image;

    if (!vkWindow->keyboard) {
        char* path = fcitx_utils_get_fcitx_path_with_filename("pkgdatadir", "skin/default/keyboard.png");
        vkWindow->keyboard = cairo_image_surface_create_from_png(path);
        free(path);
    }
    return vkWindow->keyboard;
}

void DisplayVKWindow(VKWindow* vkWindow)
{
    XMapRaised(vkWindow->dpy, vkWindow->window);
}

void DestroyVKWindow(VKWindow* vkWindow)
{
    cairo_surface_destroy(vkWindow->surface);
    XDestroyWindow(vkWindow->dpy, vkWindow->window);
}

static
void draw_input_method_button(FcitxVKState* vkstate, cairo_t* cr)
{
    char* path = "touch_cn.png";
    FcitxInstance* instance = vkstate->owner;
    FcitxIM* im = FcitxInstanceGetCurrentIM(instance);
    if (im && strncmp(im->uniqueName, "fcitx-keyboard-cn", 17) != 0) { //Chinese input method
        path = "touch_en.png";
    }
    int fallback = false;
    cairo_surface_t *button = FcitxClassicUILoadImage(instance, path, &fallback);
    /*printf("draw button begin: %d\n", cairo_surface_get_reference_count(button));*/
    cairo_set_source_surface(cr, button, 683, 168);
    cairo_paint(cr);
    /*printf("draw button after: %d\n", cairo_surface_get_reference_count(button));*/
    /*cairo_surface_destroy(button);*/
    //TODO : why can't destroy surface button
}

void DrawVKWindow(VKWindow* vkWindow)
{
    int i;
    int iPos;
    cairo_t *cr;
    FcitxVKState *vkstate = vkWindow->owner;
    VKS *vks = vkstate->vks;

    FcitxConfigColor *fontColor;
    fontColor = FcitxClassicUIGetKeyboardFontColor(vkstate->owner);
    char **font = FcitxClassicUIGetFont(vkstate->owner);

    if (!fontColor || !font) {
        fontColor = &blackColor;
        font = &vkWindow->defaultFont;
    }

    cr = cairo_create(vkWindow->surface);
    cairo_save(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
    cairo_paint(cr);
    cairo_restore(cr);
    cairo_surface_t* vkimage = LoadVKImage(vkWindow);
    cairo_set_source_surface(cr, vkimage, 0, 0);
    cairo_paint(cr);
    cairo_surface_flush(vkWindow->surface);

    draw_input_method_button(vkstate, cr);

    cairo_destroy(cr);
    cairo_surface_flush(vkWindow->surface);
}

/*
 * 处理相关鼠标键
 */
boolean VKMouseKey(FcitxVKState* vkstate, int x, int y)
{
    int r = get_click_region(x, y);
    if (r == -1)
        return true;
    static char keys[] = {
        '`', '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=',
        'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\\',
        'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'',
        'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/',

        '~', '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+',
        'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '|',
        'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"',
        'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?',
    };
    FcitxInstance* instance = vkstate->owner;
    if (FcitxInstanceGetCurrentIC(instance) == NULL)
        return false;

    if (r < 47) {
        char pstr[] = {0, 0};
        switch (r) {
            case 13: case 14: case 15: case 16: case 17: case 18: case 19: case 20: case 21: case 22:
            case 26: case 27: case 28: case 29: case 30: case 31: case 32: case 33: case 34:
            case 37: case 38: case 39: case 40: case 41: case 42: case 43:
                pstr[0] = keys[r + ((vkstate->bVKCaps ^ vkstate->bShiftPressed) ? 47 : 0)];
                break;
            default:
                pstr[0] =  keys[r + ((vkstate->bShiftPressed) ? 47 : 0)];
                break;
        }
        printf("commit: %c, shift:%d, cpas: %d\n", pstr[0], vkstate->bShiftPressed, vkstate->bVKCaps);

        FcitxKeySym sym;
        unsigned int state=0;
        FcitxHotkeyParseKey(pstr, &sym, &state);
        process_click(vkstate, sym, false);
    } else {
        switch (r) {
            case 47: 
                process_click(vkstate, FcitxKey_BackSpace, true);
                return true;
            case 48:
                process_click(vkstate, FcitxKey_Tab, true);
                return true;
            case 49:
                vkstate->bVKCaps = !vkstate->bVKCaps;
                vkstate->bShiftPressed = false;
                DrawVKWindow(vkstate->vkWindow);
                return true;
            case 50:
                {
                    process_click(vkstate, FcitxKey_Return, true);
                    return true;
                }
            case 51:
                {
                    //shift;
                    vkstate->bShiftPressed = !vkstate->bShiftPressed;
                    vkstate->bVKCaps = false;
                    DrawVKWindow(vkstate->vkWindow);
                    return true;
                }
            case 52:
                {
                    FcitxInputContext* ic = FcitxInstanceGetCurrentIC(instance);
                    FcitxInstanceChangeIMState(instance, ic);
                    DrawVKWindow(vkstate->vkWindow);
                    return true;
                }
            case 53: //ins
                process_click(vkstate, FcitxKey_Insert, true);
                return true;
            case 54: //del
                process_click(vkstate, FcitxKey_Delete, true);
                return true;
            case 55: //space
                {
                    /*strcpy(strKey, " ");*/
                }
            case 56: //esc
                {
                    /*SwitchVK(vkstate);*/
                    /*pstr = (char *) NULL;*/
                }
        }
    }
    return true;
}

/*
 * 根据字符查找符号
 */
char           *VKGetSymbol(FcitxVKState *vkstate, char cChar)
{
    int             i;

    for (i = 0; i < VK_NUMBERS; i++) {
        if (MyToUpper(vkTable[i]) == cChar)
            return vkstate->vks[vkstate->iCurrentVK].strSymbol[i][1];
        if (MyToLower(vkTable[i]) == cChar)
            return vkstate->vks[vkstate->iCurrentVK].strSymbol[i][0];
    }

    return NULL;
}

/*
 * 上/下档键字符转换，以取代toupper和tolower
 */
int MyToUpper(int iChar)
{
    const char           *pstr;

    pstr = strCharTable;
    while (*pstr) {
        if (*pstr == iChar)
            return *(pstr + 1);
        pstr += 2;
    }

    return toupper(iChar);
}

int MyToLower(int iChar)
{
    const char           *pstr;

    pstr = strCharTable + 1;
    for (;;) {
        if (*pstr == iChar)
            return *(pstr - 1);
        if (!(*(pstr + 1)))
            break;
        pstr += 2;
    }

    return tolower(iChar);
}


INPUT_RETURN_VALUE DoVKInput(FcitxVKState* vkstate, KeySym sym, int state)
{
    char           *pstr = NULL;
    FcitxInputState *input = FcitxInstanceGetInputState(vkstate->owner);

    if (FcitxHotkeyIsHotKeySimple(sym, state))
        pstr = VKGetSymbol(vkstate, sym);
    if (!pstr)
        return IRV_TO_PROCESS;
    else {
        strcpy(FcitxInputStateGetOutputString(input), pstr);
        return IRV_COMMIT_STRING;
    }
}

void SwitchVK(FcitxVKState *vkstate)
{
    FcitxInstance* instance = vkstate->owner;
    if (vkstate->vkWindow == NULL)
        vkstate->vkWindow = CreateVKWindow(vkstate);
    VKWindow *vkWindow = vkstate->vkWindow;
    /*if (!vkstate->iVKCount)*/
        /*return;*/

    vkstate->bVK = !vkstate->bVK;
    printf("huhuhuhuhu\n");

    /*if (true) {*/
    if (vkstate->bVK) {
        int             x, y;
        int dwidth, dheight;
        FcitxX11GetScreenSize(vkstate->owner, &dwidth, &dheight);

        if (!FcitxUISupportMainWindow(instance)) {
            x = dwidth / 2 - VK_WINDOW_WIDTH / 2;
            y = 40;
        } else {
            int mx = 0, my = 0, mw = 0, mh = 0;
            FcitxUIGetMainWindowSize(instance, &mx, &my, &mw, &mh);
            x = mx;
            y = my + mh + 2;
            if ((y + VK_WINDOW_HEIGHT) >= dheight)
                y = my - VK_WINDOW_HEIGHT - 2;
            if (y < 0)
                y = 0;
        }
        if ((x + VK_WINDOW_WIDTH) >= dwidth)
            x = dwidth - VK_WINDOW_WIDTH - 1;
        if (x < 0)
            x = 0;

        x = (dwidth - VK_WINDOW_WIDTH) / 2;
        y = dheight - VK_WINDOW_HEIGHT - 45;
        XMoveWindow(vkWindow->dpy, vkWindow->window, x, y);
        set_struct_partial(vkWindow->dpy, vkWindow->window, 
                dheight - y,
                x,
                x + VK_WINDOW_WIDTH);

        DisplayVKWindow(vkWindow);
        FcitxUICloseInputWindow(instance);

        FcitxInputContext* ic = FcitxInstanceGetCurrentIC(instance);

        if (ic && FcitxInstanceGetCurrentState(instance) == IS_CLOSED)
            FcitxInstanceEnableIM(instance, ic, true);
    } else {
        XUnmapWindow(vkWindow->dpy, vkWindow->window);
        FcitxInstanceCleanInputWindow(instance);
        FcitxUIUpdateInputWindow(instance);
    }
}

/*
*选择指定index的虚拟键盘
*/
void SelectVK(FcitxVKState* vkstate, int vkidx)
{
    vkstate->bVK = false;
    vkstate->iCurrentVK = vkidx;
    FcitxUIUpdateStatus(vkstate->owner, "vk");
    if (vkstate->vkWindow)
        DrawVKWindow(vkstate->vkWindow);
}

void
VKInitWindowAttribute(FcitxVKState* vkstate, Visual ** vs, Colormap * cmap,
                      XSetWindowAttributes * attrib,
                      unsigned long *attribmask, int *depth)
{
    FcitxX11InitWindowAttribute(vkstate->owner,
                                vs, cmap, attrib, attribmask, depth);
}

Visual * VKFindARGBVisual(FcitxVKState* vkstate)
{
    return FcitxX11FindARGBVisual(vkstate->owner);
}

void VKSetWindowProperty(FcitxVKState* vkstate, Window window, FcitxXWindowType type, char *windowTitle)
{
    FcitxX11SetWindowProp(vkstate->owner, &window, &type, windowTitle);
}

boolean
VKMouseClick(FcitxVKState* vkstate, Window window, int *x, int *y)
{
    boolean bMoved = false;
    FcitxX11MouseClick(vkstate->owner, &window, x, y, &bMoved);
    return bMoved;
}


void process_click(FcitxVKState* vkstate, FcitxKeySym sym, int special)
{
    FcitxInstance* instance = vkstate->owner;
    FcitxIM* im = FcitxInstanceGetCurrentIM(instance);
    if (im && strncmp(im->uniqueName, "fcitx-keyboard-cn", 17) != 0) { //Chinese input method
        printf("in Chinese input method\n");
        FcitxInstanceProcessKey(instance, FCITX_PRESS_KEY, 0, sym, 0);
    } else {
        printf("in English input method\n");
        if (special) {
            FcitxInstanceForwardKey(instance, FcitxInstanceGetCurrentIC(instance), FCITX_PRESS_KEY, sym, 0);
        } else {
            char* pstr = FcitxHotkeyGetKeyString(sym, 0);
            FcitxInstanceCommitString(instance, FcitxInstanceGetCurrentIC(instance), pstr);
            free(pstr);
        }
    }
}

static
int get_click_region(int x, int y)
{
#define BUILD_RECT(_x, _y) \
    rects[_count_].x = _x; \
    rects[_count_].y = _y; \
    rects[_count_].width = 45; \
    rects[_count_++].height = 42;

#define BUILD_SPECIAL(_x, _y, _w, _h) \
    rects[_count_].x = _x; \
    rects[_count_].y = _y; \
    rects[_count_].width = _w; \
    rects[_count_++].height = _h;

    static int _init = FALSE;
    static cairo_rectangle_int_t rects[47+10];
    if (!_init) {
        int _count_ = 0;
        BUILD_RECT(14, 15);          // ~
        BUILD_RECT(66, 15);          // 1
        BUILD_RECT(120, 15);         // 2
        BUILD_RECT(172, 15);         // 3
        BUILD_RECT(225, 15);         // 4
        BUILD_RECT(278, 15);         // 5
        BUILD_RECT(331, 15);         // 6
        BUILD_RECT(383, 15);         // 7
        BUILD_RECT(437, 15);         // 8
        BUILD_RECT(489, 15);         // 9
        BUILD_RECT(542, 15);        // 0
        BUILD_RECT(595, 15);        // -
        BUILD_RECT(648, 15);        // =

        BUILD_RECT(90, 66);             // q
        BUILD_RECT(143, 66);             // w
        BUILD_RECT(196, 66);             // e
        BUILD_RECT(249, 66);             // r
        BUILD_RECT(301, 66);             // t
        BUILD_RECT(356, 66);             // y
        BUILD_RECT(407, 66);             // u
        BUILD_RECT(460, 66);             // i
        BUILD_RECT(513, 66);             // o
        BUILD_RECT(566, 66);             // p
        BUILD_RECT(618, 66);             // [
        BUILD_RECT(672, 66);             // ]
        BUILD_RECT(725, 66);             // |

        BUILD_RECT(109, 116);             // a
        BUILD_RECT(161, 116);             // s
        BUILD_RECT(214, 116);             // d
        BUILD_RECT(265, 116);             // f
        BUILD_RECT(316, 116);             // g
        BUILD_RECT(369, 116);             // h
        BUILD_RECT(420, 116);             // j
        BUILD_RECT(472, 116);             // k
        BUILD_RECT(522, 116);             // l
        BUILD_RECT(576, 116);             // ;
        BUILD_RECT(626, 116);             // '

        BUILD_RECT(136, 169);             // z
        BUILD_RECT(187, 169);             // x
        BUILD_RECT(240, 169);             // c
        BUILD_RECT(291, 169);             // v
        BUILD_RECT(342, 169);             // b
        BUILD_RECT(394, 169);             // n
        BUILD_RECT(446, 169);             // m
        BUILD_RECT(497, 169);             // ,
        BUILD_RECT(548, 169);             // .
        BUILD_RECT(601, 169);             // /

        BUILD_SPECIAL(698, 15, 71, 42);   //backspace

        BUILD_SPECIAL(14, 66, 71, 42);    //Tab

        BUILD_SPECIAL(14, 116, 89, 42);    //CapsLock
        BUILD_SPECIAL(680, 116, 89, 42);    //Enter

        BUILD_SPECIAL(15, 168, 110, 42);    //Shift
        BUILD_SPECIAL(683, 168, 115, 51);    // IMChange

        BUILD_SPECIAL(14, 221, 88, 51);    //Insert
        BUILD_SPECIAL(137, 221, 89, 51);    //Delete
        BUILD_SPECIAL(234, 221, 365, 52);    //Space
        BUILD_SPECIAL(654, 221, 115, 51);    //Esc

        _init = TRUE;
    }
    int i=0;
    for (; i < 47+10; i++) {
        /*printf("checek (%d,%d)  wehter in %d(%d,%d,%d,%d)\n", x, y, i, rects[i].x, rects[i].y, rects[i].width, rects[i].height);*/
        if (FcitxUIIsInBox(x, y, rects[i].x, rects[i].y, rects[i].width, rects[i].height))
            return i;
    }
    return -1;
}

void set_struct_partial(Display* display, Window window, guint32 strut, guint32 strut_start, guint32 strut_end)
{
    gulong   struts [12] = { 0, };

    static Atom net_wm_strut_partial = None;
    if (net_wm_strut_partial == None)
        net_wm_strut_partial = XInternAtom (display, "_NET_WM_STRUT_PARTIAL", False);

    enum {
        STRUT_LEFT = 0,
        STRUT_RIGHT = 1,
        STRUT_TOP = 2,
        STRUT_BOTTOM = 3,
        STRUT_LEFT_START = 4,
        STRUT_LEFT_END = 5,
        STRUT_RIGHT_START = 6,
        STRUT_RIGHT_END = 7,
        STRUT_TOP_START = 8,
        STRUT_TOP_END = 9,
        STRUT_BOTTOM_START = 10,
        STRUT_BOTTOM_END = 11
    };
    struts [STRUT_BOTTOM] = strut;
    struts [STRUT_BOTTOM_START] = strut_start;
    struts [STRUT_BOTTOM_END] = strut_end;

    XChangeProperty (display, window, net_wm_strut_partial,
            XA_CARDINAL, 32, PropModeReplace,
            (guchar *) &struts, 12);
}


// kate: indent-mode cstyle; space-indent on; indent-width 0;
