#include "dearimgui/imgui.h"
#include "dearimgui/backends/imgui_impl_win32.h"
#include "dearimgui/backends/imgui_impl_dx11.h"

#include <Windows.h>
#include <d3d11.h>
#include <directxmath.h>
#include <mfidl.h>
#include <Mfapi.h>
#include <Mfreadwrite.h>
#include <Shlwapi.h>

#include <vector>
#include <string>
#include <span>

#include <stdio.h>

#define DEFAULT_CLIENT_WIDTH 1280
#define DEFAULT_CLIENT_HEIGHT 720

// Data 
static ID3D11Device*            g_pd3dDevice = nullptr;
static ID3D11DeviceContext*     g_pd3dDeviceContext = nullptr;
static IDXGISwapChain*          g_pSwapChain = nullptr;
static UINT                     g_ResizeWidth = 0, g_ResizeHeight = 0;
static ID3D11RenderTargetView*  g_mainRenderTargetView = nullptr;

// Forward declarations of helper functions

// imgui helpers
bool CreateDeviceD3D(HWND hWnd);
void CleanupDeviceD3D();
void CreateRenderTarget();
void CleanupRenderTarget();
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// Win32 Functions
void ProcessWindowMessages(bool& quitReceived, int& exitCode);
LRESULT WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);



// Copied from MSDN documentation
template <class T> void SafeRelease(T **ppT)
{
    if (*ppT)
    {
        (*ppT)->Release();
        *ppT = nullptr;
    }
}

struct VideoCaptureDeviceMediaType
{
    LONG m_Stride = 0;
    UINT32 m_BytesPerPixel = 0;
    GUID m_VideoFormat;
    UINT m_Height = 0;
    UINT m_Width = 0;
};

// VideoCaptureDevice
//specialized class
class VideoCaptureDevice: public IMFSourceReaderCallback
{
    CRITICAL_SECTION m_CriticalSection;
    long m_ReferenceCount = 1;
    WCHAR * m_wSymbolicLink = nullptr;
    UINT32 m_SymbolicLinkLen = 0;
    IMFSourceReader * m_SourceReader = nullptr;

public:
    std::vector<VideoCaptureDeviceMediaType> mediaTypes;
    char deviceNameString[2048];
    UINT32 deviceNameLen = 0;
    BYTE * rawData = nullptr;

    HRESULT SetSourceReader(IMFActivate * device);
    HRESULT IsMediaTypeSupported(IMFMediaType * pType, LONG & outStride, GUID & outSubtype);
    HRESULT GetDefaultStride(IMFMediaType * pType, LONG & outStride);
    HRESULT Close();
    VideoCaptureDevice();
    ~VideoCaptureDevice();

    // the class must implement the methods from IUnknown
    STDMETHODIMP QueryInterface(REFIID iid, void** ppv);
    STDMETHODIMP_(ULONG) AddRef();
    STDMETHODIMP_(ULONG) Release();
 
    //  the class must implement the methods from IMFSourceReaderCallback
    STDMETHODIMP OnReadSample(HRESULT status, DWORD streamIndex, DWORD streamFlags, LONGLONG timeStamp, IMFSample *sample);
    STDMETHODIMP OnEvent(DWORD, IMFMediaEvent *);
    STDMETHODIMP OnFlush(DWORD);
};

// DotDiffCpp Functions
HRESULT EnumerateVideoCaptureDevices(IMFActivate *** pppDevices, UINT32 & outNumDevices);
void PopulateVideoCaptureDeviceArray(IMFActivate *** pppDevices, const UINT32 numDevices, std::vector<VideoCaptureDevice> & outVideoCaptureDevices, std::vector<std::span<char>> & outVcdNames);
HRESULT AttributeGetString(IMFAttributes *pAttributes, REFGUID guidKey, WCHAR *& outString, UINT32 & outStrLen);

#define CLEAN_ATTRIBUTES() \
{ \
    if (pAttributes != nullptr) \
    { \
        pAttributes->Release(); \
        pAttributes = NULL; \
    } \
}

#define CLEAN_DEVICES() \
{ \
    for (DWORD i = 0; i < count; i++) \
    { \
        if (&(ppDevices[i])) \
        { \
            ppDevices[i]->Release(); \
            devices[i] = NULL; \
        } \
    } \
    CoTaskMemFree(devices); \
}


