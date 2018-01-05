#pragma once

/*
#define _HAVE_SBRK 1
#define HAVE_CMAKE_CONFIG 1
#define _REENTRANT 1
#ifndef VCHI_BULK_ALIGN
#define VCHI_BULK_ALIGN 1
#endif
#ifndef VCHI_BULK_GRANULARITY
#define VCHI_BULK_GRANULARITY 1
#endif
*/
//#define OMX_SKIP64BIT
#ifndef USE_VCHIQ_ARM
#define USE_VCHIQ_ARM
#endif
#ifndef __VIDEOCORE4__
#define __VIDEOCORE4__
#endif
#ifndef HAVE_VMCS_CONFIG
#define HAVE_VMCS_CONFIG
#endif

#ifndef HAVE_LIBBCM_HOST
#define HAVE_LIBBCM_HOST
#endif

#include "DllBCM.h"

class CRBP
{
public:
  CRBP();
  ~CRBP();

  bool Initialize();
  void Deinitialize();

private:
  DllBcmHost *m_DllBcmHost;
  bool       m_initialized;
};

extern CRBP g_RBP;
