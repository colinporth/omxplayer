#pragma once

#include <stdlib.h>
#include <assert.h>
#include <vector>

enum OMXOverlayType
{
  OMXOVERLAY_TYPE_NONE    = -1,
  OMXOVERLAY_TYPE_SPU     = 1,
  OMXOVERLAY_TYPE_TEXT    = 2,
  OMXOVERLAY_TYPE_IMAGE   = 3,
  OMXOVERLAY_TYPE_SSA     = 4
};

class COMXOverlay
{
public:
  COMXOverlay(OMXOverlayType type)
  {
    m_type = type;

    iPTSStartTime = 0LL;
    iPTSStopTime = 0LL;
    bForced = false;
    replace = false;

    iGroupId = 0;
  }

  COMXOverlay(const COMXOverlay& src)
  {
    m_type        = src.m_type;
    iPTSStartTime = src.iPTSStartTime;
    iPTSStopTime  = src.iPTSStopTime;
    bForced       = src.bForced;
    replace       = src.replace;
    iGroupId      = src.iGroupId;
  }

  virtual ~COMXOverlay()
  {
  }

  bool IsOverlayType(OMXOverlayType type) { return (m_type == type); }

  double iPTSStartTime;
  double iPTSStopTime;
  bool bForced; // display, no matter what
  bool replace; // replace by next nomatter what stoptime it has
  int iGroupId;
protected:
  OMXOverlayType m_type;
};

typedef std::vector<COMXOverlay*> VecOMXOverlays;
typedef std::vector<COMXOverlay*>::iterator VecOMXOverlaysIter;
