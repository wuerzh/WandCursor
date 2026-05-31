#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <windows.h>
#include <gdiplus.h>
#include <cmath>

#ifdef _MSC_VER
#pragma comment(lib, "gdiplus.lib")
#endif

#define HK_TOGGLE  1
#define HK_EXIT    2

/* ---- Window / anchor (design units @ 96 DPI) -------------------------------
   The overlay window tightly bounds the wand. The cursor hotspot sits at
   (CURSOR_OX, CURSOR_OY) in these design units; the wand tip is drawn exactly
   there so it points precisely at the cursor. At startup every dimension is
   multiplied by WAND_SCALE only, so the wand keeps the same visual size even
   when Windows display scaling changes.                                      */
#define WINDOW_W        320
#define WINDOW_H        320
#define CURSOR_OX       64
#define CURSOR_OY       64

/* Overall size multiplier, independent of DPI. 1.0 == original size; raise it
   to enlarge the whole wand (the tip still lands exactly on the cursor). */
#define WAND_SCALE      1.5

static const double WAND_ANGLE = 0.7853981633974483; /* 45 degrees */

static BOOL      g_visible    = TRUE;
static HWND      g_hwnd       = NULL;
static POINT     g_lastPos    = { -1, -1 };
static ULONG_PTR g_gdiToken   = 0;

/* Visual scale resolved once at startup. The process is DPI-aware so Windows
   does not bitmap-stretch the overlay, but the wand size intentionally ignores
   system DPI and follows WAND_SCALE only. */
static double    g_scale      = 1.0;
static int       g_winW       = WINDOW_W;
static int       g_winH       = WINDOW_H;
static int       g_ox         = CURSOR_OX;
static int       g_oy         = CURSOR_OY;

static void InitGDIPlus(void)
{
    Gdiplus::GdiplusStartupInput si;
    Gdiplus::GdiplusStartup(&g_gdiToken, &si, NULL);
}

static void ShutdownGDIPlus(void)
{
    Gdiplus::GdiplusShutdown(g_gdiToken);
}

typedef BOOL (WINAPI *SetProcessDpiAwarenessContextFn)(DPI_AWARENESS_CONTEXT);

static void InitDpiAwareness(void)
{
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32) {
        union {
            FARPROC proc;
            SetProcessDpiAwarenessContextFn fn;
        } setCtx;
        setCtx.proc = GetProcAddress(user32, "SetProcessDpiAwarenessContext");
        if (setCtx.fn && setCtx.fn(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2))
            return;
    }

    SetProcessDPIAware();
}

static void InitMemDC(int w, int h, HDC *pMemDC, HBITMAP *pBmp, HBITMAP *pOldBmp)
{
    HDC hdcScreen = GetDC(NULL);
    *pMemDC = CreateCompatibleDC(hdcScreen);

    BITMAPINFO bmi;
    ZeroMemory(&bmi, sizeof(bmi));
    bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth       = w;
    bmi.bmiHeader.biHeight      = -h;
    bmi.bmiHeader.biPlanes      = 1;
    bmi.bmiHeader.biBitCount    = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    LPVOID pBits = NULL;
    *pBmp = CreateDIBSection(*pMemDC, &bmi, DIB_RGB_COLORS, &pBits, NULL, 0);

    /* CreateDIBSection hands back zeroed memory; make the transparent-BGRA
       clear explicit so the layered-window alpha starts fully transparent. */
    memset(pBits, 0, (size_t)w * (size_t)h * 4);

    *pOldBmp = (HBITMAP)SelectObject(*pMemDC, *pBmp);

    ReleaseDC(NULL, hdcScreen);
}

static void CleanupMemDC(HDC memDC, HBITMAP bmp, HBITMAP oldBmp)
{
    SelectObject(memDC, oldBmp);
    DeleteObject(bmp);
    DeleteDC(memDC);
}

/* ============================================================================
   Wand renderer  --  "Crimson Wand" pointer

   A slender magician's wand whose ivory tip points exactly at the cursor: the
   wand itself IS the pointer. No glow or sparkles around the cursor, so the
   material and UI underneath stay fully readable. Glossy crimson body, gold
   ferrule and domed cap, ivory tip. All GDI+ primitives, crisp at any DPI.
   Segments run along a 45 degree axis from the tip (= cursor):

       tip(ivory) -> shaft(crimson) -> ferrule(gold) -> handle -> cap(gold)
   ============================================================================ */

