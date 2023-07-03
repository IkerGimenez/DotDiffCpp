#include <Windows.h>

#define DEFAULT_CLIENT_WIDTH 1280
#define DEFAULT_CLIENT_HEIGHT 720

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

    ShowWindow(windowHandle, nShowCmd);
    ProcessWindowMessages(quit, exitCode);

    while(quit == false)
    {
        QueryPerformanceCounter(&frameBegin);

        QueryPerformanceCounter(&frameEnd);
        timeElapsed.QuadPart = frameEnd.QuadPart - frameBegin.QuadPart;
        deltaTime = static_cast<float>(timeElapsed.QuadPart) / static_cast<float>(frequency.QuadPart);
        frameTimeMili = static_cast<float>((timeElapsed.QuadPart * 1000))/static_cast<float>((frequency.QuadPart));
        framesPerSecond = static_cast<float>((frequency.QuadPart))/static_cast<float>((timeElapsed.QuadPart));
        ProcessWindowMessages(quit, exitCode);
    }

#ifdef CONFIG_DEBUG
    FreeConsole();
#endif // CONFIG_DEBUG
       
    return exitCode;
}