int ConvertWideStringToNarrow(wchar_t * wideString, const UINT32 wideStringLen, char * destString, const UINT32 destSize); 

// Program entrypoint
int WinMain(HINSTANCE hInstance, HINSTANCE /*hPrevInstance*/, LPSTR /*lpCmdLine*/, int nShowCmd)
{
    const wchar_t dotDiffCppWindowClassName[] = L"DotDiffCpp Window Class";
    const wchar_t dotDiffCppWindowTitle[] = L"DotDiffCpp";

    WNDCLASSEX windowClass = {};

    windowClass.cbSize = sizeof(WNDCLASSEX);
    windowClass.hInstance = hInstance;
    windowClass.lpszClassName = dotDiffCppWindowClassName;
    windowClass.style = CS_DBLCLKS | CS_OWNDC;
    windowClass.lpfnWndProc = WindowProc;

    RegisterClassEx(&windowClass);

    RECT desiredClientRect { 0, 0, DEFAULT_CLIENT_WIDTH, DEFAULT_CLIENT_HEIGHT };
    AdjustWindowRect(&desiredClientRect, WS_OVERLAPPEDWINDOW, FALSE);

    HWND windowHandle = CreateWindowExW(0, dotDiffCppWindowClassName, dotDiffCppWindowTitle, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, desiredClientRect.right - desiredClientRect.left, desiredClientRect.bottom - desiredClientRect.top, nullptr, nullptr, hInstance, nullptr);

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

    bool showNewCaptureSessionWindow = false;
    
    // Video capture devices
    IMFActivate ** ppDevices = nullptr;
    UINT32 numDevices = 0;
    UINT32 selectedDeviceIdx = 0;

    std::vector<VideoCaptureDevice> videoCaptureDevices;
    std::vector<std::span<char>> vcdNames;

    // Our state
    bool show_demo_window = true;
    ImVec4 clear_color = ImVec4(0.0f, 0.0f, 0.0f, 1.00f);

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
        {
            ImGui::ShowDemoWindow(&show_demo_window);
        }

        // Main menu bar 
        {
            static float f = 0.0f;
            static int counter = 0;

            if(ImGui::BeginMainMenuBar())
            {
                if(ImGui::BeginMenu("File"))
                {
                    ImGui::MenuItem("(Test Menu)", nullptr, false, false);
                    if(ImGui::MenuItem("Start new capture session")) 
                    {
                        showNewCaptureSessionWindow = true;
                    }
                    ImGui::Separator();
                    if(ImGui::MenuItem("Quit", "Alt+F4")) {}
                    ImGui::EndMenu();
                }
                ImGui::EndMainMenuBar();
            }
        }

        // New capture session window
        {
            if(showNewCaptureSessionWindow)
            {
                ImGui::Begin("New session configuration", &showNewCaptureSessionWindow);
                if (numDevices == 0)
                {
                    EnumerateVideoCaptureDevices(&ppDevices, numDevices);

                    if(numDevices > 0)
                    {
                        PopulateVideoCaptureDeviceArray(&ppDevices, numDevices, videoCaptureDevices, vcdNames);
                    }
                }

                if (numDevices > 0)
                {
                    const char * comboPreviewValue = vcdNames[selectedDeviceIdx].data();
                    if(ImGui::BeginCombo("Video Capture Device", comboPreviewValue))
                    {
                        for (UINT32 deviceIdx = 0 ; deviceIdx < (UINT32)vcdNames.size() ; ++deviceIdx)
                        {
                            const bool isSelected = (selectedDeviceIdx == deviceIdx);
                            if(ImGui::Selectable(vcdNames[deviceIdx].data(), isSelected))
                            {
                                selectedDeviceIdx = deviceIdx;
                            }

                            if(isSelected)
                            {
                                ImGui::SetItemDefaultFocus();
                            }
                        }
                        ImGui::EndCombo();
                    }
                }
                else
                {
                    ImGui::Text("No compatible video capture devices detected. Make sure your device is properly connected and that it is recognized as a video capture device by Windows.");
                }

                const bool noCompatibleDevices = videoCaptureDevices.size() == 0;
                ImGui::BeginDisabled(noCompatibleDevices); // Disable start session button if no compatible devices detected
                if(ImGui::Button("Start Session"))
                {
                    videoCaptureDevices[selectedDeviceIdx].SetSourceReader(ppDevices[selectedDeviceIdx]);
                    showNewCaptureSessionWindow = false;
                }
                ImGui::EndDisabled();
                ImGui::SameLine();
                if(ImGui::Button("Detect New Devices"))
                {
                    numDevices = 0;
                }
                ImGui::End();
            }
        }

        {

        }

        // Rendering
        ImGui::Render();

        // Clear backbuffer
        const float clear_color_with_alpha[4] = { clear_color.x * clear_color.w, clear_color.y * clear_color.w, clear_color.z * clear_color.w, clear_color.w };
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clear_color_with_alpha);

        // Render imgui
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        // Present swap chain
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

