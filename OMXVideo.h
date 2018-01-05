#pragma once

#if defined(HAVE_OMXLIB)

#include "OMXCore.h"
#include "OMXStreamInfo.h"

#include <IL/OMX_Video.h>

#include "OMXClock.h"
#include "OMXReader.h"

#include "Geometry.h"
#include "utils/SingleLock.h"

#define VIDEO_BUFFERS 60

enum EDEINTERLACEMODE
{
  VS_DEINTERLACEMODE_OFF=0,
  VS_DEINTERLACEMODE_AUTO=1,
  VS_DEINTERLACEMODE_FORCE=2
};

#define CLASSNAME "COMXVideo"

class OMXVideoConfig
{
public:
  COMXStreamInfo hints;
  bool use_thread;
  CRect dst_rect;
  CRect src_rect;
  float display_aspect;
  EDEINTERLACEMODE deinterlace;
  bool advanced_hd_deinterlace;
  OMX_IMAGEFILTERANAGLYPHTYPE anaglyph;
  bool hdmi_clock_sync;
  bool allow_mvc;
  int alpha;
  int aspectMode;
  int display;
  int layer;
  float queue_size;
  float fifo_size;

  OMXVideoConfig()
  {
    use_thread = true;
    dst_rect.SetRect(0, 0, 0, 0);
    src_rect.SetRect(0, 0, 0, 0);
    display_aspect = 0.0f;
    deinterlace = VS_DEINTERLACEMODE_AUTO;
    advanced_hd_deinterlace = true;
    anaglyph = OMX_ImageFilterAnaglyphNone;
    hdmi_clock_sync = false;
    allow_mvc = false;
    alpha = 255;
    aspectMode = 0;
    display = 0;
    layer = 0;
    queue_size = 10.0f;
    fifo_size = (float)80*1024*60 / (1024*1024);
  }
};

class DllAvUtil;
class DllAvFormat;
class COMXVideo
{
public:
  COMXVideo();
  ~COMXVideo();

  // Required overrides
  bool SendDecoderConfig();
  bool NaluFormatStartCodes(enum AVCodecID codec, uint8_t *in_extradata, int in_extrasize);
  bool Open(OMXClock *clock, const OMXVideoConfig &config);
  bool PortSettingsChanged();
  void PortSettingsChangedLogger(OMX_PARAM_PORTDEFINITIONTYPE port_image, int interlaceEMode);
  void Close(void);
  unsigned int GetFreeSpace();
  unsigned int GetSize();
  int  Decode(uint8_t *pData, int iSize, double dts, double pts);
  void Reset(void);
  void SetDropState(bool bDrop);
  std::string GetDecoderName() { return m_video_codec_name; };
  void SetVideoRect(const CRect& SrcRect, const CRect& DestRect);
  void SetVideoRect(int aspectMode);
  void SetVideoRect();
  void SetAlpha(int alpha);
  int GetInputBufferSize();
  void SubmitEOS();
  bool IsEOS();
  bool SubmittedEOS() { return m_submitted_eos; }
  bool BadState() { return m_omx_decoder.BadState(); };
protected:
  // Video format
  bool              m_drop_state;

  OMX_VIDEO_CODINGTYPE m_codingType;

  COMXCoreComponent m_omx_decoder;
  COMXCoreComponent m_omx_render;
  COMXCoreComponent m_omx_sched;
  COMXCoreComponent m_omx_image_fx;
  COMXCoreComponent *m_omx_clock;
  OMXClock           *m_av_clock;

  COMXCoreTunel     m_omx_tunnel_decoder;
  COMXCoreTunel     m_omx_tunnel_clock;
  COMXCoreTunel     m_omx_tunnel_sched;
  COMXCoreTunel     m_omx_tunnel_image_fx;
  bool              m_is_open;

  bool              m_setStartTime;

  std::string       m_video_codec_name;

  bool              m_deinterlace;
  OMXVideoConfig    m_config;

  float             m_pixel_aspect;
  bool              m_submitted_eos;
  bool              m_failed_eos;
  OMX_DISPLAYTRANSFORMTYPE m_transform;
  bool              m_settings_changed;
  CCriticalSection  m_critSection;
};

#endif
