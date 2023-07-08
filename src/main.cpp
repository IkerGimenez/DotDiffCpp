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
#include <winnt.h>

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

// DotDiffCpp Functions
HRESULT EnumerateVideoCaptureDevices(IMFAttributes ** ppAttributes, IMFActivate *** pppDevices, UINT32 & numDevices);
HRESULT AttributeGetString(IMFAttributes *pAttributes, REFGUID guidKey, WCHAR *& outString, UINT32 & outStrLen);
HRESULT CreateMediaSourceObject(IMFActivate ** ppDevices, IMFMediaSource ** ppSource);

#define CLEAN_ATTRIBUTES() \
    if (attributes) \
    { \
        attributes->Release(); \
        attributes = NULL; \
    } \
    for (DWORD i = 0; i < count; i++) \
    { \
        if (&devices[i]) \
        { \
            devices[i]->Release(); \
            devices[i] = NULL; \
        } \
    } \
    CoTaskMemFree(devices); \
    return hr;

bool ConvertWideStringToNarrow(wchar_t * wideString, const UINT32 wideStringLen, char * destString, const UINT32 destSize);                                                               

// VideoCaptureDevice
//specialized class
class VideoCaptureDevice: public IMFSourceReaderCallback
{
    CRITICAL_SECTION m_CriticalSection;
    long m_ReferenceCount;
    WCHAR * m_wSymbolicLink;
    UINT32 m_SymbolicLinkLen;
    IMFSourceReader * m_SourceReader;

public:
    LONG m_Stride;
    int m_BytesPerPixel;
    GUID m_VideoFormat;
    UINT m_Height;
    UINT m_Width;
    WCHAR deviceNameString[2048];
    BYTE * rawData;

    HRESULT CreateCaptureDevice(void);
    HRESULT SetSourceReader(IMFActivate * device);
    HRESULT IsMediaTypeSupported(IMFMediaType * type);
    HRESULT GetDefaultStride(IMFMediaType * pType, LONG * plStride);
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
    IMFAttributes * pAttributes = nullptr;
    IMFActivate ** ppDevices = nullptr;
    UINT32 numDevices = 0;
    WCHAR * deviceName = nullptr;
    UINT32 deviceNameLen;
    UINT32 selectedDeviceIdx = 0;
    IMFMediaSource * pDeviceSource = nullptr;