HRESULT EnumerateVideoCaptureDevices(IMFActivate *** pppDevices, UINT32 & outNumDevices)
{
    IMFAttributes * pAttributes = nullptr;
    const UINT32 cElements = 1;

    HRESULT hResult = MFCreateAttributes(&pAttributes, cElements);
    if(SUCCEEDED(hResult))
    {
        hResult = pAttributes->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);

        if(SUCCEEDED(hResult))
        {
            hResult = MFEnumDeviceSources(pAttributes, pppDevices, &outNumDevices); 
        }
    }

    CLEAN_ATTRIBUTES()

    return hResult;
}

void PopulateVideoCaptureDeviceArray(IMFActivate *** pppDevices, const UINT32 numDevices, std::vector<VideoCaptureDevice> & outVideoCaptureDevices, std::vector<std::span<char>> & outVcdNames)
{
    IMFActivate ** ppDevices = *pppDevices;
    WCHAR * deviceName = nullptr;
    UINT32 deviceNameLen = 0;

    if (numDevices > 0)
    {
        outVideoCaptureDevices.resize(numDevices);
        outVcdNames.resize(numDevices);
        
        for(UINT32 deviceIdx = 0 ; deviceIdx < numDevices ; ++deviceIdx)
        {
            AttributeGetString(ppDevices[deviceIdx], MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, deviceName, deviceNameLen);
            int numBytesWritten = ConvertWideStringToNarrow(deviceName, deviceNameLen, outVideoCaptureDevices[deviceIdx].deviceNameString, ARRAYSIZE(outVideoCaptureDevices[deviceIdx].deviceNameString));
            if(!numBytesWritten)
            {
                 snprintf(outVideoCaptureDevices[deviceIdx].deviceNameString, ARRAYSIZE(outVideoCaptureDevices[deviceIdx].deviceNameString), "Error converting device name to UTF-8 from UTF-16");
            }
            outVcdNames[deviceIdx] = std::span{outVideoCaptureDevices[deviceIdx].deviceNameString, (size_t)numBytesWritten};
        }
    }
}

// TODO(Memory): Replace call to new with frame allocator calls
HRESULT AttributeGetString(IMFAttributes *pAttributes, REFGUID guidKey, WCHAR *& outString, UINT32 & outStrLen)
{
    HRESULT hResult = S_OK;

    hResult = pAttributes->GetStringLength(guidKey, &outStrLen);
    
    if (SUCCEEDED(hResult))
    {
        outString = new WCHAR[outStrLen + 1];
        if (outString == NULL)
        {
            hResult = E_OUTOFMEMORY;
        }
        ZeroMemory(outString, outStrLen + 1); 
    }

    if (SUCCEEDED(hResult))
    {
        hResult = pAttributes->GetString(guidKey, outString, outStrLen + 1, &outStrLen);
    }

    return hResult;
}

int ConvertWideStringToNarrow(wchar_t * wideString, const UINT32 wideStringLen, char * destString, const UINT32 destSize)
{
    int result = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wideString, (int)wideStringLen, destString, destSize, NULL, NULL);

    return result;
}

VideoCaptureDevice::VideoCaptureDevice()
{
	InitializeCriticalSection(&m_CriticalSection);
    ZeroMemory(deviceNameString, ARRAYSIZE(deviceNameString));
}

