#include "dearimgui/imgui.h"
#include "dearimgui/backends/imgui_impl_win32.h"
#include "dearimgui/backends/imgui_impl_dx11.h"

#include <Windows.h>
#include <d3d11.h>
#include <directxmath.h>

#define DEFAULT_CLIENT_WIDTH 1280
#define DEFAULT_CLIENT_HEIGHT 720

// Data 
static ID3D11Device*            g_pd3dDevice = nullptr;
static ID3D11DeviceContext*     g_pd3dDeviceContext = nullptr;
static IDXGISwapChain*          g_pSwapChain = nullptr;
static UINT                     g_ResizeWidth = 0, g_ResizeHeight = 0;
static ID3D11RenderTargetView*  g_mainRenderTargetView = nullptr;

// Forward declarations of helper functions
bool CreateDeviceD3D(HWND hWnd);
void CleanupDeviceD3D();
void CreateRenderTarget();
void CleanupRenderTarget();
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

void ProcessWindowMessages(bool& quitReceived, int& exitCode)
{
    MSG windowMessage;

    while(PeekMessage(&windowMessage, nullptr, 0, 0, PM_REMOVE))
    {
        switch(windowMessage.message)
        {
            case(WM_QUIT):
            {
                quitReceived = true;
                exitCode = static_cast<int>(windowMessage.wParam);
                return;
            } break;
        }

        TranslateMessage(&windowMessage);
        DispatchMessage(&windowMessage);
    }
}

LRESULT WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    if(ImGui_ImplWin32_WndProcHandler(hwnd, uMsg, wParam, lParam))
    {
        return true;
    }

    switch(uMsg)
    {
        // TODO(Iker): Handle confirmation of destructive action
        case WM_CLOSE:
        {

        } break;

        case WM_DESTROY:
        {
            PostQuitMessage(0);
            return 0;
        } break;
    }

    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

// Helper functions
bool CreateDeviceD3D(HWND hWnd)
{
    // Setup swap chain
    DXGI_SWAP_CHAIN_DESC sd;
    ZeroMemory(&sd, sizeof(sd));
    sd.BufferCount = 2;
    sd.BufferDesc.Width = 0;
    sd.BufferDesc.Height = 0;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    UINT createDeviceFlags = 0;
    //createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL featureLevelArray[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0, };
    HRESULT res = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createDeviceFlags, featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
    if (res == DXGI_ERROR_UNSUPPORTED) // Try high-performance WARP software driver if hardware is not available.
        res = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, createDeviceFlags, featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
    if (res != S_OK)
        return false;

    CreateRenderTarget();
    return true;
}

void CleanupDeviceD3D()
{
    CleanupRenderTarget();
    if (g_pSwapChain) { g_pSwapChain->Release(); g_pSwapChain = nullptr; }
    if (g_pd3dDeviceContext) { g_pd3dDeviceContext->Release(); g_pd3dDeviceContext = nullptr; }
    if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = nullptr; }
}

void CreateRenderTarget()
{
    ID3D11Texture2D* pBackBuffer;
    g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_mainRenderTargetView);
    pBackBuffer->Release();
}

void CleanupRenderTarget()
{
    if (g_mainRenderTargetView) { g_mainRenderTargetView->Release(); g_mainRenderTargetView = nullptr; }
}

