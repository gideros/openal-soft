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

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN 1
#include <Windows.h>
#else
#include <unistd.h>
#endif

#include "xaudio2.h"

#include <cassert>
#include <cstdlib>
#include <cstring>
#include <string>

#include "almalloc.h"
#include "alnumeric.h"
#include "core/device.h"
#include "core/logging.h"

#include <xaudio2.h>

namespace {

#ifdef _WIN32
#define DEVNAME_PREFIX "OpenAL Soft on "
#else
#define DEVNAME_PREFIX ""
#endif

constexpr char defaultDeviceName[] = DEVNAME_PREFIX "Default Device";

class VoiceCallback;

struct XAudio2Backend final : public BackendBase {
	XAudio2Backend(DeviceBase* device) noexcept;
	~XAudio2Backend() override;

	IXAudio2* audioengine;
	IXAudio2MasteringVoice* masteringvoice;
	IXAudio2SourceVoice* source;

	VoiceCallback* callback;
	void mix(int len) noexcept;

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

	BYTE* mBuffer{nullptr};
	size_t mBufferSize{ 0 };

    DEF_NEWDEL(XAudio2Backend)
};

class VoiceCallback : public IXAudio2VoiceCallback
{
public:
	XAudio2Backend* master;
	VoiceCallback(XAudio2Backend* backend) : master(backend) {}
	~VoiceCallback() { }

	void STDMETHODCALLTYPE OnStreamEnd() { ; }
	void STDMETHODCALLTYPE OnVoiceProcessingPassEnd() { }
	void STDMETHODCALLTYPE OnVoiceProcessingPassStart(UINT32 SamplesRequired) { master->mix(SamplesRequired); }
	void STDMETHODCALLTYPE OnBufferEnd(void* pBufferContext) { }
	void STDMETHODCALLTYPE OnBufferStart(void* pBufferContext) {    }
	void STDMETHODCALLTYPE OnLoopEnd(void* pBufferContext) {    }
	void STDMETHODCALLTYPE OnVoiceError(void* pBufferContext, HRESULT Error) { }
};

XAudio2Backend::XAudio2Backend(DeviceBase* device) noexcept : BackendBase{ device }
{
	XAudio2Create(&audioengine);
	HRESULT hr = audioengine->CreateMasteringVoice(&masteringvoice);
	source = nullptr;
	callback = new VoiceCallback(this);
}

XAudio2Backend::~XAudio2Backend()
{
	delete callback;
	audioengine->Release();
	masteringvoice->DestroyVoice();
	if (source)
		source->DestroyVoice();
	if (mBuffer != NULL)
		delete[] mBuffer;
}

void XAudio2Backend::mix(int len) noexcept
{
    const auto ulen = static_cast<unsigned int>(len);
    assert((ulen % mFrameSize) == 0);

	HRESULT hr;
	size_t samples = ulen / mFrameSize;
	size_t bufferRegionSize = samples * mFrameSize;
	if (mBufferSize < ulen) {
		if (mBuffer != NULL)
			delete[] mBuffer;
		mBufferSize = bufferRegionSize;
		mBuffer = new (std::nothrow) BYTE[mBufferSize];
	}

	mDevice->renderSamples(mBuffer, samples, mDevice->channelsFromFmt());

	XAUDIO2_BUFFER bufferRegionToSubmit;
	memset(&bufferRegionToSubmit, 0, sizeof(bufferRegionToSubmit));
	bufferRegionToSubmit.AudioBytes = bufferRegionSize;
	bufferRegionToSubmit.pAudioData = mBuffer;

	hr = source->SubmitSourceBuffer(&bufferRegionToSubmit);
	if (FAILED(hr))
	{
		ERR("SubmitSourceBuffer() failed: 0x%08lx\n", hr);
	}

}

void XAudio2Backend::open(const char *name)
{

	WAVEFORMATEX wf;

	wf.wFormatTag = WAVE_FORMAT_PCM;
	wf.nChannels = (mDevice->FmtChans == DevFmtMono) ? 1 : 2;
	wf.wBitsPerSample = 16;
	wf.nSamplesPerSec = static_cast<int>(mDevice->Frequency);
	wf.nAvgBytesPerSec = wf.nChannels * wf.nSamplesPerSec * wf.wBitsPerSample / 8;
	wf.nBlockAlign = wf.wBitsPerSample * wf.nChannels / 8;
	wf.cbSize = 0;

	audioengine->CreateSourceVoice(&source, &wf, 0, XAUDIO2_DEFAULT_FREQ_RATIO, callback, NULL, NULL);

	DevFmtType devtype = DevFmtShort;
    mFrameSize = BytesFromDevFmt(devtype) * wf.nChannels;
    mFrequency = static_cast<uint>(wf.nSamplesPerSec);
    mFmtChans = (wf.nChannels==1)?DevFmtMono:DevFmtStereo;
    mFmtType = devtype;
    mUpdateSize = 8192;

    mDevice->DeviceName = name ? name : defaultDeviceName;
}

bool XAudio2Backend::reset()
{
    mDevice->Frequency = mFrequency;
    mDevice->FmtChans = mFmtChans;
    mDevice->FmtType = mFmtType;
    mDevice->UpdateSize = mUpdateSize;
    mDevice->BufferSize = mUpdateSize * 2; /* SDL always (tries to) use two periods. */
    setDefaultWFXChannelOrder();
    return true;
}

void XAudio2Backend::start()
{
	if (source)
		source->Start();
}

void XAudio2Backend::stop()
{
	if (source)
		source->Stop();
}

} // namespace

BackendFactory & XAudio2BackendFactory::getFactory()
{
    static XAudio2BackendFactory factory{};
    return factory;
}

bool XAudio2BackendFactory::init()
{
	return true;
}

bool XAudio2BackendFactory::querySupport(BackendType type)
{
	return type == BackendType::Playback;
}

std::string XAudio2BackendFactory::probe(BackendType type)
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

BackendPtr XAudio2BackendFactory::createBackend(DeviceBase *device, BackendType type)
{
    if(type == BackendType::Playback)
        return BackendPtr{new XAudio2Backend{device}};
    return nullptr;
}
