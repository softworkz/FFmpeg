/*
 * Copyright (C) 2018 - softworkz for Emby Llc.
 * All rights reserved.
 *
 * This code is not yet published under any license.
 *
 */

#ifndef FFDETECT_FFDETECT_QSV_H
#define FFDETECT_FFDETECT_QSV_H

#include <stdint.h>

#include "config.h"
#include "libavutil/bprint.h"
#include "outputwriters.h"

#include "libavutil/hwcontext.h"
#include "libavutil/hwcontext_qsv.h"

enum QsvApiType {
    QSV_API_QSVENC = 1,
    QSV_API_QSVDEC = 2,
    QSV_API_QSVENCDEC = QSV_API_QSVENC | QSV_API_QSVDEC
};

static const struct {
    mfxStatus status;
    const char *desc;
} mfx_errors[] = {
    { MFX_ERR_NONE,                               "MFX_ERR_NONE"                             },
    { MFX_ERR_UNKNOWN,                            "MFX_ERR_UNKNOWN"                          },
    { MFX_ERR_NULL_PTR,                           "MFX_ERR_NULL_PTR"                         },
    { MFX_ERR_UNSUPPORTED,                        "MFX_ERR_UNSUPPORTED"                      },
    { MFX_ERR_MEMORY_ALLOC,                       "MFX_ERR_MEMORY_ALLOC"                     },
    { MFX_ERR_NOT_ENOUGH_BUFFER,                  "MFX_ERR_NOT_ENOUGH_BUFFER"                },
    { MFX_ERR_INVALID_HANDLE,                     "MFX_ERR_INVALID_HANDLE"                   },
    { MFX_ERR_LOCK_MEMORY,                        "MFX_ERR_LOCK_MEMORY"                      },
    { MFX_ERR_NOT_INITIALIZED,                    "MFX_ERR_NOT_INITIALIZED"                  },
    { MFX_ERR_NOT_FOUND,                          "MFX_ERR_NOT_FOUND"                        },
    { MFX_ERR_MORE_DATA,                          "MFX_ERR_MORE_DATA"                        },
    { MFX_ERR_MORE_SURFACE,                       "MFX_ERR_MORE_SURFACE"                     },
    { MFX_ERR_ABORTED,                            "MFX_ERR_ABORTED"                          },
    { MFX_ERR_DEVICE_LOST,                        "MFX_ERR_DEVICE_LOST"                      },
    { MFX_ERR_INCOMPATIBLE_VIDEO_PARAM,           "MFX_ERR_INCOMPATIBLE_VIDEO_PARAM"         },
    { MFX_ERR_INVALID_VIDEO_PARAM,                "MFX_ERR_INVALID_VIDEO_PARAM"              },
    { MFX_ERR_UNDEFINED_BEHAVIOR,                 "MFX_ERR_UNDEFINED_BEHAVIOR"               },
    { MFX_ERR_DEVICE_FAILED,                      "MFX_ERR_DEVICE_FAILED"                    },
    { MFX_ERR_MORE_BITSTREAM,                     "MFX_ERR_MORE_BITSTREAM"                   },
    { MFX_ERR_GPU_HANG,                           "MFX_ERR_GPU_HANG"                         },
    { MFX_ERR_REALLOC_SURFACE,                    "MFX_ERR_REALLOC_SURFACE"                  },
    { MFX_WRN_IN_EXECUTION,                       "MFX_WRN_IN_EXECUTION"                     },
    { MFX_WRN_DEVICE_BUSY,                        "MFX_WRN_DEVICE_BUSY"                      },
    { MFX_WRN_VIDEO_PARAM_CHANGED,                "MFX_WRN_VIDEO_PARAM_CHANGED"              },
    { MFX_WRN_PARTIAL_ACCELERATION,               "MFX_WRN_PARTIAL_ACCELERATION"             },
    { MFX_WRN_INCOMPATIBLE_VIDEO_PARAM,           "MFX_WRN_INCOMPATIBLE_VIDEO_PARAM"         },
    { MFX_WRN_VALUE_NOT_CHANGED,                  "MFX_WRN_VALUE_NOT_CHANGED"                },
    { MFX_WRN_OUT_OF_RANGE,                       "MFX_WRN_OUT_OF_RANGE"                     },
    { MFX_WRN_FILTER_SKIPPED,                     "MFX_WRN_FILTER_SKIPPED"                   },
    { MFX_TASK_WORKING,                           "MFX_TASK_WORKING"                         },
    { MFX_TASK_BUSY,                              "MFX_TASK_BUSY"                            },
    { MFX_ERR_MORE_DATA_SUBMIT_TASK,              "MFX_ERR_MORE_DATA_SUBMIT_TASK"            }
};

int DetectQsv(AVTextFormatContext *wctx, enum QsvApiType apiType, int disable_dx11);

void mfx_map_error(mfxStatus err, const char **desc);
void write_mfx_error(AVTextFormatContext *w, mfxStatus ret, const char *msg);

#endif /* FFDETECT_FFDETECT_QSV_H */
