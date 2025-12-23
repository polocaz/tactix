#pragma once

// Platform-specific definitions to prevent symbol conflicts
#if defined(_WIN32)
    #define NOGDI      // Prevents GDI drawing functions
    #define NOUSER     // Prevents user-mode functions like CloseWindow, ShowCursor
    #define NOMINMAX   // Prevents min/max macros
#endif