int WinMain(HINSTANCE hInstance, HINSTANCE /*hPrevInstance*/, LPSTR /*lpCmdLine*/, int nShowCmd)
{
    const char dotDiffCppWindowClassName[] = "DotDiffCpp Window Class";

    WNDCLASSEX windowClass = {};

    windowClass.cbSize = sizeof(WNDCLASSEX);
    windowClass.hInstance = hInstance;
    windowClass.lpszClassName = dotDiffCppWindowClassName;
    windowClass.style = CS_DBLCLKS | CS_OWNDC;
    windowClass.lpfnWndProc = WindowProc;

    RegisterClassEx(&windowClass);

    RECT desiredClientRect { 0, 0, DEFAULT_CLIENT_WIDTH, DEFAULT_CLIENT_HEIGHT };
    AdjustWindowRect(&desiredClientRect, WS_OVERLAPPEDWINDOW, FALSE);

    HWND windowHandle = CreateWindowEx(0, dotDiffCppWindowClassName, "DotDiffCpp", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, desiredClientRect.right - desiredClientRect.left, desiredClientRect.bottom - desiredClientRect.top, nullptr, nullptr, hInstance, nullptr);

    if(windowHandle == nullptr)
    {
        // TODO(Iker): Add message explaining Window registration failed
        return -1;
    }

    // Initialize Direct3D
    if (!CreateDeviceD3D(windowHandle))
    {
        CleanupDeviceD3D();
        ::UnregisterClass(windowClass.lpszClassName, hInstance);
        return -1;
    }

#ifdef CONFIG_DEBUG
    AllocConsole();
    AttachConsole(GetCurrentProcessId());
    freopen_s((FILE**)stdout, "CONOUT$", "w", stdout);
    freopen_s((FILE**)stderr, "CONOUT$", "w", stderr);
#endif // CONFIG_DEBUG
       
    // QPC (Query Performance Counter) setup
    LARGE_INTEGER frameBegin, frameEnd, timeElapsed;
    LARGE_INTEGER frequency;

    QueryPerformanceFrequency(&frequency);

    bool quit = false;
    int exitCode = 0;
    float frameTimeMili = 0.0f;
    float framesPerSecond = 0.0f;
    float deltaTime = 0.016f;

    // Our state
    bool show_demo_window = true;
    bool show_another_window = false;
    ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

    ShowWindow(windowHandle, nShowCmd);
    UpdateWindow(windowHandle);

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();
    //ImGui::StyleColorsLight();

    // Setup Platform/Renderer backends
    ImGui_ImplWin32_Init(windowHandle);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);


    ProcessWindowMessages(quit, exitCode);

    while(quit == false)
    {
        QueryPerformanceCounter(&frameBegin);

        // Start the Dear ImGui frame
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        // 1. Show the big demo window (Most of the sample code is in ImGui::ShowDemoWindow()! You can browse its code to learn more about Dear ImGui!).
        if (show_demo_window)
            ImGui::ShowDemoWindow(&show_demo_window);

        // 2. Show a simple window that we create ourselves. We use a Begin/End pair to create a named window.
        {
            static float f = 0.0f;
            static int counter = 0;

            ImGui::Begin("Hello, world!");                          // Create a window called "Hello, world!" and append into it.

            ImGui::Text("This is some useful text.");               // Display some text (you can use a format strings too)
            ImGui::Checkbox("Demo Window", &show_demo_window);      // Edit bools storing our window open/close state
            ImGui::Checkbox("Another Window", &show_another_window);

            ImGui::SliderFloat("float", &f, 0.0f, 1.0f);            // Edit 1 float using a slider from 0.0f to 1.0f
            ImGui::ColorEdit3("clear color", (float*)&clear_color); // Edit 3 floats representing a color

            if (ImGui::Button("Button"))                            // Buttons return true when clicked (most widgets return true when edited/activated)
                counter++;
            ImGui::SameLine();
            ImGui::Text("counter = %d", counter);

            ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / io.Framerate, io.Framerate);
            ImGui::End();
        }

        // 3. Show another simple window.
        if (show_another_window)
        {
            ImGui::Begin("Another Window", &show_another_window);   // Pass a pointer to our bool variable (the window will have a closing button that will clear the bool when clicked)
            ImGui::Text("Hello from another window!");
            if (ImGui::Button("Close Me"))
                show_another_window = false;
            ImGui::End();
        }

        // Rendering
        ImGui::Render();
        const float clear_color_with_alpha[4] = { clear_color.x * clear_color.w, clear_color.y * clear_color.w, clear_color.z * clear_color.w, clear_color.w };
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clear_color_with_alpha);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        g_pSwapChain->Present(1, 0); // Present with vsync

        // QPC Update
        QueryPerformanceCounter(&frameEnd);
        timeElapsed.QuadPart = frameEnd.QuadPart - frameBegin.QuadPart;
        deltaTime = static_cast<float>(timeElapsed.QuadPart) / static_cast<float>(frequency.QuadPart);
        frameTimeMili = static_cast<float>((timeElapsed.QuadPart * 1000))/static_cast<float>((frequency.QuadPart));
        framesPerSecond = static_cast<float>((frequency.QuadPart))/static_cast<float>((timeElapsed.QuadPart));

        ProcessWindowMessages(quit, exitCode);
    }

    // Cleanup
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    CleanupDeviceD3D();

#ifdef CONFIG_DEBUG
    FreeConsole();
#endif // CONFIG_DEBUG
    
    DestroyWindow(windowHandle);
    UnregisterClass(windowClass.lpszClassName, windowClass.hInstance);
       
    return exitCode;
}
