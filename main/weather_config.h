#pragma once

/* Keep credentials out of version control. Copy weather_private.example.h to
 * weather_private.h and fill in the account-specific values. */
#if __has_include("weather_private.h")
#include "weather_private.h"
#else
#define QWEATHER_API_HOST ""
#define QWEATHER_API_KEY ""
#define QWEATHER_LATITUDE "36.08"
#define QWEATHER_LONGITUDE "120.36"
#endif
