// omxplayer.cpp
//{{{  includes
#include <stdio.h>
#include <signal.h>
#include <stdlib.h>
#include <stdint.h>
#include <termios.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <getopt.h>
#include <string.h>

#include <string>
#include <utility>

#define AV_NOWARN_DEPRECATED
extern "C" {
  #include <libavformat/avformat.h>
  #include <libavutil/avutil.h>
  };

#include "OMXStreamInfo.h"

#include "utils/log.h"

#include "DllAvUtil.h"
#include "DllAvFormat.h"
#include "DllAvCodec.h"

#include "linux/RBP.h"

#include "OMXVideo.h"
#include "OMXAudioCodecOMX.h"
#include "utils/PCMRemap.h"
#include "OMXClock.h"
#include "OMXAudio.h"
#include "OMXReader.h"
#include "OMXPlayerVideo.h"
#include "OMXPlayerAudio.h"
#include "OMXPlayerSubtitles.h"
#include "DllOMX.h"

#include "KeyConfig.h"
#include "utils/Strprintf.h"
#include "Keyboard.h"

#include "version.h"

using namespace std;
//}}}
//{{{  macros
#define DISPLAY_TEXT_SHORT(text) m_player_subtitles.DisplayText(text, 1000)
#define DISPLAY_TEXT_LONG(text) m_player_subtitles.DisplayText(text, 2000)
//}}}
//{{{  const
const float kFontSize = 0.035f;
const string kFontPath = "/usr/share/fonts/truetype/freefont/FreeSans.ttf";
const string kItalicFontPath = "/usr/share/fonts/truetype/freefont/FreeSansOblique.ttf";

const int font_opt        = 0x100;
const int italic_font_opt = 0x201;
const int font_size_opt   = 0x101;
const int align_opt       = 0x102;
const int no_ghost_box_opt = 0x203;
const int subtitles_opt   = 0x103;
const int lines_opt       = 0x104;
const int pos_opt         = 0x105;
const int vol_opt         = 0x106;
const int audio_fifo_opt  = 0x107;
const int video_fifo_opt  = 0x108;
const int audio_queue_opt = 0x109;
const int video_queue_opt = 0x10a;
const int no_deinterlace_opt = 0x10b;
const int threshold_opt   = 0x10c;
const int timeout_opt     = 0x10f;
const int boost_on_downmix_opt = 0x200;
const int no_boost_on_downmix_opt = 0x207;
const int key_config_opt  = 0x10d;
const int amp_opt         = 0x10e;
const int no_osd_opt      = 0x202;
const int orientation_opt = 0x204;
const int fps_opt         = 0x208;
const int live_opt        = 0x205;
const int layout_opt      = 0x206;
const int dbus_name_opt   = 0x209;
const int loop_opt        = 0x20a;
const int layer_opt       = 0x20b;
const int no_keys_opt     = 0x20c;
const int anaglyph_opt    = 0x20d;
const int native_deinterlace_opt = 0x20e;
const int display_opt     = 0x20f;
const int alpha_opt       = 0x210;
const int advanced_opt    = 0x211;
const int aspect_mode_opt = 0x212;
const int crop_opt        = 0x213;
const int http_cookie_opt = 0x300;
const int http_user_agent_opt = 0x301;
const int lavfdopts_opt   = 0x400;
const int avdict_opt      = 0x401;
//}}}
//{{{  vars
enum PCMChannels* m_pChannelMap = NULL;
volatile sig_atomic_t g_abort = false;

DllBcmHost m_BcmHost;

Keyboard* m_keyboard = NULL;
OMXReader m_omx_reader;
OMXClock* m_av_clock = NULL;
OMXAudioConfig m_config_audio;
OMXVideoConfig m_config_video;
OMXPacket* m_omx_pkt = NULL;
OMXPlayerVideo m_player_video;
OMXPlayerAudio m_player_audio;
OMXPlayerSubtitles  m_player_subtitles;

long m_Volume = 0;
long m_Amplification = 0;
bool m_HWDecode = false;
bool m_NativeDeinterlace = false;

bool m_Pause = false;
int m_audio_index_use = 0;
int m_subtitle_index = -1;
bool m_no_hdmi_clock_sync = false;

bool mHasVideo = false;
bool mHasAudio = false;
bool mHasSubtitle = false;

bool m_gen_log = false;
bool m_loop = false;
//}}}

//{{{
void sigHandler (int s) {

  if (s == SIGINT && !g_abort) {
    signal (SIGINT, SIG_DFL);
    g_abort = true;
    return;
    }

  signal (SIGABRT, SIG_DFL);
  signal (SIGSEGV, SIG_DFL);
  signal (SIGFPE, SIG_DFL);

  if (m_keyboard)
    m_keyboard->Close();

  abort();
  }
//}}}
//{{{
void printSubtitleInfo() {

  auto count = m_omx_reader.SubtitleStreamCount();

  size_t index = 0;
  if (mHasSubtitle)
    index = m_player_subtitles.GetActiveStream();

  printf ("Subtitle count: %d, state: %s, index: %d, delay: %d\n",
          count,
          mHasSubtitle && m_player_subtitles.GetVisible() ? " on" : "off",
          index+1,
          mHasSubtitle ? m_player_subtitles.GetDelay() : 0);
  }
//}}}

//{{{
float getDisplayAspectRatio (HDMI_ASPECT_T aspect) {

  switch (aspect) {
    case HDMI_ASPECT_4_3:   return 4.f/ 3.f;
    case HDMI_ASPECT_14_9:  return 14.f/ 9.f;
    case HDMI_ASPECT_16_9:  return 16.f/ 9.f;
    case HDMI_ASPECT_5_4:   return 5.f/ 4.f;
    case HDMI_ASPECT_16_10: return 16.f/ 10.f;
    case HDMI_ASPECT_15_9:  return 15.f/ 9.f;
    case HDMI_ASPECT_64_27: return 64.f/ 27.f;
    default:                return 16.f/ 9.f;
    }
  }
//}}}
//{{{
void callbackTvServiceCallback (void *userdata, uint32_t reason, uint32_t param1, uint32_t param2) {

  sem_t* tv_synced = (sem_t*)userdata;

  switch (reason) {
    case VC_SDTV_NTSC:
    case VC_SDTV_PAL:
    case VC_HDMI_HDMI:
    case VC_HDMI_DVI:
      // Signal we are ready now
      sem_post(tv_synced);
      break;

    case VC_HDMI_UNPLUGGED:
      break;

    case VC_HDMI_STANDBY:
      break;

    default:
      break;
    }

  }
