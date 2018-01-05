#pragma once

#include "DllAvCodec.h"
#include "DllAvFormat.h"
#include "DllAvUtil.h"
#include "DllSwResample.h"

#include "OMXStreamInfo.h"
#include "utils/PCMRemap.h"
#include "linux/PlatformDefs.h"

class COMXAudioCodecOMX
{
public:
  COMXAudioCodecOMX();
  ~COMXAudioCodecOMX();
  bool Open(COMXStreamInfo &hints, enum PCMLayout layout);
  void Dispose();
  int Decode(BYTE* pData, int iSize, double dts, double pts);
  int GetData(BYTE** dst, double &dts, double &pts);
  void Reset();
  int GetChannels();
  uint64_t GetChannelMap();
  int GetSampleRate();
  int GetBitsPerSample();
  static const char* GetName() { return "FFmpeg"; }
  int GetBitRate();
  unsigned int GetFrameSize() { return m_frameSize; }

protected:
  AVCodecContext* m_pCodecContext;
  SwrContext*     m_pConvert;
  enum AVSampleFormat m_iSampleFormat;
  enum AVSampleFormat m_desiredSampleFormat;

  AVFrame* m_pFrame1;

  BYTE *m_pBufferOutput;
  int   m_iBufferOutputUsed;
  int   m_iBufferOutputAlloced;

  bool m_bOpenedCodec;

  int     m_channels;

  bool m_bFirstFrame;
  bool m_bGotFrame;
  bool m_bNoConcatenate;
  unsigned int  m_frameSize;
  double m_dts, m_pts;
  DllAvCodec m_dllAvCodec;
  DllAvUtil m_dllAvUtil;
  DllSwResample m_dllSwResample;
};
