#include "OMXThread.h"
#include <map>

 class Keyboard : public OMXThread
 {
 protected:
  struct termios orig_termios;
  int orig_fl;
  int m_action;
  std::map<int,int> m_keymap;

 public:
  Keyboard();
  ~Keyboard();
  void Close();
  void Process();
  void setKeymap(std::map<int,int> keymap);
  void Sleep(unsigned int dwMilliSeconds);
  int getEvent();

 private:
  void restore_term();
  void send_action(int action);
 };