//}}}
//{{{
void setVideoMode (int width, int height, int fpsrate, int fpsscale) {

  HDMI_RES_GROUP_T prefer_group;
  HDMI_RES_GROUP_T group = HDMI_RES_GROUP_CEA;

  float fps = 60.0f; // better to force to higher rate if no information is known
  if (fpsrate && fpsscale)
    fps = DVD_TIME_BASE / OMXReader::NormalizeFrameduration((double)DVD_TIME_BASE * fpsscale / fpsrate);

  // Supported HDMI CEA/DMT resolutions, preferred resolution will be returned
  TV_SUPPORTED_MODE_NEW_T *supported_modes = NULL;

  // query the number of modes first
  uint32_t prefer_mode;
  int max_supported_modes = m_BcmHost.vc_tv_hdmi_get_supported_modes_new (group, NULL, 0, &prefer_group, &prefer_mode);
  if (max_supported_modes > 0)
    supported_modes = new TV_SUPPORTED_MODE_NEW_T[max_supported_modes];

  int32_t num_modes = 0;
  if (supported_modes) {
    num_modes = m_BcmHost.vc_tv_hdmi_get_supported_modes_new (
        group, supported_modes, max_supported_modes, &prefer_group, &prefer_mode);
    if (m_gen_log)
      CLog::Log (LOGDEBUG, "EGL get supported modes (%d) = %d, prefer_group=%x, prefer_mode=%x\n",
                 group, num_modes, prefer_group, prefer_mode);
    }

  TV_SUPPORTED_MODE_NEW_T* tv_found = NULL;
  if ((num_modes > 0) && (prefer_group != HDMI_RES_GROUP_INVALID)) {
    //{{{  score modes
    uint32_t best_score = 1<<30;
    uint32_t scan_mode = m_NativeDeinterlace;

    for (int i = 0; i < num_modes; i++) {
      TV_SUPPORTED_MODE_NEW_T *tv = supported_modes + i;
      uint32_t score = 0;
      uint32_t w = tv->width;
      uint32_t h = tv->height;
      uint32_t r = tv->frame_rate;

      /* Check if frame rate match (equal or exact multiple) */
      if(fabs(r - 1.0f*fps) / fps < 0.002f)
        score += 0;
      else if(fabs(r - 2.0f*fps) / fps < 0.002f)
        score += 1<<8;
      else
        score += (1<<16) + (1<<20)/r; // bad - but prefer higher framerate

      /* Check size too, only choose, bigger resolutions */
      if (width && height) {
        /* cost of too small a resolution is high */
        score += max((int)(width -w), 0) * (1<<16);
        score += max((int)(height-h), 0) * (1<<16);
        /* cost of too high a resolution is lower */
        score += max((int)(w-width ), 0) * (1<<4);
        score += max((int)(h-height), 0) * (1<<4);
        }

      // native is good
      if (!tv->native)
        score += 1<<16;

      // interlace is bad
      if (scan_mode != tv->scan_mode)
        score += (1<<16);

      // prefer square pixels modes
      float par = getDisplayAspectRatio ((HDMI_ASPECT_T)tv->aspect_ratio)*(float)tv->height/(float)tv->width;
      score += fabs(par - 1.0f) * (1<<12);

      /*printf("mode %dx%d@%d %s%s:%x par=%.2f score=%d\n", tv->width, tv->height,
             tv->frame_rate, tv->native?"N":"", tv->scan_mode?"I":"", tv->code, par, score);*/
      if (score < best_score) {
        tv_found = tv;
        best_score = score;
        }
      }
    }
    //}}}

  if (tv_found) {
    //{{{  set mode
    char response[80];
    printf ("Output mode %d: %dx%d@%d %s%s:%x\n", tv_found->code, tv_found->width, tv_found->height,
            tv_found->frame_rate, tv_found->native?"N":"", tv_found->scan_mode?"I":"", tv_found->code);
    if (m_NativeDeinterlace && tv_found->scan_mode)
      vc_gencmd (response, sizeof response, "hvs_update_fields %d", 1);

    // if we are closer to ntsc version of framerate, let gpu know
    int ifps = (int)(fps+0.5f);
    bool ntsc_freq = fabs(fps*1001.0f/1000.0f - ifps) < fabs(fps-ifps);

    /* inform TV of ntsc setting */
    HDMI_PROPERTY_PARAM_T property;
    property.property = HDMI_PROPERTY_PIXEL_CLOCK_TYPE;
    property.param1 = ntsc_freq ? HDMI_PIXEL_CLOCK_TYPE_NTSC : HDMI_PIXEL_CLOCK_TYPE_PAL;
    property.param2 = 0;
    printf ("ntsc_freq:%d %s\n", ntsc_freq, property.param1 == HDMI_3D_FORMAT_SBS_HALF ? "3DSBS" :
            property.param1 == HDMI_3D_FORMAT_TB_HALF ? "3DTB" : property.param1 == HDMI_3D_FORMAT_FRAME_PACKING ? "3DFP":"");

    sem_t tv_synced;
    sem_init (&tv_synced, 0, 0);
    m_BcmHost.vc_tv_register_callback (callbackTvServiceCallback, &tv_synced);
    int success = m_BcmHost.vc_tv_hdmi_power_on_explicit_new (HDMI_MODE_HDMI, (HDMI_RES_GROUP_T)group, tv_found->code);
    if (success == 0)
      sem_wait (&tv_synced);
    m_BcmHost.vc_tv_unregister_callback (callbackTvServiceCallback);
    sem_destroy (&tv_synced);
    }
    //}}}

  if (supported_modes)
    delete[] supported_modes;
  }
//}}}
//{{{
void blankBackground (uint32_t rgba) {

  // if alpha is fully transparent then background has no effect
  if (!(rgba & 0xff000000))
    return;

  // we create a 1x1 black pixel image that is added to display just behind video
  uint32_t vc_image_ptr;
  int layer = m_config_video.layer - 1;

  auto display = vc_dispmanx_display_open (m_config_video.display);
  assert (display);

  VC_IMAGE_TYPE_T type = VC_IMAGE_ARGB8888;
  auto resource = vc_dispmanx_resource_create (type, 1 /*width*/, 1 /*height*/, &vc_image_ptr );
  assert (resource);

  VC_RECT_T dst_rect;
  vc_dispmanx_rect_set (&dst_rect, 0, 0, 1, 1);

  auto ret = vc_dispmanx_resource_write_data (resource, type, sizeof(rgba), &rgba, &dst_rect );
  assert (ret == 0);

  VC_RECT_T src_rect;
  vc_dispmanx_rect_set (&src_rect, 0, 0, 1<<16, 1<<16);
  vc_dispmanx_rect_set (&dst_rect, 0, 0, 0, 0);

  auto update = vc_dispmanx_update_start (0);
  assert (update);

  auto element = vc_dispmanx_element_add (update, display, layer, &dst_rect, resource, &src_rect,
                                          DISPMANX_PROTECTION_NONE, NULL, NULL,
                                          DISPMANX_STEREOSCOPIC_MONO );
  assert (element);

  ret = vc_dispmanx_update_submit_sync( update );
  assert (ret == 0);
  }
//}}}

//{{{
bool exists (const string& path) {

  struct stat buf;
  auto error = stat (path.c_str(), &buf);
  return !error || errno != ENOENT;
  }
//}}}
//{{{
bool isURL (const string& str) {

  auto result = str.find ("://");
  if (result == string::npos || result == 0)
    return false;

  for (size_t i = 0; i < result; ++i)
    if (!isalpha (str[i]))
      return false;

  return true;
  }
//}}}
//{{{
bool isPipe (const string& str) {

  if (str.compare (0, 5, "pipe:") == 0)
    return true;

  return false;
  }
