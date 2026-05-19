#ifndef UNICODE
#define UNICODE
#endif 

#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <d2d1.h>
#include <dwrite.h>
#include <string>
#include "resource.h"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")

// ================================================================================================
// Globals
// ================================================================================================
bool gRunning = true;
bool gInSizeMove = false;
UINT_PTR gRenderTimerId = 1;

ID3D11Device* gDevice = nullptr;
ID3D11DeviceContext* gContext = nullptr;
IDXGISwapChain* gSwapChain = nullptr;
ID3D11RenderTargetView* gRTV = nullptr;

ID2D1Factory* gD2DFactory = nullptr;
ID2D1RenderTarget* gD2DTarget = nullptr;
IDWriteFactory* gDWriteFactory = nullptr;
IDWriteTextFormat* gTextFormat = nullptr;
ID2D1SolidColorBrush* gWhiteBrush = nullptr;
ID2D1SolidColorBrush* gBlackBrush = nullptr;

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

// ================================================================================================
// Vector Outlined Text (with MITER + miterLimit = 2.0)
// ================================================================================================
void DrawOutlinedText(
    ID2D1RenderTarget* target,
    IDWriteFactory* dwriteFactory,
    IDWriteTextFormat* format,
    const wchar_t* text,
    float x, float y,
    float outlineThickness,
    ID2D1Brush* fillBrush,
    ID2D1Brush* outlineBrush)
{
    if (!target || !dwriteFactory || !format)
        return;

    IDWriteTextLayout* layout = nullptr;
    dwriteFactory->CreateTextLayout(
        text,
        (UINT32)wcslen(text),
        format,
        2000.0f,
        2000.0f,
        &layout
    );
    if (!layout)
        return;

    ID2D1PathGeometry* geometry = nullptr;
    gD2DFactory->CreatePathGeometry(&geometry);

    ID2D1GeometrySink* sink = nullptr;
    geometry->Open(&sink);

    struct GlyphRenderer : public IDWriteTextRenderer {
        ID2D1GeometrySink* sink;
        FLOAT ascent;
        GlyphRenderer(ID2D1GeometrySink* s) : sink(s), ascent(0.0f) {}

        IFACEMETHOD(DrawGlyphRun)(
            void*, FLOAT, FLOAT, DWRITE_MEASURING_MODE,
            const DWRITE_GLYPH_RUN* glyphRun,
            const DWRITE_GLYPH_RUN_DESCRIPTION*, IUnknown*) override
        {
            if (!glyphRun || !sink) return S_OK;

            DWRITE_FONT_METRICS fm = {};
            glyphRun->fontFace->GetMetrics(&fm);
            ascent = (FLOAT)fm.ascent / fm.designUnitsPerEm * glyphRun->fontEmSize;

            glyphRun->fontFace->GetGlyphRunOutline(
                glyphRun->fontEmSize,
                glyphRun->glyphIndices,
                glyphRun->glyphAdvances,
                glyphRun->glyphOffsets,
                glyphRun->glyphCount,
                FALSE, FALSE,
                sink
            );
            return S_OK;
        }

        IFACEMETHOD(DrawUnderline)(void*, FLOAT, FLOAT, const DWRITE_UNDERLINE*, IUnknown*) override { return S_OK; }
        IFACEMETHOD(DrawStrikethrough)(void*, FLOAT, FLOAT, const DWRITE_STRIKETHROUGH*, IUnknown*) override { return S_OK; }
        IFACEMETHOD(DrawInlineObject)(void*, FLOAT, FLOAT, IDWriteInlineObject*, BOOL, BOOL, IUnknown*) override { return S_OK; }
        IFACEMETHOD(IsPixelSnappingDisabled)(void*, BOOL* disabled) override { *disabled = TRUE; return S_OK; }
        IFACEMETHOD(GetCurrentTransform)(void*, DWRITE_MATRIX* m) override { *m = DWRITE_MATRIX{ 1,0,0,1,0,0 }; return S_OK; }
        IFACEMETHOD(GetPixelsPerDip)(void*, FLOAT* ppd) override { *ppd = 1.0f; return S_OK; }
        IFACEMETHOD_(ULONG, AddRef)() override { return 1; }
        IFACEMETHOD_(ULONG, Release)() override { return 1; }
        IFACEMETHOD(QueryInterface)(REFIID riid, void** ppv) override {
            if (riid == __uuidof(IDWriteTextRenderer)) { *ppv = this; return S_OK; }
            *ppv = nullptr; return E_NOINTERFACE;
        }
    };

    GlyphRenderer renderer(sink);
    layout->Draw(nullptr, &renderer, 0, 0);
    sink->Close();

    float baselineY = y + renderer.ascent;
    target->SetTransform(D2D1::Matrix3x2F::Translation(x, baselineY));

    // -------------------------------------------------------------------------
    // NEW: Stroke style with MITER join + miterLimit = 2.0
    // -------------------------------------------------------------------------
    D2D1_STROKE_STYLE_PROPERTIES strokeProps = {};
    strokeProps.lineJoin = D2D1_LINE_JOIN_MITER;
    strokeProps.miterLimit = 2.0f;

    ID2D1StrokeStyle* strokeStyle = nullptr;
    gD2DFactory->CreateStrokeStyle(strokeProps, nullptr, 0, &strokeStyle);

    target->DrawGeometry(geometry, outlineBrush, outlineThickness, strokeStyle);
    target->FillGeometry(geometry, fillBrush);

    if (strokeStyle) strokeStyle->Release();
    target->SetTransform(D2D1::Matrix3x2F::Identity());

    if (sink) sink->Release();
    if (geometry) geometry->Release();
    if (layout) layout->Release();
}

