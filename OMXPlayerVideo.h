#ifndef _OMX_PLAYERVIDEO_H_
#define _OMX_PLAYERVIDEO_H_

#include "DllAvUtil.h"
#include "DllAvFormat.h"
#include "DllAvCodec.h"

#include "OMXReader.h"
#include "OMXClock.h"
#include "OMXStreamInfo.h"
#include "OMXVideo.h"
#include "OMXThread.h"

#include <deque>
#include <sys/types.h>

#include <string>
#include <atomic>

using namespace std;

class OMXPlayerVideo : public OMXThread
{
protected:
  AVStream                  *m_pStream;
  int                       m_stream_id;
  std::deque<OMXPacket *>   m_packets;
  DllAvUtil                 m_dllAvUtil;
  DllAvCodec                m_dllAvCodec;
  DllAvFormat               m_dllAvFormat;
  bool                      m_open;
  double                    m_iCurrentPts;
  pthread_cond_t            m_packet_cond;
  pthread_cond_t            m_picture_cond;
  pthread_mutex_t           m_lock;
  pthread_mutex_t           m_lock_decoder;
  OMXClock                  *m_av_clock;
  COMXVideo                 *m_decoder;
  float                     m_fps;
  double                    m_frametime;
  float                     m_display_aspect;
  bool                      m_bAbort;
  bool                      m_flush;
  std::atomic<bool>         m_flush_requested;
  unsigned int              m_cached_size;
  double                    m_iVideoDelay;
  OMXVideoConfig            m_config;

  void Lock();
  void UnLock();
  void LockDecoder();
  void UnLockDecoder();
private:
public:
  OMXPlayerVideo();
  ~OMXPlayerVideo();
  bool Open(OMXClock *av_clock, const OMXVideoConfig &config);
  bool Close();
  bool Reset();
  bool Decode(OMXPacket *pkt);
  void Process();
  void Flush();
  bool AddPacket(OMXPacket *pkt);
  bool OpenDecoder();
  bool CloseDecoder();
  int  GetDecoderBufferSize();
  int  GetDecoderFreeSpace();
  double GetCurrentPTS() { return m_iCurrentPts; };
  double GetFPS() { return m_fps; };
  unsigned int GetCached() { return m_cached_size; };
  unsigned int GetMaxCached() { return m_config.queue_size * 1024 * 1024; };
  unsigned int GetLevel() { return m_config.queue_size ? 100.0f * m_cached_size / (m_config.queue_size * 1024.0f * 1024.0f) : 0; };
  void SubmitEOS();
  bool IsEOS();
  void SetDelay(double delay) { m_iVideoDelay = delay; }
  double GetDelay() { return m_iVideoDelay; }
  void SetAlpha(int alpha);
  void SetVideoRect(const CRect& SrcRect, const CRect& DestRect);
  void SetVideoRect(int aspectMode);

};
#endif