//}}}

//{{{
int main (int argc, char* argv[]) {

  //{{{  vars
  bool m_send_eos = false;
  bool m_seek_flush = false;

  double m_incr = 0;
  double m_loop_from = 0;

  double startpts = 0;
  bool m_refresh = false;
  bool m_stop = false;

  bool sentStarted = false;

  double last_seek_pos = 0;
  bool idle = false;

  float m_latency = 0.0f;
  double m_last_check_time = 0.0;

  vector<Subtitle> external_subtitles;
  //}}}
  printf ("omxplayer %s\n", VERSION_DATE);
  //{{{  sig handlers
  signal (SIGSEGV, sigHandler);
  signal (SIGABRT, sigHandler);
  signal (SIGFPE, sigHandler);
  signal (SIGINT, sigHandler);
  //}}}

  //{{{  options
  //{{{
  struct option longopts[] = {
    { "info",         no_argument,        NULL,          'i' },
    { "with-info",    no_argument,        NULL,          'I' },
    { "help",         no_argument,        NULL,          'h' },
    { "version",      no_argument,        NULL,          'v' },
    { "keys",         no_argument,        NULL,          'k' },
    { "aidx",         required_argument,  NULL,          'n' },
    { "adev",         required_argument,  NULL,          'o' },
    { "stats",        no_argument,        NULL,          's' },
    { "passthrough",  no_argument,        NULL,          'p' },
    { "vol",          required_argument,  NULL,          vol_opt },
    { "amp",          required_argument,  NULL,          amp_opt },
    { "deinterlace",  no_argument,        NULL,          'd' },
    { "nodeinterlace",no_argument,        NULL,          no_deinterlace_opt },
    { "nativedeinterlace",no_argument,    NULL,          native_deinterlace_opt },
    { "anaglyph",     required_argument,  NULL,          anaglyph_opt },
    { "advanced",     optional_argument,  NULL,          advanced_opt },
    { "hw",           no_argument,        NULL,          'w' },
    { "3d",           required_argument,  NULL,          '3' },
    { "allow-mvc",    no_argument,        NULL,          'M' },
    { "hdmiclocksync", no_argument,       NULL,          'y' },
    { "nohdmiclocksync", no_argument,     NULL,          'z' },
    { "refresh",      no_argument,        NULL,          'r' },
    { "genlog",       no_argument,        NULL,          'g' },
    { "sid",          required_argument,  NULL,          't' },
    { "pos",          required_argument,  NULL,          'l' },
    { "blank",        optional_argument,  NULL,          'b' },
    { "font",         required_argument,  NULL,          font_opt },
    { "italic-font",  required_argument,  NULL,          italic_font_opt },
    { "font-size",    required_argument,  NULL,          font_size_opt },
    { "align",        required_argument,  NULL,          align_opt },
    { "no-ghost-box", no_argument,        NULL,          no_ghost_box_opt },
    { "subtitles",    required_argument,  NULL,          subtitles_opt },
    { "lines",        required_argument,  NULL,          lines_opt },
    { "win",          required_argument,  NULL,          pos_opt },
    { "crop",         required_argument,  NULL,          crop_opt },
    { "aspect-mode",  required_argument,  NULL,          aspect_mode_opt },
    { "audio_fifo",   required_argument,  NULL,          audio_fifo_opt },
    { "video_fifo",   required_argument,  NULL,          video_fifo_opt },
    { "audio_queue",  required_argument,  NULL,          audio_queue_opt },
    { "video_queue",  required_argument,  NULL,          video_queue_opt },
    { "threshold",    required_argument,  NULL,          threshold_opt },
    { "timeout",      required_argument,  NULL,          timeout_opt },
    { "boost-on-downmix", no_argument,    NULL,          boost_on_downmix_opt },
    { "no-boost-on-downmix", no_argument, NULL,          no_boost_on_downmix_opt },
    { "key-config",   required_argument,  NULL,          key_config_opt },
    { "no-osd",       no_argument,        NULL,          no_osd_opt },
    { "no-keys",      no_argument,        NULL,          no_keys_opt },
    { "orientation",  required_argument,  NULL,          orientation_opt },
    { "fps",          required_argument,  NULL,          fps_opt },
    { "live",         no_argument,        NULL,          live_opt },
    { "layout",       required_argument,  NULL,          layout_opt },
    { "dbus_name",    required_argument,  NULL,          dbus_name_opt },
    { "loop",         no_argument,        NULL,          loop_opt },
    { "layer",        required_argument,  NULL,          layer_opt },
    { "alpha",        required_argument,  NULL,          alpha_opt },
    { "display",      required_argument,  NULL,          display_opt },
    { "cookie",       required_argument,  NULL,          http_cookie_opt },
    { "user-agent",   required_argument,  NULL,          http_user_agent_opt },
    { "lavfdopts",    required_argument,  NULL,          lavfdopts_opt },
    { "avdict",       required_argument,  NULL,          avdict_opt },
    { 0, 0, 0, 0 }
  };
  //}}}

  string m_cookie = "";
  string m_user_agent = "";
  string m_lavfdopts = "";
  string m_avdict = "";

  float m_threshold = -1.0f; // amount of audio/video required to come out of buffering
  float m_timeout = 10.0f;   // amount of time file/network operation can stall for before timing out
  int m_orientation = -1;    // unset
  float m_fps = 0.0f;        // unset

  int c;
  while ((c = getopt_long(argc, argv, "wiIhvkn:l:o:cslb::pd3:Myzt:rg", longopts, NULL)) != -1) {
    switch (c) {
      //{{{
      case 'r':
        m_refresh = true;
        break;
      //}}}
      //{{{
      case 'g':
        m_gen_log = true;
        break;
      //}}}
      //{{{
      case 'y':
        m_config_video.hdmi_clock_sync = true;
        break;
      //}}}
      //{{{
      case 'z':
        m_no_hdmi_clock_sync = true;
        break;
      //}}}
      //{{{
      case 'M':
        m_config_video.allow_mvc = true;
        break;
      //}}}
      //{{{
      case 'd':
        m_config_video.deinterlace = VS_DEINTERLACEMODE_FORCE;
        break;
      //}}}
      //{{{
      case no_deinterlace_opt:
        m_config_video.deinterlace = VS_DEINTERLACEMODE_OFF;
        break;
      //}}}
      //{{{
      case native_deinterlace_opt:
        m_config_video.deinterlace = VS_DEINTERLACEMODE_OFF;
        m_NativeDeinterlace = true;
        break;
      //}}}
      //{{{
      case anaglyph_opt:
        m_config_video.anaglyph = (OMX_IMAGEFILTERANAGLYPHTYPE)atoi(optarg);
        break;
      //}}}
      //{{{
      case advanced_opt:
        m_config_video.advanced_hd_deinterlace = optarg ? (atoi(optarg) ? true : false): true;
        break;
      //}}}
      //{{{
      case 'w':
        m_config_audio.hwdecode = true;
        break;
      //}}}
      //{{{
      case 'p':
        m_config_audio.passthrough = true;
        break;
      //}}}
      //{{{
      case 'o': {
          CStdString str = optarg;
          int colon = str.Find(':');
          if(colon >= 0) {
            m_config_audio.device = str.Mid(0, colon);
            m_config_audio.subdevice = str.Mid(colon + 1, str.GetLength() - colon);
            }
          else {
            m_config_audio.device = str;
            m_config_audio.subdevice = "";
            }
          }

        if (m_config_audio.device != "local" &&
            m_config_audio.device != "hdmi" &&
            m_config_audio.device != "both" &&
            m_config_audio.device != "alsa") {
          printf ("Bad argument for -%c: Output device must be `local', `hdmi', `both' or `alsa'\n", c);
          return EXIT_FAILURE;
          }

        m_config_audio.device = "omx:" + m_config_audio.device;
        break;
      //}}}
      //{{{
      case 't':
        m_subtitle_index = atoi(optarg) - 1;
        if(m_subtitle_index < 0)
          m_subtitle_index = 0;
        break;
      //}}}
      //{{{
      case 'n':
        m_audio_index_use = atoi(optarg);
        break;
      //}}}
      //{{{
      case 'l': {
        if(strchr(optarg, ':')) {
          unsigned int h, m, s;
          if (sscanf(optarg, "%u:%u:%u", &h, &m, &s) == 3)
            m_incr = h*3600 + m*60 + s;
          }
        else
          m_incr = atof(optarg);
        if (m_loop)
          m_loop_from = m_incr;
        }
        break;
      //}}}
      //{{{
      case pos_opt:
        sscanf(optarg, "%f %f %f %f", &m_config_video.dst_rect.x1, &m_config_video.dst_rect.y1, &m_config_video.dst_rect.x2, &m_config_video.dst_rect.y2) == 4 ||
        sscanf(optarg, "%f,%f,%f,%f", &m_config_video.dst_rect.x1, &m_config_video.dst_rect.y1, &m_config_video.dst_rect.x2, &m_config_video.dst_rect.y2);
        break;
      //}}}
      //{{{
      case crop_opt:
        sscanf(optarg, "%f %f %f %f", &m_config_video.src_rect.x1, &m_config_video.src_rect.y1, &m_config_video.src_rect.x2, &m_config_video.src_rect.y2) == 4 ||
        sscanf(optarg, "%f,%f,%f,%f", &m_config_video.src_rect.x1, &m_config_video.src_rect.y1, &m_config_video.src_rect.x2, &m_config_video.src_rect.y2);
        break;
      //}}}
      //{{{
      case aspect_mode_opt:
        if (optarg) {
          if (!strcasecmp(optarg, "letterbox"))
            m_config_video.aspectMode = 1;
          else if (!strcasecmp(optarg, "fill"))
            m_config_video.aspectMode = 2;
          else if (!strcasecmp(optarg, "stretch"))
            m_config_video.aspectMode = 3;
          else
            m_config_video.aspectMode = 0;
        }
        break;
      //}}}
      //{{{
      case vol_opt:
        m_Volume = atoi(optarg);
        break;
      //}}}
      //{{{
      case amp_opt:
        m_Amplification = atoi(optarg);
        break;
      //}}}
      //{{{
      case boost_on_downmix_opt:
        m_config_audio.boostOnDownmix = true;
        break;
      //}}}
      //{{{
      case no_boost_on_downmix_opt:
        m_config_audio.boostOnDownmix = false;
        break;
      //}}}
      //{{{
      case audio_fifo_opt:
        m_config_audio.fifo_size = atof(optarg);
        break;
      //}}}
      //{{{
      case video_fifo_opt:
        m_config_video.fifo_size = atof(optarg);
        break;
      //}}}
      //{{{
      case audio_queue_opt:
        m_config_audio.queue_size = atof(optarg);
        break;
      //}}}
      //{{{
      case video_queue_opt:
        m_config_video.queue_size = atof(optarg);
        break;
      //}}}
      //{{{
      case threshold_opt:
        m_threshold = atof(optarg);
        break;
      //}}}
      //{{{
      case timeout_opt:
        m_timeout = atof(optarg);
        break;
      //}}}
      //{{{
      case orientation_opt:
        m_orientation = atoi(optarg);
        break;
      //}}}
      //{{{
      case fps_opt:
        m_fps = atof(optarg);
        break;
      //}}}
      //{{{
      case live_opt:
        m_config_audio.is_live = true;
        break;
      //}}}
      //{{{
      case layout_opt: {
        const char *layouts[] = {"2.0", "2.1", "3.0", "3.1", "4.0", "4.1", "5.0", "5.1", "7.0", "7.1"};
        unsigned i;
        for (i=0; i<sizeof layouts/sizeof *layouts; i++)
          if (strcmp(optarg, layouts[i]) == 0) {
            m_config_audio.layout = (enum PCMLayout)i;
            break;
          }
        if (i == sizeof layouts/sizeof *layouts) {
          printf ("Wrong layout specified: %s\n", optarg);
          return EXIT_FAILURE;
        }
        break;
      }
      //}}}
      //{{{
      case loop_opt:
        if (m_incr != 0)
          m_loop_from = m_incr;
        m_loop = true;
        break;
      //}}}
      //{{{
      case layer_opt:
        m_config_video.layer = atoi(optarg);
        break;
      //}}}
      //{{{
      case alpha_opt:
        m_config_video.alpha = atoi(optarg);
        break;
      //}}}
      //{{{
      case display_opt:
        m_config_video.display = atoi(optarg);
        break;
      //}}}
      //{{{
      case http_cookie_opt:
        m_cookie = optarg;
        break;
      //}}}
      //{{{
      case http_user_agent_opt:
        m_user_agent = optarg;
        break;
      //}}}
      //{{{
      case lavfdopts_opt:
        m_lavfdopts = optarg;
        break;
      //}}}
      //{{{
      case avdict_opt:
        m_avdict = optarg;
        break;
      //}}}
      //{{{
      case 0:
        break;
      //}}}
      //{{{
      case ':':
        return EXIT_FAILURE;
        break;
      //}}}
      //{{{
      default:
        return EXIT_FAILURE;
        break;
      //}}}
      }
    }
  //}}}
  string m_filename = argv[optind];
  if (!isURL (m_filename) && !isPipe (m_filename) && !exists (m_filename)) {
    //{{{  error, return
    printf ("File \"%s\" not found.\n", m_filename.c_str());
    return EXIT_FAILURE;
    }
    //}}}
  //{{{  log
  if (m_gen_log) {
    CLog::SetLogLevel(LOG_LEVEL_DEBUG);
    CLog::Init("./");
    }
  else
    CLog::SetLogLevel(LOG_LEVEL_NONE);
  //}}}

  CRBP g_RBP;
  g_RBP.Initialize();

  COMXCore g_OMX;
  g_OMX.Initialize();

  blankBackground (true);

  m_av_clock = new OMXClock();

  map<int,int> keymap = KeyConfig::buildDefaultKeymap();
  m_keyboard = new Keyboard();
  m_keyboard->setKeymap (keymap);

  if (!m_omx_reader.Open (m_filename.c_str(), true, m_config_audio.is_live, m_timeout,
                          m_cookie.c_str(), m_user_agent.c_str(), m_lavfdopts.c_str(), m_avdict.c_str()))
    goto exit;

  mHasVideo = m_omx_reader.VideoStreamCount();
  mHasAudio = (m_audio_index_use < 0) ? false : m_omx_reader.AudioStreamCount();
  mHasSubtitle  = m_omx_reader.SubtitleStreamCount();
  m_loop = m_loop && m_omx_reader.CanSeek();

  if (m_NativeDeinterlace)
    m_refresh = true;

  // you really don't want want to match refresh rate without hdmi clock sync
  if ((m_refresh || m_NativeDeinterlace) && !m_no_hdmi_clock_sync)
    m_config_video.hdmi_clock_sync = true;
  if (!m_av_clock->OMXInitialize())
    goto exit;
  if (m_config_video.hdmi_clock_sync && !m_av_clock->HDMIClockSync())
    goto exit;

  m_av_clock->OMXStateIdle();
  m_av_clock->OMXStop();
  m_av_clock->OMXPause();

  m_omx_reader.GetHints (OMXSTREAM_AUDIO, m_config_audio.hints);
  m_omx_reader.GetHints (OMXSTREAM_VIDEO, m_config_video.hints);

  if (m_fps > 0.0f) {
    m_config_video.hints.fpsrate = m_fps * DVD_TIME_BASE;
    m_config_video.hints.fpsscale = DVD_TIME_BASE;
    }
  if (m_audio_index_use > 0)
    m_omx_reader.SetActiveStream (OMXSTREAM_AUDIO, m_audio_index_use-1);

  //{{{  video
  TV_DISPLAY_STATE_T tv_state;
  if (mHasVideo && m_refresh) {
    memset (&tv_state, 0, sizeof(TV_DISPLAY_STATE_T));
    m_BcmHost.vc_tv_get_display_state (&tv_state);
    setVideoMode (m_config_video.hints.width, m_config_video.hints.height,
                  m_config_video.hints.fpsrate, m_config_video.hints.fpsscale);
    }
  //}}}
  //{{{  display aspect
  TV_DISPLAY_STATE_T current_tv_state;
  memset (&current_tv_state, 0, sizeof(TV_DISPLAY_STATE_T));
  m_BcmHost.vc_tv_get_display_state (&current_tv_state);
  m_config_video.display_aspect = getDisplayAspectRatio ((HDMI_ASPECT_T)current_tv_state.display.hdmi.aspect_ratio);
  m_config_video.display_aspect *= (float)current_tv_state.display.hdmi.height / (float)current_tv_state.display.hdmi.width;
  //}}}
  if (m_orientation >= 0)
    m_config_video.hints.orientation = m_orientation;
  if (mHasVideo && !m_player_video.Open (m_av_clock, m_config_video))
    goto exit;
  //{{{  subtitles
  if (!m_player_subtitles.Open (m_omx_reader.SubtitleStreamCount(),
                                move(external_subtitles),
                                kFontPath, kItalicFontPath, kFontSize,
                                false, true, 3,
                                m_config_video.display, m_config_video.layer + 1, m_av_clock))
    goto exit;

  //m_player_subtitles.SetSubtitleRect (m_config_video.dst_rect.x1, m_config_video.dst_rect.y1,
  //                                    m_config_video.dst_rect.x2, m_config_video.dst_rect.y2);

  if (mHasSubtitle) {
    if (m_subtitle_index != -1)
      m_player_subtitles.SetActiveStream (min (m_subtitle_index, m_omx_reader.SubtitleStreamCount()-1));
    m_player_subtitles.SetUseExternalSubtitles (false);
    m_player_subtitles.SetVisible (true);
    }

  printSubtitleInfo();
  //}}}
  //{{{  audio
  m_omx_reader.GetHints (OMXSTREAM_AUDIO, m_config_audio.hints);
  if (m_config_audio.device == "") {
    if (m_BcmHost.vc_tv_hdmi_audio_supported (EDID_AudioFormat_ePCM, 2, EDID_AudioSampleRate_e44KHz, EDID_AudioSampleSize_16bit ) == 0)
      m_config_audio.device = "omx:hdmi";
    else
      m_config_audio.device = "omx:local";
    }

  if (m_config_audio.device == "omx:alsa" && m_config_audio.subdevice.empty())
    m_config_audio.subdevice = "default";

  if ((m_config_audio.hints.codec == AV_CODEC_ID_AC3 || m_config_audio.hints.codec == AV_CODEC_ID_EAC3) &&
      m_BcmHost.vc_tv_hdmi_audio_supported (EDID_AudioFormat_eAC3, 2, EDID_AudioSampleRate_e44KHz, EDID_AudioSampleSize_16bit ) != 0)
    m_config_audio.passthrough = false;

  if (m_config_audio.hints.codec == AV_CODEC_ID_DTS &&
      m_BcmHost.vc_tv_hdmi_audio_supported (EDID_AudioFormat_eDTS, 2, EDID_AudioSampleRate_e44KHz, EDID_AudioSampleSize_16bit ) != 0)
    m_config_audio.passthrough = false;

  if (mHasAudio && !m_player_audio.Open (m_av_clock, m_config_audio, &m_omx_reader))
    goto exit;

  if (mHasAudio) {
    m_player_audio.SetVolume (pow (10, m_Volume / 2000.0));
    if (m_Amplification)
      m_player_audio.SetDynamicRangeCompression (m_Amplification);
    }

  if (m_threshold < 0.0f)
    m_threshold = m_config_audio.is_live ? 0.7f : 0.2f;
  //}}}

  m_av_clock->OMXReset (mHasVideo, mHasAudio);
  m_av_clock->OMXStateExecute();
  sentStarted = true;

  while (!m_stop) {
    //{{{  main player loop
    if (g_abort)
      goto exit;

    double now = m_av_clock->GetAbsoluteClock();
    bool update = false;
    if (m_last_check_time == 0.f||
        m_last_check_time + DVD_MSEC_TO_TIME(20) <= now) {
      update = true;
      m_last_check_time = now;
      auto key = m_keyboard->getEvent();
      //{{{  action key
      switch (key) {
        //{{{
        case KeyConfig::ACTION_EXIT:
          m_stop = true;
          goto exit;
          break;
        //}}}

        //{{{
        case KeyConfig::ACTION_PREVIOUS_AUDIO:
          if(mHasAudio)
          {
            int new_index = m_omx_reader.GetAudioIndex() - 1;
            if(new_index >= 0)
            {
              m_omx_reader.SetActiveStream(OMXSTREAM_AUDIO, new_index);
              DISPLAY_TEXT_SHORT(strprintf("Audio stream: %d", m_omx_reader.GetAudioIndex() + 1));
            }
          }
          break;
        //}}}
        //{{{
        case KeyConfig::ACTION_NEXT_AUDIO:
          if(mHasAudio)
          {
            m_omx_reader.SetActiveStream(OMXSTREAM_AUDIO, m_omx_reader.GetAudioIndex() + 1);
            DISPLAY_TEXT_SHORT(strprintf("Audio stream: %d", m_omx_reader.GetAudioIndex() + 1));
          }
          break;
        //}}}

        //{{{
        case KeyConfig::ACTION_PREVIOUS_SUBTITLE:
          if(mHasSubtitle) {
            if(!m_player_subtitles.GetUseExternalSubtitles()) {
              if (m_player_subtitles.GetActiveStream() == 0) {
              }
              else {
                auto new_index = m_player_subtitles.GetActiveStream()-1;
                DISPLAY_TEXT_SHORT(strprintf("Subtitle stream: %d", new_index+1));
                m_player_subtitles.SetActiveStream(new_index);
              }
            }

            m_player_subtitles.SetVisible(true);
            printSubtitleInfo();
          }
          break;
        //}}}
        //{{{
        case KeyConfig::ACTION_NEXT_SUBTITLE:
          if(mHasSubtitle) {
            if(m_player_subtitles.GetUseExternalSubtitles()) {
              if(m_omx_reader.SubtitleStreamCount()) {
                assert(m_player_subtitles.GetActiveStream() == 0);
                DISPLAY_TEXT_SHORT("Subtitle stream: 1");
                m_player_subtitles.SetUseExternalSubtitles(false);
              }
            }
            else {
              auto new_index = m_player_subtitles.GetActiveStream()+1;
              if(new_index < (size_t) m_omx_reader.SubtitleStreamCount()) {
                DISPLAY_TEXT_SHORT(strprintf("Subtitle stream: %d", new_index+1));
                m_player_subtitles.SetActiveStream(new_index);
              }
            }

            m_player_subtitles.SetVisible(true);
            printSubtitleInfo();
          }
          break;
        //}}}
        //{{{
        case KeyConfig::ACTION_TOGGLE_SUBTITLE:
          if(mHasSubtitle) {
            m_player_subtitles.SetVisible(!m_player_subtitles.GetVisible());
            printSubtitleInfo();
          }
          break;
        //}}}
        //{{{
        case KeyConfig::ACTION_HIDE_SUBTITLES:
          if(mHasSubtitle) {
            m_player_subtitles.SetVisible(false);
            printSubtitleInfo();
          }
          break;
        //}}}
        //{{{
        case KeyConfig::ACTION_SHOW_SUBTITLES:
          if(mHasSubtitle) {
            m_player_subtitles.SetVisible(true);
            printSubtitleInfo();
          }
          break;
        //}}}
        //{{{
        case KeyConfig::ACTION_DECREASE_SUBTITLE_DELAY:
          if(mHasSubtitle && m_player_subtitles.GetVisible()) {
            auto new_delay = m_player_subtitles.GetDelay() - 250;
            DISPLAY_TEXT_SHORT(strprintf("Subtitle delay: %d ms", new_delay));
            m_player_subtitles.SetDelay(new_delay);
            printSubtitleInfo();
          }
          break;
        //}}}
        //{{{
        case KeyConfig::ACTION_INCREASE_SUBTITLE_DELAY:
          if(mHasSubtitle && m_player_subtitles.GetVisible()) {
            auto new_delay = m_player_subtitles.GetDelay() + 250;
            DISPLAY_TEXT_SHORT(strprintf("Subtitle delay: %d ms", new_delay));
            m_player_subtitles.SetDelay(new_delay);
            printSubtitleInfo();
          }
          break;
        //}}}

        //{{{
        case KeyConfig::ACTION_STEP:

          m_av_clock->OMXStep();
          printf("Step\n");
          {
            auto t = (unsigned) (m_av_clock->OMXMediaTime()*1e-3);
            auto dur = m_omx_reader.GetStreamLength() / 1000;
            DISPLAY_TEXT_SHORT(strprintf("Step\n%02d:%02d:%02d.%03d / %02d:%02d:%02d",
                (t/3600000), (t/60000)%60, (t/1000)%60, t%1000,
                (dur/3600), (dur/60)%60, dur%60));
          }
          break;
        //}}}

        //{{{
        case KeyConfig::ACTION_SEEK_BACK_SMALL:
          if (m_omx_reader.CanSeek())
            m_incr = -30.0;
          break;
        //}}}
        //{{{
        case KeyConfig::ACTION_SEEK_FORWARD_SMALL:
          if(m_omx_reader.CanSeek())
            m_incr = 30.0;
          break;
        //}}}
        //{{{
        case KeyConfig::ACTION_SEEK_FORWARD_LARGE:
          if (m_omx_reader.CanSeek())
            m_incr = 600.0;
          break;
        //}}}
        //{{{
        case KeyConfig::ACTION_SEEK_BACK_LARGE:
          if (m_omx_reader.CanSeek())
            m_incr = -600.0;
          break;
        //}}}
        //{{{
        case KeyConfig::ACTION_SEEK_RELATIVE:
          m_incr = 1 * 1e-6;
          break;
        //}}}
        //{{{
        case KeyConfig::ACTION_SEEK_ABSOLUTE:
          break;
        //}}}

        //{{{
        case KeyConfig::ACTION_PLAY:
          m_Pause = false;
          if (mHasSubtitle) {
            m_player_subtitles.Resume();
            }
          break;
        //}}}
        //{{{
        case KeyConfig::ACTION_PAUSE:
          m_Pause=true;
          if(mHasSubtitle) {
            m_player_subtitles.Pause();
          }
          break;
        //}}}
        //{{{
        case KeyConfig::ACTION_PLAYPAUSE:
          m_Pause = !m_Pause;
          if (m_av_clock->OMXPlaySpeed() != DVD_PLAYSPEED_NORMAL &&
              m_av_clock->OMXPlaySpeed() != DVD_PLAYSPEED_PAUSE) {
            printf("resume\n");
            m_omx_reader.SetSpeed (1.0f);
            m_av_clock->OMXSetSpeed (1.0f);
            m_av_clock->OMXSetSpeed (1.0f, true, true);
            m_seek_flush = true;
            }

          if (m_Pause) {
            if (mHasSubtitle)
              m_player_subtitles.Pause();

            auto t = (unsigned) (m_av_clock->OMXMediaTime()*1e-6);
            auto dur = m_omx_reader.GetStreamLength() / 1000;
            DISPLAY_TEXT_LONG(strprintf("Pause\n%02d:%02d:%02d / %02d:%02d:%02d",
                                        (t/3600), (t/60)%60, t%60, (dur/3600), (dur/60)%60, dur%60));
            }
          else {
            if (mHasSubtitle)
              m_player_subtitles.Resume();

            auto t = (unsigned) (m_av_clock->OMXMediaTime()*1e-6);
            auto dur = m_omx_reader.GetStreamLength() / 1000;
            DISPLAY_TEXT_SHORT (strprintf ("Play\n%02d:%02d:%02d / %02d:%02d:%02d",
                                           (t/3600), (t/60)%60, t%60, (dur/3600), (dur/60)%60, dur%60));
            }
          break;
        //}}}

        //{{{
        case KeyConfig::ACTION_HIDE_VIDEO:
          // set alpha to minimum
          m_player_video.SetAlpha(0);
          break;
        //}}}
        //{{{
        case KeyConfig::ACTION_UNHIDE_VIDEO:
          // set alpha to maximum
          m_player_video.SetAlpha(255);
          break;
        //}}}

        //{{{
        case KeyConfig::ACTION_DECREASE_VOLUME:
          m_Volume -= 300;
          m_player_audio.SetVolume (pow (10, m_Volume / 2000.0));
          DISPLAY_TEXT_SHORT (strprintf ("Volume: %.2f dB", m_Volume / 100.0f));
          printf ("Current Volume: %.2fdB\n", m_Volume / 100.0f);
          break;
        //}}}
        //{{{
        case KeyConfig::ACTION_INCREASE_VOLUME:
          m_Volume += 300;
          m_player_audio.SetVolume (pow (10, m_Volume / 2000.0));
          DISPLAY_TEXT_SHORT (strprintf ("Volume: %.2f dB", m_Volume / 100.0f));
          printf ("Current Volume: %.2fdB\n", m_Volume / 100.0f);
          break;
        //}}}

        case KeyConfig::ACTION_SET_ASPECT_MODE:
        case KeyConfig::ACTION_SHOW_INFO:
        case KeyConfig::ACTION_DECREASE_SPEED:
        case KeyConfig::ACTION_INCREASE_SPEED:
        case KeyConfig::ACTION_REWIND:
        case KeyConfig::ACTION_FAST_FORWARD:
        case KeyConfig::ACTION_PREVIOUS_CHAPTER:
        case KeyConfig::ACTION_NEXT_CHAPTER:
        case KeyConfig::ACTION_SET_ALPHA:
        case KeyConfig::ACTION_MOVE_VIDEO:
        case KeyConfig::ACTION_CROP_VIDEO:
          break;

        //{{{
        default:
          break;
        //}}}
        }
      //}}}
      }

    if (idle) {
      //{{{  sleep for 10ms
      usleep(10000);
      continue;
      }
      //}}}
    if (m_seek_flush || m_incr != 0) {
      //{{{  seek
      double pts = 0;
      double seek_pos = 0;
      if (mHasSubtitle)
        m_player_subtitles.Pause();

      pts = m_av_clock->OMXMediaTime();
      seek_pos = (pts ? pts / DVD_TIME_BASE : last_seek_pos) + m_incr;
      last_seek_pos = seek_pos;
      seek_pos *= 1000.0;
      if (m_omx_reader.SeekTime((int)seek_pos, m_incr < 0.0f, &startpts)) {
        unsigned t = (unsigned)(startpts*1e-6);
        auto dur = m_omx_reader.GetStreamLength() / 1000;

        DISPLAY_TEXT_LONG (strprintf("Seek\n%02d:%02d:%02d / %02d:%02d:%02d",
                           (t/3600), (t/60)%60, t%60, (dur/3600), (dur/60)%60, dur%60));
        printf ("Seek to: %02d:%02d:%02d\n", (t/3600), (t/60)%60, t%60);

        // flush
        m_av_clock->OMXStop();
        m_av_clock->OMXPause();

        if (mHasVideo)
          m_player_video.Flush();
        if (mHasAudio)
          m_player_audio.Flush();
        if (pts != DVD_NOPTS_VALUE)
          m_av_clock->OMXMediaTime (startpts);
        if (mHasSubtitle)
          m_player_subtitles.Flush();
        if (m_omx_pkt) {
          m_omx_reader.FreePacket (m_omx_pkt);
          m_omx_pkt = NULL;
          }
        }

      sentStarted = false;

      if (m_omx_reader.IsEof())
        goto exit;

      // Quick reset to reduce delay during loop & seek.
      if (mHasVideo && !m_player_video.Reset())
        goto exit;

      CLog::Log (LOGDEBUG, "Seeked %.0f %.0f %.0f\n", DVD_MSEC_TO_TIME(seek_pos), startpts, m_av_clock->OMXMediaTime());

      m_av_clock->OMXPause();

      if (mHasSubtitle)
        m_player_subtitles.Resume();
      m_seek_flush = false;
      m_incr = 0;
      }
      //}}}
    if (m_player_audio.Error()) {
      //{{{  error, exit
      printf ("audio player error. emergency exit!!!\n");
      goto exit;
      }
      //}}}
    if (update) {
      //{{{  when the video/audio fifos are low, we pause clock, when high we resume
      double stamp = m_av_clock->OMXMediaTime();
      double audio_pts = m_player_audio.GetCurrentPTS();
      double video_pts = m_player_video.GetCurrentPTS();

      float audio_fifo = audio_pts == DVD_NOPTS_VALUE ? 0.0f : audio_pts / DVD_TIME_BASE - stamp * 1e-6;
      float video_fifo = video_pts == DVD_NOPTS_VALUE ? 0.0f : video_pts / DVD_TIME_BASE - stamp * 1e-6;
      float threshold = min(0.1f, (float)m_player_audio.GetCacheTotal() * 0.1f);

      bool audio_fifo_low = false;
      bool video_fifo_low = false;
      bool audio_fifo_high = false;
      bool video_fifo_high = false;
      if (audio_pts != DVD_NOPTS_VALUE) {
        //{{{  audio fifo levels
        audio_fifo_low = mHasAudio && audio_fifo < threshold;
        audio_fifo_high = !mHasAudio || (audio_pts != DVD_NOPTS_VALUE && audio_fifo > m_threshold);
        }
        //}}}
      if (video_pts != DVD_NOPTS_VALUE) {
        //{{{  video fifo levels
        video_fifo_low = mHasVideo && video_fifo < threshold;
        video_fifo_high = !mHasVideo || (video_pts != DVD_NOPTS_VALUE && video_fifo > m_threshold);
        }
        //}}}

      //{{{  subtitle display
      DISPLAY_TEXT_SHORT (
        strprintf ("M:%8.0f V:%6.2fs %6dk/%6dk A:%6.2f %6.02fs/%6.02fs Cv:%6dk Ca:%6dk",
                   stamp,
                   video_fifo,
                   (m_player_video.GetDecoderBufferSize() - m_player_video.GetDecoderFreeSpace())>>10,
                   m_player_video.GetDecoderBufferSize() >> 10,
                   audio_fifo,
                    m_player_audio.GetDelay(),
                   m_player_audio.GetCacheTotal(),
                   m_player_video.GetCached() >> 10,
                   m_player_audio.GetCached() >> 10));
      //}}}
      //{{{  log display
      CLog::Log(LOGDEBUG, "Normal M:%.0f (A:%.0f V:%.0f) P:%d A:%.2f V:%.2f/T:%.2f (%d,%d,%d,%d) A:%d%% V:%d%% (%.2f,%.2f)\n", stamp, audio_pts, video_pts, m_av_clock->OMXIsPaused(),
                           audio_pts == DVD_NOPTS_VALUE ? 0.0:audio_fifo,
                           video_pts == DVD_NOPTS_VALUE ? 0.0:video_fifo,
                           m_threshold,
                           audio_fifo_low,
                           video_fifo_low,
                           audio_fifo_high,
                           video_fifo_high,
                           m_player_audio.GetLevel(),
                           m_player_video.GetLevel(),
                           m_player_audio.GetDelay(),
                           (float)m_player_audio.GetCacheTotal());
      //}}}

      if (m_config_audio.is_live) {
        //{{{  keep latency under control by adjusting clock (and so resampling audio)
        float latency = DVD_NOPTS_VALUE;
        if (mHasAudio && audio_pts != DVD_NOPTS_VALUE)
          latency = audio_fifo;
        else if (!mHasAudio && mHasVideo && video_pts != DVD_NOPTS_VALUE)
          latency = video_fifo;

        if (!m_Pause && latency != DVD_NOPTS_VALUE) {
          if (m_av_clock->OMXIsPaused()) {
            if (latency > m_threshold) {
              CLog::Log (LOGDEBUG, "Resume %.2f,%.2f (%d,%d,%d,%d) EOF:%d PKT:%p\n", audio_fifo, video_fifo, audio_fifo_low, video_fifo_low, audio_fifo_high, video_fifo_high, m_omx_reader.IsEof(), m_omx_pkt);
              m_av_clock->OMXResume();
              m_latency = latency;
              }
            }
          else {
            m_latency = m_latency * 0.99f + latency * 0.01f;
            float speed = 1.f;
            if (m_latency < 0.5f * m_threshold)
              speed = 0.990f;
            else if (m_latency < 0.9f * m_threshold)
              speed = 0.999f;
            else if (m_latency > 2.f * m_threshold)
              speed = 1.010f;
            else if (m_latency > 1.1f * m_threshold)
              speed = 1.001f;

            m_av_clock->OMXSetSpeed ((int)(DVD_PLAYSPEED_NORMAL*speed));
            m_av_clock->OMXSetSpeed ((int)(DVD_PLAYSPEED_NORMAL*speed), true, true);
            CLog::Log (LOGDEBUG, "Live: %.2f (%.2f) S:%.3f T:%.2f\n", m_latency, latency, speed, m_threshold);
            }
          }
        }
        //}}}
      else if (!m_Pause && (m_omx_reader.IsEof() || m_omx_pkt || (audio_fifo_high && video_fifo_high))) {
        //{{{  resume
        if (m_av_clock->OMXIsPaused()) {
          CLog::Log (LOGDEBUG, "Resume %.2f,%.2f (%d,%d,%d,%d) EOF:%d PKT:%p\n", audio_fifo, video_fifo, audio_fifo_low, video_fifo_low, audio_fifo_high, video_fifo_high, m_omx_reader.IsEof(), m_omx_pkt);
          m_av_clock->OMXResume();
          }
        }
        //}}}
      else if (m_Pause || audio_fifo_low || video_fifo_low) {
        //{{{  pause
        if (!m_av_clock->OMXIsPaused()) {
          if (!m_Pause)
            m_threshold = min(2.0f*m_threshold, 16.0f);
          CLog::Log (LOGDEBUG, "Pause %.2f,%.2f (%d,%d,%d,%d) %.2f\n", audio_fifo, video_fifo, audio_fifo_low, video_fifo_low, audio_fifo_high, video_fifo_high, m_threshold);
          m_av_clock->OMXPause();
          }
        }
        //}}}
      }
      //}}}

    if (!sentStarted) {
      //{{{  omx reset
      CLog::Log (LOGDEBUG, "COMXPlayer::HandleMessages - player started RESET");
      m_av_clock->OMXReset (mHasVideo, mHasAudio);
      sentStarted = true;
      }
      //}}}

    if (!m_omx_pkt)
      m_omx_pkt = m_omx_reader.Read();
    if (m_omx_pkt)
      m_send_eos = false;

    if (m_omx_reader.IsEof() && !m_omx_pkt) {
      //{{{  demuxer EOF, but may have not played out data yet
      if ((mHasVideo && m_player_video.GetCached()) ||
          (mHasAudio && m_player_audio.GetCached()) ) {
        OMXClock::OMXSleep(10);
        continue;
        }
      if (!m_send_eos && mHasVideo)
        m_player_video.SubmitEOS();
      if (!m_send_eos && mHasAudio)
        m_player_audio.SubmitEOS();
      m_send_eos = true;
      if ((mHasVideo && !m_player_video.IsEOS()) ||
          (mHasAudio && !m_player_audio.IsEOS()) ) {
        OMXClock::OMXSleep (10);
        continue;
        }

      if (m_loop) {
        m_incr = m_loop_from - (m_av_clock->OMXMediaTime() ? m_av_clock->OMXMediaTime() / DVD_TIME_BASE : last_seek_pos);
        continue;
        }

      break;
      }
      //}}}

    if (m_omx_pkt) {
      if (mHasVideo && m_omx_reader.IsActive(OMXSTREAM_VIDEO, m_omx_pkt->stream_index)) {
        if (m_player_video.AddPacket (m_omx_pkt))
          m_omx_pkt = NULL;
        else
          OMXClock::OMXSleep (10);
        }
      else if (mHasAudio && m_omx_pkt->codec_type == AVMEDIA_TYPE_AUDIO) {
        if (m_player_audio.AddPacket (m_omx_pkt))
          m_omx_pkt = NULL;
        else
          OMXClock::OMXSleep (10);
        }
      else if (mHasSubtitle && m_omx_pkt->codec_type == AVMEDIA_TYPE_SUBTITLE) {
        auto result = m_player_subtitles.AddPacket (m_omx_pkt, m_omx_reader.GetRelativeIndex(m_omx_pkt->stream_index));
        if (result)
          m_omx_pkt = NULL;
        else
          OMXClock::OMXSleep (10);
        }
      else {
        m_omx_reader.FreePacket (m_omx_pkt);
        m_omx_pkt = NULL;
        }
      }
    else
      OMXClock::OMXSleep (10);
    }
    //}}}

exit:
  auto stopTime = (unsigned)(m_av_clock->OMXMediaTime()*1e-6);
  printf ("Stopped at: %02d:%02d:%02d\n", (stopTime / 3600), (stopTime / 60) % 60, stopTime % 60);

  if (m_NativeDeinterlace) {
    char response[80];
    vc_gencmd (response, sizeof response, "hvs_update_fields %d", 0);
    }

  if (mHasVideo && m_refresh &&
      tv_state.display.hdmi.group && tv_state.display.hdmi.mode)
    m_BcmHost.vc_tv_hdmi_power_on_explicit_new (HDMI_MODE_HDMI, (HDMI_RES_GROUP_T)tv_state.display.hdmi.group, tv_state.display.hdmi.mode);

  m_av_clock->OMXStop();
  m_av_clock->OMXStateIdle();

  m_player_subtitles.Close();
  m_player_video.Close();
  m_player_audio.Close();
  m_keyboard->Close();

  if (m_omx_pkt) {
    m_omx_reader.FreePacket (m_omx_pkt);
    m_omx_pkt = NULL;
    }
  m_omx_reader.Close();

  m_av_clock->OMXDeinitialize();
  if (m_av_clock)
    delete m_av_clock;

  g_OMX.Deinitialize();
  g_RBP.Deinitialize();

  // exit status success on playback end
  if (m_send_eos)
    return EXIT_SUCCESS;

  // exit status OMXPlayer defined value on user quit
  if (m_stop)
    return 3;

  // exit status failure on other cases
  return EXIT_FAILURE;
  }
//}}}