// ================================================================================================
// Init Direct3D + Direct2D
// ================================================================================================
void InitD3D(HWND hwnd)
{
    DXGI_SWAP_CHAIN_DESC scd = {};
    scd.BufferCount = 1;
    scd.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.OutputWindow = hwnd;
    scd.SampleDesc.Count = 1;
    scd.Windowed = TRUE;

    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;

    D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
        flags, nullptr, 0, D3D11_SDK_VERSION,
        &scd, &gSwapChain, &gDevice, nullptr, &gContext
    );

    ID3D11Texture2D* backBuffer = nullptr;
    gSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&backBuffer);
    gDevice->CreateRenderTargetView(backBuffer, nullptr, &gRTV);
    backBuffer->Release();

    D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &gD2DFactory);

    DWriteCreateFactory(
        DWRITE_FACTORY_TYPE_SHARED,
        __uuidof(IDWriteFactory),
        (IUnknown**)&gDWriteFactory
    );

    IDXGISurface* dxgiSurface = nullptr;
    gSwapChain->GetBuffer(0, __uuidof(IDXGISurface), (void**)&dxgiSurface);

    D2D1_RENDER_TARGET_PROPERTIES props =
        D2D1::RenderTargetProperties(
            D2D1_RENDER_TARGET_TYPE_DEFAULT,
            D2D1::PixelFormat(DXGI_FORMAT_UNKNOWN, D2D1_ALPHA_MODE_PREMULTIPLIED)
        );

    gD2DFactory->CreateDxgiSurfaceRenderTarget(dxgiSurface, &props, &gD2DTarget);
    dxgiSurface->Release();
}

void InitD2D()
{
    gDWriteFactory->CreateTextFormat(
        L"Segoe UI Semibold", nullptr,
        DWRITE_FONT_WEIGHT_DEMI_BOLD,
        DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        48.0f, L"en-us",
        &gTextFormat
    );

    gD2DTarget->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::White), &gWhiteBrush);
    gD2DTarget->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::Black), &gBlackBrush);
}

// ================================================================================================
// Resize Handling
// ================================================================================================
void Resize(UINT width, UINT height)
{
    if (!gSwapChain) return;

    if (gD2DTarget) { gD2DTarget->Release(); gD2DTarget = nullptr; }
    if (gRTV) { gRTV->Release(); gRTV = nullptr; }

    gSwapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_B8G8R8A8_UNORM, 0);

    ID3D11Texture2D* backBuffer = nullptr;
    gSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&backBuffer);
    gDevice->CreateRenderTargetView(backBuffer, nullptr, &gRTV);
    backBuffer->Release();

    IDXGISurface* dxgiSurface = nullptr;
    gSwapChain->GetBuffer(0, __uuidof(IDXGISurface), (void**)&dxgiSurface);

    D2D1_RENDER_TARGET_PROPERTIES props =
        D2D1::RenderTargetProperties(
            D2D1_RENDER_TARGET_TYPE_DEFAULT,
            D2D1::PixelFormat(DXGI_FORMAT_UNKNOWN, D2D1_ALPHA_MODE_PREMULTIPLIED)
        );

    gD2DFactory->CreateDxgiSurfaceRenderTarget(dxgiSurface, &props, &gD2DTarget);
    dxgiSurface->Release();
}

