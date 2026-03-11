/*
 * Copyright (C) 2018 - softworkz for Emby Llc.
 * All rights reserved.
 *
 * This code is not yet published under any license.
 *
 */

#ifndef FFDETECT_FFDETECT_AMF_H
#define FFDETECT_FFDETECT_AMF_H

#include "config.h"
#include "outputwriters.h"

enum AmfApiType {
    AMF_API_AMFENC = 1
};

int DetectAmf(AVTextFormatContext *wctx, enum AmfApiType apiType, int disable_dx11);

#endif /* FFDETECT_FFDETECT_AMF_H */