    std::vector<std::string> videoCaptureDeviceNames;

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
                    EnumerateVideoCaptureDevices(&pAttributes, &ppDevices, numDevices);
                }

                if (numDevices > 0)
                {
                    if (videoCaptureDeviceNames.empty())
                    {
                        videoCaptureDeviceNames.emplace_back("No device selected");
                        for (UINT32 deviceIdx = 0; deviceIdx < numDevices; ++deviceIdx)
                        {
                            AttributeGetString(ppDevices[deviceIdx], MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, deviceName, deviceNameLen);
                            std::string& currDeviceNarrowName = videoCaptureDeviceNames.emplace_back();
                            currDeviceNarrowName.resize(deviceNameLen * 3); // Worst case for UTF-16 to UTF-8 conversion, the string needs 3 times the length

                            bool conversionSuccess = ConvertWideStringToNarrow(deviceName, deviceNameLen, currDeviceNarrowName.data(), deviceNameLen * 3);
                            if (!conversionSuccess)
                            {
                                currDeviceNarrowName = "Error converting device name to UTF-8 from UTF-16";
                            }
                        }
                    }

                    const char * comboPreviewValue = videoCaptureDeviceNames[selectedDeviceIdx].c_str();
                    if(ImGui::BeginCombo("Video Capture Device", comboPreviewValue))
                    {
                        for (UINT32 deviceIdx = 0 ; deviceIdx < (UINT32)videoCaptureDeviceNames.size() ; ++deviceIdx)
                        {
                            const bool isSelected = (selectedDeviceIdx == deviceIdx);
                            if(ImGui::Selectable(videoCaptureDeviceNames[deviceIdx].c_str(), isSelected))
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

                const bool noCompatibleDevices = videoCaptureDeviceNames.size() == 1;
                const bool noValidDeviceSelected = selectedDeviceIdx == 0;
                ImGui::BeginDisabled(noCompatibleDevices || noValidDeviceSelected); // Disable start session button if no compatible devices detected
                if(ImGui::Button("Start Session"))
                {
                    showNewCaptureSessionWindow = false;
                    CreateMediaSourceObject(ppDevices, &pDeviceSource);
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
            if(pDeviceSource != nullptr)
            {
                HRESULT hResult = S_OK;
                IMFAttributes * pSourceReaderAttributes = nullptr;

                hResult = MFCreateAttributes(&pSourceReaderAttributes, 1);
                if(SUCCEEDED(hResult))
                {
                    //hResult = pSourceReaderAttributes->SetUnknown(MF_SOURCE_READER_ASYNC_CALLBACK, 
                }
            }
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

HRESULT EnumerateVideoCaptureDevices(IMFAttributes ** ppAttributes, IMFActivate *** pppDevices, UINT32 & numDevices)
{
    const UINT32 cElements = 1;

    HRESULT hResult = MFCreateAttributes(ppAttributes, cElements);
    if(SUCCEEDED(hResult))
    {
        hResult = (*ppAttributes)->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);

        if(SUCCEEDED(hResult))
        {
            hResult = MFEnumDeviceSources(*ppAttributes, pppDevices, &numDevices); 
        }
    }

    return hResult;
}

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

HRESULT CreateMediaSourceObject(IMFActivate ** ppDevices, IMFMediaSource ** ppSource)
{
    IMFMediaSource * pSource = nullptr;
    HRESULT hResult = ppDevices[0]->ActivateObject(IID_PPV_ARGS(&pSource));

    if(SUCCEEDED(hResult))
    {
        *ppSource = pSource;
        (*ppSource)->AddRef();
    }

    return hResult;
}

bool ConvertWideStringToNarrow(wchar_t * wideString, const UINT32 wideStringLen, char * destString, const UINT32 destSize)
{
    int result = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wideString, (int)wideStringLen, destString, destSize, NULL, NULL);

    // 0 means no characters were written to the destination string
    return (result != 0);
}

VideoCaptureDevice::VideoCaptureDevice()
{
	InitializeCriticalSection(&m_CriticalSection);
	m_ReferenceCount = 1;
	m_wSymbolicLink = NULL;
	m_SymbolicLinkLen = 0;
	m_Width = 0;
	m_Height = 0;
	m_SourceReader = NULL;
	rawData = NULL;
	
}

VideoCaptureDevice::~VideoCaptureDevice()
{
	
	if (m_wSymbolicLink)
	{	
		delete m_wSymbolicLink;
		m_wSymbolicLink = NULL;
	}
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

HRESULT VideoCaptureDevice::CreateCaptureDevice()
{
	HRESULT hr = S_OK;
	
	//this is important!!
	hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

	UINT32 count = 0;
	IMFAttributes *attributes = NULL;
	IMFActivate **devices = NULL;

	if (FAILED(hr)) { CLEAN_ATTRIBUTES() }
	// Create an attribute store to specify enumeration parameters.
	hr = MFCreateAttributes(&attributes, 1);

	if (FAILED(hr)) { CLEAN_ATTRIBUTES() }

	//The attribute to be requested is devices that can capture video
	hr = attributes->SetGUID(
		MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
		MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID
	);
	if (FAILED(hr)) { CLEAN_ATTRIBUTES() }
	//Enummerate the video capture devices
	hr = MFEnumDeviceSources(attributes, &devices, &count);
	
	if (FAILED(hr)) { CLEAN_ATTRIBUTES() }
	//if there are any available devices
	if (count > 0)
	{
		/*If you actually need to select one of the available devices
		this is the place to do it. For this example the first device
		is selected
		*/
		//Get a source reader from the first available device
		SetSourceReader(devices[0]);
		
		WCHAR *nameString = NULL;
		// Get the human-friendly name of the device
		UINT32 cchName;
		hr = devices[0]->GetAllocatedString(
			MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME,
			&nameString, &cchName);

		if (SUCCEEDED(hr))
		{
			//allocate a byte buffer for the raw pixel data
			m_BytesPerPixel = abs(m_Stride) / m_Width;
			rawData = new BYTE[m_Width*m_Height * m_BytesPerPixel];
			wcscpy_s(deviceNameString, ARRAYSIZE(deviceNameString), nameString); 
		}
		CoTaskMemFree(nameString);
	}

	//clean
	CLEAN_ATTRIBUTES()
}


HRESULT VideoCaptureDevice::SetSourceReader(IMFActivate *device)
{
	HRESULT hr = S_OK;

	IMFMediaSource *source = NULL;
	IMFAttributes *attributes = NULL;
	IMFMediaType *mediaType = NULL;

	EnterCriticalSection(&m_CriticalSection);

	hr = device->ActivateObject(__uuidof(IMFMediaSource), (void**)&source);

	//get symbolic link for the device
	if(SUCCEEDED(hr))
		hr = device->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK, &m_wSymbolicLink, &m_SymbolicLinkLen);
	//Allocate attributes
	if (SUCCEEDED(hr))
		hr = MFCreateAttributes(&attributes, 2);
	//get attributes
	if (SUCCEEDED(hr))
		hr = attributes->SetUINT32(MF_READWRITE_DISABLE_CONVERTERS, TRUE);
	// Set the callback pointer.
	if (SUCCEEDED(hr))
		hr = attributes->SetUnknown(MF_SOURCE_READER_ASYNC_CALLBACK,this);
	//Create the source reader
	if (SUCCEEDED(hr))
		hr = MFCreateSourceReaderFromMediaSource(source,attributes,&m_SourceReader);
	// Try to find a suitable output type.
	if (SUCCEEDED(hr))
	{
		for (DWORD i = 0; ; i++)
		{
			hr = m_SourceReader->GetNativeMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM,i,&mediaType);
			if (FAILED(hr)) { break; }
			
			hr = IsMediaTypeSupported(mediaType);
			if (FAILED(hr)) { break; }
			//Get width and height
			MFGetAttributeSize(mediaType, MF_MT_FRAME_SIZE, &m_Width, &m_Height);
			if (mediaType) 
			{ mediaType->Release(); mediaType = NULL; }

			if (SUCCEEDED(hr))// Found an output type.
				break;
		}
	}
	if (SUCCEEDED(hr))
	{
		// Ask for the first sample.
		hr = m_SourceReader->ReadSample((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM,	0, NULL, NULL,NULL,NULL);
	}

	if (FAILED(hr))
	{
		if (source)
		{
			source->Shutdown();	
		}
		Close();
	}
	if (source) { source->Release(); source = NULL; }
	if (attributes) { attributes->Release(); attributes = NULL; }
	if (mediaType) { mediaType->Release(); mediaType = NULL; }

	LeaveCriticalSection(&m_CriticalSection);
	return hr;
}

HRESULT VideoCaptureDevice::IsMediaTypeSupported(IMFMediaType *pType)
{
	HRESULT hr = S_OK;

	//BOOL bFound = FALSE;
	GUID subtype = { 0 };

	//Get the stride for this format so we can calculate the number of bytes per pixel
	GetDefaultStride(pType, &m_Stride);

	if (FAILED(hr)) { return hr; }
	hr = pType->GetGUID(MF_MT_SUBTYPE, &subtype);

	m_VideoFormat = subtype;

	if (FAILED(hr))	{return hr;	}

	if (subtype == MFVideoFormat_RGB32 || subtype == MFVideoFormat_RGB24 || subtype == MFVideoFormat_YUY2 || subtype == MFVideoFormat_NV12)
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
HRESULT VideoCaptureDevice::GetDefaultStride(IMFMediaType *type, LONG *stride)
{
	LONG tempStride = 0;

	// Try to get the default stride from the media type.
	HRESULT hr = type->GetUINT32(MF_MT_DEFAULT_STRIDE, (UINT32*)&tempStride);
	if (FAILED(hr))
	{
		//Setting this atribute to NULL we can obtain the default stride
		GUID subtype = GUID_NULL;

		UINT32 width = 0;
		UINT32 height = 0;

		// Obtain the subtype
		hr = type->GetGUID(MF_MT_SUBTYPE, &subtype);
		//obtain the width and height
		if (SUCCEEDED(hr))
			hr = MFGetAttributeSize(type, MF_MT_FRAME_SIZE, &width, &height);
		//Calculate the stride based on the subtype and width
		if (SUCCEEDED(hr))
			hr = MFGetStrideForBitmapInfoHeader(subtype.Data1, width, &tempStride);
		// set the attribute so it can be read
		if (SUCCEEDED(hr))
			(void)type->SetUINT32(MF_MT_DEFAULT_STRIDE, UINT32(tempStride));
	}

	if (SUCCEEDED(hr))
			*stride = tempStride;
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
				CopyMemory(rawData, data, m_Width*m_Height * m_BytesPerPixel);

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