// ================================================================================================
// wstring to Get Current System Time
// ================================================================================================
std::wstring GetCurrentTimeString()
{
    SYSTEMTIME st;
    GetLocalTime(&st);

    wchar_t buffer[64];
    swprintf_s(buffer, L"%02d:%02d:%02d %s",
        (st.wHour % 12 == 0 ? 12 : st.wHour % 12),
        st.wMinute,
        st.wSecond,
        (st.wHour < 12 ? L"AM" : L"PM")
    );

    return std::wstring(buffer);
}

// ================================================================================================
// Render Loop
// ================================================================================================
void Render()
{
    if (!gRunning) return;
    if (!gContext || !gRTV || !gD2DTarget) return;
    float clearColor[4] = { 0.1f, 0.1f, 0.3f, 1.0f };
    gContext->ClearRenderTargetView(gRTV, clearColor);

    gD2DTarget->BeginDraw();

    DrawOutlinedText(
        gD2DTarget,
        gDWriteFactory,
        gTextFormat,
        // L"Hello DirectX~",            // <-- this is static long string
        GetCurrentTimeString().c_str(),  // <-- this will call get current time string
        30.0f, 20.0f,
        8.0f,
        gWhiteBrush,
        gBlackBrush
    );

    // Another Outlined Text Test
    DrawOutlinedText(
        gD2DTarget,
        gDWriteFactory,
        gTextFormat,
        L"This is only a test",             // <-- this is static long string
        // GetCurrentTimeString().c_str(),  // <-- this will call get current time string
        320.0f, 20.0f,
        8.0f,
        gWhiteBrush,
        gBlackBrush
    );

    gD2DTarget->EndDraw();
    gSwapChain->Present(1, 0);
}

// ================================================================================================
// WinMain
// ================================================================================================
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int nCmdShow)
{
    const wchar_t CLASS_NAME[] = L"DX Window Class";

    WNDCLASS wc = {};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_ICON1));


    RegisterClass(&wc);

    HWND hwnd = CreateWindowEx(
        0, CLASS_NAME, L"DirectX Test Program",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        1024, 600,
        nullptr, nullptr,
        hInstance, nullptr
    );

    ShowWindow(hwnd, nCmdShow);

    InitD3D(hwnd);
    InitD2D();

    MSG msg = {};

    while (gRunning)
    {
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_QUIT)
                gRunning = false;

            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        if (!gInSizeMove)
            Render();
    }

    return 0;
}

// ================================================================================================
// Window Procedure
// ================================================================================================
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_ENTERSIZEMOVE:
        gInSizeMove = true;
        // ~100 FPS timer (10 ms)
        SetTimer(hwnd, gRenderTimerId, 10, nullptr);
        return 0;

    case WM_EXITSIZEMOVE:
        gInSizeMove = false;
        KillTimer(hwnd, gRenderTimerId);
        return 0;

    case WM_TIMER:
        if (wParam == gRenderTimerId)
        {
            Render();
        }
        return 0;

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        BeginPaint(hwnd, &ps);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_SIZE:
    {
        UINT w = LOWORD(lParam);
        UINT h = HIWORD(lParam);
        if (w > 0 && h > 0)
            Resize(w, h);
        return 0;
    }

    case WM_DESTROY:
        gRunning = false;

        if (gWhiteBrush)    gWhiteBrush->Release();
        if (gBlackBrush)    gBlackBrush->Release();
        if (gTextFormat)    gTextFormat->Release();
        if (gD2DTarget)     gD2DTarget->Release();
        if (gDWriteFactory) gDWriteFactory->Release();
        if (gD2DFactory)    gD2DFactory->Release();
        if (gRTV)           gRTV->Release();
        if (gSwapChain)     gSwapChain->Release();
        if (gContext)       gContext->Release();
        if (gDevice)        gDevice->Release();

        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}
