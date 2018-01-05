#pragma once

#include "OMXOverlayCodec.h"

class COMXOverlayText;

class COMXOverlayCodecText : public COMXOverlayCodec
{
public:
  COMXOverlayCodecText();
  virtual ~COMXOverlayCodecText();
  virtual bool Open(COMXStreamInfo &hints);
  virtual void Dispose();
  virtual int Decode(BYTE* data, int size, double pts, double duration);
  virtual void Reset();
  virtual void Flush();
  virtual COMXOverlay* GetOverlay();

private:
  bool             m_bIsSSA;
  COMXOverlayText* m_pOverlay;
};
