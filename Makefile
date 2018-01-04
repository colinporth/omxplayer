CFLAGS=-pipe -mfloat-abi=hard -mcpu=arm1176jzf-s -fomit-frame-pointer -mabi=aapcs-linux -mtune=arm1176jzf-s -mfpu=vfp -Wno-psabi -mno-apcs-stack-check -g -mstructure-size-boundary=32 -mno-sched-prolog
CFLAGS+=-std=c++0x -D__STDC_CONSTANT_MACROS -D__STDC_LIMIT_MACROS -DTARGET_POSIX -DTARGET_LINUX -fPIC -DPIC -D_REENTRANT -D_LARGEFILE64_SOURCE -D_FILE_OFFSET_BITS=64 -DHAVE_CMAKE_CONFIG -D__VIDEOCORE4__ -U_FORTIFY_SOURCE -Wall -DHAVE_OMXLIB -DUSE_EXTERNAL_FFMPEG  -DHAVE_LIBAVCODEC_AVCODEC_H -DHAVE_LIBAVUTIL_OPT_H -DHAVE_LIBAVUTIL_MEM_H -DHAVE_LIBAVUTIL_AVUTIL_H -DHAVE_LIBAVFORMAT_AVFORMAT_H -DHAVE_LIBAVFILTER_AVFILTER_H -DHAVE_LIBSWRESAMPLE_SWRESAMPLE_H -DOMX -DOMX_SKIP64BIT -ftree-vectorize -DUSE_EXTERNAL_OMX -DTARGET_RASPBERRY_PI -DUSE_EXTERNAL_LIBBCM_HOST

LDFLAGS+= -L ./ \
	  -L $(SDKSTAGE)/opt/vc/lib/ \
	  -l c \
	  -l brcmGLESv2 \
	  -l brcmEGL \
	  -l bcm_host \
	  -l openmaxil \
	  -l freetype \
	  -l z \
	  -l vchiq_arm \
	  -l vchostif \
	  -l vcos \
	  -l dbus-1 \
	  -l rt \
	  -l pthread \
	  -l pcre \
	  -l asound \
	  -l avutil \
	  -l avcodec \
	  -l avformat \
	  -l swscale \
	  -l swresample

INCLUDES+= -I. \
	   -I./ \
	   -Ilinux \
	   -I$(SDKSTAGE)/usr/local/include/ \
	   -I$(SDKSTAGE)/usr/include/dbus-1.0 \
	   -I$(SDKSTAGE)/usr/include/freetype2 \
	   -I$(SDKSTAGE)/usr/lib/arm-linux-gnueabihf/dbus-1.0/include \
	   -I$(SDKSTAGE)/opt/vc/include \
	   -I$(SDKSTAGE)/opt/vc/include/interface/vcos/pthreads

STRIP ?= strip

SRC=        linux/XMemUtils.cpp \
	    linux/OMXAlsa.cpp \
	    utils/log.cpp \
	    DynamicDll.cpp \
	    utils/PCMRemap.cpp \
	    utils/RegExp.cpp \
	    OMXSubtitleTagSami.cpp \
	    OMXOverlayCodecText.cpp \
	    BitstreamConverter.cpp \
	    linux/RBP.cpp \
	    OMXThread.cpp \
	    OMXReader.cpp \
	    OMXStreamInfo.cpp \
	    OMXAudioCodecOMX.cpp \
	    OMXCore.cpp \
	    OMXVideo.cpp \
	    OMXAudio.cpp \
	    OMXClock.cpp \
	    File.cpp \
	    OMXPlayerVideo.cpp \
	    OMXPlayerAudio.cpp \
	    OMXPlayerSubtitles.cpp \
	    SubtitleRenderer.cpp \
	    Unicode.cpp \
	    Srt.cpp \
	    KeyConfig.cpp \
	    OMXControl.cpp \
	    Keyboard.cpp \
	    omxplayer.cpp \

OBJS+=$(filter %.o,$(SRC:.cpp=.o))

all: omxplayer.bin

%.o: %.cpp
	rm -f $@
	$(CXX) $(CFLAGS) $(INCLUDES) -c $< -o $@ -Wno-deprecated-declarations

omxplayer.o: help.h keys.h

version:
	bash gen_version.sh > version.h

omxplayer.bin: version $(OBJS)
	$(CXX) $(LDFLAGS) -o omxplayer.bin $(OBJS)
#        $(STRIP) omxplayer.bin

help.h: README.md Makefile
	awk '/SYNOPSIS/{p=1;print;next} p&&/KEY BINDINGS/{p=0};p' $< \
	| sed -e '1,3 d' -e 's/^/"/' -e 's/$$/\\n"/' \
	> $@
keys.h: README.md Makefile
	awk '/KEY BINDINGS/{p=1;print;next} p&&/KEY CONFIG/{p=0};p' $< \
	| sed -e '1,3 d' -e 's/^/"/' -e 's/$$/\\n"/' \
	> $@

clean:
	rm -f *.o
	rm -f omxplayer.old.log omxplayer.log
	rm -f omxplayer.bin

ifndef LOGNAME
SDKSTAGE  = /SysGCC/Raspberry/arm-linux-gnueabihf/sysroot
endif
CC      := arm-linux-gnueabihf-gcc
CXX     := arm-linux-gnueabihf-g++
