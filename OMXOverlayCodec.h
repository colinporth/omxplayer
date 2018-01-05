#pragma once

#include "OMXOverlay.h"
#include "PlatformDefs.h"

#include <string>

// VC_ messages, messages can be combined
#define OC_ERROR    0x00000001  // an error occured, no other messages will be returned
#define OC_BUFFER   0x00000002  // the decoder needs more data
#define OC_OVERLAY  0x00000004  // the decoder decoded an overlay, call Decode(NULL, 0) again to parse the rest of the data

class COMXStreamInfo;

class COMXOverlayCodec
{
public:

  COMXOverlayCodec(const char* name)
  {
    m_codecName = name;
  }

  virtual ~COMXOverlayCodec() {}

  /*
   * Open the decoder, returns true on success
   */
  virtual bool Open(COMXStreamInfo &hints) = 0;

  /*
   * Dispose, Free all resources
   */
  virtual void Dispose() = 0;

  /*
   * returns one or a combination of VC_ messages
   * pData and iSize can be NULL, this means we should flush the rest of the data.
   */
  virtual int Decode(BYTE* data, int size, double pts, double duration) = 0;

  /*
   * Reset the decoder.
   * Should be the same as calling Dispose and Open after each other
   */
  virtual void Reset() = 0;

  /*
   * Flush the current working packet
   * This may leave the internal state intact
   */
  virtual void Flush() = 0;

  /*
   * returns a valid overlay or NULL
   * the data is valid until the next Decode call
   */
  virtual COMXOverlay* GetOverlay() = 0;

  /*
   * return codecs name
   */
  virtual const char* GetName() { return m_codecName.c_str(); }

private:
  std::string m_codecName;
};