VideoCaptureDevice::~VideoCaptureDevice()
{
	EnterCriticalSection(&m_CriticalSection);

	if (m_SourceReader)
	{
		m_SourceReader->Release();
		m_SourceReader = NULL;
	}

	
	if (rawData)
	{
		delete rawData;
		rawData = NULL;
	}

	CoTaskMemFree(m_wSymbolicLink);
	m_wSymbolicLink = NULL;
	m_SymbolicLinkLen = 0;

	LeaveCriticalSection(&m_CriticalSection);
	DeleteCriticalSection(&m_CriticalSection);
}

HRESULT VideoCaptureDevice::SetSourceReader(IMFActivate *device)
{
	HRESULT hr = S_OK;

	IMFMediaSource * pSource = NULL;
	IMFAttributes * pAttributes = NULL;
	IMFMediaType * pMediaType = NULL;

	EnterCriticalSection(&m_CriticalSection);

	hr = device->ActivateObject(__uuidof(IMFMediaSource), (void**)&pSource);

	//get symbolic link for the device
	if(SUCCEEDED(hr))
    {
		hr = device->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK, &m_wSymbolicLink, &m_SymbolicLinkLen);
    }
	//Allocate attributes
	if (SUCCEEDED(hr))
    {
		hr = MFCreateAttributes(&pAttributes, 2);
    }
	//get attributes
	if (SUCCEEDED(hr))
    {
		hr = pAttributes->SetUINT32(MF_READWRITE_DISABLE_CONVERTERS, TRUE);
    }
	// Set the callback pointer.
	if (SUCCEEDED(hr))
    {
		hr = pAttributes->SetUnknown(MF_SOURCE_READER_ASYNC_CALLBACK,this);
    }
	//Create the source reader
	if (SUCCEEDED(hr))
    {
		hr = MFCreateSourceReaderFromMediaSource(pSource,pAttributes,&m_SourceReader);
    }
	// Try to find a suitable output type.
	if (SUCCEEDED(hr))
	{
        LONG stride = 0;
        UINT width = 0, height = 0;
	    GUID subtype = { 0 };
		for (DWORD i = 0; ; i++)
		{
			hr = m_SourceReader->GetNativeMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM,i,&pMediaType);
			if (FAILED(hr)) 
            { 
                break; 
            }
			hr = IsMediaTypeSupported(pMediaType, stride, subtype);
			if (FAILED(hr))
            { 
                break;
            }
			//Get width and height
			MFGetAttributeSize(pMediaType, MF_MT_FRAME_SIZE, &width, &height);

            mediaTypes.emplace_back(VideoCaptureDeviceMediaType{ stride, abs(stride)/width, subtype, height, width });

			if (pMediaType) 
			{ 
                pMediaType->Release(); 
                pMediaType = NULL; 
            }
		}
	}

    // If any compatible media types found
	if (mediaTypes.size() > 0)
	{
		// Ask for the first sample.
		hr = m_SourceReader->ReadSample((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM,	0, NULL, NULL,NULL,NULL);
	}

	if (FAILED(hr))
	{
		if (pSource)
		{
			pSource->Shutdown();	
		}
		Close();
	}
	if (pSource) 
    { 
        pSource->Release(); 
        pSource = NULL; 
    }

	if (pMediaType) 
    { 
        pMediaType->Release(); 
        pMediaType = NULL; 
    }

    CLEAN_ATTRIBUTES();

	LeaveCriticalSection(&m_CriticalSection);
	return hr;
}

HRESULT VideoCaptureDevice::IsMediaTypeSupported(IMFMediaType *pType, LONG & outStride, GUID & outSubtype)
{
	HRESULT hr = S_OK;

	//Get the stride for this format so we can calculate the number of bytes per pixel
	GetDefaultStride(pType, outStride);

	if (FAILED(hr)) { return hr; }
	hr = pType->GetGUID(MF_MT_SUBTYPE, &outSubtype);

	if (FAILED(hr))	{return hr;	}

	if (outSubtype == MFVideoFormat_RGB32 || outSubtype == MFVideoFormat_RGB24 || outSubtype == MFVideoFormat_YUY2 || outSubtype == MFVideoFormat_NV12)
		return S_OK;
	else
		return S_FALSE;
}

