#ifndef __CUSTOM_CONFIG__H
#define __CUSTOM_CONFIG__H

/************************ INCLUDES **************************/

/******************** CONFIG DEFINITIONS ********************/

// Develop configuration
#if !_RELEASE

    // FW version configuration
    #define FW_VERSION						{__DATE__, "00.001_D", {0,1}}

    // DEBUG configuration and verbosity
    #define GLOBAL_DEBUG (1)
    #if GLOBAL_DEBUG

        #define DEBUG_WIFI     (1)
        #define DEBUG_RTC      (1)
        #define DEBUG_FIREBASE (1)
        #define DEBUG_PARAMS   (1)
        #define DEBUG_METERING (0)
        #define DEBUG_MAIN     (1)
    #endif


// Release configuration
#else
    #define FW_VERSION						{__DATE__, "00.001_R", {0,1}}

#endif


#endif
