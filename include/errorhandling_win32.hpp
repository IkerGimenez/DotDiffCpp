#pragma once

#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)
#define AT __FILE__ ":" TOSTRING(__LINE__)

void PresentErrorMessageBox(const char* errorMessage, const char* windowTitle, uint32 options)
{
    MessageBox(nullptr, errorMessage, windowTitle, options);
}


#define PresentErrorMessageBoxFormatted(errorMessage, windowTitle, options, ...)                    \
{                                                                                                   \
    char errorMessageBuffer[512] = { 0 };                                                           \
    StringCchPrintfA(errorMessageBuffer, ARRAYSIZE(errorMessageBuffer), errorMessage, __VA_ARGS__); \
    MessageBox(nullptr, errorMessageBuffer, windowTitle, options);                                  \
}

#ifdef CONFIG_DEBUG

#define PRESENT_ASSERT_MESSAGE_BOX(message) int32 result = MessageBox(nullptr, message ".\n Click Abort to end the process, Retry to break into the debugger, and Ignore to ignore it.", "Assert Failed", MB_ICONERROR | MB_ABORTRETRYIGNORE); \
    switch(result) \
        { \
            case IDABORT: \
            { \
                abort(); \
            } \
            case IDRETRY: \
            { \
                DebugBreak(); \
            } break; \
            case IDIGNORE: \
            { \
              \
            } break; \
            default: \
            { \
                abort(); \
            } \
        }

#define CUSTOM_ASSERT_W_MESSAGE(expression, message) \
do \
{  \
    if(!(expression)) \
    { \
        PRESENT_ASSERT_MESSAGE_BOX("Assert " #expression " failed at " AT " with message: " message); \
    } \
} while(0); 


#define CUSTOM_ASSERT(expression) \
do \
{  \
    if(!(expression)) \
    { \
        PRESENT_ASSERT_MESSAGE_BOX("Assert " #expression " failed at " AT); \
    } \
} while(0); 

#define CUSTOM_ASSERT_FUNCTION_RETVAL(functionCall) CUSTOM_ASSERT(functionCall)

#else  // !CONFIG_DEBUG

#define CUSTOM_ASSERT_W_MESSAGE(expression, message) 
#define CUSTOM_ASSERT(expression)
#define CUSTOM_ASSERT_FUNCTION_RETVAL(functionCall) functionCall

#endif // CONFIG_DEBUG
