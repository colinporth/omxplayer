#pragma once
#include <stdio.h>

#include <string>
#include <vector>

#define FLAG_BOLD   0
#define FLAG_ITALIC 1
#define FLAG_COLOR  2
#define FLAG_LANGUAGE   3

class COMXOverlayText;
class CDVDSubtitleStream;
class CRegExp;

class COMXSubtitleTagSami
{
public:
  COMXSubtitleTagSami()
  {
    m_tags = NULL;
    m_tagOptions = NULL;
    m_flag[FLAG_BOLD] = false;
    m_flag[FLAG_ITALIC] = false;
    m_flag[FLAG_COLOR] = false;
    m_flag[FLAG_LANGUAGE] = false; //set to true when classID != lang
  }
  virtual ~COMXSubtitleTagSami();
  bool Init();
  void ConvertLine(COMXOverlayText* pOverlay, const char* line, int len, const char* lang = NULL);
  void CloseTag(COMXOverlayText* pOverlay);
  //void LoadHead(CDVDSubtitleStream* samiStream);

  typedef struct
  {
    std::string ID;
    std::string Name;
    std::string Lang;
    std::string SAMIType;
  } SLangclass;

  std::vector<SLangclass> m_Langclass;

private:
  CRegExp *m_tags;
  CRegExp *m_tagOptions;
  bool m_flag[4];
};