HRESULT VideoCaptureDevice::Close()
{
	EnterCriticalSection(&m_CriticalSection);
	if(m_SourceReader)
	{
        m_SourceReader->Release(); 
        m_SourceReader = NULL;
    }

	CoTaskMemFree(m_wSymbolicLink);
	m_wSymbolicLink = NULL;
	m_SymbolicLinkLen = 0;

	LeaveCriticalSection(&m_CriticalSection);
	return S_OK;
}

//From IUnknown 
STDMETHODIMP VideoCaptureDevice::QueryInterface(REFIID riid, void** ppvObject)
{
	static const QITAB qit[] = {QITABENT(VideoCaptureDevice
        , IMFSourceReaderCallback),{ 0 },};
	return QISearch(this, qit, riid, ppvObject);
}
//From IUnknown
ULONG VideoCaptureDevice::Release()
{
	ULONG count = InterlockedDecrement(&m_ReferenceCount);
	if (count == 0)
		delete this;
	// For thread safety
	return count;
}
//From IUnknown
ULONG VideoCaptureDevice::AddRef()
{
	return InterlockedIncrement(&m_ReferenceCount);
}


//Calculates the default stride based on the format and size of the frames
HRESULT VideoCaptureDevice::GetDefaultStride(IMFMediaType * pType, LONG & outStride)
{
	// Try to get the default stride from the media type.
	HRESULT hr = pType->GetUINT32(MF_MT_DEFAULT_STRIDE, (UINT32*)&outStride);
	if (FAILED(hr))
	{
		//Setting this atribute to NULL we can obtain the default stride
		GUID subtype = GUID_NULL;

		UINT32 width = 0;
		UINT32 height = 0;

		// Obtain the subtype
		hr = pType->GetGUID(MF_MT_SUBTYPE, &subtype);
		//obtain the width and height
		if (SUCCEEDED(hr))
        {
			hr = MFGetAttributeSize(pType, MF_MT_FRAME_SIZE, &width, &height);
        }
		//Calculate the stride based on the subtype and width
		if (SUCCEEDED(hr))
        {
			hr = MFGetStrideForBitmapInfoHeader(subtype.Data1, width, &outStride);
        }
		// set the attribute so it can be read
		if (SUCCEEDED(hr))
        {
			(void)pType->SetUINT32(MF_MT_DEFAULT_STRIDE, UINT32(outStride));
        }
	}

	if (FAILED(hr))
    {
        outStride = 0;
    }

	return hr;
}

//Method from IMFSourceReaderCallback
HRESULT VideoCaptureDevice::OnReadSample(HRESULT status, DWORD /*streamIndex*/, DWORD /*streamFlags*/, LONGLONG /*timeStamp*/, IMFSample *sample)
{
	HRESULT hr = S_OK;
	IMFMediaBuffer *mediaBuffer = NULL;

	EnterCriticalSection(&m_CriticalSection);

	if (FAILED(status))
		hr = status;

	if (SUCCEEDED(hr))
	{
		if (sample)
		{// Get the video frame buffer from the sample.
			hr = sample->GetBufferByIndex(0, &mediaBuffer);
			// Draw the frame.
			if (SUCCEEDED(hr))
			{
				BYTE* data;
				mediaBuffer->Lock(&data, NULL, NULL);
				//This is a good place to perform color conversion and drawing
				//Instead we're copying the data to a buffer
				//CopyMemory(rawData, data, m_Width*m_Height * m_BytesPerPixel);

			}
		}
	}
	// Request the next frame.
	if (SUCCEEDED(hr))
		hr = m_SourceReader->ReadSample((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, NULL, NULL, NULL, NULL);

	if (FAILED(hr))
	{
		//Notify there was an error
		printf("Error HRESULT = 0x%d", hr);
		PostMessage(NULL, 1, (WPARAM)hr, 0L);
	}
	if (mediaBuffer) { mediaBuffer->Release(); mediaBuffer = NULL; }

	LeaveCriticalSection(&m_CriticalSection);
	return hr;
}
//Method from IMFSourceReaderCallback 
STDMETHODIMP VideoCaptureDevice::OnEvent(DWORD, IMFMediaEvent *) { return S_OK; }
//Method from IMFSourceReaderCallback 
STDMETHODIMP VideoCaptureDevice::OnFlush(DWORD) { return S_OK; }