/* Fill a tapered segment with a perpendicular (cylindrical) multi-stop gradient. */
static void FillSeg(Gdiplus::Graphics &g,
    double cx, double cy, double dx, double dy, double nx, double ny,
    double d0, double d1, double w0, double w1,
    const Gdiplus::Color *cols, const Gdiplus::REAL *pos, int n)
{
    using namespace Gdiplus;
    double dm = (d0 + d1) * 0.5;
    double W  = (w0 > w1 ? w0 : w1);
    PointF gp0((REAL)(cx + dx*dm - nx*W), (REAL)(cy + dy*dm - ny*W));
    PointF gp1((REAL)(cx + dx*dm + nx*W), (REAL)(cy + dy*dm + ny*W));
    LinearGradientBrush br(gp0, gp1, cols[0], cols[n - 1]);
    br.SetInterpolationColors(cols, pos, n);
    br.SetWrapMode(WrapModeTileFlipX);
    PointF pts[4] = {
        PointF((REAL)(cx + dx*d0 + nx*w0), (REAL)(cy + dy*d0 + ny*w0)),
        PointF((REAL)(cx + dx*d1 + nx*w1), (REAL)(cy + dy*d1 + ny*w1)),
        PointF((REAL)(cx + dx*d1 - nx*w1), (REAL)(cy + dy*d1 - ny*w1)),
        PointF((REAL)(cx + dx*d0 - nx*w0), (REAL)(cy + dy*d0 - ny*w0)),
    };
    g.FillPolygon(&br, pts, 4);
}

/* Radial fill (PathGradientBrush): edge -> mid -> core, optional center offset.
   Note GDI+ path-gradient positions run boundary(0) -> center(1).            */
static void RadialFill(Gdiplus::Graphics &g, double cx, double cy, double r,
    Gdiplus::Color edge, Gdiplus::Color mid, Gdiplus::Color core,
    double offx, double offy)
{
    using namespace Gdiplus;
    GraphicsPath path;
    path.AddEllipse((REAL)(cx - r), (REAL)(cy - r), (REAL)(2.0*r), (REAL)(2.0*r));
    PathGradientBrush pgb(&path);
    pgb.SetCenterPoint(PointF((REAL)(cx + offx), (REAL)(cy + offy)));
    Color cols[3] = { edge, mid, core };
    REAL  pos[3]  = { 0.0f, 0.55f, 1.0f };
    pgb.SetInterpolationColors(cols, pos, 3);
    g.FillPath(&pgb, &path);
}

/* A specular highlight line along the axis, fading at both ends. */
static void DrawSpec(Gdiplus::Graphics &g,
    double cx, double cy, double dx, double dy, double nx, double ny,
    double d0, double d1, double off, double w, Gdiplus::Color col)
{
    using namespace Gdiplus;
    double ax = cx + dx*d0 + nx*off, ay = cy + dy*d0 + ny*off;
    double bx = cx + dx*d1 + nx*off, by = cy + dy*d1 + ny*off;
    Color trans(0, col.GetRed(), col.GetGreen(), col.GetBlue());
    LinearGradientBrush br(PointF((REAL)ax,(REAL)ay), PointF((REAL)bx,(REAL)by), trans, trans);
    Color cols[4] = { trans, col, col, trans };
    REAL  pos[4]  = { 0.0f, 0.18f, 0.8f, 1.0f };
    br.SetInterpolationColors(cols, pos, 4);
    Pen pen(&br, (REAL)w);
    pen.SetStartCap(LineCapRound);
    pen.SetEndCap(LineCapRound);
    g.DrawLine(&pen, (REAL)ax, (REAL)ay, (REAL)bx, (REAL)by);
}

