/**
 * OpenAL cross platform audio library
 * Copyright (C) 2018 by authors.
 * This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Library General Public
 *  License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 *  License along with this library; if not, write to the
 *  Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 * Or go to http://www.gnu.org/copyleft/lgpl.html
 */

#include "config.h"

#include <unistd.h>
#include "webaudio.h"

#include <cassert>
#include <cstdlib>
#include <cstring>
#include <string>

#include "almalloc.h"
#include "alnumeric.h"
#include "core/device.h"
#include "core/logging.h"

#include <emscripten.h>

namespace {

#ifdef _WIN32
#define DEVNAME_PREFIX "OpenAL Soft on "
#else
#define DEVNAME_PREFIX ""
#endif

constexpr char defaultDeviceName[] = DEVNAME_PREFIX "Default Device";

class VoiceCallback;

#define UPD_SIZE 8192
struct WebAudioBackend final : public BackendBase {
	WebAudioBackend(DeviceBase* device) noexcept;
	~WebAudioBackend() override;

	void tick() override;

	void open(const char* name) override;
	bool reset() override;
	void start() override;
	void stop() override;

	uint mDeviceID{ 0 };
	uint mFrameSize{ 0 };

	uint mFrequency{ 0u };
	DevFmtChannels mFmtChans{};
	DevFmtType     mFmtType{};
	uint mUpdateSize{ 0u };
	double mTime{0};
	float *buf;

    DEF_NEWDEL(WebAudioBackend)
};

WebAudioBackend::WebAudioBackend(DeviceBase* device) noexcept : BackendBase{ device }
{
	buf=new float[UPD_SIZE*2];
}

WebAudioBackend::~WebAudioBackend()
{
	EM_ASM({
		if (_WebAudio_ALSoft)
			_WebAudio_ALSoft.close();
		_WebAudio_ALSoft=undefined;
	});
	delete[] buf;
}

void WebAudioBackend::tick()
{
	size_t samples=UPD_SIZE;
	double fTime=(((double)samples)/mFrequency);
	double aTime=EM_ASM_DOUBLE({ return _WebAudio_ALSoft.currentTime; });
	if (aTime>mTime) mTime=aTime;
	if (mTime<(aTime+fTime))
	{
		mDevice->renderSamples(buf, samples, 2);
		EM_ASM({
			var audioBuf = _WebAudio_ALSoft.createBuffer(2, $1, $2);
			var channel0 = audioBuf.getChannelData(0);
			var channel1 = audioBuf.getChannelData(1);
			var pData=$0;
			pData >>= 2;
			for (var i = 0; i < $1; ++i) {
				channel0[i] = HEAPF32[pData++];
				channel1[i] = HEAPF32[pData++];
			}
			var audioSrc = _WebAudio_ALSoft.createBufferSource();
			audioSrc.buffer = audioBuf;
			audioSrc.connect(_WebAudio_ALSoft.destination);
			audioSrc.start($3);
		}, buf, samples, mFrequency,mTime);
		mTime+=fTime;
	}
}

void WebAudioBackend::open(const char *name)
{
    mFrequency=EM_ASM_INT({
        var AudioContext = window.AudioContext || window.webkitAudioContext;
        _WebAudio_ALSoft = new AudioContext();
        return _WebAudio_ALSoft.sampleRate;
    });

	DevFmtType devtype = DevFmtFloat;
    mFrameSize = BytesFromDevFmt(devtype) * 2;
    mFmtChans = DevFmtStereo;
    mFmtType = devtype;
    mUpdateSize = UPD_SIZE;

    mDevice->DeviceName = name ? name : defaultDeviceName;
}

bool WebAudioBackend::reset()
{
    mDevice->Frequency = mFrequency;
    mDevice->FmtChans = mFmtChans;
    mDevice->FmtType = mFmtType;
    mDevice->UpdateSize = mUpdateSize;
    mDevice->BufferSize = mUpdateSize * 2;
    setDefaultWFXChannelOrder();
    return true;
}

void WebAudioBackend::start()
{
	EM_ASM({
		if (_WebAudio_ALSoft)
			_WebAudio_ALSoft.resume();
	});
}

void WebAudioBackend::stop()
{
	EM_ASM({
		if (_WebAudio_ALSoft)
			_WebAudio_ALSoft.suspend();
	});
}

} // namespace

BackendFactory & WebAudioBackendFactory::getFactory()
{
    static WebAudioBackendFactory factory{};
    return factory;
}

bool WebAudioBackendFactory::init()
{
	return true;
}

bool WebAudioBackendFactory::querySupport(BackendType type)
{
	return type == BackendType::Playback;
}

std::string WebAudioBackendFactory::probe(BackendType type)
{
    std::string outnames;

    if(type != BackendType::Playback)
        return outnames;

    //int num_devices{SDL_GetNumAudioDevices(SDL_FALSE)};

    /* Includes null char. */
    outnames.append(defaultDeviceName, sizeof(defaultDeviceName));
	/*
    for(int i{0};i < num_devices;++i)
    {
        std::string name{DEVNAME_PREFIX};
        name += SDL_GetAudioDeviceName(i, SDL_FALSE);
        if(!name.empty())
            outnames.append(name.c_str(), name.length()+1);
    }
	*/
    return outnames;
}

BackendPtr WebAudioBackendFactory::createBackend(DeviceBase *device, BackendType type)
{
    if(type == BackendType::Playback)
        return BackendPtr{new WebAudioBackend{device}};
    return nullptr;
}