static void RenderWand(HDC hdc)
{
    using namespace Gdiplus;

    Graphics g(hdc);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetCompositingQuality(CompositingQualityHighQuality);
    g.ScaleTransform((REAL)g_scale, (REAL)g_scale);  /* design units -> device px */

    double cx = CURSOR_OX;
    double cy = CURSOR_OY;
    double dx = cos(WAND_ANGLE);
    double dy = sin(WAND_ANGLE);
    double nx = -dy;     /* unit normal; the lit side faces -n (upper right) */
    double ny =  dx;

    /* Axis positions of the segment joints (tip at 0 -> cap end at 300). */
    const double D_TIP0 = 0.0,   D_TIP1 = 34.0;   /* ivory tip    : 34        */
    const double D_SHF1 = 214.0;                  /* crimson shaft: +180      */
    const double D_FER1 = 228.0;                  /* gold ferrule : +14       */
    const double D_HND1 = 288.0;                  /* handle       : +60       */
    const double D_CAP1 = 300.0;                  /* gold cap     : +12       */

    /* --- palette: crimson shaft + gold metal + ivory tip --------------- */
    Color shaft[5] = {
        Color(255, 53, 4, 6), Color(255, 255, 86, 56), Color(255, 163, 22, 12),
        Color(255, 108, 12, 7), Color(255, 42, 3, 4) };
    REAL  shaftP[5] = { 0.0f, 0.27f, 0.5f, 0.72f, 1.0f };

    Color metal[5] = {
        Color(255, 110, 71, 17), Color(255, 255, 231, 160), Color(255, 236, 187, 85),
        Color(255, 185, 131, 31), Color(255, 90, 58, 15) };
    REAL  metalP[5] = { 0.0f, 0.22f, 0.44f, 0.64f, 1.0f };

    Color tip[4] = {
        Color(255, 205, 191, 159), Color(255, 255, 250, 240),
        Color(255, 241, 230, 207), Color(255, 199, 180, 143) };
    REAL  tipP[4] = { 0.0f, 0.3f, 0.55f, 1.0f };

    /* segment geometry: d0,d1 along axis, w0,w1 half-widths */
    double seg[5][4] = {
        { D_TIP0, D_TIP1, 0.0, 5.0 },
        { D_TIP1, D_SHF1, 5.0, 8.5 },
        { D_SHF1, D_FER1, 9.6, 9.6 },
        { D_FER1, D_HND1, 9.0, 9.6 },
        { D_HND1, D_CAP1, 9.6, 7.5 },
    };

    /* 1) the wand body, tip -> cap (cylindrical cross-section shading) */
    FillSeg(g, cx, cy, dx, dy, nx, ny, seg[0][0], seg[0][1], seg[0][2], seg[0][3], tip,   tipP,   4);
    FillSeg(g, cx, cy, dx, dy, nx, ny, seg[1][0], seg[1][1], seg[1][2], seg[1][3], shaft, shaftP, 5);
    FillSeg(g, cx, cy, dx, dy, nx, ny, seg[2][0], seg[2][1], seg[2][2], seg[2][3], metal, metalP, 5);
    FillSeg(g, cx, cy, dx, dy, nx, ny, seg[3][0], seg[3][1], seg[3][2], seg[3][3], shaft, shaftP, 5);
    FillSeg(g, cx, cy, dx, dy, nx, ny, seg[4][0], seg[4][1], seg[4][2], seg[4][3], metal, metalP, 5);

    /* domed gold cap at the far end */
    {
        double ce = D_CAP1 - 2.0;
        double cxe = cx + dx * ce, cye = cy + dy * ce;
        RadialFill(g, cxe, cye, 8.4,
            Color(255, 90, 58, 15), Color(255, 236, 187, 85), Color(255, 255, 231, 160),
            -nx * 3.0, -ny * 3.0);
    }

    /* 2) specular reflection lines along shaft and handle */
    DrawSpec(g, cx, cy, dx, dy, nx, ny,  36.0, 212.0, -3.0, 1.7, Color(217, 255, 245, 235));
    DrawSpec(g, cx, cy, dx, dy, nx, ny, 230.0, 286.0, -3.4, 1.7, Color(217, 255, 245, 235));
}

static void UpdateOverlay(void)
{
    if (!g_visible)
        return;

    POINT pt;
    GetCursorPos(&pt);

    if (pt.x == g_lastPos.x && pt.y == g_lastPos.y)
        return;
    g_lastPos = pt;

    HDC memDC = NULL;
    HBITMAP bmp = NULL, oldBmp = NULL;
    InitMemDC(g_winW, g_winH, &memDC, &bmp, &oldBmp);

    RenderWand(memDC);

    BLENDFUNCTION bf;
    ZeroMemory(&bf, sizeof(bf));
    bf.BlendOp             = AC_SRC_OVER;
    bf.SourceConstantAlpha = 255;
    bf.AlphaFormat         = AC_SRC_ALPHA;

    POINT ptDst = { pt.x - g_ox, pt.y - g_oy };
    SIZE  szWnd = { g_winW, g_winH };
    POINT ptSrc = { 0, 0 };

    UpdateLayeredWindow(g_hwnd, NULL, &ptDst, &szWnd,
        memDC, &ptSrc, 0, &bf, ULW_ALPHA);

    CleanupMemDC(memDC, bmp, oldBmp);
}

static void ShowHideOverlay(void)
{
    if (g_visible) {
        ShowWindow(g_hwnd, SW_SHOWNOACTIVATE);
        g_lastPos.x = -1;
        g_lastPos.y = -1;
        UpdateOverlay();
    } else {
        ShowWindow(g_hwnd, SW_HIDE);
    }
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE:
        if (!RegisterHotKey(hwnd, HK_TOGGLE, MOD_CONTROL | MOD_ALT, 'Z'))
            return -1;
        if (!RegisterHotKey(hwnd, HK_EXIT,   MOD_CONTROL | MOD_ALT, 'X'))
            return -1;
        SetTimer(hwnd, 1, 16, NULL);
        break;

    case WM_HOTKEY:
        if (wp == HK_TOGGLE) {
            g_visible = !g_visible;
            ShowHideOverlay();
        } else if (wp == HK_EXIT) {
            DestroyWindow(hwnd);
        }
        break;

    case WM_TIMER:
        UpdateOverlay();
        break;

    case WM_DPICHANGED:
        /* Per-monitor aware: ignore the suggested DPI-scaled rect because the
           wand's visual size is intentionally fixed by WAND_SCALE. */
        g_lastPos.x = -1;
        g_lastPos.y = -1;
        UpdateOverlay();
        break;

    case WM_DESTROY:
        KillTimer(hwnd, 1);
        UnregisterHotKey(hwnd, HK_TOGGLE);
        UnregisterHotKey(hwnd, HK_EXIT);
        PostQuitMessage(0);
        break;

    default:
        return DefWindowProc(hwnd, msg, wp, lp);
    }
    return 0;
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE hPrev, LPWSTR lpCmd, int nShow)
{
    (void)hPrev;
    (void)lpCmd;
    (void)nShow;

    /* Become DPI-aware so Windows reports physical cursor coordinates and does
       not bitmap-stretch the layered window. The wand's visual size stays fixed
       across system display scaling changes and is controlled by WAND_SCALE. */
    InitDpiAwareness();
    {
        g_scale = WAND_SCALE;
        g_winW  = (int)(WINDOW_W  * g_scale + 0.5);
        g_winH  = (int)(WINDOW_H  * g_scale + 0.5);
        g_ox    = (int)(CURSOR_OX * g_scale + 0.5);
        g_oy    = (int)(CURSOR_OY * g_scale + 0.5);
    }

    InitGDIPlus();

    WNDCLASSEX wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize        = sizeof(WNDCLASSEX);
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;
    wc.lpszClassName = L"WandCursorCls";
    wc.style         = CS_HREDRAW | CS_VREDRAW;

    if (!RegisterClassEx(&wc))
        return 1;

    g_hwnd = CreateWindowEx(
        WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        wc.lpszClassName,
        L"WandCursor",
        WS_POPUP,
        0, 0, g_winW, g_winH,
        NULL, NULL, hInst, NULL);

    if (!g_hwnd)
        return 1;

    ShowWindow(g_hwnd, SW_SHOWNOACTIVATE);
    UpdateOverlay();

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    ShutdownGDIPlus();
    return (int)msg.wParam;
}
